# Draft: AnaPlug Evolution - Surpassing Harmor

## Project Analysis (Confirmed)

### Current Architecture
- **Framework**: JUCE 8.0.0, C++17, VST3 plugin
- **Core Pipeline**: STFT Analysis → Peak Detection → Partial Tracking → Resynthesis
- **Synthesis Modes**: Additive (partials), Granular, Image-to-Sound
- **Filter System**: 8 types (LP, HP, BP, Notch, AllPass, Comb, Formant, Morph), Serial/Parallel/Split routing
- **Modulation**: LFO (5 waveforms), Multi-point Envelope (32 breakpoints), Filter Modulation System
- **Effects**: Chorus, Delay, Distortion, EQ, Reverb, Prism (5 spectral modes)
- **Voice Management**: 16 voices, ADSR, Round-Robin/Oldest-First/Random allocation
- **Other**: Unison Engine (1-8 voices), Arpeggiator (6 modes), Preset Manager, GUI components

### Strengths
1. **Solid additive foundation** - STFT → Partial Tracking → Resynthesis pipeline is well-structured
2. **Image synthesis** - Already supports image-to-sound conversion (like Harmor)
3. **Prism effect** - 5 spectral modes (FrequencyShift, SpectralBlur, HarmonicRotation, SpectralMirror, CombSweep)
4. **Flexible filter routing** - Serial/Parallel/Split with morphing capability
5. **Granular synthesis** - 256 grains, multiple window types, position modulation
6. **Partial editor canvas** - Manual editing of partial data (draw/line/rect/eraser)
7. **Modulation system** - LFO + Envelope + Velocity/Modwheel/Aftertouch sources

### Weaknesses (vs Harmor)
1. **No dual-timbre system** - Harmor has Timbre 1+2 with multiple blend modes
2. **No sub-harmonic generation** - Harmor adds sub harmonics for weight
3. **No harmonic protection** - Harmor protects low harmonics from filtering
4. **No blur effect** - Horizontal/Vertical partial smearing
5. **No harmonizer** - Clone and transpose harmonics
6. **No phaser in frequency domain** - Harmor's phaser operates on partials
7. **No pluck simulation** - Frequency-dependent decay
8. **Limited voice count** - 16 vs Harmor's 516 partials per note per unison voice
9. **No A/B parts** - Harmor has two independent synthesis sections
10. **No strum effect** - Chord delay simulation
11. **No local EQ** - Per-note EQ relative to root frequency
12. **No adaptive envelope** - Bandwidth changes with note frequency
13. **No modulation X/Y/Z** - Freely assignable modulation targets
14. **No visual feedback panel** - Real-time partial visualization

### Missing for MAD/YTPMV (vs SlowSampler2)
1. **Real-time audio input sampling** - SlowSampler2 can sample audio tracks in real-time
2. **Advanced pitch tuning** - Multiple algorithms for off-key sample correction
3. **Formant preservation** - Separate pitch and formant control
4. **Non-linear time stretching** - Speed curves for creative effects
5. **MIDI-fit mode** - Fit audio to MIDI note length
6. **Per-voice noise reduction** - Clean up sampled audio
7. **Per-voice comb filters** - Enhance sampled audio
8. **Vocoder mode** - Use another audio sample for modulation

## Requirements (Confirmed)

### Core Objective
将 AnaPlug 转变为超越 Harmor 的顶尖加减法合成器，同时针对音MAD/YTPMV采样处理进行优化。

### Scope Boundaries
- **包含**: 所有合成、滤波、调制、效果器和GUI改进
- **排除**: 完全重写（在现有基础上构建）、DAW特定功能

### 技术决策
- **性能目标**: 每个复音声部512+泛音（匹配Harmor）
- **架构**: 保持JUCE 8.0.0兼容性，C++17
- **插件格式**: VST3（主要），后续扩展到AU/AAX

