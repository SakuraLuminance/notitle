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
// AnaVoice — MPESynthesiserVoice overrides
//==============================================================================

bool AnaVoice::isActive() const
{
    return state.load(std::memory_order_relaxed) != VoiceState::free;
}

void AnaVoice::noteStarted()
{
    const auto& mpeNote = getCurrentlyPlayingNote();

    note       = mpeNote.initialNote;
    velocity   = mpeNote.noteOnVelocity.asUnsignedFloat();
    midiChannel = mpeNote.midiChannel;

    // Frequency from MPENote already includes pitch bend
    pitchHz.store(mpeNote.getFrequencyInHertz(), std::memory_order_relaxed);
    amplitude.store(velocity, std::memory_order_relaxed);

    // Reset oscillator state
    phase     = 0.0f;
    phasorRe  = 1.0f;
    phasorIm  = 0.0f;
    envelopeLevel = 0.0f;
    releaseStartLevel.store(0.0f, std::memory_order_relaxed);

    // Reset modulation
    aftertouch.store(0.0f, std::memory_order_relaxed);
    pitchBend.store(1.0f, std::memory_order_relaxed);
    slideAmount = 0.0f;
    pressure    = 0.0f;
    timbre      = 0.0f;

    // Initialize cached modulation factor
    cachedMod = amplitude.load(std::memory_order_relaxed);

    // Reset filter states
    lp0 = 0.0f; lp1 = 0.0f; bp0 = 0.0f; bp1 = 0.0f; hp0 = 0.0f; hp1 = 0.0f;
    smoothFc = 0.0f;

    // State is set to attack by the caller (noteAdded override)
    state.store(VoiceState::attack, std::memory_order_release);
}

void AnaVoice::noteStopped(bool allowTailOff)
{
    if (allowTailOff)
    {
        VoiceState expected = state.load(std::memory_order_relaxed);
        while (expected >= VoiceState::attack && expected <= VoiceState::sustain)
        {
            if (state.compare_exchange_weak(expected, VoiceState::release, std::memory_order_release))
            {
                releaseStartLevel.store(envelopeLevel, std::memory_order_release);
                break;
            }
        }
    }
    else
    {
        clearCurrentNote();
        state.store(VoiceState::free, std::memory_order_release);
        note = -1;
        midiChannel = -1;
    }
}

void AnaVoice::notePressureChanged()
{
    pressure = getCurrentlyPlayingNote().pressure.asUnsignedFloat();

    // Refresh cached amplitude modulation
    cachedMod = amplitude.load(std::memory_order_relaxed)
              * (1.0f + aftertouch.load(std::memory_order_relaxed) * 0.5f)
              * (1.0f + pressure * 0.5f);
}

void AnaVoice::notePitchbendChanged()
{
    const auto& mpeNote = getCurrentlyPlayingNote();

    // pitchHz holds the base frequency (set in noteStarted) — do NOT change it.
    // pitchBend is the ratio applied in renderNextBlock and by external consumers
    // (sub-harmonic generator).  The bend is fully expressed through pitchBend.
    const float freqWithBend = mpeNote.getFrequencyInHertz();
    const float baseFreq = pitchHz.load(std::memory_order_relaxed);
    if (baseFreq > 0.0f)
        pitchBend.store(freqWithBend / baseFreq, std::memory_order_relaxed);
}

void AnaVoice::noteTimbreChanged()
{
    timbre = getCurrentlyPlayingNote().timbre.asUnsignedFloat();
}

void AnaVoice::noteKeyStateChanged()
{
    // No custom handling needed — MPESynthesiser manages sustain/release internally.
}

void AnaVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                int startSample,
                                int numSamples)
{
    auto currentState = state.load(std::memory_order_relaxed);
    if (currentState == VoiceState::free)
        return;

    // --- Per-block setup ---
    const float sr = static_cast<float>(getSampleRate());
    const float dt = 1.0f / sr;

    const float baseFreq  = pitchHz.load(std::memory_order_relaxed);
    const float bend      = pitchBend.load(std::memory_order_relaxed);
    float freq      = baseFreq * bend;
    float phaseDelta = twoPi * freq * dt;

    // Update complex phasor rotation coefficients (may be overridden per-sample
    // if portamento is active — see sample-loop section below)
    float currentCosDelta = std::cos(phaseDelta);
    float currentSinDelta = std::sin(phaseDelta);

    // Precompute envelope rates (hoisted divisions)
    const float attackDt  = attackSeconds > 0.0f ? dt / attackSeconds : 1.0f;
    const float decayRange  = 1.0f - sustainLevel;
    const float decayDt   = decaySeconds > 0.0f ? dt * decayRange / decaySeconds : 1.0f;
    const float releaseDt = releaseSeconds > 0.0f ? dt / releaseSeconds : 1.0f;

    // Hoist filter constants
    const float nyquist   = sr * 0.5f;
    const float minCutoff = 200.0f;
    const float maxCutoff = nyquist * 0.95f;

    // Hoist channel write pointers
    const int numChannels = outputBuffer.getNumChannels();
    float* chData[32];
    for (int ch = 0; ch < numChannels; ++ch)
        chData[ch] = outputBuffer.getWritePointer(ch);

    // --- Sample loop ---
    for (int s = 0; s < numSamples; ++s)
    {
        // --- Portamento pitch interpolation (per-sample when active) ---
        if (portamentoActive)
        {
            const float totalS = static_cast<float>(portamentoTotalSamples);
            float t = (totalS > 0.0f) ? std::min(1.0f, static_cast<float>(portamentoElapsed) / totalS) : 1.0f;

            float currentPitch;
            if (t < 1.0f)
            {
                switch (portamentoCurve)
                {
                    case PortamentoCurve::Linear:
                        currentPitch = portamentoStartPitch + (portamentoTargetPitch - portamentoStartPitch) * t;
                        break;
                    case PortamentoCurve::Exponential:
                        currentPitch = (portamentoStartPitch > 0.0f && portamentoTargetPitch > 0.0f)
                            ? portamentoStartPitch * std::pow(portamentoTargetPitch / portamentoStartPitch, t)
                            : portamentoTargetPitch;
                        break;
                    case PortamentoCurve::Logarithmic:
                        currentPitch = portamentoStartPitch + (portamentoTargetPitch - portamentoStartPitch)
                                     * std::log10(1.0f + 9.0f * t);
                        break;
                }
                ++portamentoElapsed;
            }
            else
            {
                currentPitch = portamentoTargetPitch;
                portamentoActive = false;
            }

            const float pd = twoPi * currentPitch * bend * dt;
            currentCosDelta = std::cos(pd);
            currentSinDelta = std::sin(pd);
        }

        // Generate sample: phasorIm * envelopeLevel * cachedMod
        float sample = phasorIm * envelopeLevel * cachedMod;

        // Rotate complex phasor (using currentCosDelta/currentSinDelta which
        // track portamento when active or the pre-computed values otherwise)
        float re = phasorRe * currentCosDelta - phasorIm * currentSinDelta;
        float im = phasorRe * currentSinDelta + phasorIm * currentCosDelta;
        phasorRe = re;
        phasorIm = im;

        // Apply state-variable filter if timbre has been set
        if (timbre > 0.001f)
        {
            const float t = clampFloat(timbre, 0.0f, 1.0f);
            const float targetCutoff = minCutoff + (maxCutoff - minCutoff) * t * t;
            const float targetFc = clampFloat(targetCutoff / nyquist, 0.001f, 0.95f);

            constexpr float smoothCoeff = 0.999f;
            smoothFc = smoothFc * smoothCoeff + targetFc * (1.0f - smoothCoeff);
            const float fc = smoothFc;

            const float f = fc * 1.5f;
            const float scale = 1.0f / (1.0f + f + f * f);
            const float hpCoeff = f + f * f;
            const float bpCoeff = 1.0f + f;

            const float hp = (sample - lp1 * hpCoeff - bp1 * bpCoeff) * scale;
            const float bp = bp1 + f * hp;
            const float lp = lp1 + f * bp;

            lp1 = lp + 1e-30f;
            bp1 = bp + 1e-30f;
            hp0 = hp + 1e-30f;

            const float mix = clampFloat(t, 0.0f, 1.0f);
            sample = lp * (1.0f - mix) + hp * mix;
        }

        // Inline envelope update
        switch (currentState)
        {
            case VoiceState::attack:
            {
                float env = envelopeLevel + attackDt;
                envelopeLevel = env;
                if (env >= 1.0f)
                {
                    envelopeLevel = 1.0f;
                    state.store(VoiceState::decay, std::memory_order_relaxed);
                    currentState = VoiceState::decay;
                }
                break;
            }

            case VoiceState::decay:
            {
                float env = envelopeLevel - decayDt;
                envelopeLevel = env;
                if (env <= sustainLevel)
                {
                    envelopeLevel = sustainLevel;
                    state.store(VoiceState::sustain, std::memory_order_relaxed);
                    currentState = VoiceState::sustain;
                }
                break;
            }

            case VoiceState::sustain:
            {
                envelopeLevel = sustainLevel;
                break;
            }

            case VoiceState::release:
            {
                const float rsl = releaseStartLevel.load(std::memory_order_relaxed);
                if (rsl <= 0.0f)
                {
                    envelopeLevel = 0.0f;
                    state.store(VoiceState::idle, std::memory_order_relaxed);
                    currentState = VoiceState::idle;
                }
                else
                {
                    float env = envelopeLevel - rsl * releaseDt;
                    envelopeLevel = env;
                    if (env <= 0.0f)
                    {
                        envelopeLevel = 0.0f;
                        state.store(VoiceState::idle, std::memory_order_relaxed);
                        currentState = VoiceState::idle;
                    }
                }
                break;
            }

            default:
                break;
        }

        // Write to all channels
        const int writeIdx = startSample + s;
        for (int ch = 0; ch < numChannels; ++ch)
            chData[ch][writeIdx] += sample;
    }

    // If we finished release and reached idle, notify the base synth
    if (currentState == VoiceState::idle)
        clearCurrentNote();
}

