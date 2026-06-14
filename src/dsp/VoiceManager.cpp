#include "VoiceManager.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace ana {

//==============================================================================
// Local helpers
//==============================================================================

namespace {

constexpr float clampFloat(float value, float min, float max)
{
    return value < min ? min : (value > max ? max : value);
}

constexpr int clampInt(int value, int min, int max)
{
    return value < min ? min : (value > max ? max : value);
}

constexpr float twoPi = 6.2831853071795864769f;

} // namespace

//==============================================================================
// Construction
//==============================================================================

VoiceManager::VoiceManager() = default;

//==============================================================================
// Preparation
//==============================================================================

void VoiceManager::prepare(double sr)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
}

//==============================================================================
// Note events
//==============================================================================

void VoiceManager::noteOn(int note, float velocity)
{
    note    = clampInt(note, 0, 127);
    velocity = clampFloat(velocity, 0.0f, 1.0f);

    int idx = allocateVoice();
    if (idx < 0)
        return;

    startVoice(idx, note, velocity);
}

void VoiceManager::noteOff(int note)
{
    note = clampInt(note, 0, 127);

    for (auto& v : voices)
    {
        if (v.note != note)
            continue;

        // When MPE is enabled, we need the specific channel to match.
        // Without it, we match any voice on that note (legacy behaviour).
        if (mpeEnabled && v.midiChannel < 1)
            continue;

        // Atomically transition from attack/decay/sustain -> release
        VoiceState expected = v.state.load(std::memory_order_relaxed);
        while (expected >= VoiceState::attack && expected <= VoiceState::sustain)
        {
            if (v.state.compare_exchange_weak(expected, VoiceState::release, std::memory_order_release))
            {
                v.releaseStartLevel.store(v.envelopeLevel.load(std::memory_order_relaxed), std::memory_order_release);
                break;
            }
        }
    }
}

void VoiceManager::noteOffWithChannel(int midiChannel, int note)
{
    note = clampInt(note, 0, 127);

    for (auto& v : voices)
    {
        if (v.note != note || v.midiChannel != midiChannel)
            continue;

        VoiceState expected = v.state.load(std::memory_order_relaxed);
        while (expected >= VoiceState::attack && expected <= VoiceState::sustain)
        {
            if (v.state.compare_exchange_weak(expected, VoiceState::release, std::memory_order_release))
            {
                v.releaseStartLevel.store(v.envelopeLevel.load(std::memory_order_relaxed), std::memory_order_release);
                break;
            }
        }
    }
}

void VoiceManager::noteOnWithChannel(int midiChannel, int note, float velocity, double sr)
{
    note    = clampInt(note, 0, 127);
    velocity = clampFloat(velocity, 0.0f, 1.0f);

    int idx = allocateVoice();
    if (idx < 0)
        return;

    startVoice(idx, note, velocity);
    voices[idx].midiChannel = midiChannel;

    // Recompute phasor delta with proper sample rate
    auto& v = voices[idx];
    v.cosDelta = std::cos(twoPi * v.pitchHz.load(std::memory_order_relaxed) / static_cast<float>(sr > 0.0 ? sr : sampleRate));
    v.sinDelta = std::sin(twoPi * v.pitchHz.load(std::memory_order_relaxed) / static_cast<float>(sr > 0.0 ? sr : sampleRate));
}

void VoiceManager::allVoicesOff()
{
    for (auto& v : voices)
    {
        VoiceState expected = VoiceState::attack;
        if (v.state.compare_exchange_strong(expected, VoiceState::release))
        {
            v.releaseStartLevel.store(v.envelopeLevel.load(std::memory_order_relaxed), std::memory_order_release);
            continue;
        }

        expected = VoiceState::decay;
        if (v.state.compare_exchange_strong(expected, VoiceState::release))
        {
            v.releaseStartLevel.store(v.envelopeLevel.load(std::memory_order_relaxed), std::memory_order_release);
            continue;
        }

        expected = VoiceState::sustain;
        if (v.state.compare_exchange_strong(expected, VoiceState::release))
        {
            v.releaseStartLevel.store(v.envelopeLevel.load(std::memory_order_relaxed), std::memory_order_release);
            continue;
        }
    }
}

//==============================================================================
// Audio processing
//==============================================================================

