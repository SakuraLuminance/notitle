# AnaPlug 交接文档 V3 — 2026-09-17

> 本文是**当前唯一权威交接**。V2（`HANDOFF_V2.md`）保留作为**历史故障取证库**：58 次 CI 失败逐条分析、cdb/pluginval/WER 取证流程、成品安装脚本。需要查"某个红过的 Run 是怎么挂的"再去翻 V2。

---

## 0. 30 秒快照

| 项 | 值 |
|---|---|
| 仓库 | `SakuraLuminance/notitle`（public），`main` 直推 |
| 目标 | Windows x64 合成器插件：**VST3 + CLAP** |
| 技术栈 | JUCE **8.0.13**（FetchContent，`GIT_SHALLOW`）、clap-juce-extensions（`main`）、Catch2 v3.5.2、**C++17**、MSVC `/MT` |
| 构建目标名 | 插件 `AnaPlug`，测试 `AnaPlugTests` |
| 本地 HEAD | `ba80393`（**15 个提交未推送**），`origin/main` = `26d18ae` |
| 最近已推送 CI | Run `#35234761589`（`26d18ae`）：Build / Test / pluginval 步骤均 success；**用例计数当时没读到**（GitHub 下载通道故障） |
| 最近读到计数的全绿 | Run `#35216572487`：**583 用例全过 + pluginval strictness 5** |
| 未推送批次预期 | **595+ 用例**全过、strictness 5 |
| 路线图 | **P1–P6 全部完成**，额外完成 **P6b（时变谐波图像）** 与 **多主题视觉系统** |
| 环境 | **本机没有任何编译工具链**（无 cmake/msbuild/cl/ninja）——所有验证只能推 GitHub Actions |

---

## 1. 环境铁律（违反必炸）

| 规则 | 说明 |
|---|---|
| 唯一验证手段 | `git push origin main` → GitHub Actions（约 12–20 分钟/轮）。**禁止**声称"本地编译/测试通过" |
| 推送 | `git push origin main`（Windows 凭据管理器已缓存 PAT，无需手输） |
| 取 CI 状态 | `GET /repos/SakuraLuminance/notitle/actions/runs?branch=main&per_page=1` |
| 取 CI 日志 | `GET /actions/runs/{id}/jobs` 拿 job id → `GET /actions/jobs/{job_id}/logs`（需 `Authorization: token <PAT>`） |
| 取 PAT（不落盘） | `$cred = "protocol=https`nhost=github.com`n" \| git credential fill`，取 `password=` 字段，仅用于请求头 |
| **最大陷阱** | `.github/workflows/cmake.yml` 的 `Test` 步骤带 **`continue-on-error: true`** —— **测试失败 run 仍然是绿的**。每轮必须从日志里读：① `All tests passed (… in N test cases)` ② `Strictness level: N` ③ 没有 `CRASH-OR-FAIL ::` 条目 |
| 日志下载故障 | 若 `/logs` 或 artifact `zip` 下载失败（本机出现过整段时间的下载通道故障），可等网络恢复后重试，或改读 `test-forensics-*` artifact 里的 `forensics.txt` |
| 批次纪律 | **改一批 → 推一轮 → 取证 → 再改**。失败先拉日志定位，禁止凭空大改 |
| 成品安装 | 需要 UAC，用 V2 里的 EncodedCommand + RunAs 脚本；artifact 名 `AnaPlug-windows-latest` |

---

## 2. 项目是什么

一句话：**把采样分析成"谐波图像"，再用加法合成/重合成去雕塑它**——采样 → STFT 分析 → 逐帧谐波集 →（加法合成 SYNTH 模式 或 重合成回放）→ 20 个效果器 → 主输出；配调制矩阵、包络画布、DNA 进化、频谱冻结、粒子视图。

