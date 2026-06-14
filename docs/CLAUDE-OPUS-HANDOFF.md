# AnaPlug VST3 — Claude Opus 完整参考 + 开发路线图

> 生成日期: 2026-06-14
> 总模块: 60+ DSP | 15 GUI | 21 测试
> 技术栈: JUCE 8.0.0, C++17, CMake 3.22+, VST3 + CLAP
> 项目路径: F:\anaplug

---

## 第一章：项目全貌

### 一分钟理解

AnaPlug 是一个加法合成器 VST3 插件。它先把音频做 STFT（短时傅里叶变换），检测出频谱里的峰值（泛音），然后把这些泛音变成可以独立控制的 512 个"旋钮"（频率/振幅/相位）。之后可以对这些泛音做各种处理（滤波、效果器、音高校正、时间拉伸），最后合成回音频。

```
输入音频 → STFT → 峰值检测 → 泛音跟踪 → [处理] → 重建输出
                                    ↓
                             512个可独立控制的频率/振幅/相位
```

### 文件结构

```text
F:\anaplug/
├── CMakeLists.txt              # 构建系统（JUCE 8.0.0 + Catch2）
├── src/
│   ├── PluginProcessor.h/.cpp   # 音频/MIDI 入口
│   ├── PluginEditor.h/.cpp      # GUI 入口（赛博朋克 Harmor 风格）
│   ├── dsp/                     # 60+ DSP 模块
│   │   ├── PartialData.h         # 旧版数据模型（正淘汰中）
│   │   ├── PartialDataSIMD.h     # 新版 SIMD 数据模型（新开发用这个）
│   │   ├── SIMDSupport.h         # AVX2/SSE2/NEON 运行时检测
│   │   ├── AnaPlugEngine.h/.cpp  # 顶层编排器
│   │   ├── VoiceManager.h/.cpp   # 32复音引擎
│   │   ├── PitchCorrector.h/.cpp # 5种音高校正（920行）
│   │   ├── effects/              # 5种效果器
│   │   └── filters/              # Comb + Formant
│   ├── gui/                     # 15个GUI组件
│   │   ├── CyberpunkTheme.h      # 赛博朋克 LookAndFeel
│   │   ├── VisualFeedbackPanel   # 实时泛音可视化
│   │   ├── WaterfallDisplay      # 3D瀑布频谱图
│   │   └── SpectrumEditorCanvas  # 交互式频谱编辑
│   └── ...
├── tests/                      # 21个测试文件
└── docs/
    └── CLAUDE-OPUS-HANDOFF.md   # ← 你现在正在读这个
```

---

## 第二章：核心架构

### 数据模型

两种数据模型并存，新版是迁移目标：

| 特性 | PartialData (旧) | PartialDataSIMD (新) |
|------|-----------------|---------------------|
| 数据布局 | AoS（`vector<Frame>`） | SoA（`float[512]` × 3） |
| 分配方式 | `std::vector` 堆分配 | 固定数组，0 分配 |
| 对齐 | 无 | `alignas(32)` |
| 活跃跟踪 | `partials.size()` | 位掩码 + `activeCount` (O(1)) |
| 转换方法 | — | `fromPartialData()` / `toPartialData()` |
| SIMD 友好 | ❌ | ✅ 缓存连续 + 对齐加载 |
| 使用方 | AnaPlugEngine, STFTAnalyzer, BlurEffect, Harmonizer, PrismEffect | 所有 Phase 1+ 新模块 |

**核心规则**: 新功能全部用 `PartialDataSIMD`。需要和旧引擎交互的地方用 `fromPartialData()` 转。

### PartialDataSIMD 结构

```cpp
// src/dsp/PartialDataSIMD.h
struct PartialDataSIMD {
    static constexpr int kMaxPartials = 512;
    int maxPartials = kMaxPartials;
    double sampleRate = 44100.0;
    int hopSize = 512;
    int activeCount = 0;
    
    alignas(32) float frequency[kMaxPartials];
    alignas(32) float amplitude[kMaxPartials];
    alignas(32) float phase[kMaxPartials];
    uint32_t activeMask[16];
    
    bool isActive(int idx) const;
    void updateActiveMask();
    int getNextActive(int from) const;
};
```

### 线程安全模型

**原则**: 音频线程永不等待。所有线程间通信用 `std::atomic<>` + CAS。

```
音频线程 (processBlock)          UI线程 (setter/editor)
        │                               │
        │  std::atomic<> read            │  std::atomic<> write
        │  CAS voice allocation          │  double-buffer flip
        │                               │
        └──────────────┬────────────────┘
                       ▼
             无锁 + 无等待通信
```

```cpp
// 原子字段（跨线程共享）
std::atomic<VoiceState> state{VoiceState::free};
std::atomic<float> pitchHz{440.0f};
std::atomic<float> amplitude{0.5f};
std::atomic<float> envelopeLevel{0.0f};
std::atomic<float> aftertouch{0.0f};

// 非原子字段（仅音频线程访问）
float phase = 0.0f;
float phasorRe = 1.0f;
float phasorIm = 0.0f;
```

