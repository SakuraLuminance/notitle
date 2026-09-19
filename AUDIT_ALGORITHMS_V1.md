# AnaPlug 算法与漏洞审计 V1

**日期**：2026-09-19
**范围**：`src/dsp`（147 文件 / 33 128 行）、`src/gui`（50 / 9 772）、`src/dsp/effects`（61 / 8 776），含 `PluginProcessor.cpp`（94 KB）、`PresetManager.cpp`（63 KB）、`PresetFactory.cpp`（110 KB）
**判据**：① 逐行静态阅读并记录 `文件:行号`；② 与 JUCE 8.0.13 源码（`F:\juce813\modules`）对照，框架行为不靠猜；③ 本地 zig/clang 全量编译并运行 Catch2；④ 每条标注 **已核实**（本人读过该行）或 **未复核**（仅子代理报告）。CI（MSVC）仍是最终判据。

---

## 0. 本批已修复

| 位置 | 问题（已核实） | 修复 | 验证 |
|---|---|---|---|
| `src/dsp/WavLoader.cpp:47` | `AudioBuffer(numChannels, numSamples)` 两个维度都来自文件头（fmt 声道数、data 声明长度），只挡了 `numSamples <= 0`。60 字节 WAV 声明 0x8000 声道 + 512 MB data，就让 JUCE 的 `HeapBlock<Type, true>` 抛 `std::bad_alloc`；`src/` 全树无 try/catch，异常逃出 FileChooser 回调即宿主进程终止。重采样长度 `numSamples*44100/sr` 超过 INT_MAX 后 `static_cast<int>` 是 UB，负尺寸再变成天文数字分配。 | 校验声道 1..8、采样率 1000..768000、样本数 ≤ 32 M 且 ≤ 文件字节数；重采样长度用 double 计算并限幅；整段 try/catch 返回 `nullopt`。 | `tests/test_input_hardening.cpp` 3 例（坏声道 / 坏采样率 / 正常文件仍可加载） |
| `src/dsp/MultiFilter.cpp:61,113` | `slot.delayLine` 是默认构造的 `juce::dsp::DelayLine`，从未 `setMaximumDelayInSamples`。JUCE 构造把上限设为 0 → `totalSize = jmax(4, 0+2) = 4` → `getMaximumDelayInSamples() == 2`（`juce_DelayLine.h:135`），`setDelay()` 把一切钳进 [0,2]。Comb 的延迟是 `sampleRate/cutoff`（2..2400），所以 **Comb 恒为 2 采样反馈环，cutoff 旋钮完全无效**。 | 新增 `prepareCombDelayLine()`：先 `setMaximumDelayInSamples(ceil(sr/20)+4)` 再 `prepare()`（`prepare` 按上限分配，顺序必须如此）。 | `tests/test_input_hardening.cpp`：1200 Hz→40 采样、4800 Hz→10 采样（±2） |
| `src/dsp/MeteringEngine.cpp:70-73` | `process()` 里 `interleaveBuffer_.resize(needed)` = 音频线程分配。`prepare()` 只按声明的最大块分配，而宿主允许送更大的块（离线渲染、render-ahead）。 | 按已分配容量钳制计量样本数，不再重分配。 | 「MeteringEngine tolerates a block larger than it was prepared for」 |
| `src/PluginProcessor.cpp:1156` | `setStateInformation` 把宿主/工程文件给的 blob 直接交给 `ValueTree::readFromData`，无大小/深度/子节点数限制。 | 解析前限制 `1 ≤ sizeInBytes ≤ 32 MB`。 | 现有安全测试 |
| `src/dsp/PresetManager.cpp:776,1429` | `<Filters>`/`<Effects>` 子节点数直接决定实例化数量，无上限；每个效果 slot 会真实分配 DSP 状态（单个 `ConsolidatedDelay` 在 48 kHz 立体声就是 ~768 KB 延迟线），音频线程还要每块遍历全部 slot。 | 分别上限 16 / 32。 | — |
| `src/dsp/MidiLearn.cpp:108` | 音频线程 `for (auto& mapping : mappings_)` 与消息线程的 `erase/push_back/clear/``std::function` 赋值并发；元素持有 `std::function`，旧代码可能**调用已析构的函数对象**。头注释把「只在消息线程改」当作机制，实际没有任何锁。 | 新增 `juce::SpinLock lock_`：消息线程全部加锁；`processMidi` 用 `ScopedTryLockType`，抢不到就丢这一条 CC（永不阻塞音频线程）。学习模式改为音频线程只写原子 `pendingLearnCc_`，由消息线程 `applyPendingLearn()`（编辑器 30 Hz 轮询调用）或 `stopLearn()` 建表——建表要分配，不能落在音频线程；`learning_` 改原子并调整写入顺序。 | `tests/test_midi_learn.cpp`（2 处补 `applyPendingLearn()`，其余断言不变） |

