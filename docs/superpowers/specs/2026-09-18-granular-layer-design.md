# 颗粒层接线 + GRAIN 页（P7）

**状态**：已实现并推送（`578253a`），待 CI 取证
**日期**：2026-09-18
**背景**：`GranularSynthesizer` 从 P1 起就实现完毕（256 粒子、4 窗型、4 种位置调制、
采样精确调度 + 预计算窗表），但**从未接到音频路径，也没有任何 UI**——按"不接线
就不出现在 UI"原则，它此前既不可听也不可见。

## 1. 音频路径

| 决策 | 取值 | 理由 |
|---|---|---|
| 混入位置 | 两条输出路径都在 `processSpectralFreeze()` **之前** | 颗粒云要过 freeze/master/VCA；效果器 rack 不参与（rack 是"每层之外"的处理） |
| 湿声 | `buffer.addFrom(..., mix)`，mix 0..1 | 与 freeze/mix 语义一致；mix=0 时完全旁路（不加分支以外的成本） |
| 源采样率 | `GranularSynthesizer::setSourceBuffer(samples, audioData.sampleRate)` | 粒子时长/密度按**源**采样率计时，载入后与工程采样率解耦 |
| 源数据 | 引擎分析用的 mono `std::vector<float>` 的**拷贝** | 后续图像编辑/重合成不会让粒子引擎悬空 |
| 缓冲 | `granularScratch_` 在 `prepareToPlay` 分配 2×maxBlock | 实时路径零分配；`process()` 内部 `clear()` 后写 |
| 窗表 | `reserveWindowCache(sampleRate*0.1+2)` | `getCachedWindowValue()` 会 `resize(duration)`；预留 100 ms 后任何粒度变化都不再分配 |

## 2. 参数与 UI（GRAIN 页，第 9 个页签）

| 控件 | 范围 | 处理器 API | 读数 |
|---|---|---|---|
| GRAIN（toggle） | on/off | `setGranularEnabled` | 状态行 `GRAIN ON/OFF` |
| MIX | 0..100 % | `setGrainMix` | `formatPercent` |
| SIZE | 1..100 ms（skew 25） | `setGrainSizeMs` | `formatNumber(…,1) + " ms"` |
| DENSITY | 1..1000 /s（skew 60） | `setGrainDensity` | `formatNumber(…,0) + " /s"` |
| SPACE | 0..100 % | `setGrainPosition` | `formatPercent` |
| PITCH | −24..+24 st | `setGrainPitch` | `formatNumber(…,0) + " st"` |
| WINDOW | HANN/TRIANGLE/GAUSSIAN/SINC | `setGrainWindow` | combo |
| MOD | OFF/LFO/ENVELOPE/RANDOM | `setGrainModMode` | combo |
| DEPTH / RATE | 0..100 % / 0.05..10 Hz | `setGrainModDepth` / `setGrainModRate` | `formatPercent` / `formatNumber(…,2) + " Hz"` |

- 状态行：`STATUS :: N GRAINS ACTIVE | GRAIN ON`；无采样时 `STATUS :: WAITING FOR SAMPLE`。
- 空状态：画布居中全大写 `NO SAMPLE LOADED - IMPORT A SAMPLE TO GRAIN`。
- 全部 10 个参数随插件状态持久化（`grain*` 属性）。

## 3. 线程模型

| 侧 | 行为 |
|---|---|
| UI（消息线程） | 只写 12 个原子；timer 里 `syncFromProcessor()` 回读（用于 preset 载入/自动化） |
| 音频线程 | `renderGranularLayer()` 每块把原子推入引擎 → `process()` → 混音；发布 `activeGrainCount_` |

## 4. 目标文件归属

`GranularPage.cpp` **只加入 `CMakeLists.txt`（插件目标）**，不加入
`tests/CMakeLists.txt`：它 `#include "../PluginProcessor.h"`，而测试目标一旦包含
处理器头就会触发 clap 静态初始化崩溃（HANDOFF §8 陷阱）。同 `EnvelopePage.cpp` 先例。
粒子引擎自身的测试（`tests/test_granular_synthesis.cpp`）直接测 `GranularSynthesizer`，
不经过处理器。

## 5. 测试

| 用例 | 断言 |
|---|---|
| `reserveWindowCache` 扩容 | 预留后容量 ≥ 请求值；多次不同粒度渲染后不缩小；`reserveWindowCache(0)` 被忽略 |
| 预留不改变输出 | 预留实例与普通实例输出能量一致（窗表内容不受容量影响） |

## 验证

| 项 | 值 |
|---|---|
| Run | `#35311445334`（`578253a`） |
| Build | 0 条 error |
| 用例 | **611 执行 / 0 失败**（runner 注解，见 HANDOFF_V3 §1） |
| pluginval | `Strictness level: 5` SUCCESS |
| forensics | 0 CRASH-OR-FAIL，7 SKIP-UNMATCHED |
| 备注 | 本批次随 578253a 全绿；用例数在 #35315329172 读到（611），#35316399784 = 613、#35318939971 = 616、#35319079605 = 617 |