void VoiceManager::process(juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0)
        return;

    buffer.clear();

    const float dt = 1.0f / static_cast<float>(sampleRate);

    for (int v = 0; v < maxVoices; ++v)
    {
        if (voices[v].state.load(std::memory_order_relaxed) == VoiceState::free)
            continue;

        for (int s = 0; s < numSamples; ++s)
        {
            // Generate the sample value for this voice at the current phase
            float sample = generateSample(v);

            // Apply state-variable filter modulated by MPE timbre
            if (mpeEnabled)
                sample = applyFilter(v, sample);

            // Advance the envelope one sample
            updateEnvelope(v, dt);

            // Advance the oscillator phase (including pitch bend)
            voices[v].phase += twoPi * voices[v].pitchHz.load(std::memory_order_relaxed) * voices[v].pitchBend.load(std::memory_order_relaxed) * dt;
            if (voices[v].phase >= twoPi)
                voices[v].phase -= twoPi;

            // Accumulate into all output channels
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addSample(ch, s, sample);
        }
    }
}

//==============================================================================
// Voice allocation
//==============================================================================

int VoiceManager::allocateVoice()
{
    // --- Phase 1: find and atomically claim a FREE voice ---
    if (allocationMode.load(std::memory_order_relaxed) == AllocationMode::roundRobin)
    {
        for (int i = 0; i < maxVoices; ++i)
        {
            const int candidate = (nextVoiceIndex + i) % maxVoices;
            VoiceState expected = VoiceState::free;
            if (voices[candidate].state.compare_exchange_strong(expected, VoiceState::attack))
            {
                nextVoiceIndex = (candidate + 1) % maxVoices;
                return candidate;
            }
        }
    }
    else if (allocationMode.load(std::memory_order_relaxed) == AllocationMode::oldestFirst)
    {
        for (int i = 0; i < maxVoices; ++i)
        {
            VoiceState expected = VoiceState::free;
            if (voices[i].state.compare_exchange_strong(expected, VoiceState::attack))
                return i;
        }
    }
    else // random
    {
        // Try random slots; up to maxVoices attempts
        for (int attempt = 0; attempt < maxVoices; ++attempt)
        {
            const int candidate = juce::Random::getSystemRandom().nextInt(maxVoices);
            VoiceState expected = VoiceState::free;
            if (voices[candidate].state.compare_exchange_strong(expected, VoiceState::attack))
            {
                nextVoiceIndex = (candidate + 1) % maxVoices;
                return candidate;
            }
        }
    }

    // --- Phase 2: find and atomically claim an IDLE voice ---
    {
        int      bestIdx = -1;
        uint64_t bestAge = 0;

        for (int i = 0; i < maxVoices; ++i)
        {
            VoiceState expected = VoiceState::idle;
            if (voices[i].state.compare_exchange_strong(expected, VoiceState::attack))
            {
                if (bestIdx < 0 || voices[i].noteOnIndex < bestAge)
                {
                    // Release previously claimed idle, keep this better one
                    if (bestIdx >= 0)
                        voices[bestIdx].state.store(VoiceState::idle);
                    bestIdx = i;
                    bestAge  = voices[i].noteOnIndex;
                }
                else
                {
                    // This claim is worse, release it
                    voices[i].state.store(VoiceState::idle);
                }
            }
        }

        if (bestIdx >= 0)
            return bestIdx;
    }

    // --- Phase 3: steal a voice ---
    {
        const int bestIdx = stealVoice();
        if (bestIdx >= 0)
        {
            // Atomically claim from whatever state (CAS loop with compare_exchange_weak)
            VoiceState current = voices[bestIdx].state.load();
            while (!voices[bestIdx].state.compare_exchange_weak(current, VoiceState::attack))
            {
                // current is automatically reloaded on failure; retry
            }
            return bestIdx;
        }
    }

    return -1;
}

int VoiceManager::stealVoice()
{
    int      bestIdx      = -1;
    int      bestPriority = -1;
    uint64_t bestAge      = 0;

    for (int i = 0; i < maxVoices; ++i)
    {
        int priority = 0;

        switch (voices[i].state.load(std::memory_order_relaxed))
        {
            case VoiceState::sustain: priority = 4; break;
            case VoiceState::release: priority = 3; break;
            case VoiceState::decay:   priority = 2; break;
            case VoiceState::attack:  priority = 1; break;
            default:                  continue;     // skip free/idle
        }

        // Prefer higher-priority (more stealable) state.
        // Within the same state, prefer the oldest voice (lowest noteOnIndex).
        if (priority > bestPriority
            || (priority == bestPriority && voices[i].noteOnIndex < bestAge))
        {
            bestPriority = priority;
            bestAge      = voices[i].noteOnIndex;
            bestIdx      = i;
        }
    }

    return bestIdx;
}

//==============================================================================
// Voice lifecycle
//==============================================================================

