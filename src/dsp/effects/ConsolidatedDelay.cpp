#include "ConsolidatedDelay.h"
#include <cmath>

namespace ana {

//==============================================================================
// Construction
//==============================================================================
ConsolidatedDelay::ConsolidatedDelay() {}

//==============================================================================
// Prepare / Reset
//==============================================================================
void ConsolidatedDelay::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);

    // --- Delay lines (max 2 s per channel) ---
    delayLines_.resize(numChannels);
    for (auto& l : delayLines_) {
        l.prepare(spec);
        l.setMaximumDelayInSamples(static_cast<int>(spec.sampleRate * 2.0));
    }

    // --- Dry buffer ---
    dryBuffer_.setSize(numChannels, static_cast<int>(spec.maximumBlockSize), false, false, true);

    // --- Reverse buffer (max 2 s) ---
    const auto maxReverseSamps = static_cast<int>(spec.sampleRate * 2.0);
    reverseBuffer_.setSize(numChannels, maxReverseSamps, false, false, true);
    reverseBuffer_.clear();
    reverseWritePos_ = 0;

    // --- Wet filters ---
    wetFilters_.resize(numChannels);
    for (auto& f : wetFilters_) {
        f.hpf.prepare(spec);
        f.lpf.prepare(spec);
        f.dirty = true;
    }
    updateWetFilters();

    // --- Tape per-channel state ---
    tapeFbLP_.resize(numChannels);
    tapePhase_ = 0.0;

    // --- Ducking ---
    duckEnv_ = 0.0f;

    reset();
}

void ConsolidatedDelay::reset() {
    for (auto& l : delayLines_)
        l.reset();
    reverseBuffer_.clear();
    reverseWritePos_ = 0;
    tapePhase_ = 0.0;
    duckEnv_ = 0.0f;
    for (auto& f : wetFilters_) {
        f.hpf.reset();
        f.lpf.reset();
    }
    for (auto& t : tapeFbLP_)
        t.y1 = 0.0f;
}

//==============================================================================
// Main process() — switch dispatch
//==============================================================================
void ConsolidatedDelay::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed)
        return;

    const auto numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    // Save dry signal for post-filter dry/wet mix
    dryBuffer_.makeCopyOf(buffer, true);

    const float delaySamples = getCurrentTimeSamples();

    // Refresh dirty wet-filter coefficients (all channels toggled together)
    if (!wetFilters_.empty() && wetFilters_[0].dirty)
        updateWetFilters();

    const auto m = static_cast<DelayMode>(modeAtomic_.load(std::memory_order_relaxed));
    switch (m) {
        case DelayMode::Mono:     processMono(buffer, delaySamples);     break;
        case DelayMode::Stereo:   processStereo(buffer, delaySamples);   break;
        case DelayMode::PingPong: processPingPong(buffer, delaySamples); break;
        case DelayMode::Reverse:  processReverse(buffer, delaySamples);  break;
        case DelayMode::Tape:     processTape(buffer, delaySamples);     break;
        case DelayMode::Ducking:  processDucking(buffer, delaySamples);  break;
    }

    // --- Apply wet HPF → LPF cascade ---
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* data = buffer.getWritePointer(ch);
        auto& f = wetFilters_[static_cast<size_t>(ch)];

        juce::dsp::AudioBlock<float> block(&data, 1, static_cast<size_t>(numSamples));
        f.hpf.process(juce::dsp::ProcessContextReplacing<float>(block));
        f.lpf.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    // --- Dry / wet mix ---
    if (mixVal < 1.0f) {
        const auto numCh = buffer.getNumChannels();
        for (int ch = 0; ch < numCh; ++ch) {
            const auto* dry = dryBuffer_.getReadPointer(ch);
            auto* wet = buffer.getWritePointer(ch);
            for (int s = 0; s < numSamples; ++s)
                wet[s] = dry[s] * (1.0f - mixVal) + wet[s] * mixVal;
        }
    }
}

//==============================================================================
// Shared setters (with clamping)
//==============================================================================
void ConsolidatedDelay::setTime(float ms) {
    timeMs = juce::jlimit(20.0f, 2000.0f, ms);
}

void ConsolidatedDelay::setFeedback(float val) {
    feedback = juce::jlimit(0.0f, 0.99f, val);
}

