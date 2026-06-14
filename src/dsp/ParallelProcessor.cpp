#include "ParallelProcessor.h"

//==============================================================================
namespace ana {

//==============================================================================
ParallelProcessor::ParallelProcessor()
{
    numThreads_ = static_cast<int>(juce::SystemStats::getNumCpus());
    if (numThreads_ < 1)
        numThreads_ = 1;

    threadPool_ = std::make_unique<juce::ThreadPool>(numThreads_);
}

//==============================================================================
void ParallelProcessor::init(int numThreads)
{
    setNumThreads(numThreads);
    resetStats();
}

void ParallelProcessor::setNumThreads(int numThreads)
{
    numThreads_ = (numThreads > 0) ? numThreads
                                   : static_cast<int>(juce::SystemStats::getNumCpus());
    if (numThreads_ < 1)
        numThreads_ = 1;

    threadPool_ = std::make_unique<juce::ThreadPool>(numThreads_);
}

int ParallelProcessor::getNumThreads() const noexcept
{
    return numThreads_;
}

//==============================================================================
void ParallelProcessor::processPartials(PartialDataSIMD& partials,
                                        PartialProcessFunc func,
                                        void* userData,
                                        ParallelPolicy policy)
{
    // ---- single-thread path: no pool overhead --------------------------------
    if (numThreads_ <= 1)
    {
        const auto start = juce::Time::getHighResolutionTicks();

        func(partials.frequency, partials.amplitude, partials.phase,
             0, PartialDataSIMD::kMaxPartials, userData);

        const auto end = juce::Time::getHighResolutionTicks();
        const int64_t elapsed = static_cast<int64_t>(
            juce::Time::highResolutionTicksToSeconds(end - start) * 1e6);

        totalSerialUs_.fetch_add(elapsed, std::memory_order_relaxed);
        totalParallelUs_.fetch_add(elapsed, std::memory_order_relaxed);
        ++statCount_;
        return;
    }

    // ---- build chunk list from policy ---------------------------------------
    std::vector<Chunk> chunks;

    switch (policy)
    {
        case ParallelPolicy::SplitByPartial:
        {
            int numChunks = std::min(numThreads_,
                                     PartialDataSIMD::kMaxPartials / kMinChunkPartials);
            if (numChunks < 1) numChunks = 1;

            const int baseSize = PartialDataSIMD::kMaxPartials / numChunks;
            const int rem      = PartialDataSIMD::kMaxPartials % numChunks;

            int s = 0;
            for (int i = 0; i < numChunks; ++i)
            {
                const int e = s + baseSize + (i < rem ? 1 : 0);
                chunks.push_back({s, e});
                s = e;
            }
            break;
        }

        case ParallelPolicy::SplitByBlock:
        {
            for (int s = 0; s < PartialDataSIMD::kMaxPartials; s += kMinChunkPartials)
            {
                const int e = std::min(s + kMinChunkPartials,
                                       PartialDataSIMD::kMaxPartials);
                chunks.push_back({s, e});
            }
            break;
        }

        case ParallelPolicy::SplitByActive:
        {
            const int activeEff = std::max(partials.activeCount, 1);
            int numChunks = std::min(numThreads_, activeEff / kMinChunkPartials);
            if (numChunks < 1) numChunks = 1;
            if (numChunks > activeEff) numChunks = activeEff;

            // Collect the indices of every active partial
            std::vector<int> activeIdx;
            activeIdx.reserve(static_cast<size_t>(partials.activeCount));
            for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
                if (partials.isActive(i))
                    activeIdx.push_back(i);

            const int numActive = static_cast<int>(activeIdx.size());
            if (numActive == 0)
                return;                     // nothing to process

            if (numChunks > numActive)
                numChunks = numActive;

            const int baseCount = numActive / numChunks;
            const int rem       = numActive % numChunks;

            int idx = 0;
            for (int i = 0; i < numChunks; ++i)
            {
                const int cnt = baseCount + (i < rem ? 1 : 0);
                const int startPartial = activeIdx[idx];
                const int endPartial   = (i == numChunks - 1)
                    ? PartialDataSIMD::kMaxPartials
                    : activeIdx[std::min(idx + cnt, numActive - 1)] + 1;
                chunks.push_back({startPartial, endPartial});

                // Bump idx *after* computing endPartial (endPartial uses old idx)
                idx += cnt;
            }
            break;
        }
    }

    // ---- single-chunk shortcut ----------------------------------------------
    if (chunks.size() <= 1)
    {
        const auto start = juce::Time::getHighResolutionTicks();

        for (const auto& c : chunks)
            func(partials.frequency, partials.amplitude, partials.phase,
                 c.startIdx, c.endIdx, userData);

        const auto end = juce::Time::getHighResolutionTicks();
        const int64_t elapsed = static_cast<int64_t>(
            juce::Time::highResolutionTicksToSeconds(end - start) * 1e6);

        totalSerialUs_.fetch_add(elapsed, std::memory_order_relaxed);
        totalParallelUs_.fetch_add(elapsed, std::memory_order_relaxed);
        ++statCount_;
        return;
    }

    // ---- multi-chunk: dispatch across the pool ------------------------------
    int64_t serialUs = 0, parallelUs = 0;
    dispatchJobs(chunks, partials, func, userData, serialUs, parallelUs);

    totalSerialUs_.fetch_add(serialUs, std::memory_order_relaxed);
    totalParallelUs_.fetch_add(parallelUs, std::memory_order_relaxed);
    ++statCount_;
}

//==============================================================================
void ParallelProcessor::dispatchJobs(const std::vector<Chunk>& chunks,
                                      PartialDataSIMD& partials,
                                      PartialProcessFunc& func,
                                      void* userData,
                                      int64_t& outSerialUs,
                                      int64_t& outParallelUs)
{
    const int numJobs = static_cast<int>(chunks.size());

    std::atomic<int> counter{numJobs};
    juce::WaitableEvent doneEvent;

    // Build all job objects first
    std::vector<std::unique_ptr<InternalJob>> jobs;
    jobs.reserve(static_cast<size_t>(numJobs));

    for (int i = 0; i < numJobs; ++i)
    {
        const auto& c = chunks[static_cast<size_t>(i)];
        auto job = std::make_unique<InternalJob>(counter, doneEvent);
        job->func = [&partials, &func, userData, c]()
        {
            func(partials.frequency, partials.amplitude, partials.phase,
                 c.startIdx, c.endIdx, userData);
        };
        jobs.push_back(std::move(job));
    }

    // Wall-clock timing starts here
    const auto wallStart = juce::Time::getHighResolutionTicks();

    // Enqueue every job
    for (auto& j : jobs)
        threadPool_->addJob(j.get(), false);

    // Block the calling thread until every job is finished
    doneEvent.wait(-1);

    const auto wallEnd = juce::Time::getHighResolutionTicks();

    outParallelUs = static_cast<int64_t>(
        juce::Time::highResolutionTicksToSeconds(wallEnd - wallStart) * 1e6);

    // Sum per-job execution times to obtain the serial equivalent
    outSerialUs = 0;
    for (auto& j : jobs)
        outSerialUs += j->elapsedUs;
}

//==============================================================================
double ParallelProcessor::getAverageSpeedup() const noexcept
{
    if (statCount_ == 0)
        return 1.0;

    const int64_t s = totalSerialUs_.load(std::memory_order_relaxed);
    const int64_t p = totalParallelUs_.load(std::memory_order_relaxed);

    if (p == 0)
        return 1.0;

    return static_cast<double>(s) / static_cast<double>(p);
}

void ParallelProcessor::resetStats() noexcept
{
    totalSerialUs_.store(0,  std::memory_order_relaxed);
    totalParallelUs_.store(0, std::memory_order_relaxed);
    statCount_ = 0;
}

//==============================================================================
void ParallelProcessor::reset()
{
    threadPool_->removeAllJobs(true, 5000);
    resetStats();
}

//==============================================================================
// InternalJob
//==============================================================================
ParallelProcessor::InternalJob::InternalJob(std::atomic<int>& counter,
                                              juce::WaitableEvent& done) noexcept
    : juce::ThreadPoolJob("pp"),
      counterRef(counter),
      doneEvent(done)
{
}

juce::ThreadPoolJob::JobStatus ParallelProcessor::InternalJob::runJob()
{
    const auto start = juce::Time::getHighResolutionTicks();

    if (func)
        func();

    const auto end = juce::Time::getHighResolutionTicks();
    elapsedUs = static_cast<int64_t>(
        juce::Time::highResolutionTicksToSeconds(end - start) * 1e6);

    // Decrement the live-job counter; if this was the last one, wake the
    // caller that is blocked on doneEvent.wait()
    if (--counterRef == 0)
        doneEvent.signal();

    return jobHasFinished;
}

} // namespace ana
