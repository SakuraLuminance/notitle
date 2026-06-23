cmake_minimum_required(VERSION 3.22)
project(AnaPlug VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dependencies ----
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.13
    PATCH_COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/patch_juce_mingw.ps1" -SourceDir "<SOURCE_DIR>"
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    GIT_SHALLOW TRUE
)

# libebur128 — ITU-R BS.1770-4 LUFS loudness metering (MIT)
FetchContent_Declare(
    libebur128
    GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
    GIT_TAG v1.2.6
    GIT_SHALLOW TRUE
)

# clap-juce-extensions — CLAP polyphonic modulation & note expressions (MIT)
FetchContent_Declare(
    clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 51a9359315298de632cf44e9d7524940868441e6
    GIT_SHALLOW TRUE
)

set(SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(JUCE Catch2 libebur128 clap-juce-extensions)
set(BUILD_SHARED_LIBS ${SAVED_BUILD_SHARED_LIBS})

# libebur128 does not set INTERFACE_INCLUDE_DIRECTORIES on its target,
# so ensure the header can be found via #include <ebur128.h>
if(TARGET ebur128)
    target_include_directories(ebur128 PUBLIC ${libebur128_SOURCE_DIR}/ebur128)
endif()

# ---- Plugin Target ----
juce_add_plugin(AnaPlug
    PRODUCT_NAME "AnaPlug"
    COMPANY_NAME "AnaPlug"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Anap
    PLUGIN_CODE Anap
    FORMATS VST3 CLAP
    VST3_CAN_REPLACE_VST2 FALSE
)

target_sources(AnaPlug
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/dsp/AnaPlugEngine.cpp
        src/dsp/WavLoader.cpp
        src/dsp/STFTAnalyzer.cpp
        src/dsp/PeakDetector.cpp
        src/dsp/PartialTracker.cpp
        src/dsp/ResynthesisEngine.cpp
        src/dsp/PhasePropagation.cpp
        src/dsp/MultiFilter.cpp
        src/dsp/FilterModulation.cpp
        src/dsp/Arpeggiator.cpp
        src/dsp/effects/ChorusEffect.cpp
        src/dsp/effects/ConsolidatedDelay.cpp
        src/dsp/effects/DelayEffect.cpp
        src/dsp/effects/DriveModule.cpp
        src/dsp/effects/SaturationEffect.cpp
        src/dsp/effects/StereoWidenerEffect.cpp
        src/dsp/effects/BitcrusherEffect.cpp
        src/dsp/effects/DistortionEffect.cpp
        src/dsp/effects/EQEffect.cpp
        src/dsp/effects/ReverbEffect.cpp
        src/dsp/effects/AutoTuneEffect.cpp
        src/dsp/ProcessorStore.cpp
        src/dsp/effects/SpaceModule.cpp
        src/dsp/effects/ModulationModule.cpp
        src/dsp/effects/ModulationEffects.cpp
        src/dsp/effects/DeEsserModule.cpp
        src/dsp/effects/DynamicsModule.cpp
        src/dsp/effects/EQModule.cpp
        src/dsp/effects/PitchModule.cpp
        src/dsp/EffectsChain.cpp
        src/dsp/GranularSynthesizer.cpp
        src/dsp/ImageSynthesizer.cpp
        src/dsp/LFOSystem.cpp
        src/dsp/MultiPointEnvelope.cpp
        src/dsp/PresetManager.cpp
        src/dsp/PresetFactory.cpp
        src/dsp/PrismEffect.cpp
        src/dsp/UnisonEngine.cpp
        src/dsp/VoiceManager.cpp
        src/gui/PartialEditorCanvas.cpp
        src/dsp/filters/CombFilter.cpp
        src/dsp/filters/FormantFilterBank.cpp
        src/gui/WaveformDisplay.cpp
        src/gui/SpectrumDisplay.cpp
        src/gui/FilterVisualization.cpp
        src/gui/WaterfallDisplay.cpp
        src/gui/VisualFeedbackPanel.cpp
        src/gui/SpectrumEditorCanvas.cpp
        src/gui/PresetBrowserPanel.cpp
        src/gui/EvolutionPanel.cpp
        src/gui/XYPad.cpp
        src/gui/ModulationMatrixPanel.cpp
        src/gui/MeteringPanel.cpp
        src/gui/ModulationAssignPanel.cpp
        src/gui/EffectRackComponent.cpp
        src/gui/CreditsPanel.cpp
        # ---- New Phase 1 Modules ----
        src/dsp/SpectralFilter.cpp
        src/dsp/DualTimbre.cpp
        src/dsp/FMEngine.cpp
        src/dsp/SubHarmonicGenerator.cpp
        src/dsp/BlurEffect.cpp
        src/dsp/Harmonizer.cpp
        src/dsp/TimeStretchEngine.cpp
        src/dsp/PitchCorrector.cpp
        src/dsp/VocoderMode.cpp
        src/dsp/SpectralSculptor.cpp
        src/dsp/AdvancedGranularEngine.cpp
        src/dsp/FrequencyShaper.cpp
        src/dsp/SpectralFreezeEngine.cpp
        src/dsp/SpectralSequencer.cpp
        src/dsp/SpectralDNA.cpp
        src/dsp/SpectralMorpher.cpp
        src/dsp/GenerativeTimbreDesigner.cpp
        src/dsp/SpectralParticleSystem.cpp
        src/dsp/QuantumSpectralProcessor.cpp
        src/dsp/ParallelProcessor.cpp
        src/dsp/MacroController.cpp
        src/dsp/MidiLearn.cpp
        src/dsp/UndoManager.cpp
        src/dsp/PhysicalModel.cpp
        src/dsp/SampleProcessor.cpp
        src/dsp/SampleSlicer.cpp
        src/dsp/SpectralDynamics.cpp
        src/dsp/SpectralReverb.cpp
        src/dsp/NeuralUpsampler.cpp
        src/dsp/NeuralStyleTransfer.cpp
        src/dsp/StrumManager.cpp
        src/dsp/LocalEQ.cpp
        src/dsp/AudioToMidiConverter.cpp
        src/dsp/DualSignalChain.cpp
        src/dsp/HPSSEngine.cpp
        src/dsp/BpmDetector.cpp
        src/dsp/MeteringEngine.cpp
        src/dsp/ModulationMatrix.cpp
        src/dsp/effects/PhaserEffect.cpp
        src/dsp/effects/FlangerEffect.cpp
        src/dsp/effects/RingModulatorEffect.cpp
        src/dsp/effects/CompressorEffect.cpp
        src/dsp/effects/LimiterEffect.cpp
        src/dsp/effects/BreathNoiseGenerator.cpp
        src/dsp/effects/FormantTuner.cpp
        src/dsp/effects/SoloistVocalChain.cpp
        src/dsp/effects/VocalNoiseReducer.cpp
        src/dsp/effects/VocalProcessor.cpp
        src/dsp/effects/VocalThickenerEffect.cpp
        src/dsp/WavetableEngine.cpp
        src/dsp/MultibandProcessor.cpp
        src/dsp/ModulationBus.cpp
        src/dsp/PartialModulator.cpp
        src/dsp/StepSequencer.cpp
        src/dsp/Randomizer.cpp
)

target_link_libraries(AnaPlug
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_opengl
        ebur128
        d2d1 d3d11 dcomp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(AnaPlug
    PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# ---- Performance optimizations ----
option(ENABLE_PGO_GENERATE "Enable PGO Profile Generation" OFF)
option(ENABLE_PGO_USE "Enable PGO Profile Use" OFF)

if(MSVC)
    target_compile_options(AnaPlug PRIVATE
        /O2                    # Maximize speed
        /fp:fast               # Fast floating point
        /arch:AVX2             # For MSVC 64-bit, enable AVX2
    )
    # /GL and /LTCG not needed here — already set by juce_recommended_lto_flags (linked PUBLIC above)
    
    if(ENABLE_PGO_GENERATE)
        target_link_options(AnaPlug PRIVATE /GENPROFILE)
    elseif(ENABLE_PGO_USE)
        target_link_options(AnaPlug PRIVATE /USEPROFILE)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(AnaPlug PRIVATE
        -O3
        -ffast-math
        -march=native
        -flto
        -Wno-nan-infinity-disabled
        -Wno-trigraphs
        -Wno-unknown-pragmas
        -Wno-unused-variable
        -Wno-implicit-int-float-conversion
        -Wno-unused-but-set-variable
    )
    
    if(ENABLE_PGO_GENERATE)
        target_compile_options(AnaPlug PRIVATE -fprofile-generate)
        target_link_options(AnaPlug PRIVATE -fprofile-generate)
    elseif(ENABLE_PGO_USE)
        target_compile_options(AnaPlug PRIVATE -fprofile-use -fprofile-correction)
        target_link_options(AnaPlug PRIVATE -fprofile-use)
    endif()
endif()

# ARM NEON detection
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
    target_compile_definitions(AnaPlug PRIVATE
        __ARM_NEON__=1
    )
endif()

# ---- Linux Compatibility ----
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    pkg_check_modules(JACK REQUIRED jack)
    find_package(X11 REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(AnaPlug PRIVATE
        X11::X11
        ${ALSA_LIBRARIES}
        ${JACK_LIBRARIES}
        Threads::Threads
    )
    target_include_directories(AnaPlug PRIVATE
        ${ALSA_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
    )
    target_compile_options(AnaPlug PRIVATE -pthread)
endif()

# ---- CLAP Extensions (clap-juce-extensions) ----
# Creates AnaPlug_CLAP target with note expressions & polyphonic modulation.
# The original AnaPlug target (VST3) is unaffected.
if(TARGET clap_juce_extensions)
    clap_juce_extensions_plugin(TARGET AnaPlug
        CLAP_ID "com.anaplug.synth"
        CLAP_FEATURES instrument synthesizer
        CLAP_MANUAL_URL "https://github.com/anaplug/AnaPlug"
        CLAP_SUPPORT_URL "https://github.com/anaplug/AnaPlug"
    )
endif()

# ---- Testing ----
option(TESTING "Enable testing" OFF)

if(TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
