# 帧混合曲线（P6 遗留）设计 — 2026-09-18

> 目标：图像（时变谐波集）在相邻两帧之间不再只有线性淡入淡出，提供 **LINEAR / SMOOTH / STEP** 三种混合形状，并保证与既有线性行为**逐样本一致**（默认就是 LINEAR）。

## 1. 现状与问题

`AdditiveVoice::renderNextBlock` 每次渲染调用把图像位置前进一步，然后对相邻两帧做**线性**交叉淡化：

```cpp
f0 = (int) framePos;  f1 = min(lastFrame, f0 + 1);
fMix = framePos - f0;                       // 0..1 线性
iAmp[i] = A.amp[i] + (B.amp[i] - A.amp[i]) * fMix;
```

问题：线性混合让每一帧的**起止都是"斜坡"**——听感上像连续 morph，做不出"序列化"的硬切换；而有些素材（打击型帧序列）恰恰需要 STEP 的断奏感，另一些需要 SMOOTH 的缓入缓出。

## 2. 设计

| 项 | 决定 |
|---|---|
| 参数位置 | `AnaPlugAudioProcessor::setImageCurve(int)`（0=LINEAR/1=SMOOTH/2=STEP），`std::atomic<int> imageCurve_` |
| 曲线 | 0 = `m`（现状）；1 = smoothstep `m²(3-2m)`；2 = `m < 0.5 ? 0 : 1` |
| 落点 | **只改 `fMix` 一个标量**（`AdditiveSynth::shapeFrameMix`），不碰插值公式、不碰 partial 合并逻辑 → 默认 LINEAR 与旧版本逐位一致 |
| 线程模型 | 处理器原子 → `AdditiveSynth::imageCurve_` 原子 → `renderNextSubBlock` 每子块写入 voice 的 `imageCurve` 字段（与既有 `imageLoop`/`framesPerBlock` 完全同一模式，音频线程只读） |
| 持久化 | 状态属性 `imageCurve`（int）；`setStateInformation` 读回后经 `setImageCurve()` 推给 synth |
| UI | TIMBRE 页图像条上新增 `CURVE LIN / CURVE SMOOTH / CURVE STEP` 下拉（108 px，带 tooltip），timer 里与处理器原子双向同步 |
| 越界 | `setImageCurve` 夹到 0..2；`shapeFrameMix` 未知模式回落到 LINEAR |

### STEP 的语义边界

STEP 下 `fMix` 只取 0 或 1，因此：

- 两帧都有的 partial：切换前 = A，切换后 = B（硬切）；
- 只在 A 里的：`amp * (1 - fMix)` → 切换前满、切换时归零；
- 只在 B 里的：`amp * fMix` → 切换前静音、切换时满。

即"整帧替换"，与"逐 partial 淡入淡出"不同——这正是断奏感的来源，且有测试固定。

## 3. 测试

`tests/test_additive_synth.cpp`（新增 2 例）：

1. `shapeFrameMix` 纯函数：线性恒等 + 输入夹取、smoothstep 对称（0.25→0.15625、0.75→0.84375）、STEP 在 0.49/0.5 两侧跳变、未知模式回落、synth 对 mode 的夹取。
2. 渲染级：两帧 440 Hz / 2000 Hz 图像、20 f/s、**取第二块**（图像走到 21%–43%，线性已明显漏出帧 B）——LINEAR 的 B/A 能量比 > 0.2，STEP 的比值 < LINEAR 的 20%。

## 4. 不做

- 不引入"逐 partial 曲线"或曲线编辑器（收益低、UI 成本高）；
- 不改 `AdditiveBank` 的帧结构（曲线是播放期参数，不是数据属性）。

## 验证

| 项 | 值 |
|---|---|
| Run | `#35316399784`（`c335326`） |
| Build | 0 条 error |
| 用例 | **613 执行 / 0 失败**（runner 注解，见 HANDOFF_V3 §1） |
| pluginval | `Strictness level: 5` SUCCESS |
| forensics | 0 CRASH-OR-FAIL，7 SKIP-UNMATCHED |
| 备注 | 较上一轮 611 增加 2 例（本 spec 的 2 个用例） |

