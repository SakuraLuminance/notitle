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

## JITTER（间距随机化）

调度原本是钟表式的：累加器每满 1.0 生成一颗粒子。`JITTER` 让**每次生成消耗的间隔**围绕 1.0 随机：`cost = 1 + jitter · U(-0.5, +0.5)`。

| 性质 | 说明 |
|---|---|
| 平均速率不变 | `E[cost] = 1` → `DENSITY` 的语义（每秒粒子数）完全保留，只有间距在呼吸 |
| jitter=0 | 与旧实现逐样本等价（`nextSpawnCost()` 直接返回 1.0，不消耗随机数） |
| jitter=1 | 间隔落在 [0.5, 1.5] 倍之间，听感从「机械脉冲」变成「云雾」 |
| 池满行为不变 | 仍然是「丢一颗，不顺延」：累加器先减，`spawnGrain()` 失败就 break |

UI：`JITTER ▬ 0%` 放在第 1 行**右侧原本空着的那半行**（和 DENSITY 语义上属于同一组），带百分比读数、tooltip 与 MIDI Learn id `grain_jitter`。

## SPREAD / REVERSE（立体声散布 + 反向粒子）

| 参数 | 语义 | 实现 |
|---|---|---|
| `grain_spread`（SPREAD，0–100%） | 每个粒子在 \(pan\) 上再加一个均匀随机偏移 `±spread`，再 clamp 到 [-1,1]；等功率 pan 不变 | `spawnGrain()`：`panValue = juce::jlimit(-1.0f, 1.0f, pan_ + U(-spread_, +spread_))` |
| `grain_reverse`（REVERSE，0–100%） | 该比例的粒子**倒放**：起点放在同一跨度的**远端**、步进为负 | `startPos = centre + halfSpan`，`pitchRatio = -ratio` |

关键不变量：倒放粒子的**跨度、窗、时长、个数**与正放完全一致，只有样本顺序翻转 —— 因此密度、SIZE、POSITION 的语义和听感平衡都不变，而且渲染循环里除 `pitchRatio` 的符号外**一行都不用改**（读指针越界由生成时的 clamp 保证：起点 ∈ [0, len-1]，之后单调递减且不会低于 `centre - halfSpan` ≥ 0）。

接线（“不接线就不出现在 UI”）：

| 层 | 改动 |
|---|---|
| DSP | `setStereoSpread()` / `setReverseProbability()`，clamp 0..1 |
| 处理器 | 原子 `granularSpread_` / `granularReverse_` + `set/get`，`renderGranularLayer()` 与 `prepareToPlay()` 推入引擎 |
| 状态 | `grainSpread` / `grainReverse` 属性保存 + 恢复（老工程缺属性时默认 0） |
| UI | GRAIN 页第 6 行 `SPREAD ▬ 0%` / `REVERSE ▬ 0%`，均带 tooltip 与百分比读数 |
| MIDI Learn | `grain_spread` / `grain_reverse`（% ↔ 0..1 转换器） |

## GRAIN 页的粒子云（视觉反馈）

页面上原来只有控件，没有任何反馈：DENSITY / SIZE / SPACE+MOD / RATE 到底在做什么看不出来。

| 项 | 决定 |
|---|---|
| 数据源 | `GranularSynthesizer::getActiveGrainSnapshots(out, maxCount)`：把活跃粒子归一化成 `{position, duration, progress, amplitude, pan}`，**无分配、无锁**，沿用活跃前缀扫描 |
| 跨线程 | 音频线程每块发布一次到 `grainVisual_[64]`：**seqlock**（generation 先 +1 变奇数 → 写缓冲 → 写计数 → 再 +1 变偶数）。UI 端 `getGrainVisualisation()` 读到奇数或前后 generation 不等就重试，4 次都不一致就**这一帧不画**（宁可空一帧，不画撕裂数据） |
| 绘制 | 顶部 18 px = **采样包络条**（镜像，256 桶峰值）；下方 = 粒子云：x = 源内读取位置，y = 年龄（新粒子在顶部、随进度下沉），条宽 = 粒子长度（占源比例），**颜色 cyan→magenta 按 pan**（左→右，SPREAD 一动就散开），透明度 = 粒子振幅，条的前进端有一道短竖线标**粒子方向**（REVERSE）；另画 25/50/75% 参考线与黄色的基准读取位置线 |
| 主题 | 全部走 token（`kCanvasBgDarken` / `kCanvasBorderAlpha` / `kCanvasGridAlpha` / cyan_ / magenta_ / yellow_），无硬编码颜色 |
| 刷新 | 只在 GRAIN 页可见时（编辑器只同步当前页）`repaint(cloudBounds_)`，不做整页重绘 |
| 顺手修的 bug | `renderGranularLayer()` 提前返回（层关掉或没载入采样）时原来会把 `activeGrainCount_` 留成旧值，UI 会一直显示“N GRAINS ACTIVE”；现在归零并清空云 |

## MIDI Learn

GRAIN 页的 7 个旋钮也支持右键 MIDI Learn（与主界面/rack 一致），参数 id：`grain_mix` / `grain_size` / `grain_density` / `grain_position` / `grain_pitch` / `grain_mod_depth` / `grain_mod_rate`。

页面显示的是 % / ms / 粒每秒，而处理器原子是归一化的，因此走 `setupMidiLearnForSlider(slider, id, setter, getter)` 这个**带转换器**的重载（% ↔ 0..1 在转换器里做）。转换器**捕获处理器指针而不是编辑器**：映射的生命周期长于编辑器，捕获 `this` 会在编辑器销毁后变成悬垂调用。

测试（`test_granular_synthesis.cpp`）：空源/空指针/maxCount=0 返回 0；渲染后条数 == `getActiveGrainCount()`；每个字段落在 [0,1]（pan 为 [-1,1]）、amplitude == 设定值、duration == 100 ms / 1 s；小缓冲截断；`reset()` 后为空。

## 验证

| 项 | 值 |
|---|---|
| Run | `#35311445334`（`578253a`） |
| Build | 0 条 error |
| 用例 | **611 执行 / 0 失败**（runner 注解，见 HANDOFF_V3 §1） |
| pluginval | `Strictness level: 5` SUCCESS |
| forensics | 0 CRASH-OR-FAIL，7 SKIP-UNMATCHED |
| 备注 | 本批次随 578253a 全绿；用例数在 #35315329172 读到（611），#35316399784 = 613、#35318939971 = 616、#35319079605 = 617 |