### 用户确认的决策
1. **优先级**: 性能与核心引擎
2. **优化方法**: SIMD优先
3. **SIMD目标**: 运行时检测（AVX2/SSE2自适应）
4. **声部架构**: 混合模式（固定分配，跳过静音泛音）
5. **MAD/YTPMV方法**: 两者都实现（SlowSampler2 + Harmor风格）
6. **测试策略**: 三者都要（单元测试 + 集成测试 + 性能基准）

## Confirmed Design Decisions

### 1. 核心引擎设计
- **SIMD运行时检测**: CPUID检测AVX2/SSE2，函数指针分派
- **混合泛音架构**: 固定512泛音分配，跳过静音泛音
- **SoA数据布局**: Structure of Arrays for SIMD efficiency
- **无锁声部分配**: 原子状态转换，实时安全

### 2. 合成模式设计
- **双音色系统**: 6种混合模式（Fade/Subtract/Multiply/Maximum/Minimum/Pluck）
- **次谐波生成器**: 2种配置（Around/Below）
- **模糊效果**: 水平（时间）+ 垂直（谐波）模糊
- **谐波器**: 克隆+转置，宽度/强度/偏移/间隙控制

### 3. MAD/YTPMV功能设计
- **实时音频输入**: 环形缓冲区，触发采样
- **高级音高校正**: 5种算法（Simple/Formant/Spectral/Grain/PhaseVocoder）
- **非线性时间拉伸**: 速度曲线，MIDI-Fit模式
- **每声部降噪**: 频谱门限，自适应噪声轮廓
- **每声部梳状滤波器**: 前馈/反馈模式
- **声码器模式**: 外部音频调制

### 4. 前卫创新功能
- **AI音高检测**: 神经网络+维特比算法
- **频谱变形引擎**: 5种变形模式
- **高级粒子合成**: 粒子云，L-System轨迹，粒子效果
- **频谱冻结**: 4种冻结模式，实时操纵
- **频谱序列器**: 步进控制，随机化
- **频率塑形器**: 6种塑形类型

### 5. 突破性创新功能
- **神经风格迁移**: 音频风格迁移
- **生成式音色设计**: GAN生成音色，潜在空间操作，遗传算法进化
- **频谱雕刻**: 直接在频谱上绘画，8种工具
- **频谱粒子系统**: 物理模拟，力场，生命周期
- **频谱区块链**: 去中心化音色共享和交易
- **量子频谱计算**: 量子傅里叶变换，叠加合成

## Research Findings

### Harmor Key Features to Implement
- Dual timbre system with blend modes (Fade, Subtract, Multiply, Max, Min, Pluck)
- Sub-harmonic generation (configurable: Around/Below fundamental)
- Harmonic protection (low harmonics immune to filtering)
- Blur effect (horizontal time smearing, vertical harmonic smearing)
- Harmonizer (clone + transpose harmonics with width/strength/shift/gap)
- Frequency-domain phaser (multiple types: Classic, Triangle, Deep, etc.)
- Pluck simulation (frequency-dependent decay)
- A/B parts (independent synthesis sections)
- Strum effect (chord delay with tension)
- Local EQ (per-note relative to root frequency)
- Adaptive envelopes (bandwidth changes with note frequency)
- Modulation X/Y/Z (freely assignable targets)
- Visual feedback panel (real-time partial visualization)

### SlowSampler2 Key Features for MAD/YTPMV
- Real-time audio input sampling
- Advanced pitch tuning algorithms (off-key correction)
- Formant preservation (separate pitch/formant control)
- Non-linear time stretching (speed curves)
- MIDI-fit mode (fit audio to note length)
- Per-voice noise reduction
- Per-voice comb filters
- Vocoder mode (external audio modulation)

## Scope Boundaries
- **INCLUDE**: Synthesis engine, filtering, modulation, effects, GUI, MAD/YTPMV features
- **EXCLUDE**: Complete rewrite, DAW-specific features, third-party plugin integration