void ConsolidatedDelay::setMix(float val) {
    mixVal = juce::jlimit(0.0f, 1.0f, val);
}

void ConsolidatedDelay::setWetHPF(float hz) {
    wetHPFHz = juce::jlimit(20.0f, 20000.0f, hz);
    for (auto& f : wetFilters_)
        f.dirty = true;
}

void ConsolidatedDelay::setWetLPF(float hz) {
    wetLPFHz = juce::jlimit(20.0f, 20000.0f, hz);
    for (auto& f : wetFilters_)
        f.dirty = true;
}

void ConsolidatedDelay::setMode(DelayMode m) {
    modeAtomic_.store(static_cast<int>(m), std::memory_order_relaxed);
}

//==============================================================================
// Reverse setters
//==============================================================================
void ConsolidatedDelay::setWindowLength(float ms) {
    windowLengthMs = juce::jlimit(50.0f, 1000.0f, ms);
}

//==============================================================================
// Tape setters
//==============================================================================
void ConsolidatedDelay::setWowFlutter(float val) {
    wowFlutter = juce::jlimit(0.0f, 1.0f, val);
}

void ConsolidatedDelay::setTone(float val) {
    toneVal = juce::jlimit(0.0f, 1.0f, val);
}

//==============================================================================
// Ducking setters
//==============================================================================
void ConsolidatedDelay::setThreshold(float dB) {
    thresholdDB = juce::jlimit(-60.0f, 0.0f, dB);
}

void ConsolidatedDelay::setDuckRelease(float ms) {
    duckReleaseMs = juce::jlimit(10.0f, 1000.0f, ms);
}

//==============================================================================
void ConsolidatedDelay::setBypass(bool b) {
    bypassed = b;
}

//==============================================================================
// Helpers
//==============================================================================
float ConsolidatedDelay::getCurrentTimeSamples() const noexcept {
    return timeMs * static_cast<float>(sampleRate) / 1000.0f;
}

void ConsolidatedDelay::updateWetFilters() {
    const auto hpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, wetHPFHz);
    const auto lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, wetLPFHz);
    for (auto& f : wetFilters_) {
        f.hpf.coefficients = hpfCoeffs;
        f.lpf.coefficients = lpfCoeffs;
        f.dirty = false;
    }
}

//==============================================================================
// processMono — sum to mono, single delay line, broadcast to all channels
//==============================================================================
void ConsolidatedDelay::processMono(juce::AudioBuffer<float>& buffer, float delaySamples) {
    const auto numSamples = buffer.getNumSamples();
    const auto numCh = buffer.getNumChannels();

    for (int s = 0; s < numSamples; ++s) {
        // Sum all channels to mono
        float monoIn = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            monoIn += buffer.getSample(ch, s);
        monoIn /= static_cast<float>(numCh);

        const float delayed = delayLines_[0].popSample(0, delaySamples);
        delayLines_[0].pushSample(0, monoIn + delayed * feedback);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample(ch, s, delayed);
    }
}

//==============================================================================
// processStereo — independent delay lines per channel
//==============================================================================
void ConsolidatedDelay::processStereo(juce::AudioBuffer<float>& buffer, float delaySamples) {
    const auto numSamples = buffer.getNumSamples();
    const auto numCh = juce::jmin(buffer.getNumChannels(),
                                  static_cast<int>(delayLines_.size()));

    for (int s = 0; s < numSamples; ++s) {
        for (int ch = 0; ch < numCh; ++ch) {
            const float input = buffer.getSample(ch, s);
            const float delayed = delayLines_[ch].popSample(ch, delaySamples);
            delayLines_[ch].pushSample(ch, input + delayed * feedback);
            buffer.setSample(ch, s, delayed);
        }
    }
}

//==============================================================================
// processPingPong — delay bounces left → right → left ...
//==============================================================================
void ConsolidatedDelay::processPingPong(juce::AudioBuffer<float>& buffer, float delaySamples) {
    const auto numSamples = buffer.getNumSamples();
    const auto numCh = buffer.getNumChannels();

    // Fallback to mono for single-channel
    if (numCh < 2) {
        processMono(buffer, delaySamples);
        return;
    }

    for (int s = 0; s < numSamples; ++s) {
        // Read from both delay lines
        const float inL = buffer.getSample(0, s);
        const float inR = buffer.getSample(1, s);
        const float delL = delayLines_[0].popSample(0, delaySamples);
        const float delR = delayLines_[1].popSample(1, delaySamples);

        // Cross-feed: L delay gets R's feedback, R delay gets L's feedback
        delayLines_[0].pushSample(0, inL + delR * feedback);
        delayLines_[1].pushSample(1, inR + delL * feedback);

        // Output: L gets R's delayed signal, R gets L's delayed signal
        buffer.setSample(0, s, delR);
        buffer.setSample(1, s, delL);

        // Mute any extra channels
        for (int ch = 2; ch < numCh; ++ch)
            buffer.setSample(ch, s, 0.0f);
    }
}

