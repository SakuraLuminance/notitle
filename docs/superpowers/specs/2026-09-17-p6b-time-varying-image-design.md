# P6b — 时变谐波图像（Harmor image）设计

日期：2026-09-17 · 状态：用户离线授权自主进行（"继续写继续修"）· 归属：P6 遗留批次

## 问题

P6 的 SYNTH 模式只用**单帧**谐波集（分析结果中 Σamp² 最大帧）。Harmor 的核心是**时变图像**：把采样分析出的多帧谐波随时间推进回放，音色随音符进行而演化。当前缺这一块，因此长音是"冻结的静态频谱"。

## 决策

| 决策点 | 结论 |
|---|---|
| 帧来源 | `engine.getPartialData().frames` 均匀下采样到 ≤32 帧；每帧按**频率升序**取前 128 partial（索引跨帧对应稳定），保留精确频率/相位 |
| 索引语义 | 单帧路径（现有）保持"按幅度排序"；图像路径"按频率排序" |
| 播放 | 每 voice 维护 `framePos_`，按 `rate`（帧/秒）推进；`IMAGE` 关 → 恒为帧 0（**行为与今天完全一致**，零回归风险） |
| 插值 | 每个 render 子块把 floor/ceil 两帧线性插值成本地表（频率/幅度），再做 phasor 循环；相位连续性由 phasor 自身累积保证 |
| 编辑语义 | 频谱编辑器仍编辑 `editedPartials_` = **图像第 0 帧**（本轮不做逐帧选择器，列为后续） |
| 静态集来源变更 | 图像关时的静态集由"Σamp² 最大帧"改为**分析第 0 帧**（= 图像第 0 帧），使编辑器与图像播放一致；`sourcePartials_` 保留最大能量帧仅作兜底 |
| 音色控件 | bright/HPF/blur/A-B blend/GENERATIVE 对**每一帧**生效（消息线程重算） |
| 帧选择器/逐帧编辑 | **已实现**：`setImageEditFrame(n)` + TIMBRE 页 FRAME 滑条；编辑器只改所选帧，切换帧时自动保存上一帧的编辑；状态栏显示 `IMAGE  i/N FRAMES` |
| 内存 | `Frame` = 128×3 float + count ≈ 1.5 KB；32 帧/库 ≈ 49 KB；发布/激活两库 ≈ 98 KB |
| 发布 | `publishedGeneration_` 原子计数；音频线程仅在 generation 变化时拷贝整库（避免每块 memcpy） |

## 数据模型（`ana::AdditiveSynth`）

```cpp
static constexpr int kMaxFrames = 32;
static constexpr int kMaxStoredPartials = 128;

struct Frame { float frequency[kMaxStoredPartials]{}; float amplitude[...]{}; float phase[...]{}; int count = 0; };

void setPartials(const PartialDataSIMD& p);                  // 单帧库（现有 API / 测试）
void setFrames(const std::vector<PartialDataSIMD>& frames);  // 图像库（≤32 帧）
void setImageEnabled(bool);        // 默认 false
void setImageRate(float fps);      // 0.05..20
void setImageLoop(bool);           // true = 循环，false = 停在末帧
int  getFrameCount() const;
int  getActiveFrameCount() const;  // 测试用（已发布帧数）
```

`AdditiveVoice`：`framePos_`；每子块插值出本地表后渲染。`noteStarted` 用帧 0 初始化 phasor。

## Processor

- `loadFile`/`resetPartialsFromEngine`：构建 `imageFrames_`（≤32 帧，频率升序，前 128）。
- `refreshPartialsFromEngine` 同步 `imageFrames_[0] = editedPartials_`。
- `applyTimbreProcessing()`：对 `imageFrames_` 每帧做 A/B shape + blend + generative，推入 `additiveSynth_.setFrames(live)`。
- 新 API：`setImageEnabled/isImageEnabled`、`setImageRate/getImageRate`、`setImageLoop/isImageLoop`；持久化 enabled/rate/loop。
- 图像关时只发布 1 帧（= 今天的单帧渲染）。

## UI（TIMBRE 页，GENERATIVE 行上方新增一行）

`IMAGE` 开关 / `RATE` 滑条（0.05..20 帧/秒）/ `LOOP` 开关 + 状态读数（帧数）。editor timer 同步。

## 测试

- `AdditiveSynth`：两帧图像（帧 0 只有 440Hz、帧 1 只有 880Hz）→ rate 高时足够时间后 880 的能量显著上升；`rate=0` 或 `IMAGE` 关 → 频谱不变；`setFrames` 空/1 帧安全。
- 现有 583 用例保持全绿（图像默认关 → 行为不变）。
- pluginval strictness 5。

## 风险

| 风险 | 缓解 |
|---|---|
| 跨帧索引对应不准（非谐波音源） | 频率升序 + 线性插值；本轮接受，后续可做谐波分箱 |
| 抖动/相位不连续 | phasor 累积相位不重置，频率变化自然滑音 |
| applyTimbreProcessing 变慢（32 帧 × blur） | 仅在参数/编辑变化时重算；blur=0 时跳过 |