| 目录 | 作用 |
|---|---|
| `src/PluginProcessor.{h,cpp}` | 处理器：参数原子、调制、效果链、preset、状态持久化、**音频主链路** |
| `src/PluginEditor.{h,cpp}` | 编辑器：顶栏、频谱视图区（8 种视图）、页签（8 页）、timer 同步 |
| `src/dsp/**` | 209 个文件：分析（`STFTAnalyzer`/`PartialTracker`/`PhasePropagation`/`ResynthesisEngine`）、合成（`AdditiveSynth`/`VoiceManager`/`WavetableEngine`/`GranularSynthesizer`）、频谱效果（`PrismEffect`/`BlurEffect`/`Harmonizer`/`TimbreShaper`/`DualTimbre`/`SpectralFreezeEngine`/`PartialModulator`）、调制（`MultiPointEnvelope`/`LFOSystem`/`StepSequencer`/`ModulationEngine`/`MacroController`/`MidiLearn`）、效果器（`EffectsChain` + 20 个 `effects/*`）、工具（`PresetManager`/`PartialDataSIMD`/`SIMDSupport`） |
| `src/gui/**` | 59 个文件：`CyberpunkTheme`（LookAndFeel + 设计 token）、`ThemePalettes`（7 套主题）、各种画布与面板 |
| `src/gui/panels/**` | 页签内面板：`TimbrePanel`/`FilterPanel`/`MacroPanel`/`SequencerPanel`/`TransportBar`/`MasterSection`/`PageTabs` |
| `tests/**` | 74 个 `test_*.cpp` + `bench_performance.cpp`（Catch2） |
| `docs/superpowers/{specs,plans}/**` | 每个里程碑的 spec 与实现计划（**新工作必须先写 spec**） |
| `.github/workflows/cmake.yml` | 唯一 CI |

---

## 3. 音频架构与线程模型

```
loadFile
  └─ AnaPlugEngine::analyze()
       ├─ PartialTracker::trackPartials()   → PartialData.frames（每帧一串 partial：freq/amp/phase）
       ├─ PhasePropagation::propagatePhases()
       ├─ ResynthesisEngine::resynthesize() → resynthBuffer_（采样播放用，双重锁发布）
       └─ buildImageFramesFromPartialData() → imageFrames_（≤32 帧，P6b 图像）
                                              └─ applyTimbreProcessing() → AdditiveSynth::setFrames()

processBlock 顺序（简化）：
  MIDI/MPE → VoiceManager 渲染 voiceBuffer
           → AdditiveSynth 渲染（仅 SYNTH 模式）
           → effectsChain（20 个，bypass/mix=0 会跳过）
           → vocalProcessor → multiFilter
           → processSpectralFreeze（post-effects / pre-master；空闲时只 recordOnly，零拷贝）
           → 采样播放写入 buffer（或 voiceBuffer 拷贝）
           → master vol/pan → volumeAdsr（VCA）→ LUFS metering → scope 捕获
```

**线程与锁**

| 对象 | 机制 |
|---|---|
| `resynthLock_`（Processor） | `SpinLock` + 音频线程 `ScopedTryLockType`（拿不到就用上一份） |
| `envLock_`（Processor） | `SpinLock` 保护包络断点编辑 vs 音频线程推进/触发；`PresetManager::setEnvLockRef(&envLock_)` 也持锁 |
| `AdditiveSynth::partialLock_` + `publishedGeneration_` | 消息线程写 `published_` 并 `generation++`；音频线程仅在 generation 变化时 try-lock 拷贝到 `active_` |
| `SpectralFreezeEngine` | **只允许音频线程触碰**；处理器用 atomics（`freezeEngineRequested_`/`freezeTriggerRequested_`/`freezeMode_`/`freezeMix_`）延迟应用 |
| GUI | 只读原子 / 只通过处理器 API 改状态；跨线程共享的只有 `std::atomic` |

**实时安全**：`processBlock` 内**无堆分配**；AdditiveSynth 的 bank 拷贝与 freeze 的 ring/filter 分配都已在 `prepareToPlay` 预热。

**离线性能**：`PartialTracker` 预留帧数组、`ResynthesisEngine` 复用频谱缓冲（长采样载入时间优化）。

---

## 4. GUI 架构

**顶栏**：标题 · `SYNTH`（加法合成开关）· `IMPORT` · **主题下拉** · `PRESET` · `DNA EVOLVE`（跳 EVO 页）· `FLATTEN` · `CREDITS`

**频谱区（`viewModeCombo_`，8 种视图）**：`LIVE`（实时频谱）· `PARTIALS`（经典柱）· `WATERFALL`（3D 瀑布）· `EDITOR`（频谱编辑）· `3D` · `SCOPE`（示波器）· `PARTICLES`（粒子）· `IMAGE`（时间×谐波图像画布，带 UNDO/REDO/CLEAR/NORM/SMOOTH + 悬停读数）

**页签（`PageTabs`，8 页）**：`TIMBRE` · `FILTER` · `MOD` · `SEQ` · `FX` · `MASTER` · `ENV`（包络画布）· `EVO`（DNA 进化面板）

**主题系统**