---

## 第三章：命名规范与模式速查

### 命名规则

| 元素 | 规范 | 示例 |
|------|------|------|
| 命名空间 | `namespace ana {}` | 所有 DSP/GUI |
| 插件类 | 全局命名空间 | `AnaPlugAudioProcessor` |
| 类名 | PascalCase | `VoiceManager`, `MultiFilter` |
| 方法 | camelCase | `noteOn()`, `process()` |
| 成员变量 | 尾部 `_` | `rootNote_`, `sampleRate_` |
| 常量 | `k` 前缀 | `kMaxPartials` |
| 文件 | PascalCase | `PartialTracker.h` |
| 测试 | snake\_case | `test_voice_manager.cpp` |
| 枚举 | `enum class` | `enum class FilterType { ... }` |

### DSP 模块模板

```cpp
class SomeModule {
public:
    SomeModule();
    ~SomeModule() = default;
    void prepare(double sampleRate);
    void reset();
    void process(PartialDataSIMD& data);

private:
    double sampleRate_ = 44100.0;
    mutable std::vector<float> scratch_;      // 预分配暂存区
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SomeModule)
};
```

### GUI 组件模板

```cpp
class SomeWidget : public juce::Component, public juce::Timer {
public:
    SomeWidget();
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    mutable juce::CriticalSection dataLock_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SomeWidget)
};
```

---

## 第四章：构建系统

### 编译

```bash
cd F:\anaplug
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# 输出: build/AnaPlug_artefacts/Release/VST3/AnaPlug.vst3
```

### 添加源文件

在 `CMakeLists.txt` 的 `target_sources(AnaPlug PRIVATE ...)` 中加一行。

### 添加测试

在 `tests/CMakeLists.txt` 的 `add_executable(...)` 中加一行。

### 当前优化配置

| 编译器 | Flags |
|--------|-------|
| MSVC | `/O2 /fp:fast /GL /arch:AVX2 /LTCG` |
| Clang/GCC | `-O3 -ffast-math -march=native -flto` |
| ARM | `__ARM_NEON__=1` |

---

## 第五章：已知问题清单

### 🔴 严重（必须修）

| # | 文件 | 问题 | 修复方式 |
|---|------|------|----------|
| 1 | `CMakeLists.txt` | **NeuralStyleTransfer.cpp 未注册** | target_sources 加一行 |
| 2 | `PluginProcessor.cpp` | `getStateInformation/setStateInformation` **为空** | 实现预设序列化—所有参数都不能保存 |

### 🟡 中等

| # | 文件 | 问题 | 修复状态 |
|---|------|------|----------|
| 3 | `PluginEditor.cpp:36` | SubHarmonicGenerator 回调为空 | 未修 |
| 4 | `PluginEditor.cpp` | `reduced()` 4参数调用 | **已修** ✅ |
| 5 | `SIMDSupport.h` | AVX2+SSE2 同时编译冲突 | **已修** ✅ |
| 6 | `PluginEditor.cpp:506` | `fromPartialData` 参数错误 | **已修** ✅ |

### 🔵 技术债务

| # | 文件 | 问题 |
|---|------|------|
| 7 | `SpectralSequencer.cpp` (845行) | 复杂度高，18个提前返回 |
| 8 | `PresetManager.cpp` (1422行) | 超大文件，考虑拆分 |
| 9 | `effects/*.cpp` | 全是 JUCE 的薄封装（有意为之）|

---

## 第六章：测试覆盖

### 已有测试（21个）

`sanity, data_structures, wav_loader, stft_analyzer, peak_detector, partial_tracker, resynthesis, phase_propagation, engine_integration, unison_engine, voice_manager, lfo_system, arpeggiator, granular_synthesis, image_synthesizer, multi_point_envelope, partial_editor_canvas, prism_effect, multi_filter, bench_performance`

### 无测试（29个模块，按风险排列）

**P0**: `PitchCorrector` (920行, 5算法), `TimeStretchEngine`, `VocoderMode`
**P1**: `DualTimbre`, `FrequencyShaper`, `SpectralSculptor`, `SpectralFreeze`, `SpectralSequencer`, `PhysicalModel`, `SampleProcessor`
**P2**: 其余 Spectral* / Neural* / ParallelProcessor / MacroController / GenerativeTimbreDesigner / QuantumSpectral

---

## 第七章：修复验证（全部已确认）

| # | Bug | 位置 | 状态 |
|---|-----|------|------|
| 1 | PhasePropagation while 无限循环 | L50: `std::remainder` | ✅ |
| 2 | VoiceManager 数据竞争 | 关键字段全 `std::atomic<>` | ✅ |
| 3 | Resynthesis int32 溢出 | L25: `const size_t outputLength` | ✅ |
| 4 | PitchCorrector 堆分配/帧 | `ensureScratchSizes()` 16个暂存区 | ✅ |
| 5 | MultiFilter 静态线程不安全 | `lastDelayed` 按 slot + `std::call_once` | ✅ |
| 6 | FTZ/DAZ 未设 | 3 个入口点都设了 | ✅ |
| 7 | SpinLock 丢帧 | 替换为原子双缓冲 + WaitableEvent | ✅ |

