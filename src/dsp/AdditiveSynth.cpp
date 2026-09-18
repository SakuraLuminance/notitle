#include "AdditiveSynth.h"

namespace ana
{

namespace
{
constexpr int kInitialVoices = 16;

/** Partials quieter than this are inaudible at the synth's output gain. */
constexpr float kMinPartialAmplitude = 2.0e-4f;

struct Cand { float f, a, ph; };

/** Sanitises one partial set into a frame.
    keepByAmplitude = true  -> keep the loudest N (single static set)
    keepByAmplitude = false -> keep the N lowest-frequency partials, then order
                               by frequency (image frames: stable index across
                               frames so interpolation is musically sensible) */
int buildFrame(const PartialDataSIMD& src, int maxP, float nyquistHz,
               bool byFrequency, AdditiveFrame& out)
{
    std::vector<Cand> cands;
    cands.reserve(static_cast<std::size_t>(maxP));

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        const float a = src.amplitude[i];
        const float f = src.frequency[i];
        if (! std::isfinite(a) || ! std::isfinite(f)) continue;
        if (a <= 1.0e-6f) continue;
        if (f <= 0.0f || f >= nyquistHz) continue;

        cands.push_back({ f,
                          juce::jlimit(0.0f, 1.0f, a),
                          std::isfinite(src.phase[i]) ? src.phase[i] : 0.0f });
    }

    const int limit = juce::jlimit(1, AdditiveFrame::kMaxPartials, maxP);

    // Always keep the loudest N, then (for image frames) order those by
    // frequency so partial index i stays comparable between frames.
    std::sort(cands.begin(), cands.end(),
              [](const Cand& x, const Cand& y) { return x.a > y.a; });

    int n = static_cast<int>(juce::jmin(static_cast<std::size_t>(limit), cands.size()));

    // Drop the inaudible tail (the amplitude sort puts the quietest last).
    // Real material rarely needs all 128 partials, and the render loop cost is
    // proportional to the count.
    while (n > 0 && cands[static_cast<std::size_t>(n - 1)].a <= kMinPartialAmplitude)
        --n;

    if (byFrequency && n > 0)
        std::sort(cands.begin(), cands.begin() + n,
                  [](const Cand& x, const Cand& y) { return x.f < y.f; });

    out.count = n;
    for (int i = 0; i < n; ++i)
    {
        out.frequency[i] = cands[static_cast<std::size_t>(i)].f;
        out.amplitude[i] = cands[static_cast<std::size_t>(i)].a;
        out.phase[i]     = cands[static_cast<std::size_t>(i)].ph;
    }

    return n;
}
}

//==============================================================================
// AdditiveVoice
//==============================================================================

bool AdditiveVoice::isActive() const
{
    return state_.load(std::memory_order_relaxed) != State::free;
}

void AdditiveVoice::noteStarted()
{
    const auto& mpe = getCurrentlyPlayingNote();

    baseFreq  = juce::jmax(1.0f, static_cast<float>(mpe.getFrequencyInHertz()));
    bendRatio = 1.0f;
    velocity  = juce::jlimit(0.0f, 1.0f, mpe.noteOnVelocity.asUnsignedFloat());

    envelopeLevel = 0.0f;
    releaseStart  = 0.0f;
    framePos      = 0.0f;

    const AdditiveFrame* first =
        (bank != nullptr && bank->frameCount > 0) ? &bank->frames[0] : nullptr;
    const int n = (first != nullptr) ? first->count : 0;
    for (int k = 0; k < AdditiveFrame::kMaxPartials; ++k)
    {
        const float ph = (k < n) ? first->phase[k] : 0.0f;
        phasorRe[k] = std::cos(ph);
        phasorIm[k] = std::sin(ph);
    }

    state_.store(State::attack, std::memory_order_release);
}

void AdditiveVoice::noteStopped(bool allowTailOff)
{
    if (allowTailOff)
    {
        State expected = state_.load(std::memory_order_relaxed);
        while (expected >= State::attack && expected <= State::sustain)
        {
            if (state_.compare_exchange_weak(expected, State::release,
                                             std::memory_order_release))
            {
                releaseStart = envelopeLevel;
                break;
            }
        }
    }
    else
    {
        clearCurrentNote();
        state_.store(State::free, std::memory_order_release);
    }
}

