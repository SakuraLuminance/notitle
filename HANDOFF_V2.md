# AnaPlug 交接文档 V2（2026-08-29）

> 面向下一个接手的 AI / 开发者。包含：项目全貌、环境铁律、已完成修复及根因、**全部遗留失败的精确清单与修复策略**、UI 重构计划、CI 取证工作流。
> 旧文档 HANDOFF.md（2026-07-09）中的 token 已失效，本文档**不含任何密钥**。

---

## 0. 交接时刻的实时状态（2026-08-29，接手者先读这里）

**用户已指示暂停修复，交接给下一个 AI。进度冻结在 Batch 2 中途。**

### 0.1 Git / CI 坐标

| 项 | 值 |
|---|---|
| 本地 HEAD（已推送） | `2a34afb` docs: HANDOFF_V2 + test: arpeggiator advanceSteps 时序修复 |
| 前一提交 | `517621c` fix: MultiFilter ProcessorDuplicator（**尚未经 CI 确认**） |
| 再前 | `b63b560` ProcessSpec 零初始化 + TestHarness prepare + Delay 测试修正 → Run #87 |
| Run #84 | **里程碑全绿**（pluginval 绿，产物已下载到 `F:\anaplug\artifacts\84\`） |
| Run #86 | `33207910631` completed/**success**（a2651f7） |
| Run #87 | `33209217295` completed/**success**（b63b560） |
| Run #88/#89 | 分别对应 517621c / 2a34afb，**交接时未核验状态**——接手第一件事：`runs?head_sha=<sha>` 查这两轮，确认 MultiFilter LP 与 Arpeggiator 6 用例是否转绿 |

### 0.2 Run #87 取证产物坐标（已在本地，勿重复下载）