- `src/gui/ThemePalettes.h`：`ThemePalette{ name, bg, fg, accent, accent2, highlight, monoBody }` + 7 套调色板（NEON/VAPOR/CRT/ICE/MONO/TOXIC/SAKURA）
- `CyberpunkTheme`：`applyPalette(i)` 写入 5 个历史颜色槽（`bg_/fg_/cyan_/magenta_/yellow_` = bg/fg/accent/accent2/highlight）→ **既有面板代码零改动**；`monoBody` 让正文也用等宽；`remapComponentColours(root, oldPalette)` 递归改写"构造时抓取的颜色"，实现**不重建 UI 的实时换肤**
- 持久化：`PluginProcessor::setThemeIndex/getThemeIndex`（随状态存取）
- **加主题只需在 `ThemePalettes.h` 追加一行**（`themeCombo_` 自动列出）

**设计 token（`CyberpunkTheme`）**：`kControlHeight=20`、`kSliderWidth=132`、`kReadoutWidth=48`、`kReadoutFontH=10`、`kCanvasBgDarken=0.25`、`kCanvasBorderAlpha=0.20`、`kCanvasGridAlpha=0.08`；`formatPercent/formatNumber/styleReadout`。

**颜色语义（所有主题共享）**：`accent`=可交互 / `accent2`=次强调（HOLD、选中） / `highlight`=数值（包络点、图像热力、粒子） / `fg_`=文本（读数用 72% 不抢强调色）。

**性能**：editor timer（30Hz）按 `activePage_` 与当前视图门控同步；scope/live 频谱在不可见时跳过取数与 FFT；`EnvelopeCanvas` 复用 `Path`、`TimbreShaper` 32 帧塑形复用 scratch。

---

## 5. 功能与接线清单

| 功能 | 实现 | UI 入口 | 状态 |
|---|---|---|---|
| 实时加法合成 | `AdditiveSynth`（MPESynthesiser，≤16 voices × ≤128 partials，递归 phasor） | 顶栏 `SYNTH` | ✓ |
| 时变谐波图像 | `AdditiveFrame`/`AdditiveBank`（≤32 帧，floor/ceil 插值） | TIMBRE 页 `IMAGE / RATE / LOOP / FRAME` | ✓ |
| 频谱编辑 | `SpectrumEditorCanvas` | 视图 `EDITOR`/`3D` | ✓ |
| 图像整体编辑 | `PartialEditorCanvas`（时间×谐波笔刷 + 撤销） | 视图 `IMAGE` | ✓ |
| Timbre A/B | `TimbreShaper`（bright/HPF/blur）+ `DualTimbre` blend | TIMBRE 页 A/B 面板 + BLEND | ✓ |
| 生成式音色 | `GenerativeTimbreDesigner`（latent→谐波集） | TIMBRE 页 `GEN/RND/CAP` + 8 预设 + mix | ✓ |
| 频谱冻结 | `SpectralFreezeEngine`（4 模式，快照循环播放） | FX 页 `FREEZE/TRIG/MODE/MIX` | ✓ |
| 粒子视图 | `SpectralParticleSystem` | 视图 `PARTICLES` | ✓（消息线程驱动物理） |
| DNA 进化 | `SpectralDNA`/`SpectralDNAEvolver` | 页签 `EVO`（常驻面板） | ✓ |
| 包络画布 | `MultiPointEnvelope` + `EnvelopeEditOps` | 页签 `ENV`（VOL/ENV1/2/3，断点/曲线/loop/sync） | ✓ |
| 调制/宏/音序 | `ModulationEngine`（16 槽）、`MacroController`、`StepSequencer` | MOD/SEQ 页 | ✓ |
| 预设 | `PresetManager`（含断点/loop/sync/主题/图像参数持久化） | 顶栏 `PRESET` | ✓ |
| `HPSSEngine` | 离线谐波/打击分离 | — | **有意不接线**（离线分配、无实时价值、按"不接线不进 UI"原则） |
| `QuantumSpectralProcessor` | 评估后保持零引用 | — | 未接线 |
| **rack 内 MIDI Learn** | `MidiLearn` 目前只支持 `std::atomic<float>*` 目标 | — | **未做**（设计见 §6.2） |
| **枚举参数真菜单** | 范围未确认 | — | 未做 |

---

## 6. 下一步路线

### 6.1 第一件事：推送并取证（必须）