//==============================================================================
// processReverse — continuously record input, read backwards within window
//==============================================================================
void ConsolidatedDelay::processReverse(juce::AudioBuffer<float>& buffer, float /*delaySamples*/) {
    const auto numSamples = buffer.getNumSamples();
    const auto numCh = juce::jmin(buffer.getNumChannels(),
                                  reverseBuffer_.getNumChannels());
    const auto windowSamps = static_cast<int>(windowLengthMs * sampleRate / 1000.0f);
    const auto bufSize = reverseBuffer_.getNumSamples();

    if (windowSamps < 2 || bufSize < 2)
        return;

    // Snapshot the write position at block start so the reverse read-position
    // formula works correctly (both writePos and s advance in lockstep,
    // so we need a fixed reference point).
    const int startWritePos = reverseWritePos_;

    for (int s = 0; s < numSamples; ++s) {
        // Write current input frame into circular buffer
        for (int ch = 0; ch < numCh; ++ch)
            reverseBuffer_.setSample(ch, reverseWritePos_, buffer.getSample(ch, s));

        // Reverse read: step backwards within the window from the most recent
        // sample at the time this block started.
        const int offset = s % windowSamps;
        int readPos = startWritePos - 1 - offset;
        while (readPos < 0)
            readPos += bufSize;
        readPos %= bufSize;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample(ch, s, reverseBuffer_.getSample(ch, readPos));

        // Advance write head
        reverseWritePos_ = (reverseWritePos_ + 1) % bufSize;
    }
}

//==============================================================================
// processTape — tape-echo with wow/flutter modulation and feedback tone
//==============================================================================
void ConsolidatedDelay::processTape(juce::AudioBuffer<float>& buffer, float delaySamples) {
    const auto numSamples = buffer.getNumSamples();
    const auto numCh = juce::jmin(buffer.getNumChannels(),
                                  static_cast<int>(delayLines_.size()));

    // Wow & flutter rates
    static constexpr double wowRate = 3.0;      // Hz
    static constexpr double flutterRate = 14.0;  // Hz

    // Tone filter: map toneVal 0–1 to LP cutoff 20 kHz → 200 Hz
    const float fbLPFreq = 20000.0f * std::exp(-toneVal * 4.6f);
    const float fbLPCoeff = std::exp(-2.0 * juce::MathConstants<double>::pi * fbLPFreq / sampleRate);

    for (int s = 0; s < numSamples; ++s) {
        // Advance LFO phase
        tapePhase_ += juce::MathConstants<double>::twoPi * wowRate / sampleRate;
        if (tapePhase_ > juce::MathConstants<double>::twoPi)
            tapePhase_ -= juce::MathConstants<double>::twoPi;

        const double flutterPhase = tapePhase_ * (flutterRate / wowRate);

        // Modulation: wow (±3% max) + flutter (±1% max) scaled by wowFlutter
        const float mod = 1.0f + wowFlutter * (
            static_cast<float>(0.03 * std::sin(tapePhase_)) +
            static_cast<float>(0.01 * std::sin(flutterPhase)));

        const float modDelay = delaySamples * mod;

        for (int ch = 0; ch < numCh; ++ch) {
            const float input = buffer.getSample(ch, s);
            const float delayed = delayLines_[ch].popSample(ch, modDelay);

            // Tone: 1-pole LP on feedback path
            auto& fbLP = tapeFbLP_[static_cast<size_t>(ch)];
            const float fbFiltered = fbLP.y1 = delayed * (1.0f - fbLPCoeff) + fbLP.y1 * fbLPCoeff;

            delayLines_[ch].pushSample(ch, input + fbFiltered * feedback);
            buffer.setSample(ch, s, delayed);
        }
    }
}