//==============================================================================
// VoiceManager — Construction
//==============================================================================

VoiceManager::VoiceManager()
{
    for (int i = 0; i < maxVoices; ++i)
        addVoice(new AnaVoice());
}

//==============================================================================
// Preparation
//==============================================================================

void VoiceManager::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    setCurrentPlaybackSampleRate(sampleRate_);
}

//==============================================================================
// Legacy process() — audio-only, no MIDI processing
//==============================================================================

void VoiceManager::process(juce::AudioBuffer<float>& buffer)
{
    buffer.clear();
    juce::MidiBuffer empty;
    renderNextBlock(buffer, empty, 0, buffer.getNumSamples());
}

//==============================================================================
// MPESynthesiser overrides — note lifecycle
//==============================================================================

void VoiceManager::noteAdded(MPENote newNote)
{
    const auto mode = voiceMode_.load(std::memory_order_relaxed);

    // --- Mono / Legato: single voice, key tracking ---
    if (mode == VoiceMode::Mono || mode == VoiceMode::Legato)
    {
        auto* voice = getVoice(0);
        anaVoiceInit(voice, newNote, mode);
        heldKeys_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // --- Poly: normal multi-voice allocation ---
    auto* voice = findFreeVoice(newNote, true);
    if (voice == nullptr)
        return;

    anaVoiceInit(voice, newNote, VoiceMode::Poly);
}

void VoiceManager::noteReleased(MPENote finishedNote)
{
    const auto mode = voiceMode_.load(std::memory_order_relaxed);

    // --- Mono / Legato: track held keys, only release when all keys released ---
    if (mode == VoiceMode::Mono || mode == VoiceMode::Legato)
    {
        const int prevHeld = heldKeys_.fetch_sub(1, std::memory_order_relaxed);
        if (prevHeld <= 1)
        {
            heldKeys_.store(0, std::memory_order_relaxed);
            // All keys released — allow voice 0 to tail off
            auto* voice = getVoice(0);
            auto currentState = voice->state.load(std::memory_order_relaxed);
            if (currentState >= VoiceState::attack && currentState <= VoiceState::sustain)
                stopVoice(voice, voice->getCurrentlyPlayingNote(), true);
        }
        return;
    }

    // --- Poly: find the voice playing this note and release it ---
    for (int i = 0; i < maxVoices; ++i)
    {
        auto* voice = getVoice(i);
        if (voice->isCurrentlyPlayingNote(finishedNote))
        {
            auto currentState = voice->state.load(std::memory_order_relaxed);
            if (currentState >= VoiceState::attack && currentState <= VoiceState::sustain)
                stopVoice(voice, finishedNote, true);
            break;
        }
    }
}

//==============================================================================
// MPESynthesiser overrides — audio rendering
//==============================================================================

void VoiceManager::renderNextSubBlock(juce::AudioBuffer<float>& outputAudio,
                                       int startSample,
                                       int numSamples)
{
    // Call each active voice's renderNextBlock.
    // The buffer is cleared once before renderNextBlock is called (in
    // PluginProcessor), so voices add to silence.  We do NOT clear here
    // because multiple sub-blocks exist when MIDI events split the block.
    for (int i = 0; i < maxVoices; ++i)
    {
        auto* voice = getVoice(i);
        if (voice->state.load(std::memory_order_relaxed) != VoiceState::free)
            voice->renderNextBlock(outputAudio, startSample, numSamples);
    }
}

//==============================================================================
// MPESynthesiser overrides — voice allocation
//==============================================================================

MPESynthesiserVoice* VoiceManager::findFreeVoice(MPENote /*noteToFindVoiceFor*/,
                                                   bool stealIfNoneAvailable) const
{
    // Mono / Legato: always return voice 0
    {
        const auto vm = voiceMode_.load(std::memory_order_relaxed);
        if (vm == VoiceMode::Mono || vm == VoiceMode::Legato)
            return getVoice(0);
    }

    // Phase 1: find a FREE voice using the configured allocation mode
    const auto allocMode = allocationMode_.load(std::memory_order_relaxed);

    if (allocMode == AllocationMode::roundRobin)
    {
        for (int i = 0; i < maxVoices; ++i)
        {
            const int candidate = (nextVoiceIndex_.load(std::memory_order_relaxed) + i) % maxVoices;
            auto* voice = getVoice(candidate);
            VoiceState expected = VoiceState::free;
            if (voice->state.compare_exchange_strong(expected, VoiceState::attack))
            {
                nextVoiceIndex_.store((candidate + 1) % maxVoices, std::memory_order_relaxed);
                return voice;
            }
        }
    }
    else if (allocMode == AllocationMode::oldestFirst)
    {
        for (int i = 0; i < maxVoices; ++i)
        {
            auto* voice = getVoice(i);
            VoiceState expected = VoiceState::free;
            if (voice->state.compare_exchange_strong(expected, VoiceState::attack))
                return voice;
        }
    }
    else // random
    {
        for (int attempt = 0; attempt < maxVoices; ++attempt)
        {
            const int candidate = juce::Random::getSystemRandom().nextInt(maxVoices);
            auto* voice = getVoice(candidate);
            VoiceState expected = VoiceState::free;
            if (voice->state.compare_exchange_strong(expected, VoiceState::attack))
            {
                nextVoiceIndex_.store((candidate + 1) % maxVoices, std::memory_order_relaxed);
                return voice;
            }
        }
    }

    // Phase 2: find an IDLE voice (youngest first)
    if (const int idx = allocateVoice(); idx >= 0)
        return getVoice(idx);

    // Phase 3: steal if allowed
    if (stealIfNoneAvailable)
        return findVoiceToSteal(MPENote());

    return nullptr;
}

MPESynthesiserVoice* VoiceManager::findVoiceToSteal(MPENote /*noteToStealVoiceFor*/) const
{
    if (const int idx = stealVoice(); idx >= 0)
        return getVoice(idx);
    return nullptr;
}

//==============================================================================
// Custom allocation helpers
//==============================================================================

int VoiceManager::allocateVoice() const
{
    int      bestIdx = -1;
    uint64_t bestAge = 0;

    for (int i = 0; i < maxVoices; ++i)
    {
        VoiceState expected = VoiceState::idle;
        if (getVoice(i)->state.compare_exchange_strong(expected, VoiceState::attack))
        {
            if (bestIdx < 0 || getVoice(i)->noteOnIndex < bestAge)
            {
                // Release previously claimed idle, keep this better one
                if (bestIdx >= 0)
                    getVoice(bestIdx)->state.store(VoiceState::idle, std::memory_order_relaxed);
                bestIdx = i;
                bestAge = getVoice(i)->noteOnIndex;
            }
            else
            {
                // This claim is worse, release it
                getVoice(i)->state.store(VoiceState::idle, std::memory_order_relaxed);
            }
        }
    }

    return bestIdx;
}

int VoiceManager::stealVoice() const
{
    int      bestIdx      = -1;
    int      bestPriority = -1;
    uint64_t bestAge      = 0;

    for (int i = 0; i < maxVoices; ++i)
    {
        int priority = 0;

        switch (getVoice(i)->state.load(std::memory_order_relaxed))
        {
            case VoiceState::sustain: priority = 4; break;
            case VoiceState::release: priority = 3; break;
            case VoiceState::decay:   priority = 2; break;
            case VoiceState::attack:  priority = 1; break;
            default:                  continue;
        }

        if (priority > bestPriority
            || (priority == bestPriority && getVoice(i)->noteOnIndex < bestAge))
        {
            bestPriority = priority;
            bestAge      = getVoice(i)->noteOnIndex;
            bestIdx      = i;
        }
    }

    return bestIdx;
}

//==============================================================================
// Voice lifecycle helpers
//==============================================================================

void VoiceManager::anaVoiceInit(MPESynthesiserVoice* voice, const MPENote& newNote, VoiceMode mode)
{
    auto* anaVoice = static_cast<AnaVoice*>(voice);
    const bool isLegato = (mode == VoiceMode::Legato);
    const bool wasActive = anaVoice->state.load(std::memory_order_relaxed) != VoiceState::free;

    // Save envelope state if legato and voice was already active (don't retrigger)
    const float savedEnvLevel      = (isLegato && wasActive) ? anaVoice->envelopeLevel : 0.0f;
    const auto  savedState         = (isLegato && wasActive) ? anaVoice->state.load(std::memory_order_relaxed) : VoiceState::free;
    const float savedReleaseStart  = (isLegato && wasActive) ? anaVoice->releaseStartLevel.load(std::memory_order_relaxed) : 0.0f;
    const float oldPitch           = wasActive ? anaVoice->pitchHz.load(std::memory_order_relaxed) : 0.0f;

    // For mono (non-legato), force-stop any existing note on voice 0
    if (!isLegato && wasActive)
    {
        anaVoice->clearCurrentNote();
        anaVoice->state.store(VoiceState::free, std::memory_order_release);
    }

    // Set up the voice's note tracking
    anaVoice->noteOnIndex = globalNoteCounter_++;

    // Call the base class startVoice (sets MPENote association, calls noteStarted)
    startVoice(voice, newNote);

    // In legato mode with an already-active voice, restore envelope state
    // so the envelope does NOT retrigger
    if (isLegato && wasActive)
    {
        anaVoice->envelopeLevel = savedEnvLevel;
        anaVoice->state.store(savedState, std::memory_order_release);
        anaVoice->releaseStartLevel.store(savedReleaseStart, std::memory_order_release);
    }

    // -- Override values after noteStarted has set the basics ---------------
    const float curvedVel = applyVelocityCurve(newNote.noteOnVelocity.asUnsignedFloat());
    anaVoice->velocity   = curvedVel;
    anaVoice->amplitude.store(curvedVel, std::memory_order_relaxed);
    anaVoice->cachedMod  = curvedVel;

    anaVoice->envScale = std::exp2f(-adaptiveEnvelopeAmount_
                                    * (static_cast<float>(newNote.initialNote) - 60.0f) / 12.0f);

    anaVoice->attackSeconds  = defaultAttack_ * anaVoice->envScale;
    anaVoice->decaySeconds   = defaultDecay_ * anaVoice->envScale;
    anaVoice->sustainLevel   = defaultSustain_;
    anaVoice->releaseSeconds = defaultRelease_ * anaVoice->envScale;

    // --- Portamento setup ---
    const float newFreq = newNote.getFrequencyInHertz();
    if (portamentoTime_ > 0.0f && oldPitch > 0.0f && wasActive)
    {
        anaVoice->portamentoStartPitch  = oldPitch;
        anaVoice->portamentoTargetPitch = newFreq;
        anaVoice->portamentoElapsed     = 0;
        anaVoice->portamentoTotalSamples = static_cast<int>(portamentoTime_ * sampleRate_);
        anaVoice->portamentoCurve       = portamentoCurve_;
        anaVoice->portamentoActive      = true;
    }
    else
    {
        anaVoice->portamentoActive = false;
    }
}

void VoiceManager::startVoice(int voiceIndex, int note, float velocity)
{
    auto* v = getVoice(voiceIndex);

    v->note              = note;
    v->velocity          = velocity;
    v->pitchHz.store(noteToFrequency(note), std::memory_order_relaxed);
    v->amplitude.store(applyVelocityCurve(velocity), std::memory_order_relaxed);
    v->phase             = 0.0f;
    v->phasorRe          = 1.0f;
    v->phasorIm          = 0.0f;
    v->cosDelta          = std::cos(twoPi * v->pitchHz.load(std::memory_order_relaxed) / sampleRate_);
    v->sinDelta          = std::sin(twoPi * v->pitchHz.load(std::memory_order_relaxed) / sampleRate_);
    v->envelopeLevel = 0.0f;
    v->releaseStartLevel.store(0.0f, std::memory_order_relaxed);
    v->noteOnIndex       = globalNoteCounter_++;
    v->aftertouch.store(0.0f, std::memory_order_relaxed);
    v->pitchBend.store(1.0f, std::memory_order_relaxed);

    // Initialize MPE fields
    v->midiChannel = -1;
    v->slideAmount = 0.0f;
    v->pressure    = 0.0f;
    v->timbre      = 0.0f;

    // Initialize cached amplitude modulation
    v->cachedMod = v->amplitude.load(std::memory_order_relaxed);

    // Reset filter states
    v->lp0 = 0.0f; v->lp1 = 0.0f; v->bp0 = 0.0f; v->bp1 = 0.0f; v->hp0 = 0.0f; v->hp1 = 0.0f;

    // Calculate adaptive envelope scale
    v->envScale = std::exp2f(-adaptiveEnvelopeAmount_
                             * (static_cast<float>(note) - 60.0f) / 12.0f);

    // Apply default ADSR values scaled by adaptive amount
    v->attackSeconds  = defaultAttack_ * v->envScale;
    v->decaySeconds   = defaultDecay_ * v->envScale;
    v->sustainLevel   = defaultSustain_;
    v->releaseSeconds = defaultRelease_ * v->envScale;

    // State is already set to attack by allocateVoice() via CAS
}

//==============================================================================
// Velocity curve
//==============================================================================

void VoiceManager::setVelocityCurve(float amount)
{
    velocityCurveAmount_ = clampFloat(amount, 0.0f, 1.0f);
}

float VoiceManager::getVelocityCurve() const
{
    return velocityCurveAmount_;
}

float VoiceManager::applyVelocityCurve(float velocity) const
{
    const float expo = std::pow(velocity, velocity + 0.5f);
    return velocity + (expo - velocity) * velocityCurveAmount_;
}

//==============================================================================
// MIDI conversion
//==============================================================================

float VoiceManager::noteToFrequency(int note)
{
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

//==============================================================================
// Allocation mode
//==============================================================================

void VoiceManager::setAllocationMode(AllocationMode mode)
{
    allocationMode_.store(mode, std::memory_order_relaxed);
}

AllocationMode VoiceManager::getAllocationMode() const
{
    return allocationMode_.load(std::memory_order_relaxed);
}

//==============================================================================
// Per-voice ADSR
//==============================================================================

void VoiceManager::setVoiceAttack(int voiceIndex, float seconds)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    getVoice(voiceIndex)->attackSeconds = clampFloat(seconds, 0.0f, 10.0f);
}

void VoiceManager::setVoiceDecay(int voiceIndex, float seconds)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    getVoice(voiceIndex)->decaySeconds = clampFloat(seconds, 0.0f, 10.0f) * getVoice(voiceIndex)->envScale;
}