void VoiceManager::startVoice(int voiceIndex, int note, float velocity)
{
    auto& v = voices[voiceIndex];

    v.note              = note;
    v.velocity          = velocity;
    v.pitchHz.store(noteToFrequency(note), std::memory_order_relaxed);
    v.amplitude.store(applyVelocityCurve(velocity), std::memory_order_relaxed);
    v.phase             = 0.0f;
    v.phasorRe          = 1.0f;
    v.phasorIm          = 0.0f;
    v.cosDelta          = std::cos(twoPi * v.pitchHz.load(std::memory_order_relaxed) / sampleRate);
    v.sinDelta          = std::sin(twoPi * v.pitchHz.load(std::memory_order_relaxed) / sampleRate);
    v.envelopeLevel.store(0.0f, std::memory_order_relaxed);
    v.releaseStartLevel.store(0.0f, std::memory_order_relaxed);
    v.noteOnIndex       = globalNoteCounter++;
    v.aftertouch.store(0.0f, std::memory_order_relaxed);
    v.pitchBend.store(1.0f, std::memory_order_relaxed);

    // --- Initialize MPE fields ---
    v.midiChannel = -1;
    v.slideAmount = 0.0f;
    v.pressure    = 0.0f;
    v.timbre      = 0.0f;

    // --- Reset filter states ---
    resetFilter(voiceIndex);

    // Calculate adaptive envelope scale based on pitch (C4 = 60 is center)
    // Positive amount makes higher notes shorter.
    v.envScale = std::exp2f(-adaptiveEnvelopeAmount * (static_cast<float>(note) - 60.0f) / 12.0f);

    // Apply default ADSR values scaled by adaptive amount
    v.attackSeconds  = defaultAttack * v.envScale;
    v.decaySeconds   = defaultDecay * v.envScale;
    v.sustainLevel   = defaultSustain;
    v.releaseSeconds = defaultRelease * v.envScale;

    // State is already set to attack by allocateVoice() via CAS; no need to set it here.
}

//==============================================================================
// Envelope
//==============================================================================

void VoiceManager::updateEnvelope(int voiceIndex, float dt)
{
    auto& v = voices[voiceIndex];

    switch (v.state.load(std::memory_order_relaxed))
    {
        case VoiceState::attack:
        {
            if (v.attackSeconds <= 0.0f)
            {
                v.envelopeLevel.store(1.0f, std::memory_order_relaxed);
                VoiceState expected = VoiceState::attack;
                v.state.compare_exchange_strong(expected, VoiceState::decay,
                    std::memory_order_relaxed, std::memory_order_relaxed);
            }
            else
            {
                float env = v.envelopeLevel.load(std::memory_order_relaxed);
                env += dt / v.attackSeconds;
                v.envelopeLevel.store(env, std::memory_order_relaxed);
                if (env >= 1.0f)
                {
                    v.envelopeLevel.store(1.0f, std::memory_order_relaxed);
                    VoiceState expected = VoiceState::attack;
                    v.state.compare_exchange_strong(expected, VoiceState::decay,
                        std::memory_order_relaxed, std::memory_order_relaxed);
                }
            }
            break;
        }

        case VoiceState::decay:
        {
            if (v.decaySeconds <= 0.0f)
            {
                v.envelopeLevel.store(v.sustainLevel, std::memory_order_relaxed);
                VoiceState expected = VoiceState::decay;
                v.state.compare_exchange_strong(expected, VoiceState::sustain,
                    std::memory_order_relaxed, std::memory_order_relaxed);
            }
            else
            {
                const float decayRange = 1.0f - v.sustainLevel;
                float env = v.envelopeLevel.load(std::memory_order_relaxed);
                env -= dt * decayRange / v.decaySeconds;
                v.envelopeLevel.store(env, std::memory_order_relaxed);
                if (env <= v.sustainLevel)
                {
                    v.envelopeLevel.store(v.sustainLevel, std::memory_order_relaxed);
                    VoiceState expected = VoiceState::decay;
                    v.state.compare_exchange_strong(expected, VoiceState::sustain,
                        std::memory_order_relaxed, std::memory_order_relaxed);
                }
            }
            break;
        }

        case VoiceState::sustain:
        {
            v.envelopeLevel.store(v.sustainLevel, std::memory_order_relaxed);
            break;
        }

        case VoiceState::release:
        {
            if (v.releaseSeconds <= 0.0f || v.releaseStartLevel.load(std::memory_order_relaxed) <= 0.0f)
            {
                v.envelopeLevel.store(0.0f, std::memory_order_relaxed);
                VoiceState expected = VoiceState::release;
                v.state.compare_exchange_strong(expected, VoiceState::idle,
                    std::memory_order_relaxed, std::memory_order_relaxed);
            }
            else
            {
                float env = v.envelopeLevel.load(std::memory_order_relaxed);
                env -= dt * v.releaseStartLevel.load(std::memory_order_relaxed) / v.releaseSeconds;
                v.envelopeLevel.store(env, std::memory_order_relaxed);
                if (env <= 0.0f)
                {
                    v.envelopeLevel.store(0.0f, std::memory_order_relaxed);
                    VoiceState expected = VoiceState::release;
                    v.state.compare_exchange_strong(expected, VoiceState::idle,
                        std::memory_order_relaxed, std::memory_order_relaxed);
                }
            }
            break;
        }

        default:
            break;
    }
}