```powershell
git push origin main
# 等 12–20 分钟，然后读日志确认：
#   ① All tests passed (… in N test cases)   ← 预期 595+
#   ② Strictness level: 5
#   ③ 无 CRASH-OR-FAIL :: 条目
```
未推送批次含：P6b 时变图像、逐帧编辑、IMAGE 画布、EVO 页、一轮性能优化与 5 个 bug 修复、多主题系统、约 12 个新测试与 1 个基准。若编译/测试红了，日志会直接指到文件行。

### 6.2 rack 内 MIDI Learn（设计已定，可直接实现）

1. `src/dsp/MidiLearn.h`：`struct MidiMapping` 增加 `std::function<void(float)> targetSetter`（`targetParam == nullptr` 时使用；已存在原子目标优先，保证既有路径行为不变）
2. `MidiLearn::addMapping/startLearn/reconnectTarget` 各加一个带 setter 的重载；`processMidi` 里所有写值处统一走一个 `applyMappingValue(mapping, v)` 私有函数
3. `EffectParamPanel`：暴露 `EffectBase* getEffect()` 与 `visitKnobs(fn)`（旋钮 → 参数下标）
4. `EffectSlotWidget`：暴露 `EffectParamPanel* getParamPanel()`
5. `EffectRackComponent`：暴露 `visitSlots(fn)`
6. `PluginEditor`：timer 里按槽位轮询注册 `setupMidiLearnForEffectKnob(knob, "fx{slot}_p{idx}", setter, getter)`
7. 测试：`test_ui_colors` 同级加一个 headless 单测覆盖 `MidiMapping` 的回调目标分支

### 6.3 其他候选（按价值）

| 项 | 说明 |
|---|---|
| 枚举参数真菜单 | 需先与用户确认范围（把 filter type / effect type 等枚举做成宿主可自动化的 choice 参数或 UI 菜单） |
| 颗粒 UI | `GranularSynthesizer` 已修好采样精确调度，但零 UI；可做 grain 密度/长度面板 |
| DNA fittest → 当前音色 | `loadSampleAsParent()` 已在 Processor 里，缺一个"用 fittest 当 timbre"的按钮（$6.2 之后） |
| P6 遗留 | 图像跨帧索引目前是"频率升序近似对应"，可升级为谐波分箱；帧插值可加曲线 |
| 成品安装 | UAC 脚本见 V2；artifact 名 `AnaPlug-windows-latest` |

---

## 7. 设计方针（下一个 AI 必须遵守）

**视觉**
- 只用主题槽位色（`CyberpunkTheme::bg_/fg_/cyan_/magenta_/yellow_`）与 token；**不要硬编码颜色**（自定义语义色如 `Colours::red` 仅用于告警）
- 颜色语义：accent=可交互、accent2=次强调、highlight=数值、fg=文本；读数用 `styleReadout`
- 每个连续控件旁边必须有数值读数（`formatPercent/formatNumber`）
- 行高统一 `kControlHeight`（20）；画布背景/边框/网格用 token
- 字体只用 `getCyberFont(height, bold)`（**不要用 `juce::Font(size, style)`**，JUCE 8 已弃用）；空状态文案全大写
- 新画布：背景 `bg_.darker(kCanvasBgDarken)`、边框 `fg_.withAlpha(kCanvasBorderAlpha)`、网格 `kCanvasGridAlpha`

**交互**
- **不接线就不出现在 UI**（避免死控件）；每个 UI 控件必须落到处理器 API
- 每个控件都要 tooltip；危险/高级操作放右键菜单
- 状态显示统一走顶栏/面板状态标签，大写字距风格

**代码**
- **被测试目标编译的文件严禁 `#include "PluginProcessor.h"` / `"PluginEditor.h"`**（clap 陷阱）；需要处理器时用"处理器侧 API + 原子"或把组件放到插件目标
- 新 `.cpp` 必须**同时**加入 `CMakeLists.txt`（插件）与 `tests/CMakeLists.txt`（测试）
- 小型工具优先 header-only（如 `ThemePalettes.h`）；不引入新依赖；保持 C++17
- 线程边界：GUI 只读原子；跨线程数据用"消息线程写 + 音频线程 try-lock 拷贝"模式；锁不可重入（回调/通知前先放锁）
- 实时路径禁止分配；新缓冲区在 `prepareToPlay` 分配/预热