void VoiceManager::setVoiceSustain(int voiceIndex, float level)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    getVoice(voiceIndex)->sustainLevel = clampFloat(level, 0.0f, 1.0f);
}

void VoiceManager::setVoiceRelease(int voiceIndex, float seconds)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    getVoice(voiceIndex)->releaseSeconds = clampFloat(seconds, 0.0f, 10.0f) * getVoice(voiceIndex)->envScale;
}

//==============================================================================
// Default ADSR
//==============================================================================

void VoiceManager::setDefaultAttack(float seconds)
{
    defaultAttack_ = clampFloat(seconds, 0.0f, 10.0f);
}

void VoiceManager::setDefaultDecay(float seconds)
{
    defaultDecay_ = clampFloat(seconds, 0.0f, 10.0f);
}

void VoiceManager::setDefaultSustain(float level)
{
    defaultSustain_ = clampFloat(level, 0.0f, 1.0f);
}

void VoiceManager::setDefaultRelease(float seconds)
{
    defaultRelease_ = clampFloat(seconds, 0.0f, 10.0f);
}

//==============================================================================
// Queries
//==============================================================================

int VoiceManager::getNumActiveVoices() const
{
    int count = 0;
    for (int i = 0; i < maxVoices; ++i)
    {
        if (getVoice(i)->state.load(std::memory_order_acquire) != VoiceState::free)
            ++count;
    }
    return count;
}