- 本地目录：`$env:TEMP\forensics87\`（forensics.txt / test-names.txt / test-results.xml / test-stderr.log / test-stdout.log）
- Artifact ID：test-forensics `9701385680`、pluginval-log `9701411318`、VST3 `9701396457`、CLAP `9701396950`（run 33209217295）
- `test-results.xml` 解析模板见 §6.5。

### 0.3 工作区有未提交的 Batch 1 修复（本地 F:\anaplug，已编辑、未 CI 验证、未提交）

接手者可选择直接提交推送验证，或先审查。**文件与内容**：

1. `src/dsp/ResynthesisEngine.cpp`（resynthesize 开头）— `if (numFrames == 0) return {};`
   根因：`(numFrames - 1) * hopSize + fftSize` 在 numFrames=0 时 **size_t 下溢** → 返回非空垃圾缓冲。
   修 2 个用例：ResynthEngine - empty partial data returns empty result；ResynthesisEngine - resynthesize empty data。
2. `src/dsp/SpectralDNA.cpp`（evolveGeneration 开头）— 种群 <2 直接 return（原来会克隆扩容到 2）。
   修：Edge cases: single individual。设计取舍已定：种群大小是固定契约，单个体无法与自己重组；EvolutionPanel 只在 popSize>0 时调用，不受影响。
3. `tests/test_consolidated_effects.cpp:90` — `names[6]` → `names[5]`
   工厂表实测序（PresetFactory.cpp:1211-1217）：0 Warm Drive / 1 Edge Of Breakup / 2 Tube Scream / 3 British Plexi / 4 Tape Saturate / **5 Console Drive** / 6 Hard Clipper / 7 Fuzz Face / 8 Fold Synth / 9 Octa Fold / 10 Lo-Fi Crush / 11 8 Bit / 12 Tremolo Ring / 13 Bell Tone。测试原期望与工厂序矛盾，以实现为准（与 UI 类型分组注释一致）。
4. `tests/test_effect_presets.cpp`（AutoTuneEffect 序列化用例）— `setRetuneSpeed(25)` → `15`，期望同步改 15。
   实现 clamp 到 [0.01, 20] ms（AutoTuneEffect.cpp:33）是有意契约；测试原值越界。

### 0.4 Batch 2 根因侦察结果（VoiceManager — 4 个 bug 已定位，修复代码未写）

以下结论已通过读实现 + JUCE 8 源码（`juce-test-clone/` 本地副本）核实，**接手者可直接照写**：

1. **oldest-first allocation**（test_voice_manager.cpp:558，`getVoice(0)->note==72 => 60`）
   `findFreeVoice`（VoiceManager.cpp:434-484）三个分支全部只 CAS `VoiceState::free`——**idle 声音永不复用**。slot 0 释放转 idle 后，noteOn(72) 找不到 free → 走 steal 抢了 sustain 的 slot 1。
   修法：三个分配分支（roundRobin/oldestFirst/random）的 CAS 改为接受 free **或** idle（先用 `expected = free` CAS，失败则 `expected = idle` 再 CAS）。注意 VoiceManager.cpp:508 的 `allocateVoice()` 本就处理 idle，但 findFreeVoice 路径根本没走它（疑似死代码——顺手确认后二选一收敛）。
2. **noteOff releaseStartLevel**（test_voice_manager.cpp:112，`> 0 => 0`）
   遗留 noteOff（:1051）→ `noteStopped(true)` → noteStopped 存当前 `envelopeLevel`（:83）。但该测试 noteOn 后**没 process 就 noteOff**，envelopeLevel 必为 0 → 期望永不可能满足。
   修法：**改测试**——noteOn 后先 `vm.process(makeBuffer(int(0.005 * testSampleRate)))`（默认 attack 0.01s → env≈0.5）再 noteOff，`releaseStartLevel ≈ 0.5 > 0` 成立，测试意图（"noteOff 记录释放起点电平"）不变。
3. **envelope attack phase**（test_voice_manager.cpp:268-276，`state == decay => {?}==2`）
   逐样本 float 累加 attackDt 恰好在 4410 步（=100ms）时累加误差 ~1e-4 → `env >= 1.0f` 判定失败 → 状态停在 attack。
   修法（VoiceManager.cpp:252）：阈值加容差 `if (env >= 0.999f)`，进入时 `envelopeLevel = 1.0f` 钳位不变。50ms 用例（env≈0.5）无误触发风险。
4. **process called before prepare**（test_voice_manager.cpp:806，`hasAudio => false`）
   JUCE 8 源码核实：`MPESynthesiserBase::renderNextBlock` 对 sampleRate==0 只有 jassert（release 下无效）→ 照常渲染；`AnaVoice` 用 `getSampleRate()`（VoiceManager.cpp:139）= 0 → sinDelta/cosDelta = NaN → 输出 NaN → `abs(NaN)>0` 为 false → 静音。
   **关键**：不能在 process() 里调 `setCurrentPlaybackSampleRate`——`MPESynthesiser::setCurrentPlaybackSampleRate` 会 `turnOffAllVoices(false)` + base 层 `instrument.releaseAllNotes()`（测试先 noteOn 后 process，会杀掉音符）。
   修法：VoiceManager 加 `bool voicesPrepared_ = false;` 成员；`process()` 开头 `if (!voicesPrepared_) { for (i…) getVoice(i)->setCurrentPlaybackSampleRate(sampleRate_); voicesPrepared_ = true; }`（`SynthesiserVoice::setCurrentPlaybackSampleRate` 是 public，逐 voice 设置不触发杀音符）；`prepare()` 里置 true。
   注意 testSampleRate 常量在 test 文件内（44100），与 VoiceManager 默认 sampleRate_=44100 一致。

### 0.5 其余批次的侦察线索（未开工，见 §5/§7 全量清单）

- MultiPointEnvelope ADSR shape（test_wired_modules.cpp:497，sustain 0.43 vs 0.65 / vSustain 0.12 vs 0.5）：a2651f7 把段曲线改为取**段末**断点——ADSR 段可能需要曲线归属**段起点**断点；需做契约决断并同步 a2651f7 的对齐。
- MultiPointEnvelope loop modes（test_multi_point_envelope.cpp:328）：循环结束后 isActive 仍 true + 循环点值 0 vs 1.0。
- LFO tempo sync（test_lfo_system.cpp:272）：相位边界 0.00028 vs 1.0（wrap off-by-one 嫌疑）。
- Volume ADSR（test_modulation.cpp:397）：0.031 vs <0.01（release 尾巴）。
- Batch 3（ENV pool vals 映射 / LFO+ModBus / MPE voiceIdx / Source switching）、Batch 4（DSP 精度族 13 项）、Batch 5（Preset round-trip ×4）、Batch 6（日志级 12 条）完全未开工。
- UI 重构（Phase 3，§9）未开工。

## 1. 项目是什么

**AnaPlug** — JUCE 8.0.17 合成器/效果器插件（VST3 + CLAP，Windows x64，静态 CRT /MT）。
仓库：`github.com/SakuraLuminance/notitle`（public，main 分支直推）。
本地路径：`F:\anaplug`。许可证见仓库。

### 架构总览

```
src/
  PluginProcessor.h/.cpp   AudioProcessor + 全部成员对象（engine/effectsChain_/voiceManager/presetManager/dnaEvolver_ 等，声明序 390-617 行）
  PluginEditor.h/.cpp      编辑器（单体 1623 行，~90 成员组件，手写百分比布局 computeRegions()，无 APVTS，手动 onValueChange + MidiLearn + Timer 刷新）
  dsp/                     全部 DSP 模块（约 60+ 模块；EffectsChain/MultiFilter/ResynthesisEngine/PresetFactory/SpectralDNA/…）
  gui/                     独立 UI 组件（12 个；CyberpunkTheme 单例主题；WaveformDisplay 硬编码颜色、ModulationMatrixPanel 未接入主题 = 已知缺口）
  PresetManager / ProcessorStore / PresetFactory（工厂预设表在 PresetFactory.cpp 1200-1248 行）
