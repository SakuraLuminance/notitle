# AnaPlug Evolution Design Spec

> 目标：将 AnaPlug 转变为超越 Harmor 的顶尖加减法合成器，针对音MAD/YTPMV采样处理进行优化。

## Architecture

```
Input → STFT Analysis → Peak Detection → Partial Tracking → Phase Propagation → Resynthesis → Effects → Output
                                         ↓
                                  Dual Timbre (A/B Parts)
                                  Sub-Harmonic Generator
                                  Blur / Harmonizer / Phaser / Pluck / Prism
                                  Filter System (8 types, Serial/Parallel/Split)
                                  MAD/YTPMV: Pitch Correction, Time Stretch, Vocoder
```

## Phase 1: Core Engine Performance (Priority)

1. SIMD infrastructure (runtime detection, aligned allocators, vector math)
2. SoA refactor (PartialData → PartialDataSIMD)
3. Hybrid voice/partial architecture (512 partials, skip silent)
4. Lock-free voice state management
5. Performance benchmark suite

## Phase 2: Synthesis Features

6. Dual timbre system with 6 blend modes
7. Sub-harmonic generator (Around/Below)
8. Blur effect (horizontal + vertical)
9. Harmonizer (clone + transpose)
10. Frequency-domain phaser
11. Pluck simulation
12. Harmonic protection

## Phase 3: MAD/YTPMV Features

13. Real-time audio input sampling
14. 5 pitch correction algorithms
15. Non-linear time stretching + MIDI-fit
16. Per-voice noise reduction + comb filters
17. Vocoder mode

## Phase 4: GUI & Advanced Features

18. Visual feedback panel (real-time partials)
19. Spectrum sculpting
20. Advanced granular (L-system, particle cloud)
21. Neural style transfer / Generative timbre

## Tech Stack

- JUCE 8.0.0, C++17
- SIMD: Runtime detection (AVX2/SSE2), function pointer dispatch
- Testing: Catch2 v3.5.2
- Format: VST3