bool VoiceManager::isVoiceActive(int voiceIndex) const
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return false;
    return getVoice(voiceIndex)->state.load(std::memory_order_acquire) != VoiceState::free;
}

//==============================================================================
// Per-voice modulation
//==============================================================================

void VoiceManager::setVoiceAftertouch(int voiceIndex, float amount)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    auto* v = getVoice(voiceIndex);
    v->aftertouch.store(clampFloat(amount, 0.0f, 1.0f), std::memory_order_relaxed);
    v->cachedMod = v->amplitude.load(std::memory_order_relaxed)
                 * (1.0f + v->aftertouch.load(std::memory_order_relaxed) * 0.5f)
                 * (1.0f + v->pressure * 0.5f);
}

void VoiceManager::setVoicePitchBend(int voiceIndex, float bend)
{
    if (voiceIndex < 0 || voiceIndex >= maxVoices)
        return;
    const float clamped = clampFloat(bend, -1.0f, 1.0f);
    getVoice(voiceIndex)->pitchBend.store(std::exp2f(clamped), std::memory_order_relaxed);
}

//==============================================================================
// MPE methods
//==============================================================================

void VoiceManager::enableMPE(bool enabled)
{
    mpeEnabled_.store(enabled, std::memory_order_relaxed);

    if (enabled)
    {
        // Configure a lower MPE zone (channels 1-15).
        // Channel 1 is the master channel; channels 2-15 are per-note channels.
        // The master channel carries global pitch bend, pressure, and CC messages.
        juce::MPEZoneLayout layout;
        layout.addZone(juce::MPEZone(juce::MPEZone::Type::lower, 15));
        setZoneLayout(layout);
    }
    else
    {
        // Legacy mode: standard MIDI on all channels with ±1 octave pitch bend range
        // to match our existing mapping: [-1, 1] → [0.5, 2.0] via exp2f.
        enableLegacyMode(12, {1, 17});
    }
}