void AdditiveVoice::notePressureChanged()    { /* MVP: pressure not yet routed */ }
void AdditiveVoice::noteTimbreChanged()      { /* MVP: timbre not yet routed */ }
void AdditiveVoice::noteKeyStateChanged()    { /* handled by MPESynthesiser */ }

void AdditiveVoice::notePitchbendChanged()
{
    const float f = static_cast<float>(getCurrentlyPlayingNote().getFrequencyInHertz());
    if (baseFreq > 0.0f)
        bendRatio = f / baseFreq;
}

void AdditiveVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                    int startSample, int numSamples)
{
    State currentState = state_.load(std::memory_order_relaxed);
    if (currentState == State::free || bank == nullptr || bank->frameCount <= 0)
        return;

    const double srD = getSampleRate();
    if (srD <= 0.0)
        return;

    const float sr = static_cast<float>(srD);
    const float dt = 1.0f / sr;

    // --- Advance the image position (once per render call = per sub-block) ---
    const int lastFrame = bank->frameCount - 1;
    int f0 = 0, f1 = 0;
    float fMix = 0.0f;

    if (lastFrame > 0)
    {
        const float last = static_cast<float>(lastFrame);
        framePos += framesPerBlock;

        if (framePos >= last)
        {
            if (imageLoop)
                framePos -= last * std::floor(framePos / last);
            else
                framePos = last;
        }
        else if (framePos < 0.0f)
        {
            framePos = 0.0f;
        }

        f0 = juce::jlimit(0, lastFrame, static_cast<int>(framePos));
        f1 = juce::jmin(lastFrame, f0 + 1);
        fMix = juce::jlimit(0.0f, 1.0f, framePos - static_cast<float>(f0));
    }

    // Blend shape (P6 leftover): linear by default, smoothstep eases in/out,
    // step holds frame A and jumps.  Applied per render call - two multiplies.
    fMix = AdditiveSynth::shapeFrameMix(fMix, imageCurve);

    const AdditiveFrame& A = bank->frames[f0];
    const AdditiveFrame& B = bank->frames[f1];
    const int nA = juce::jmin(A.count, AdditiveFrame::kMaxPartials);
    const int nB = juce::jmin(B.count, AdditiveFrame::kMaxPartials);

    // Static images (the common case) read straight from frame 0: no
    // interpolation table is built at all.
    const float* freqSrc = A.frequency;
    const float* ampSrc  = A.amplitude;
    int n = nA;

    float iFreq[AdditiveFrame::kMaxPartials];
    float iAmp [AdditiveFrame::kMaxPartials];

    if (lastFrame > 0)
    {
        n = juce::jmax(nA, nB);

        for (int i = 0; i < n; ++i)
        {
            const bool inA = (i < nA);
            const bool inB = (i < nB);

            if (inA && inB)
            {
                iFreq[i] = A.frequency[i] + (B.frequency[i] - A.frequency[i]) * fMix;
                iAmp[i]  = A.amplitude[i] + (B.amplitude[i] - A.amplitude[i]) * fMix;
            }
            else if (inA)
            {
                iFreq[i] = A.frequency[i];
                iAmp[i]  = A.amplitude[i] * (1.0f - fMix);
            }
            else
            {
                iFreq[i] = B.frequency[i];
                iAmp[i]  = B.amplitude[i] * fMix;
            }
        }

        freqSrc = iFreq;
        ampSrc  = iAmp;
    }

    if (n <= 0)
        return;

    const float ratio = bendRatio * (baseFreq / juce::jmax(1.0f, bank->rootHz));

    // Per-partial rotation coefficients (block rate).
    float cosD[AdditiveFrame::kMaxPartials];
    float sinD[AdditiveFrame::kMaxPartials];
    for (int k = 0; k < n; ++k)
    {
        const float f = juce::jlimit(0.0f, sr * 0.49f, freqSrc[k] * ratio);
        const float d = juce::MathConstants<float>::twoPi * f * dt;
        cosD[k] = std::cos(d);
        sinD[k] = std::sin(d);
    }

    const float attackDt   = attackSeconds  > 0.0f ? dt / attackSeconds  : 1.0f;
    const float decayRange = 1.0f - sustainLevel;
    const float decayDt    = decaySeconds   > 0.0f ? dt * decayRange / decaySeconds : 1.0f;
    const float releaseDt  = releaseSeconds > 0.0f ? dt / releaseSeconds : 1.0f;

    constexpr float kOutputGain = 0.25f;

    const int numChannels = outputBuffer.getNumChannels();
    auto* const* chData = outputBuffer.getArrayOfWritePointers();

    for (int s = 0; s < numSamples; ++s)
    {
        float acc = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            acc += phasorIm[k] * ampSrc[k];
            const float re = phasorRe[k] * cosD[k] - phasorIm[k] * sinD[k];
            const float im = phasorRe[k] * sinD[k] + phasorIm[k] * cosD[k];
            phasorRe[k] = re;
            phasorIm[k] = im;
        }

        switch (currentState)
        {
            case State::attack:
            {
                const float env = envelopeLevel + attackDt;
                envelopeLevel = env;
                if (env >= 0.999f)
                {
                    envelopeLevel = 1.0f;
                    currentState = State::decay;
                    state_.store(State::decay, std::memory_order_relaxed);
                }
                break;
            }
            case State::decay:
            {
                const float env = envelopeLevel - decayDt;
                envelopeLevel = env;
                if (env <= sustainLevel)
                {
                    envelopeLevel = sustainLevel;
                    currentState = State::sustain;
                    state_.store(State::sustain, std::memory_order_relaxed);
                }
                break;
            }
            case State::sustain:
                envelopeLevel = sustainLevel;
                break;
            case State::release:
            {
                const float rsl = releaseStart;
                if (rsl <= 0.0f)
                {
                    envelopeLevel = 0.0f;
                    currentState = State::idle;
                }
                else
                {
                    const float env = envelopeLevel - rsl * releaseDt;
                    envelopeLevel = env;
                    if (env <= 0.0f)
                    {
                        envelopeLevel = 0.0f;
                        currentState = State::idle;
                    }
                }
                break;
            }
            default:
                break;
        }

        const float sample = acc * envelopeLevel * velocity * kOutputGain;
        const int wi = startSample + s;
        for (int ch = 0; ch < numChannels; ++ch)
            chData[ch][wi] += sample;
    }

    if (currentState == State::idle)
    {
        // Return the slot to the pool so MPESynthesiser's default free-voice
        // search (which keys off isActive) can reuse it.
        state_.store(State::free, std::memory_order_release);
        clearCurrentNote();
    }
}