//==============================================================================
// processDucking — delay output attenuates when input exceeds threshold
//==============================================================================
void ConsolidatedDelay::processDucking(juce::AudioBuffer<float>& buffer, float delaySamples) {
    const auto numSamples = buffer.getNumSamples();
    const auto numCh = juce::jmin(buffer.getNumChannels(),
                                  static_cast<int>(delayLines_.size()));
    const float thresholdLin = juce::Decibels::decibelsToGain(thresholdDB);
    const float releaseCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(
        duckReleaseMs * sampleRate / 1000.0f));

    for (int s = 0; s < numSamples; ++s) {
        // Detect peak level across all channels
        float level = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            level = juce::jmax(level, std::abs(buffer.getSample(ch, s)));

        // Envelope follower — instant attack, programmable release
        if (level > duckEnv_)
            duckEnv_ = level;
        else
            duckEnv_ += (level - duckEnv_) * releaseCoeff;

        // Duck gain: 1.0 when below threshold, drops toward 0 above
        float duckGain = 1.0f;
        if (duckEnv_ > thresholdLin && duckEnv_ > 0.0f)
            duckGain = thresholdLin / duckEnv_;

        for (int ch = 0; ch < numCh; ++ch) {
            const float input = buffer.getSample(ch, s);
            const float delayed = delayLines_[ch].popSample(ch, delaySamples);

            const float ducked = delayed * duckGain;

            // Feed ducked signal back to prevent buildup
            delayLines_[ch].pushSample(ch, input + ducked * feedback);

            buffer.setSample(ch, s, ducked);
        }
    }
}

//==============================================================================
// State serialisation (S3 pattern — every setter called from setState)
//==============================================================================
juce::ValueTree ConsolidatedDelay::getState() const {
    juce::ValueTree tree("ConsolidatedDelay");
    tree.setProperty("mode",      modeAtomic_.load(std::memory_order_relaxed), nullptr);
    tree.setProperty("timeMs",    static_cast<double>(timeMs),    nullptr);
    tree.setProperty("feedback",  static_cast<double>(feedback),  nullptr);
    tree.setProperty("mix",       static_cast<double>(mixVal),    nullptr);
    tree.setProperty("wetHPF",    static_cast<double>(wetHPFHz),  nullptr);
    tree.setProperty("wetLPF",    static_cast<double>(wetLPFHz),  nullptr);
    tree.setProperty("bypass",    bypassed,                       nullptr);
    tree.setProperty("windowLen", static_cast<double>(windowLengthMs), nullptr);
    tree.setProperty("wowFlut",   static_cast<double>(wowFlutter), nullptr);
    tree.setProperty("tone",      static_cast<double>(toneVal),   nullptr);
    tree.setProperty("threshold", static_cast<double>(thresholdDB),   nullptr);
    tree.setProperty("duckRel",   static_cast<double>(duckReleaseMs), nullptr);
    return tree;
}

void ConsolidatedDelay::setState(const juce::ValueTree& tree) {
    // Every setter is called here — S3 security pattern ensures bounds
    // are enforced by the setter itself, not by ad-hoc clamping.
    setMode(static_cast<DelayMode>(
        juce::jlimit(0, 5,
            static_cast<int>(tree.getProperty("mode", static_cast<int>(DelayMode::Stereo))))));

    // Shared
    setTime(static_cast<float>(tree.getProperty("timeMs", 250.0)));
    setFeedback(static_cast<float>(tree.getProperty("feedback", 0.3)));
    setMix(static_cast<float>(tree.getProperty("mix", 0.3)));
    setWetHPF(static_cast<float>(tree.getProperty("wetHPF", 20.0)));
    setWetLPF(static_cast<float>(tree.getProperty("wetLPF", 20000.0)));
    setBypass(tree.getProperty("bypass", false));

    // Reverse
    setWindowLength(static_cast<float>(tree.getProperty("windowLen", 200.0)));

    // Tape
    setWowFlutter(static_cast<float>(tree.getProperty("wowFlut", 0.0)));
    setTone(static_cast<float>(tree.getProperty("tone", 0.5)));

    // Ducking
    setThreshold(static_cast<float>(tree.getProperty("threshold", -24.0)));
    setDuckRelease(static_cast<float>(tree.getProperty("duckRel", 200.0)));
}

} // namespace ana