bool VoiceManager::isMPEEnabled() const
{
    return mpeEnabled_.load(std::memory_order_relaxed);
}

void VoiceManager::setMPEMasterChannel(int channel)
{
    mpeMasterChannel_.store(clampInt(channel, 0, 15), std::memory_order_relaxed);
}

int VoiceManager::getMPEMasterChannel() const
{
    return mpeMasterChannel_.load(std::memory_order_relaxed);
}

//==============================================================================
// Adaptive Envelope
//==============================================================================

void VoiceManager::setAdaptiveEnvelopeAmount(float amount)
{
    adaptiveEnvelopeAmount_ = clampFloat(amount, 0.0f, 1.0f);
}

float VoiceManager::getAdaptiveEnvelopeAmount() const
{
    return adaptiveEnvelopeAmount_;
}

//==============================================================================
// Voice mode
//==============================================================================

void VoiceManager::setVoiceMode(VoiceMode mode)
{
    voiceMode_.store(mode, std::memory_order_relaxed);
    // Reset held-keys counter when switching modes
    heldKeys_.store(0, std::memory_order_relaxed);
}

VoiceMode VoiceManager::getVoiceMode() const
{
    return voiceMode_.load(std::memory_order_relaxed);
}

//==============================================================================
// Portamento
//==============================================================================