附带：`src/dsp/PresetManager.h:284` 的 friend 改为 `friend class ::PresetManagerTestAccess;`。该类声明在全局作用域，未限定的 friend 名在 clang 下解析成另一个 `ana::PresetManagerTestAccess`，只有 MSVC 能通过——这正是 `test_security_v2.cpp` 与 `test_envelope_persistence.cpp` 在非 MSVC 工具链下编译不过、本地测试套件无法运行的原因。

---

## 1. 已核实、尚未修复

### 1.1 致命：PitchCorrector 的 STFT 每次调用重建，短块直接输出静音
- 已核实 `src/dsp/PitchCorrector.cpp:372-375`：`scratch_accum_` 虽是成员，但**每次调用**都 `std::fill(..., 0.0f)`；
- 已核实 `:389`：`for (int pos = 0; pos + fftSize <= numSamples; pos += hop)`，只合成完全落在**当前块内**的帧；
- 已核实 `:472-473`：`out[i] = accum[i] * 0.5f` 无条件写出；
- 已核实 `src/dsp/PitchCorrector.h:127-128`：`fftSize_ = 2048`、`hopSize_ = 512`。

→ 宿主块 < 2048（512 是最常见设置）时**一帧都不做**，整块输出 0。同样的三段循环结构还在 `:286`（PhaseVocoder）与 `:520`（Formant）。调用面：`src/dsp/effects/AutoTuneEffect.cpp:168`、`src/dsp/effects/PitchModule.cpp:279`（VocalProcessor 默认 Chest 走 PitchShift 分支）。
**为什么 CI 绿**：`tests/test_pitch_corrector.cpp` 只断言 `REQUIRE_NOTHROW(pc.process(buffer))`，从不检查输出非静音。
**修复**：改成有状态流式 STFT（跨调用保留输入 FIFO + OLA 累加器 + `prevPhase_/outPhase_`）。先写「512 块正弦输入 → 输出非静音、峰值在移调后频率」的失败测试。

### 1.2 高：重合成输出绕过整条 FX 链
- 已核实 `src/PluginProcessor.cpp:925`：`effectsChain_.process(voiceBuffer)` 作用在 `voiceBuffer`；
- 已核实 `:979-980`：重合成分支把采样播放**写进 `buffer`**，`:989` 才以 0.5× 把已处理的 `voiceBuffer` 叠上去，随后 `:991-1024` 直接 granular → freeze → master 并 `return`。

→ FX 页全部效果、vocal、multi-filter 只作用于音源声部（且仅 0.5×），**重合成主输出始终是干的**；`:994` 的注释「post-effects」与事实不符。

### 1.3 高：跨线程改容器（音频线程正在读同一条链）
- `src/gui/EffectRackComponent.cpp:414/565/679` 从消息线程对 `EffectsChain::slots`（`std::list`）做 erase/insert/clear，而 `src/dsp/EffectsChain.cpp:70-78` 在音频线程遍历；`src/dsp/PresetManager.cpp:1427` 的 `clear()` 由预设/宿主状态加载触发。erase 会析构 `EffectSlot` 与其 `unique_ptr<EffectBase>`。（机制已核实，具体崩溃窗口未复核）
- `src/dsp/GranularSynthesizer.cpp:20` `sourceBuffer = buffer;`（消息线程，`PluginProcessor.cpp:1249`，由 LOAD 按钮触发）与 `:514/:526` 音频线程的 `size()/[]` 并发 → 释放后使用 + 用旧长度索引新向量越界；`granularSourceReady_` 只被置 true、替换前不清零，起不到保护作用。（行号已核实）
- `src/PluginProcessor.cpp:1080` `getStateInformation()` 在宿主保存线程遍历活动效果链取状态，没有 `envLock_` 那类保护（信封路径**有** SpinLock，说明作者知道这里跨线程）。（结构已核实）