**流程**
- 新功能：`brainstorming`（问清需求）→ 写 spec（`docs/superpowers/specs/`，**简洁+表格化**）→ 写 plan（`docs/superpowers/plans/`）→ 实现 → **一次推一轮** → 读日志取证 → 再改
- 提交粒度：一个逻辑一件事；message 英文，前缀 `feat/fix/perf/style/test/docs(scope):`
- 测试：新增 DSP/UI 逻辑尽量配单测（headless paint 范式：`juce::Image` + `Graphics` + `REQUIRE_NOTHROW`）；pluginval 保持 strictness 5
- 性能优化优先级：音频线程 > UI timer > 离线载入；优化必须保持行为等价（不要顺手改语义）

---

## 8. 陷阱清单（血泪，V2 有完整取证）

| 陷阱 | 后果/对策 |
|---|---|
| 测试目标包含 `PluginProcessor.h` | clap 静态初始化崩溃；同一 exe 里禁止 |
| `juce::AudioBuffer(nCh, nSamples)` 构造**不清零** | 任何"应当静音"的路径必须显式 `clear()` |
| JUCE 8 MPE 命名 | `MPESynthesiserVoice::setCurrentSampleRate`（只存值）≠ `setCurrentPlaybackSampleRate`（会杀音符） |
| `ProcessorDuplicator` 因 user-declared move ctor 不可拷贝/赋值 | 含它的 `FilterSlot` 不能 `vector::erase`，要按序重建 |
| 非拷贝类型按值返回 | `MultiPointEnvelope` 等有 `JUCE_DECLARE_NON_COPYABLE` → 测试 helper 用引用参数，别 `return env;` |
| editor 构造函数里 `setSize` 会同步触发 `resized()` | 此时 `unique_ptr` 成员可能还未创建 → 必须判空 |
| `SpinLock` 不可重入 | 先 `guard.reset()` 再回调通知 |
| **CI `Test` 步骤 `continue-on-error`** | 绿 run ≠ 测试通过；必须读计数 |
| PowerShell `Set-Content` 覆盖文件 | 会把非 ASCII 变 ANSI 乱码；改文件只用编辑工具，别用 PS 重写 |
| CRLF 噪音 | `git status` 里的既有 `M` 文件是行尾噪音，别当成自己的改动 |
| pluginval strictness | 只能灰度升（3→4→5），升了要重跑 |
| 阻塞式 `SpinLock` 用在音频线程 | 只允许 try-lock |

---

## 9. 关键文件索引（按任务找入口）

| 任务 | 先看 |
|---|---|
| 音频主链路 | `src/PluginProcessor.cpp` 的 `processBlock`（约 517 行起） |
| 加法合成/图像 | `src/dsp/AdditiveSynth.{h,cpp}`、`src/PluginProcessor.cpp::applyTimbreProcessing/refreshPartialsFromEngine/buildImageFramesFromPartialData` |
| 频谱编辑画布 | `src/gui/SpectrumEditorCanvas.cpp`、`src/gui/PartialEditorCanvas.cpp` |
| 包络 | `src/dsp/MultiPointEnvelope.{h,cpp}`、`src/dsp/EnvelopeEditOps.{h,cpp}`、`src/gui/EnvelopeCanvas.cpp`、`src/gui/EnvelopePage.cpp` |
| 冻结 | `src/dsp/SpectralFreezeEngine.{h,cpp}`、`PluginProcessor::processSpectralFreeze` |
| 粒子 | `src/dsp/SpectralParticleSystem.*`、`src/gui/ParticleDisplay.cpp` |
| 生成式音色 | `src/dsp/GenerativeTimbreDesigner.*`、`PluginProcessor::setGenerativeTimbre*` |
| 主题/视觉 | `src/gui/CyberpunkTheme.h`、`src/gui/ThemePalettes.h` |
| 页签/视图 | `src/PluginEditor.cpp::setActivePage/onViewModeChanged/resized`、`src/gui/panels/PageTabs.h` |
| 预设与持久化 | `src/dsp/PresetManager.*`、`PluginProcessor::get/setStateInformation` |
| 测试名单/CI | `tests/CMakeLists.txt`、`.github/workflows/cmake.yml` |

---

## 10. 常用命令

```powershell
git log --oneline origin/main..HEAD      # 本地未推送批次
git push origin main                     # 触发 CI
# 日志三项必读：All tests passed / Strictness level / CRASH-OR-FAIL
```
（将来若本机装了工具链：`cmake -B build -G "Visual Studio 17 2022" -A x64; cmake --build build --config Release; ctest --test-dir build -C Release`）

---

## 11. 给下一个 AI 的提示

可直接粘贴的接任 prompt：`docs/superpowers/prompts/anaplug-next-agent-prompt.md`。
