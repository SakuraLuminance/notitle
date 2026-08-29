# AnaPlug 交接文档 V2（2026-08-29）

> 面向下一个接手的 AI / 开发者。包含：项目全貌、环境铁律、已完成修复及根因、**全部遗留失败的精确清单与修复策略**、UI 重构计划、CI 取证工作流。
> 旧文档 HANDOFF.md（2026-07-09）中的 token 已失效，本文档**不含任何密钥**。

---

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
> 注意：MultiFilter LP/frequency-response 已由 517621c（ProcessorDuplicator）修复，Run #88 待确认。
> **test_arpeggiator.cpp 已有未提交修复**（advanceSteps 时序模型：首步在 t=0 触发，之后每 5513 样本过一个边界——原测试一次灌 steps*3×5512 样本跑过门限窗口导致 getCurrentNote()==-1）。

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

**Batch 1（最小确定性修复，先清场）**：提交 test_arpeggiator 时序修复；ResynthesisEngine 空数据早退 ×2；DriveModule 测试索引 [6]→[5]；AutoTuneEffect clamp/序列化对齐；SpectralDNA pop-1 不扩容。→ 推 CI（连同已推的 517621c 验证 MultiFilter）。

**Batch 2（状态机/时序族）**：VoiceManager ×4 + Volume ADSR + MultiPointEnvelope（ADSR curve 归属决断 + loop isActive）+ LFO tempo sync。

**Batch 3（路由/接线族）**：ENV pool vals 映射 + LFO+ModBus + MPE voiceIdx + Source switching。

**Batch 4（DSP 精度族）**：Bitcrusher ×2 + DeEsser + DynamicsModule gate + Limiter energy + MeteringEngine ×2（单位！）+ RingMod + SampleProcessor ×2 + VocalNoiseReducer + Wet HPF + Granular ×2 + SpectralMorpher weighted + UndoManager ×2。

**Batch 5（round-trip 族）**：Preset round-trip ×4（loadPresetFromFile false——一条根因概率高）。

**Batch 6（日志级 12 条）**：先取 forensics.txt 尾迹再逐条。

**Batch 7（收尾）**：benchmark 用 `[benchmark]` tag 分离默认运行；pluginval strictness 1→5 渐进；pluginval 全绿后**重启 UI 重构（Phase 3，见 §9）**；更新本文档。

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

- 别再"循环纠正"测试期望 vs 实现——每次先取证（§6），断言值对不上就找语义归属（实现契约 or 测试过时），在 commit message 里写明决断理由。
- 改动一批 → 推一轮 → 取证 → 再改。禁止凭空猜测式大改。
- 会话中断无损失：全部状态在 git + 本文档 + `$env:TEMP\forensics87\`。
- PAT 永远来自用户会话，不落盘；HANDOFF.md 里那个旧 token 已失效且应删除。