//==============================================================================
// AdditiveSynth
//==============================================================================

AdditiveSynth::AdditiveSynth()
{
    for (int i = 0; i < kInitialVoices; ++i)
        addVoice(new AdditiveVoice());

    setVoiceStealingEnabled(true);
    enableLegacyMode(12, { 1, 17 });   // plain MIDI by default (MPE via setMPEEnabled)
}

float AdditiveSynth::rootHzFrom(int midiNote, float cents)
{
    return 440.0f * std::pow(2.0f,
        (static_cast<float>(midiNote) - 69.0f + cents / 100.0f) / 12.0f);
}

void AdditiveSynth::prepare(double newSampleRate)
{
    setCurrentPlaybackSampleRate(newSampleRate > 0.0 ? newSampleRate : 44100.0);
}

void AdditiveSynth::publishBank(const AdditiveBank& next)
{
    {
        const juce::SpinLock::ScopedLockType sl(partialLock_);
        published_ = next;
    }

    publishedCount_.store(next.frameCount > 0 ? next.frames[0].count : 0,
                          std::memory_order_relaxed);
    publishedFrames_.store(next.frameCount, std::memory_order_relaxed);
    publishedGeneration_.fetch_add(1, std::memory_order_release);
}

void AdditiveSynth::setPartials(const PartialDataSIMD& src)
{
    const int maxP = juce::jlimit(1, kMaxPartials, maxPartials_.load(std::memory_order_relaxed));
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const float nyq = static_cast<float>(sr * 0.5);

    AdditiveBank next;
    next.frameCount = 1;
    buildFrame(src, maxP, nyq, false, next.frames[0]);
    publishBank(next);
}

