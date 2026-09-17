# P5 — 前沿功能接线 设计

日期：2026-09-17
状态：设计已定（用户离线授权自主决策："自己完成、自己解决 bug、自己优化"）
归属：路线图 P5（前置 P1-P4 ✓ P6 ✓）
原则（路线图验收）：**每个接线功能必须可从 UI 触发 + 有单元测试 + pluginval 绿；不接线就不出现在 UI。**

## 侦察结论（已实证）

| 类 | 引用状态 | 结论 |
|---|---|---|
| `SpectralDNA` / `SpectralDNAEvolver` | 已接线（`dnaEvolver_` + `EvolutionPanel` callout） | 无需处理 |
| `DualTimbre` | P6 已接线（Timbre A/B blend） | 无需处理 |
| `GenerativeTimbreDesigner` | **零引用** | 本轮接线 |
| `SpectralParticleSystem` | **零引用** | 本轮接线（视图） |
| `SpectralFreezeEngine` | **零引用** | 本轮接线（音频路径） |
| `HPSSEngine` | **零引用**、离线、无测试 | **不接**（离线分析，无实时价值；避免造死控件） |

## 决策记录

| 决策点 | 结论 |
|---|---|
| 哪些接 | GenerativeTimbreDesigner / SpectralParticleSystem / SpectralFreezeEngine |
| HPSS | 不接（离线、分配内存、零测试；收益低风险高） |
| Design 接入点 | partial 域，挂在 `applyTimbreProcessing()` 尾部（与 P6 TimbreShaper 同架构，消息线程） |
| Particles 接入点 | 新增频谱视图 "PARTICLES"（`viewModeCombo_` 第 7 项），由 editor timer 驱动物理，**不入音频线程** |
| Freeze 接入点 | `processBlock` 音频路径（`processAudio`，干/湿混合），FX/MASTER 页开关 + 模式 + mix |
| 分批 | Round 1 = GENERATIVE；Round 2 = PARTICLES 视图；Round 3 = SPECTRAL FREEZE |

## Round 1 — GENERATIVE TIMBRE（本次实现）

**目标**：TIMBRE 页新增一行 GENERATIVE 控件；把 `GenerativeTimbreDesigner` 的潜在向量变成当前谐波集的一部分。

- Processor：
  - 成员 `ana::GenerativeTimbreDesigner timbreDesigner_;`、`std::atomic<bool> generativeEnabled_`、`std::atomic<float> generativeMix_`、`PartialDataSIMD generativeScratch_;`
  - API：`setGenerativeTimbreEnabled/isGenerativeTimbreEnabled`、`setGenerativeTimbreMix/getGenerativeTimbreMix`、`randomizeGeneratedTimbre()`、`captureGeneratedTimbreFromEdit()`、`setGenerativeTimbrePreset(int)`
  - `applyTimbreProcessing()` 尾部：若启用且 mix>0 → `timbreDesigner_.applyToPartials(result, mix)`；启用时先保证 `generatedTimbre_` 已生成（`generate(generativeScratch_)`）。
- UI（`PluginEditor`，TIMBRE 页）：`GEN` 开关 / `RANDOM` / `CAPTURE` / 8 个预设下拉 / mix 滑条；editor timer 同步。
- 测试：`test_generative_timbre.cpp` 增补 `applyToPartials` 的 mix 语义（mix=0 不改、mix=1 等于生成集、频点/相位不被覆盖）。

## Round 2 — PARTICLES 视图

- Processor：成员 `ana::SpectralParticleSystem particleSystem_;` + `setParticlesEnabled(bool)`；`syncParticlesFromEdit()` = `emitFromPartials(editedPartials_)`；`advanceParticles(double dt)`（消息线程，editor timer 调用）→ `update(dt)`。
- 新组件 `ana::ParticleDisplay`（`src/gui/ParticleDisplay.{h,cpp}`）：读 `getParticles()` 画点（freq→x 对数轴、amp→y、hue/brightness 上色、life→透明度）；headless paint 测试。
- `viewModeCombo_` 增 "PARTICLES"（id 7）+ `onViewModeChanged` case 7 + resized bounds + 加 editor timer 驱动。

## Round 3 — SPECTRAL FREEZE

- Processor：成员 `ana::SpectralFreezeEngine freezeEngine_;`（`setSampleRate`/`setFftSize` 在 `prepareToPlay`），`std::atomic<bool> freezeEnabled_`、`std::atomic<float> freezeMix_`、预分配 `juce::AudioBuffer<float> freezeScratch_`。
- 音频路径：effects 链之后、master 之前；`freezeScratch_.makeCopyOf(buffer)` → `freezeEngine_.processAudio(freezeScratch_, buffer)`；关闭时继续跑若干块以完成内部交叉淡化。
- UI（FX 页）：`FREEZE` 开关 + 模式下拉（Snapshot/Accumulate/Motion/Reverse）+ mix + pitch/blur/tilt（可选）。
- 测试：`SpectralFreezeEngine` 已有测试；补 `processAudio` 开关后输出非静音/复原。

## 风险

| 风险 | 缓解 |
|---|---|
| Freeze `processAudio` 可能音频线程分配 | 先读 `.cpp` 确认；必要时预分配/预热一次 |
| Particles 在 editor timer 驱动会占 CPU | 上限 512 粒子、30 Hz；仅视图可见时更新 |
| GenerativeTimbreDesigner 固定 100 Hz 基频/44.1k | 仅作幅度混合（`applyToPartials` 只改幅度），不替换频率/相位 |