//==============================================================================
// Sample generation
//==============================================================================

float VoiceManager::generateSample(int voiceIndex)
{
    auto& v = voices[voiceIndex];
    // Combine velocity amplitude, envelope, aftertouch, and MPE pressure
    const float amp = v.envelopeLevel.load(std::memory_order_relaxed)
                    * v.amplitude.load(std::memory_order_relaxed)
                    * (1.0f + v.aftertouch.load(std::memory_order_relaxed) * 0.5f)
                    * (1.0f + v.pressure * 0.5f);  // MPE pressure adds gain
    float sample = v.phasorIm * amp;
    float re = v.phasorRe * v.cosDelta - v.phasorIm * v.sinDelta;
    float im = v.phasorRe * v.sinDelta + v.phasorIm * v.cosDelta;
    v.phasorRe = re;
    v.phasorIm = im;
    return sample;
}

float VoiceManager::applyFilter(int voiceIndex, float sample)
{
    auto& v = voices[voiceIndex];

    // State-variable filter (Chamberlin style)
    // Cutoff is modulated by MPE timbre [0,1]:
    //   timbre=0 → ~200Hz, timbre=1 → ~Nyquist
    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    const float minCutoff = 200.0f;
    const float maxCutoff = nyquist * 0.95f;

    // Map timbre [0,1] to cutoff with exponential curve for musical feel
    const float t = clampFloat(v.timbre, 0.0f, 1.0f);
    const float cutoff = minCutoff + (maxCutoff - minCutoff) * t * t;

    const float fc = clampFloat(cutoff / nyquist, 0.001f, 0.95f);
    const float f = fc * 1.5f;           // slight resonance compensation
    const float q = 0.5f;                // fixed resonance (gentle)
    const float scale = 1.0f / (1.0f + 2.0f * q * f + f * f);

    // Two-sample TDF2 (Transposed Direct Form 2)
    const float hp = (sample - v.lp1 * (2.0f * q * f + f * f) - v.bp1 * (1.0f + 2.0f * q * f))
                     * scale;
    const float bp = v.bp1 + f * hp;
    const float lp = v.lp1 + f * bp;

    // Store new states
    v.lp1 = lp;
    v.bp1 = bp;
    v.hp0 = hp;

    // Output: blend lowpass and highpass based on timbre for brightness sweep
    // timbre=0 → full lowpass, timbre=1 → full highpass
    const float mix = clampFloat(t, 0.0f, 1.0f);
    return lp * (1.0f - mix) + hp * mix;
}

void VoiceManager::resetFilter(int voiceIndex)
{
    auto& v = voices[voiceIndex];
    v.lp0 = 0.0f;
    v.lp1 = 0.0f;
    v.bp0 = 0.0f;
    v.bp1 = 0.0f;
    v.hp0 = 0.0f;
    v.hp1 = 0.0f;
}

//==============================================================================
// MIDI conversion
//==============================================================================

float VoiceManager::noteToFrequency(int note)
{
    // A4 (note 69) = 440 Hz
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

//==============================================================================
// Allocation mode
//==============================================================================

void VoiceManager::setAllocationMode(AllocationMode mode)
{
    allocationMode.store(mode, std::memory_order_relaxed);
}

AllocationMode VoiceManager::getAllocationMode() const
{
    return allocationMode.load(std::memory_order_relaxed);
}

//==============================================================================
// Per-voice ADSR
//==============================================================================

void VoiceManager::setVoiceAttack(int voiceIndex, float seconds)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    voices[voiceIndex].attackSeconds = clampFloat(seconds, 0.0f, 10.0f);
}

void VoiceManager::setVoiceDecay(int voiceIndex, float seconds)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    voices[voiceIndex].decaySeconds = clampFloat(seconds, 0.0f, 10.0f) * voices[voiceIndex].envScale;
}

