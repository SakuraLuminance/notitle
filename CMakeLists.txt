cmake_minimum_required(VERSION 3.22)
project(AnaPlug VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dependencies ----
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE Catch2)

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
        src/dsp/effects/DelayEffect.cpp
        src/dsp/effects/DistortionEffect.cpp
        src/dsp/effects/EQEffect.cpp
        src/dsp/effects/ReverbEffect.cpp
        src/dsp/effects/AutoTuneEffect.cpp
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
        src/gui/ModulationMatrixPanel.cpp
        # ---- New Phase 1 Modules ----
        src/dsp/SpectralFilter.cpp
        src/dsp/DualTimbre.cpp
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
        src/dsp/GenerativeTimbreDesigner.cpp
        src/dsp/SpectralParticleSystem.cpp
        src/dsp/QuantumSpectralProcessor.cpp
        src/dsp/ParallelProcessor.cpp
        src/dsp/MacroController.cpp
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
        src/dsp/ModulationMatrix.cpp
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
        -ld2d1 -ld3d11 -ldcomp -ldxstub
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
        /GL                    # Whole program optimization
        /arch:AVX2             # For MSVC 64-bit, enable AVX2
    )
    target_link_options(AnaPlug PRIVATE
        /LTCG                  # Link-time code generation
    )
    
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
    target_link_libraries(AnaPlug PRIVATE
        X11::X11
        alsa
        jack
    )
    target_compile_options(AnaPlug PRIVATE
        -pthread
    )
endif()

# ---- Testing ----
option(TESTING "Enable testing" OFF)

if(TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