void VoiceManager::setPortamentoTime(float seconds)
{
    portamentoTime_ = clampFloat(seconds, 0.0f, 2.0f);
}

float VoiceManager::getPortamentoTime() const
{
    return portamentoTime_;
}

void VoiceManager::setPortamentoCurve(PortamentoCurve curve)
{
    portamentoCurve_ = curve;
}

PortamentoCurve VoiceManager::getPortamentoCurve() const
{
    return portamentoCurve_;
}

//==============================================================================
// Sample generation & filtering (used by legacy startVoice)
//==============================================================================

float VoiceManager::generateSample(int voiceIndex) const
{
    auto& v = *getVoice(voiceIndex);
    return v.phasorIm * v.envelopeLevel * v.cachedMod;
}

float VoiceManager::applyFilter(int voiceIndex, float sample, float nyquist, float minCutoff, float maxCutoff) const
{
    auto& v = *getVoice(voiceIndex);

    const float t = clampFloat(v.timbre, 0.0f, 1.0f);
    const float targetCutoff = minCutoff + (maxCutoff - minCutoff) * t * t;
    const float targetFc = clampFloat(targetCutoff / nyquist, 0.001f, 0.95f);

    constexpr float smoothCoeff = 0.999f;
    v.smoothFc = v.smoothFc * smoothCoeff + targetFc * (1.0f - smoothCoeff);
    const float fc = v.smoothFc;

    const float f = fc * 1.5f;
    const float scale = 1.0f / (1.0f + f + f * f);
    const float hpCoeff = f + f * f;
    const float bpCoeff = 1.0f + f;

    const float hp = (sample - v.lp1 * hpCoeff - v.bp1 * bpCoeff) * scale;
    const float bp = v.bp1 + f * hp;
    const float lp = v.lp1 + f * bp;

    v.lp1 = lp + 1e-30f;
    v.bp1 = bp + 1e-30f;
    v.hp0 = hp + 1e-30f;

    const float mix = clampFloat(t, 0.0f, 1.0f);
    return lp * (1.0f - mix) + hp * mix;
}

void VoiceManager::resetFilter(int voiceIndex) const
{
    auto& v = *getVoice(voiceIndex);
    v.lp0 = 0.0f; v.lp1 = 0.0f;
    v.bp0 = 0.0f; v.bp1 = 0.0f;
    v.hp0 = 0.0f; v.hp1 = 0.0f;
}

//==============================================================================
// NoteOn / NoteOff (legacy compat — not used with MPESynthesiser MIDI processing)
// These are kept for external consumers that trigger notes directly.
//==============================================================================

} // namespace ana
