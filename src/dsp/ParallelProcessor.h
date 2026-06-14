#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "PartialDataSIMD.h"

namespace ana {

//==============================================================================
/**
    Parallel processing policies for distributing partial processing across
    multiple CPU cores.

    SplitByPartial :   Split the 512-partial range evenly across threads,
                       each chunk >= 64 partials for SIMD efficiency.
    SplitByBlock   :   Split into fixed-size blocks of 64 partials each.
    SplitByActive  :   Collect only the active partials, then group them
                       into contiguous ranges across threads.
*/
enum class ParallelPolicy
{
    SplitByPartial,
    SplitByBlock,
    SplitByActive
};

//==============================================================================
/**
    A utility class that processes PartialDataSIMD across multiple CPU cores
    using a persistent juce::ThreadPool.

    Usage:
        ParallelProcessor pp;
        pp.init();                              // auto-detect hardware concurrency

        pp.processPartials(partials,
            [](float* freq, float* amp, float* phase, int start, int end, void*)
            {
                for (int i = start; i < end; ++i)
                    freq[i] *= 2.0f;            // example transform
            });

    @see PartialDataSIMD, ParallelPolicy
*/
class ParallelProcessor
{
public:
    ParallelProcessor();
    ~ParallelProcessor() = default;

    //==============================================================================
    /** Initialises the thread pool and resets stats.
        @param numThreads  0 = hardware concurrency, > 0 = fixed count.
    */
    void init(int numThreads = 0);

    /** Reconfigures the thread count.  Rebuilds the internal pool. */
    void setNumThreads(int numThreads);

    /** Returns the number of worker threads currently configured. */
    int getNumThreads() const noexcept;

    //==============================================================================
    /** Signature for the per-chunk processing callback.

        The callback receives raw pointers to the three SoA arrays followed by
        the inclusive-exclusive range [startIdx, endIdx) and an opaque
        user-data pointer.  It MUST only touch indices inside that range.

        @param freq     Pointer to PartialDataSIMD::frequency array.
        @param amp      Pointer to PartialDataSIMD::amplitude array.
        @param phase    Pointer to PartialDataSIMD::phase array.
        @param startIdx First partial index (inclusive).
        @param endIdx   One past the last partial index (exclusive).
        @param userData Opaque pointer forwarded from the caller.
    */
    using PartialProcessFunc = std::function<void(float* freq, float* amp, float* phase,
                                                  int startIdx, int endIdx,
                                                  void* userData)>;

    /** Processes partial data in parallel, splitting work according to @a policy.

        When @a numThreads_ <= 1, runs directly on the calling thread without
        any thread-pool overhead.  When only one chunk is produced (e.g. very
        few active partials), also runs inline.

        @param partials  The SIMD partial data to process.
        @param func      Per-chunk callback (must be thread-safe on disjoint ranges).
        @param userData  Optional context forwarded to every invocation of @a func.
        @param policy    Distribution strategy (default: SplitByPartial).
    */
    void processPartials(PartialDataSIMD& partials,
                         PartialProcessFunc func,
                         void* userData = nullptr,
                         ParallelPolicy policy = ParallelPolicy::SplitByPartial);

    //==============================================================================
    /** Generic parallel-for loop backed by the same thread pool.

        The functor receives (int start, int end) and must process the
        inclusive-exclusive range [start, end).  Multiple invocations run
        concurrently on disjoint sub-ranges.

        @param start  First index (inclusive).
        @param end    One past the last index (exclusive).
        @param func   Callable with signature void(int start, int end).
    */
    template <typename Func>
    void parallelFor(int start, int end, Func&& func);

    //==============================================================================
    /** Returns the average speedup measured across all processPartials calls.

        speedup = (sum of per-chunk execution times) / (wall-clock time).

        This gives a meaningful ratio: with perfect scaling on N threads the
        speedup approaches N.  Returns 1.0 when no calls have been made.
    */
    double getAverageSpeedup() const noexcept;

    /** Resets the accumulated timing statistics. */
    void resetStats() noexcept;

    /** Removes all pending pool jobs and resets stats. */
    void reset();

private:
    //==============================================================================
    // Cache-line-aligned chunk descriptor to prevent false-sharing between
    // threads that update adjacent Chunk structures.
    struct alignas(64) Chunk
    {
        int startIdx;
        int endIdx;
    };

    //==============================================================================
    // Internal ThreadPoolJob subclass that runs a std::function and records
    // its own execution time for speedup calculation.
    struct InternalJob : public juce::ThreadPoolJob
    {
        InternalJob(std::atomic<int>& counter, juce::WaitableEvent& done) noexcept;
        RunStatus runJob() override;

        std::function<void()> func;
        std::atomic<int>& counterRef;
        juce::WaitableEvent& doneEvent;
        int64_t elapsedUs = 0;
    };

    //==============================================================================
    //  Builds chunk list from partial data and policy, enqueues jobs, waits.
    void dispatchJobs(const std::vector<Chunk>& chunks,
                      PartialDataSIMD& partials,
                      PartialProcessFunc& func,
                      void* userData,
                      int64_t& outSerialUs,
                      int64_t& outParallelUs);

    //==============================================================================
    std::unique_ptr<juce::ThreadPool> threadPool_;
    int numThreads_ = 0;

    // Timing accumulators  (relaxed atomic is sufficient for best-effort stats)
    std::atomic<int64_t> totalSerialUs_{0};
    std::atomic<int64_t> totalParallelUs_{0};
    int statCount_ = 0;

    // Tuning constants
    static constexpr int kCacheLineSize     = 64;
    static constexpr int kMinChunkPartials  = 64;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParallelProcessor)
};

//==============================================================================
// Template implementation  (must live in the header)
//==============================================================================
template <typename Func>
void ParallelProcessor::parallelFor(int start, int end, Func&& func)
{
    if (start >= end)
        return;

    // Single-thread shortcut – no pool overhead
    if (numThreads_ <= 1 || (end - start) <= 1)
    {
        func(start, end);
        return;
    }

    const int range = end - start;
    int numJobs = std::min(numThreads_, range);
    if (numJobs < 1) numJobs = 1;

    const int chunkSize  = range / numJobs;
    const int remainder  = range % numJobs;

    std::atomic<int> counter{numJobs};
    juce::WaitableEvent doneEvent;

    std::vector<std::unique_ptr<InternalJob>> jobs;
    jobs.reserve(static_cast<size_t>(numJobs));

    int currentStart = start;
    for (int i = 0; i < numJobs; ++i)
    {
        const int extra    = (i < remainder) ? 1 : 0;
        const int chunkEnd = currentStart + chunkSize + extra;

        auto job = std::make_unique<InternalJob>(counter, doneEvent);
        job->func = [&func, currentStart, chunkEnd]() { func(currentStart, chunkEnd); };
        jobs.push_back(std::move(job));

        currentStart = chunkEnd;
    }

    // Enqueue all jobs
    for (auto& j : jobs)
        threadPool_->addJob(j.get(), false);

    // Block until every job finishes
    doneEvent.wait(-1);
}

} // namespace ana