tests/                     66 个 Catch2 测试文件（536 用例）；main.cpp 自带 JuceInitialiser（勿移回静态区）
.cgithub/workflows/        build-windows.yml（见 §4）
artifacts/84/              本地已下载的绿色构建产物（VST3 + CLAP）
```

**关键契约（改代码前必读）**
- `ProcessSpec` 全字段必须零初始化（历史 bug：垃圾 maximumBlockSize → bad_alloc）。
- JUCE `dsp::IIR::Filter` 是**单声道**处理器；多通道必须用 `ProcessorDuplicator`（MultiFilter.h 已修）。
- JUCE 8 `dsp::FFT::perform*RealOnly*` 实数 FFT 需要 **2×size** 缓冲。
- 实 FFT 分析侧：`std::vector<std::complex<float>>`（JUCE 8 签名变更，见旧迁移）。
- `PartialDataSIMD::getNextActive(i)` 为**排他**语义：传入 i 返回"下一个"active 索引；`getNextActive(-1)` 返回第一个。LocalEQ 依赖此契约。
- 效果 reset() 在未 prepare 时不得按通道数索引空 vector（P0 根因家族，Drive/Flanger/Phaser/RingMod 已修——新增效果器务必复用该模式）。
- MultiPointEnvelope 段曲线从**段末**断点的曲线属性取值。
- `initializeDefaultEffects()` 已从构造函数**延迟到 prepareToPlay**（commit 0b75955 家族）；编辑器打开时效果链为空是**预期状态**（EffectRackComponent 需容忍）。

---

## 2. 环境铁律（勿再踩坑）

1. **本机（F:\anaplug）没有任何编译工具链**：无 cmake/msvc/ninja，vswhere 找不到 VS。**所有构建/测试验证只能推 GitHub Actions**，每轮 ~15-20 分钟。禁止幻想本地编译。
2. **CI 日志必须用认证 API 拉取**（匿名只能看到状态）：
   ```powershell
   $tok = '<用户会话内提供的 PAT>'   # PAT 由用户在会话中提供；绝不写入文件
   curl.exe -s -H "Authorization: Bearer $tok" "https://api.github.com/repos/SakuraLuminance/notitle/actions/runs?per_page=5"
   # 拿 run_id → jobs → job_id → 下载日志：
   curl.exe -s -L -H "Authorization: Bearer $tok" "https://api.github.com/repos/SakuraLuminance/notitle/actions/jobs/{job_id}/logs" -o job.log
   ```
3. **产物（含 test-forensics）下载**：`GET /actions/runs/{run_id}/artifacts` → `archive_download_url` → curl -L -H token → Expand-Archive。
4. push 方式：`git push https://x-access-token:<PAT>@github.com/SakuraLuminance/notitle.git main`（仅命令行内使用）。
5. 临时文件放 `F:\temp\opencode`（已预授权）。取证产物已存在 `$env:TEMP\forensics87\`（test-results.xml 等）。

---

## 3. 已完成修复（全部经 CI 验证，按 commit）

| Commit | 修复 | 根因 |
|---|---|---|
| **5d27969** | **P0：VST3/REAPER 加载即崩** | `DriveModule::reset()` 未 prepare 时按通道数索引空 vector → 空指针写。路径：宿主 terminate → releaseResources → VocalProcessor::reset → drive_.reset()。同族修复 Flanger/Phaser/RingModulator |
| d3825c7 | `runEnvPool` 传 `{}` 空指针写 | 接受 nullptr 并跳过写 |
| 3497b43 | makeCounterCmd 空指针解引用 | description-only 命令守卫 |
| ffab207 | VST3/CLAP/Standalone 也输出 PDB | DLL 链接步持有 PDB |
| 3497b43→b50c352 | **取证管线**（见 §6） | 每测试隔离 + 源码提取用例名 + 输出尾迹 |
| a2651f7 | UnisonEngine noteOn 空表 OOB；PartialDataSIMD getNextActive 排他语义；MultiPointEnvelope 段曲线取段末断点；对齐过时测试期望 | 多处 |
| b63b560 | **ProcessSpec 零初始化**（EffectsChain/MultiFilter）→ 预置测试 bad_alloc+double-free 消失；TestHarness prepare 全链；Delay 序列化测试百分比 setter 误用 | 垃圾 maximumBlockSize |
| 517621c | **MultiFilter 槽位 IIR → ProcessorDuplicator** | JUCE IIR 是单声道处理器，立体声通道 1+ 直通（LP 测试 0.99999 透过） |

**里程碑**：Run **#84** 首次全绿 —— pluginval（宿主方式）strictness 1 通过，`Open plugin (cold)/(warm)` 全过。**"编译了却用不了"已解决**，产物可直接装 REAPER（§8）。

### 测试 exe 启动段错误的修复（历史）
`JuceInitialiser` 在 CRT 静态初始化期创建 MessageManager 与 JUCE SingletonHolder 类级静态锁竞争 → 移入 main()。另有：Catch2 重名用例、MultiFilter scratch 越界、JUCE 8 实 FFT 2× 缓冲（5 处）、LimiterEffect wetBuffer/延迟线、测试空指针 ×2。

---

## 4. CI 工作流（.github/workflows/，Windows runner）

步骤序：checkout → 清陈旧 CMakeCache → 缓存 build 目录（key 含 hashFiles('CMakeLists.txt')，**改 CMakeLists 必失效**）→ 缓存 _deps（FetchContent，key 同上）→ Configure（CMake 4.4 + VS 18 2026，runner 镜像 8 月已升级）→ Build（/Zi+DEBUG，含 VST3/CLAP PDB）→ **Test**（continue-on-error: true；ctest 300s + 直接运行 AnaPlugTests 生成 test-results.xml；WER LocalDumps 注册表 + cdb 收集 .dmp）→ 上传 VST3/CLAP → 依赖 dump → **pluginval --verbose --validate-in-process --strictness-level 1**（/Zi build；job 失败与否由此步决定）→ 上传 test-forensics。

- **CMakeLists 要点**：`BUILD_SHARED_LIBS OFF CACHE FORCE` 已移除（波及面过大）；libebur128 以 `add_library(ebur128 STATIC …)` 方式在 FetchContent_MakeAvailable **之前**注册（顺序关键——MakeAvailable 之后的 declare 会被 CMake 4 拒绝："no content details recorded"）。
- JUCE 由 FetchContent 拉取（juce-test-clone/ 仅作源码参考，不参与构建）。
- 测试超时已 30s→300s；benchmark（bench_performance.cpp）仍在默认运行中（可选优化：`[benchmark]` tag 分离，`~[benchmark]` 过滤）。

---

## 5. 遗留失败全景（当前 58 = 46 断言 + 12 日志级）

> 来源：Run #87 (b63b560) 取证产物 `test-results.xml` + forensics 日志。536 用例 → 478 过。
> 注意：MultiFilter LP/frequency-response 已由 517621c（ProcessorDuplicator）修复，**待 Run #88 确认**（见 §0.1）。
> **test_arpeggiator.cpp 的 advanceSteps 时序修复已随 2a34afb 提交**（首步 t=0 触发，之后每 5513 样本过一个边界——原测试一次灌 steps*3×5512 样本跑过门限窗口导致 getCurrentNote()==-1），待 Run #89 确认。

### A. 断言失败（46 条，按模块分组；格式：测试名 / 文件:行 / 实际 vs 期望 / 策略）

**Arpeggiator（6 用例 18 断言）** `tests/test_arpeggiator.cpp`
- Up/Down/UpDown/AsPlayed/octave/swing：`getCurrentNote() == N => -1`。→ **已修**（未提交）：advanceSteps 只推进到目标步且保持门限窗口内。提交前自查 gate 时长 vs 步长。

**Bitcrusher** `tests/test_new_effects.cpp:279`
- `distinct <= 17 => 33`：bitDepth=8 时量化级数 33>17。策略：检查量化是否 float 精度泄漏（quantize 公式 `floor(x*levels)/levels` 的 levels 计算，8-bit 应 256 级但有效正弦幅值内 ~17；33 提示 half-wave 或 off-by-one）。
- `rmsDown8 != rmsNoDown margin 0.5 => Δ0.019`：downsample 应显著改变 RMS 而没变？读测试意图再对齐（可能 setter 单位 bug：downsample 设 8 实际生效 1）。

**Preset round-trip 家族（4 用例）** `test_effect_presets.cpp:470,644` + Clean/Creative Rack
- `loadPresetFromFile(presetFile) => false`。保存成功但加载失败。策略：读 PresetManager::loadPresetFromFile 对 `*.anaplug` 的解析路径 + savePresetToFile 写出的 XML 根标签/版本号是否一致；用取证日志中该用例的输出尾迹定位。**首要嫌疑**：save 写的是 ValueTree 序列化而 load 期望带版本头（或反之）。

**DeEsser** `test_vocal_effects.cpp:86` — `reduction > 6.0f => -0.889`（处理后反而更响）。策略：检查 de-esser 频段检测（6kHz 正弦）与增益计算单位（dB vs linear），以及 before/after RMS 的度量窗口。

**DriveModule preset names[6]** `test_consolidated_effects.cpp:90`
- 实际工厂序（PresetFactory.cpp:1211-1217）：0 Warm Drive,1 Edge Of Breakup,2 Tube Scream,3 British Plexi,4 Tape Saturate,5 Console Drive,6 Hard Clipper,7 Fuzz Face,…,13 Bell Tone。测试期望 [6]="Console Drive" → **改测试为 names[5]**（工厂序正确且与 UI 分组注释一致）。

**DynamicsModule sidechain gate** `test_sidechain.cpp:161` — `firstHalfRms > secondHalfRms*5 => 0.00068 > 0.00172`（门限行为反了）。策略：读测试信号构造（sidechain 前半开/后半关？）与 DynamicsModule 门限包络方向。

**Edge cases: single individual** `test_spectral_dna.cpp:135` — `getPopulationSize()==1 => 2`。init(1) 后 evolveGeneration 把种群变 2。策略：SpectralDNA.cpp:592 evolveGeneration 的精英保留+繁殖逻辑在 popSize==1 时应不扩容（跳过 crossover/offspring 或钳制 population_.resize(initSize)）。

**Effect state serialization - AutoTuneEffect** `test_effect_presets.cpp:412` — set 25 → save/load → 20。策略：AutoTuneEffect::getState/setState 检查 "retuneSpeed" 属性写入（可能写成了默认 20 或 clamp 到 [0,20]——setRetuneSpeed 的 clamp 范围若是 [0,20] 则 25 进去就变 20；对齐测试用 20 或放宽 clamp——以实现 clamp 语义为准改测试值）。

**ENV pool isolation** `test_modulation.cpp:738` — envPool[1] 未被 envPool[0] 的触发波及（期望隔离）但实际 envPool[1].isActive() false 是**期望**……实际断言 `envPool[1].getValue() > 0.5 => 0.037` 与 `vals[0] == 0.7 => 0`。策略：细读测试与 runEnvPool 索引映射（d3825c7 改过 nullptr 分支）——vals[0] 全 0 提示 envPool[0] 输出没进 vals。

**GranularSynthesizer** `test_granular_synthesis.cpp:273,375`
- 空 source 应静音，实际 energy 1.56（读了未初始化内存）。策略：process 前检查 source frames empty → clear 输出。
- `total > 500 => 256`：测试期望 maxGrains=500 可达，实现钳到 256。策略：把内部上限常量提到 ≥512 或对齐测试。

**LFOSystem - tempo sync** `test_lfo_system.cpp:272` — 期望相位 0 处输出 1.0（三角/锯齿？），得 0.00028。策略：读测试（sync 模式+特定 BPM 推进 N 样本后 val）与 LFOSystem::process 的相位→波形映射（相位 wrap 边界 off-by-one）。

**LFOSystem + ModulationBus** `test_wired_modules.cpp:424` — `target == 1000 => 0`。策略：读测试接线（LFO→bus→target ptr）与 ModulationBus::apply 路径；类似 runEnvPool 的 nullptr/映射 bug 家族。

**Limiter ceiling** `test_wired_modules.cpp:272` — `eProc ≈ eOrig (10%) => 536 vs 644`（能量损失 17%）。策略：LimiterEffect 的增益平滑/释放时间过激进；或测试含瞬态被真限幅——先读测试阈值意图再决定改实现或阈值。

**MeteringEngine LUFS** `test_cutting_edge_features.cpp:276` — `-0.99 vs -4 ±1`。K-weighting 或门限（gating）实现与测试参考模型不一致。策略：读测试的正弦参考计算（-4 LU 假设绝对门限?），对齐 MeteringEngine 的 gating（绝对门限 -70 LUFS / 相对 -10 LU）与 K 滤波器系数。
**MeteringEngine true peak** `test_cutting_edge_features.cpp:381` — `tpL == -1.0 dB => 0.891`（线性值!）。**单位 bug**：TP 返回线性 0.891（≈ -1.0 dBFS 的 4×过采样峰值恰 1.1×？——0.891 线性 = -1.0 dB，说明实现把 dB 值当线性返回或反之）。策略：统一 dB<->linear（实现已对，只是返回前没做 20*log10 —— 或测试没做转换；选实现返回 dB 与 LUFS 家族一致）。

**MPE pitch bend ×2** `test_cutting_edge_features.cpp:64,143` — `voiceIdx >= 0 => -1`：pitch bend 未分配/找到 voice。策略：读 VoiceManager/MPE 的 per-channel bend 路径；noteOn 后查 voiceIdx 的接口语义（按 channel 查询？）。

**MultiPointEnvelope ADSR shape** `test_wired_modules.cpp:497` — sustain 0.43 vs 0.65（10ms 采样点）；vSustain 0.12 vs 0.5。策略：a2651f7 改过"段曲线取段末断点"——ADSR 段的 curve 现在取自 sustain 断点而非 attack 断点？细读 built-in ADSR 构造（curve 应属于**段起点**还是终点）与测试期望对齐；这可能是实现契约与测试再次错位，需决断（建议：曲线属于段起点断点，改回并同步改 a2651f7 的测试对齐）。
**MultiPointEnvelope loop modes** `test_multi_point_envelope.cpp:328` — `!env.isActive() => !true`（循环模式结束后应停止但一直活跃）+ 循环点值 0 vs 1.0。策略：loop 结束条件与 isActive 定义对齐。

**ResynthEngine empty ×2** `test_security_fixes.cpp:84` + `test_resynthesis.cpp:5` — `result.empty() => false`。**已定位**：ResynthesisEngine.cpp:25 `(numFrames - 1) * hop + fft` 在 numFrames=0 时 size_t 下溢 → 非空垃圾长度。策略：`if (frames.empty()) return {};`（ResynthEngine 与 ResynthesisEngine 两处）。

**Ring Modulator** `test_new_effects.cpp:365` — `mag440 < mag880 => 63 < 1.08`（反了）。策略：读测试（880Hz 载波 × 440Hz 正弦？边带在 440/1320）与 RingModulator 实现频率/调制深度。

**SampleProcessor detectPitch** `test_sample_processor.cpp:56` — 484.6 vs 440±5。自相关/FFT 峰值插值精度。策略：加抛物线插值或对齐允许误差。
**detectRootNote** — 71 vs 69：音高直方图众数。策略：随 detectPitch 修好大概率自愈；否则直方图 bin 中心偏移。

**Source switching during playback** `test_modulation.cpp:264` — `result2 == 1000.5 => 999.5`。切换 source 后首帧仍读旧值（off-by-one/缓存）。策略：switch 时 invalidate 缓存或读帧前先取新 source。

**SpectralMorpher::morphMulti phase** `test_spectral_morpher.cpp:237` — 期望线性加权和（如 7.15），实际 wrap 到 ±π（±1.x）。**测试写错**（相位必须 wrap；线性求和越过 ±π 无意义）。策略：改测试 expectedPhase 用 `std::atan2(sin(s),cos(s))` 或 std::fmod wrap 后再比。
**morphWeighted** `test_spectral_morpher.cpp:181` — `distHighB < distHighA => 219 < 81` 反。a2651f7 对齐过 unit-amp 期望；语义：高幅值 partial 应 morph 更快（权重大）。读实现权重公式方向，反了就调。

**UndoManager leak** `test_security_fixes.cpp:599` — `value == 0 => 40`（析构计数泄漏 40）。策略：composite/嵌套命令未释放子步；对照 test_undo_manager.cpp:274 的 nested composite（`getNumUndoSteps()==1 => 2`）——两 bug 同根：嵌套 execute 没折叠进当前 composite 且其子命令泄漏。修 UndoManager::execute 的 inComposite 分支。

**VocalNoiseReducer** `test_vocal_effects.cpp:236` — `beforeRms-afterRms > 0.5 => 0.22`。降噪量不足测试阈。策略：读测试噪声源与阈值；调 impl 频谱减法深度或对齐阈值。

**VoiceManager ×4** `test_voice_manager.cpp:247,112,558,806`
- attack phase：`v->state == decay => {?}==2`（attack 计时没推进到 decay）。**已定位嫌疑**：process-before-prepare 用 sampleRate=0（默认 48000?）——检查 mSampleRate 默认值与 test:806 的 `hasAudio => false` 同根：默认采样率路径 osc/envelope 没跑。策略：VoiceManager 成员默认 sampleRate = 48000 且 process 未 prepare 时用它；确认 attack 计数用样本数。
- noteOff release：`releaseStartLevel > 0 => 0`。noteOff 时没记录当前电平。策略：noteOff 存 envelope.getValue()。
- oldest-first：`getVoice(0)->note == 72 => 60`。抢占策略：填满后再 noteOn，oldest-first 模式 voice[0] 应持最新（或测试意图相反）——读实现 allocationOrder 枚举与测试。

**Volume ADSR independent** `test_modulation.cpp:397` — `finalVal < 0.01 => 0.031`。release 尾巴没落到底：release 时间计算（样本数=seconds*sr）或曲线指数。策略：检查 volume ADSR release 用的采样率。

**Wet filter HPF** `test_new_effects.cpp:501` — `before-after > 20 => 12.3`（dB 阻带衰减不足）。一阶 HPF 在 20× 频率处应 ~-26dB？检查滤波器阶数/Q（若测试假设 12dB/oct 而实现 6dB/oct → 改实现为 2×cascade 或调测试）。

### B. 日志级失败（12 条，无 Expression 节点，从 forensics 日志 forensics.txt 的 CRASH-OR-FAIL 块取详情）

CyberpunkTheme static paint helpers（headless 下 paint 崩？）/ SpectrumDisplay paint headless / Fitness is in [0,1]（bracket 解析）/ Missing Effects section / Old preset backward compat / Population diversity / PrismEffect finite output / Stereo Widener effect / Unknown module preset name / Uniform crossover / （另有 1-2 条以 forensics.txt 为准）。
> 处理顺序：先跑一轮 CI 拿 forensics.txt 对应块（每个失败带输出尾迹），再逐条定案。注意 "Unknown module preset name" 可能只是 PresetFactory 查询接口对未知模块应返回 invalid——实现已有（test_effect_presets 侧），需看尾迹。

---

## 6. 取证工作流（怎么在一轮 CI 里看清一切）

1. 改代码 → commit → push（§2.4）→ 记下 run_id。
2. ~20 分钟后拉 `runs?head_sha=<sha>` 得 run_id → jobs → job_id → 日志。日志尾部有 `forensics summary` 块：`CRASH-OR-FAIL :: <测试名>` 列表（测试名从源码 TEST_CASE 提取，非 XML——防崩溃截断）。
3. 精确断言值：下载 `test-forensics` artifact（§2.3），`test-results.xml` 里 `<Expression success="false"><Original>/<Expanded>`；无 Expression 的失败看 `forensics.txt` 的输出尾迹段。
4. 崩溃（进程级）：`test-stderr.log` 最后一个 crumb（ANA_CRUMB 插桩：构造函数各阶段 stderr 面包屑，宏可关）+ WER dump + cdb 输出。
5. XML 解析模板（PowerShell）：
   ```powershell
   $x=[xml](Get-Content test-results.xml -Raw)
   foreach($tc in $x.SelectNodes("//TestCase")){ if($tc.SelectSingleNode("OverallResult").success -eq 'false'){ $tc.SelectNodes(".//Expression") | ? { $_.GetAttribute('success') -eq 'false' } | % { "{0}: {1} => {2}" -f $tc.name, $_.SelectSingleNode('Original').'#text'.Trim(), $_.SelectSingleNode('Expanded').'#text'.Trim() } } }
   ```

---

## 7. 执行计划（剩余工作，按批次）

**Batch 1（最小确定性修复，先清场）— ✅ 代码完成，状态见 §0.3**：ResynthesisEngine 空数据早退 ×2、SpectralDNA pop-1 不扩容、DriveModule 测试索引 [6]→[5]、AutoTuneEffect 测试值对齐 clamp——**以上 4 项已编辑进工作区但未提交**；test_arpeggiator 时序修复已随 2a34afb 推送。→ 接手动作：审查 §0.3 后一并提交推 CI（连同已推的 517621c 验证 MultiFilter）。

**Batch 2（状态机/时序族）— 🔶 侦察完成，修复未写**：VoiceManager ×4 的根因与修法已核实（**直接照 §0.4 写**）+ Volume ADSR + MultiPointEnvelope（ADSR curve 归属决断 + loop isActive）+ LFO tempo sync（线索见 §0.5）。

**Batch 3（路由/接线族）— ⬜ 未开工**：ENV pool vals 映射 + LFO+ModBus + MPE voiceIdx + Source switching。

**Batch 4（DSP 精度族）— ⬜ 未开工**：Bitcrusher ×2 + DeEsser + DynamicsModule gate + Limiter energy + MeteringEngine ×2（单位！）+ RingMod + SampleProcessor ×2 + VocalNoiseReducer + Wet HPF + Granular ×2 + SpectralMorpher weighted + UndoManager ×2。

**Batch 5（round-trip 族）— ⬜ 未开工**：Preset round-trip ×4（loadPresetFromFile false——一条根因概率高）。

**Batch 6（日志级 12 条）— ⬜ 未开工**：先取 forensics.txt 尾迹再逐条（本地已有 $env:TEMP\forensics87\forensics.txt）。

**Batch 7（收尾）— ⬜ 未开工**：benchmark 用 `[benchmark]` tag 分离默认运行；pluginval strictness 1→5 渐进；pluginval 全绿后**重启 UI 重构（Phase 3，见 §9）**；更新本文档。

每批一轮 CI。全绿标准：**536 用例 0 失败 + pluginval 绿**。

---

## 8. 安装验证（用户侧）

产物：`artifacts/84/`（Run #84，全绿）或最新 run 的 `AnaPlug-windows-latest` artifact。
- VST3：`AnaPlug_artefacts/Release/VST3/AnaPlug.vst3` → `C:\Program Files\Common Files\VST3\`
- CLAP：`AnaPlug_artefacts/Release/CLAP/AnaPlug.clap` → REAPER CLAP 目录（`C:\Program Files\Common Files\CLAP\` 或 REAPER 提示的路径）
- REAPER 中加载，先建轨道再插入；如仍异常抓 WER dump 反馈。

---

## 9. UI 重构计划（Phase 3，等测试全绿后启动）

目标：**外观零变化**的结构拆分 + 补齐主题缺口。
1. `PluginEditor.cpp`（1623 行/90+ 组件）拆分为 `src/gui/panels/`：TimbrePanel(A/B 参数化)、FilterPanel、MacroPanel、SequencerPanel、TransportBar、MasterSection、EffectRack 已独立。布局逻辑（computeRegions 百分比）原样搬移。
2. MacroKnob/StepCell 嵌套类外提为文件。
3. 补主题：WaveformDisplay（cyan/darkgrey/red → CyberpunkTheme 紫系，~15/26/52 行）；ModulationMatrixPanel 接入主题（0xff1a1a1a/white/grey → theme）。
4. 重新启用 test_ui_paint / test_ui_colors（需 clap-juce-extensions include 路径加入测试目标）。
5. 不做 APVTS 迁移（用户已决策：本轮不做）。
6. UI 断言测试作为重构护栏：改前确认 test_ui_*.cpp 在 CI 的现状。

---

## 10. 给下一个 AI 的守则

- 用户已于 2026-08-29 指示**暂停修复、交接**——进度冻结点：Batch 1 代码在工作区未提交（§0.3），Batch 2 根因已侦察（§0.4），其余未开工。
- 别再"循环纠正"测试期望 vs 实现——每次先取证（§6），断言值对不上就找语义归属（实现契约 or 测试过时），在 commit message 里写明决断理由。
- 改动一批 → 推一轮 → 取证 → 再改。禁止凭空猜测式大改。
- 会话中断无损失：全部状态在 git + 本文档 + `$env:TEMP\forensics87\` + 本地工作区。
- PAT 永远来自用户会话，不落盘；HANDOFF.md 里那个旧 token 已失效且应删除。
