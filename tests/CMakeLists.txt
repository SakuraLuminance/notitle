add_executable(AnaPlugTests
    main.cpp
    bench_performance.cpp
    test_advanced_granular.cpp
    test_arpeggiator.cpp
    test_blur_effect.cpp
    test_bpm_detector.cpp
    test_cutting_edge_features.cpp
    test_data_structures.cpp
    test_dual_timbre.cpp
    test_effects_chain.cpp
    test_engine_integration.cpp
    test_filter_modulation.cpp
    test_frequency_shaper.cpp
    test_generative_timbre.cpp
    test_granular_synthesis.cpp
    test_harmonizer.cpp
    test_image_synthesizer.cpp
    test_lfo_system.cpp
    test_macro_controller.cpp
    test_multi_filter.cpp
    test_multi_point_envelope.cpp
    test_neural_style.cpp
    test_neural_upsampler.cpp
    test_parallel_processor.cpp
    test_partial_data_simd.cpp
    #test_partial_editor_canvas.cpp
    test_partial_tracker.cpp
    test_peak_detector.cpp
    test_phase_propagation.cpp
    test_physical_model.cpp
    test_pitch_corrector.cpp
    test_prism_effect.cpp
    test_quantum_spectral.cpp
    test_resynthesis.cpp
    test_security_fixes.cpp
    test_sample_processor.cpp
    test_sanity.cpp
    test_spectral_dna.cpp
    test_spectral_dynamics.cpp
    test_spectral_freeze.cpp
    test_spectral_particle.cpp
    test_spectral_reverb.cpp
    test_spectral_sculptor.cpp
    test_spectral_sequencer.cpp
    test_stft_analyzer.cpp
    test_sub_harmonic.cpp
    test_time_stretch.cpp
    test_unison_engine.cpp
    test_vocoder_mode.cpp
    test_voice_manager.cpp
    test_wavetable_engine.cpp
    test_wav_loader.cpp
    test_wired_modules.cpp
    test_spectral_morpher.cpp
    test_midi_learn.cpp
    test_modulation.cpp
    test_effects_new.cpp
    test_new_effects.cpp
    test_noise_oscillator.cpp
    test_undo_manager.cpp
    test_effect_presets.cpp
    test_security_v2.cpp
    test_consolidated_effects.cpp
    test_sidechain.cpp
    test_vocal_effects.cpp

    # Engine source files needed by cutting-edge features tests
    ../src/dsp/VoiceManager.cpp
    ../src/dsp/MeteringEngine.cpp
    ../src/dsp/AdvancedGranularEngine.cpp
    ../src/dsp/GranularSynthesizer.cpp

    # Effect source files needed by effects tests
    ../src/dsp/effects/StereoWidenerEffect.cpp
    ../src/dsp/effects/SaturationEffect.cpp
    ../src/dsp/effects/BitcrusherEffect.cpp
    ../src/dsp/effects/RingModulatorEffect.cpp
    ../src/dsp/EffectsChain.cpp
    ../src/dsp/effects/ConsolidatedDelay.cpp
    ../src/dsp/effects/DriveModule.cpp
    ../src/dsp/effects/DeEsserModule.cpp
    ../src/dsp/effects/DynamicsModule.cpp
    ../src/dsp/effects/EQModule.cpp
    ../src/dsp/effects/ModulationModule.cpp
    ../src/dsp/effects/SpaceModule.cpp
    ../src/dsp/effects/PitchModule.cpp
    ../src/dsp/PitchCorrector.cpp

    # Vocal effect source files needed by vocal tests
    ../src/dsp/effects/BreathNoiseGenerator.cpp
    ../src/dsp/effects/FormantTuner.cpp
    ../src/dsp/effects/VocalThickenerEffect.cpp
    ../src/dsp/effects/VocalNoiseReducer.cpp
    ../src/dsp/effects/SoloistVocalChain.cpp
    ../src/dsp/effects/CompressorEffect.cpp



    # Preset manager source files needed by effect preset tests
    ../src/dsp/PresetManager.cpp
    ../src/dsp/PresetFactory.cpp
    ../src/dsp/ProcessorStore.cpp

)

target_include_directories(AnaPlugTests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(AnaPlugTests
    PRIVATE
        Catch2::Catch2WithMain
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_gui_basics
        juce::juce_gui_extra
        ebur128
)

include(Catch)
catch_discover_tests(AnaPlugTests)