void VoiceManager::setVoiceSustain(int voiceIndex, float level)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    voices[voiceIndex].sustainLevel = clampFloat(level, 0.0f, 1.0f);
}

void VoiceManager::setVoiceRelease(int voiceIndex, float seconds)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    voices[voiceIndex].releaseSeconds = clampFloat(seconds, 0.0f, 10.0f) * voices[voiceIndex].envScale;
}

//==============================================================================
// Default ADSR
//==============================================================================

void VoiceManager::setDefaultAttack(float seconds)
{
    defaultAttack = clampFloat(seconds, 0.0f, 10.0f);
}

void VoiceManager::setDefaultDecay(float seconds)
{
    defaultDecay = clampFloat(seconds, 0.0f, 10.0f);
}

void VoiceManager::setDefaultSustain(float level)
{
    defaultSustain = clampFloat(level, 0.0f, 1.0f);
}

void VoiceManager::setDefaultRelease(float seconds)
{
    defaultRelease = clampFloat(seconds, 0.0f, 10.0f);
}

//==============================================================================
// Queries
//==============================================================================

int VoiceManager::getNumActiveVoices() const
{
    int count = 0;
    for (const auto& v : voices)
    {
        if (v.state.load(std::memory_order_acquire) != VoiceState::free)
            ++count;
    }
    return count;
}

bool VoiceManager::isVoiceActive(int voiceIndex) const
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return false;
    return voices[voiceIndex].state.load(std::memory_order_acquire) != VoiceState::free;
}

const Voice& VoiceManager::getVoice(int voiceIndex) const
{
    // Silently clamp; caller is expected to validate.
    const int idx = clampInt(voiceIndex, 0, maxVoices - 1);
    return voices[idx];
}

//==============================================================================
// Per-voice modulation
//==============================================================================

void VoiceManager::setVoiceAftertouch(int voiceIndex, float amount)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    voices[voiceIndex].aftertouch.store(clampFloat(amount, 0.0f, 1.0f), std::memory_order_relaxed);
}

void VoiceManager::setVoicePitchBend(int voiceIndex, float bend)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    // Map [-1, 1] to [0.5, 2.0] pitch multiplier
    //   -1   -> 0.5  (down one octave)
    //    0   -> 1.0  (unity)
    //    1   -> 2.0  (up one octave)
    const float clamped = clampFloat(bend, -1.0f, 1.0f);
    voices[voiceIndex].pitchBend.store(std::exp2f(clamped), std::memory_order_relaxed);
}

//==============================================================================
// Velocity curve
//==============================================================================

void VoiceManager::setVelocityCurve(float amount)
{
    velocityCurveAmount = clampFloat(amount, 0.0f, 1.0f);
}

float VoiceManager::getVelocityCurve() const
{
    return velocityCurveAmount;
}

float VoiceManager::applyVelocityCurve(float velocity) const
{
    // 0.0 = linear (identity), 1.0 = exponential (more compression)
    // Smooth blend: curve = lerp(linear, exponential, amount)
    //   linear(x)      = x
    //   exponential(x) = x^x  (gentle curve)
    const float expo = std::pow(velocity, velocity + 0.5f); // shaped curve
    return velocity + (expo - velocity) * velocityCurveAmount;
}

//==============================================================================
// Adaptive Envelope
//==============================================================================

void VoiceManager::setAdaptiveEnvelopeAmount(float amount)
{
    adaptiveEnvelopeAmount = clampFloat(amount, 0.0f, 1.0f);
}

float VoiceManager::getAdaptiveEnvelopeAmount() const
{
    return adaptiveEnvelopeAmount;
}

//==============================================================================
// MPE methods
//==============================================================================

void VoiceManager::setSlide(int voiceIndex, float amount)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    voices[voiceIndex].slideAmount = clampFloat(amount, 0.0f, 1.0f);
}

void VoiceManager::setPressure(int voiceIndex, float amount)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    voices[voiceIndex].pressure = clampFloat(amount, 0.0f, 1.0f);
}

void VoiceManager::setTimbre(int voiceIndex, float amount)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    voices[voiceIndex].timbre = clampFloat(amount, 0.0f, 1.0f);
}

void VoiceManager::enableMPE(bool enabled)
{
    mpeEnabled = enabled;
}

bool VoiceManager::isMPEEnabled() const
{
    return mpeEnabled;
}

void VoiceManager::setMPEMasterChannel(int channel)
{
    mpeMasterChannel = clampInt(channel, 0, 15);
}

int VoiceManager::getMPEMasterChannel() const
{
    return mpeMasterChannel;
}

} // namespace ana