共同修法：消息线程只发布命令或不可变快照，音频线程在块边界取用；不要从消息线程直接改活动链。

### 1.4 中
- 已核实 `src/dsp/MultibandProcessor.cpp:53-57`：`bands_` 恒空（全仓库无 `setNumBands` 调用者）→ `process()` 第一件事就是 `return` → 整个多频段级（`PluginProcessor.cpp:917` 调用）是空操作。
- 未复核：`ArpeggiatorDriver.cpp:134` 用 `swapWith` 交换宿主 MIDI 缓冲后再 `clearQuick()`；`UndoManager` undo/redo 重入；`SpectralFreezeEngine` 历史无上限；`HPSSEngine` 用帧数而非 Σw² 归一化；`MuliiFilter::processSplit` 每块构造 4 个 IIR 局部对象（音频线程堆操作）。

---

## 2. 未接线模块：编译进产物、有单测、产品从不实例化
方法：对每个 `src/**/*.h` 统计「除自身 .cpp 外还有谁 include」以及「类型名是否出现在其它文件」，再用 grep 逐条复核（`VocoderMode`/`TimeStretchEngine`/`NeuralUpsampler`/`ImageSynthesizer`/`QuantumSpectralProcessor` 等 21 个全部 0 外部引用）。

| 模块 | 状态 |
|---|---|
| `HPSSEngine`、`QuantumSpectralProcessor` | 已在 HANDOFF_V3 §5 文档化为「有意不接线 / 保持零引用」 |
| `AdvancedGranularEngine`(23.7 KB)、`AudioToMidiConverter`、`BpmDetector`、`FMEngine`、`FrequencyShaper`(17.2)、`ImageSynthesizer`、`LocalEQ`、`NeuralStyleTransfer`(44)、`NeuralUpsampler`(46.6)、`PhysicalModel`(17.3)、`SpectralDynamics`(16.2)、`SpectralReverb`(18.4)、`SpectralSculptor`(24.4)、`SpectralSequencer`(32)、`StrumManager`、`TimeStretchEngine`(26.4)、`VocoderMode`(26)、`filters/CombFilter`、`gui/ModulationMatrixPanel` | **未文档化的零引用**（19 个模块、约上万行，`StrumManager` 内部还有音频线程 `std::mutex`、无上限队列等缺陷） |

影响：这些代码进产物、进单测（单测绿会给人「算法可用」的错觉），而产品行为与它们无关；内部缺陷一旦接线就变成线上问题。建议按「不接线不进 UI」的同一原则，要么移出编译与单测，要么在 HANDOFF 表格里逐条标注状态。

---

## 3. 已核实但**不是**缺陷（避免重复调查）
- `src/PluginProcessor.cpp:2188 updateLFOTarget()`：空实现，但注释写明路由已改为按参数处理、combo 仅为向后兼容，不是漏接线。
- `WavLoader` 的采样率/声道校验、`PitchCorrector::setFftSize` 的 64..8192 且 2 的幂校验、`NeuralStyleTransfer` 的 `log(max(1e-10, mag))`、`numFrames > 1` 除法保护：原本就有。

---

## 4. 复现与验证
- 本地全量编译 + 运行测试：`pwsh -File F:\anaplug-local\build-tests.ps1`（`-Filter '[wav_loader]'` 只跑某组；zig/clang + mingw，无 MSVC）。
- 本地 UI 审计：`pwsh -File F:\anaplug-local\build-audit.ps1` → `UI AUDIT: 59 snapshots, 2707 visible controls, 0 findings || advisory: cramped=128 invisible-control=2`。
- CI 仍是最终判据（MSVC 全量构建 + pluginval strictness 5 + UI 审计）。

## 5. 下一步优先级
1. PitchCorrector 流式化（§1.1）+ 失败测试先行；
2. 重合成分支与 FX 链合并（§1.2）；
3. EffectsChain / GranularSynthesizer / 机架索引的跨线程收敛（§1.3）；
4. MultibandProcessor 空操作等「接了线但没作用」项（§1.4）；
5. 未接线模块的编译与单测清单收敛（§2）。