---

## 第八章：开发路线图

### 8.1 算法优化

| 编号 | 项目 | 难度 | 收益 | 说明 |
|------|------|------|------|------|
| **OPT1** | BlurEffect/Harmonizer/PrismEffect SIMD 迁移 | 中 | 高 | 从 `PartialData` 迁移到 `PartialDataSIMD`，消除堆分配 |
| **OPT2** | PhasePropagation 双精度 | 小 | 中 | 消除长时间相位漂移 |
| **OPT3** | WSOLA 时间拉伸算法 | 中 | 高 | 比相位声码器更适合打击乐 |
| **OPT4** | STFT 并行帧处理 | 中 | 高 | 借助 ParallelProcessor，4-6x |
| **OPT5** | Resynthesis OLA SIMD | 小 | 中 | 向量化累加 |
| **OPT6** | PGO 编译 | 小 | 中 | 白送 10-20% |

### 8.2 Harmor 缺失功能

| 编号 | 功能 | 难度 | 收益 | 说明 |
|------|------|------|------|------|
| **F1** | 🔴 谐波保护 | 小 | 高 | 低次谐音不受滤波衰减 |
| **F2** | A/B 独立信号链 | 大 | 高 | Timbre A/B 各有独立滤镜链 |
| **F3** | 扫弦 Strum | 小 | 中 | 和弦内音符交错触发 |
| **F4** | 本地 EQ | 中 | 中 | EQ 频率相对基频缩放 |
| **F5** | 自适应包络 | 小 | 中 | 包络时间随频率缩放 |
| **F6** | 调制自由路由 X/Y/Z | 大 | 高 | GUI 调制矩阵 |

### 8.3 MAD/YTPMV

| 编号 | 功能 | 难度 |
|------|------|------|
| **M1** | BPM 检测 + 自动对齐 | 中 |
| **M2** | 采样切片重组 | 大 |
| **M3** | 轻量音源分离 (HPSS/REPET) | 大 |
| **M4** | 交叉合成实时版 | 中 |

### 8.4 架构工程

| 编号 | 项目 | 难度 | 说明 |
|------|------|------|------|
| **A1** | 🔴 **预设序列化** | 小 | 当前为空——所有参数不能保存，**最高优先级** |
| **A2** | CLAP 格式验证 | 小 | 已声明但从未测试 |
| **A3** | GitHub CI | 小 | workflow 文件已创建未激活 |
| **A4** | PartialDataSIMD 测试 | 小 | 无单元测试 |
| **A5** | 29 模块补测试 | 大 | 分批补 |

### 8.5 GUI

| 编号 | 项目 | 难度 |
|------|------|------|
| **G1** | 预设浏览器 | 中 |
| **G2** | 调制矩阵 | 大 |
| **G3** | 分频段编辑 | 中 |
| **G4** | 暗/亮主题 | 小 |

### 8.6 新创新

| 编号 | 功能 | 难度 |
|------|------|------|
| **I1** | AI 音色搜索 | 大 |
| **I2** | 实时 Auto-Tune | 大 |
| **I3** | 2D 波导合成 | 大 |
| **I4** | 音频 → MIDI | 中 |

---

## 第九章：建议执行顺序

```
立即（1天内）：
  A1: 预设序列化          ← 最严重，所有参数都不能保存
  🔥 加 NeuralStyleTransfer.cpp 到 CMakeLists.txt

第 1 周：
  F1: 谐波保护             ← 小改动大效果
  G1: 预设浏览器            ← 配合 A1
  OPT6: PGO 编译           ← 白送性能
  A5: PitchCorrector 测试  ← 最高风险模块

第 2 周：
  OPT1: 遗留模块 SIMD 迁移
  OPT3: WSOLA 时间拉伸
  M1: BPM 检测

第 3 周：
  F6 + G2: 调制矩阵
  OPT4: STFT 并行

第 4 周+：
  F2: A/B 独立信号链
  M2: 采样切片
  I2: 实时 Auto-Tune
```

---

## 附录：给 Claude Opus 的第一次任务

你正在接手 AnaPlug VST3 合成器。项目在 `F:\anaplug`。

先读 `docs/CLAUDE-OPUS-HANDOFF.md` 了解全貌。

**第一次任务（两个小改动）：**

1. 把 `src/dsp/NeuralStyleTransfer.cpp` 加入 `CMakeLists.txt` 的 `target_sources` 中（当前 914 行代码被排除在构建外）
2. 实现 `PluginProcessor.cpp` 的 `getStateInformation()` / `setStateInformation()`，让预设参数可以保存和加载（当前两个方法是空的，所有旋钮设置关掉后就没了）