void AdditiveSynth::setFrames(const std::vector<PartialDataSIMD>& frames)
{
    const int maxP = juce::jlimit(1, kMaxPartials, maxPartials_.load(std::memory_order_relaxed));
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const float nyq = static_cast<float>(sr * 0.5);

    AdditiveBank next;

    if (! frames.empty())
    {
        const int count = static_cast<int>(
            juce::jmin(frames.size(), static_cast<std::size_t>(kMaxFrames)));
        next.frameCount = count;

        for (int i = 0; i < count; ++i)
            buildFrame(frames[static_cast<std::size_t>(i)], maxP, nyq, true, next.frames[i]);
    }

    publishBank(next);
}

void AdditiveSynth::clearPartials()
{
    setPartials(PartialDataSIMD{});
}

void AdditiveSynth::setMaxPartials(int n)
{
    maxPartials_.store(juce::jlimit(1, kMaxPartials, n), std::memory_order_relaxed);
}

void AdditiveSynth::setMaxVoices(int n)
{
    n = juce::jlimit(1, 32, n);
    while (getNumVoices() < n)
        addVoice(new AdditiveVoice());
    if (getNumVoices() > n)
        reduceNumVoices(n);
}

void AdditiveSynth::setRootNote(int midiNote)
{
    rootNote_.store(juce::jlimit(0, 127, midiNote), std::memory_order_relaxed);
}

void AdditiveSynth::setRootFineTune(float cents)
{
    rootFineTune_.store(juce::jlimit(-50.0f, 50.0f, cents), std::memory_order_relaxed);
}

void AdditiveSynth::setMPEEnabled(bool enabled)
{
    if (enabled)
        setZoneLayout(juce::MPEZoneLayout());
    else
        enableLegacyMode(12, { 1, 17 });
}

int AdditiveSynth::getNumActiveVoices() const
{
    int count = 0;
    for (int i = 0; i < getNumVoices(); ++i)
    {
        const auto* v = getVoice(i);
        if (v != nullptr && v->isActive())
            ++count;
    }
    return count;
}

void AdditiveSynth::renderNextSubBlock(juce::AudioBuffer<float>& outputAudio,
                                       int startSample, int numSamples)
{
    const int generation = publishedGeneration_.load(std::memory_order_acquire);
    if (generation != activeGeneration_)
    {
        // Non-blocking: if the message thread is mid-write, keep last good copy.
        const juce::SpinLock::ScopedTryLockType lock(partialLock_);
        if (lock.isLocked())
        {
            active_ = published_;
            activeGeneration_ = generation;
        }
    }

    active_.rootHz = rootHzFrom(rootNote_.load(std::memory_order_relaxed),
                                rootFineTune_.load(std::memory_order_relaxed));

    // Image playback speed for this sub-block (frames per sub-block).
    float framesPerBlock = 0.0f;
    const double sr = getSampleRate();
    if (imageEnabled_.load(std::memory_order_relaxed) && active_.frameCount > 1 && sr > 0.0)
        framesPerBlock = imageRate_.load(std::memory_order_relaxed)
                       * static_cast<float>(static_cast<double>(numSamples) / sr);

    const bool loop  = imageLoop_.load(std::memory_order_relaxed);
    const int  curve = imageCurve_.load(std::memory_order_relaxed);

    for (int i = 0; i < getNumVoices(); ++i)
        if (auto* v = static_cast<AdditiveVoice*>(getVoice(i)))
        {
            v->bank = &active_;
            v->framesPerBlock = framesPerBlock;
            v->imageLoop = loop;
            v->imageCurve = curve;
        }

    for (int ch = 0; ch < outputAudio.getNumChannels(); ++ch)
        outputAudio.clear(ch, startSample, numSamples);

    MPESynthesiser::renderNextSubBlock(outputAudio, startSample, numSamples);
}

} // namespace ana
