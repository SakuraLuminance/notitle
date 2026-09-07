# P1 — 实时频谱/示波（Live Spectrum）设计

日期：2026-09-07
状态：已批准（用户确认"批准，按此实施"）
归属：AnaPlug UI 重构路线图第一批（P1→P6，顺序为工作假设，用户可随时推翻）

## 背景与问题

用户反馈"频谱功能无法正常显示对应的频谱"。根因（已代码实证）：

1. 中央显示器的全部数据（PARTIALS/WATERFALL/EDITOR/3D）都来自**采样引擎的 partials 分析结果**，
   且在 `PluginEditor.cpp` timerCallback 里被 `isEngineLoaded()` 门住——**不加载采样就没有任何频谱**。
2. SCOPE 视图（WaveformDisplay）的 scope 数据推送同样被裹在 `isEngineLoaded()` 门里，
   尽管 `AnaPlugAudioProcessor::captureScopeOutput()`（PluginProcessor.cpp:1416）在 processBlock
   末尾**无条件**捕获最终混音（2048 点 mono mix，双缓冲，无锁发布）。
   也就是说数据其实一直都在，只是 UI 没用上。

## 目标

- 打开插件（不加载任何采样、宿主不发音频时静默待机；有音频立即显示）→ 中央面板显示
  **随最终输出实时变化的频谱**（Serum/Harmor 式）。
- 采样引擎视图（PARTIALS/WATERFALL/EDITOR/3D）行为保持不变，加载采样后照常使用。
- 零音频线程新增负载（FFT 全部在 UI timer 侧做）。

## 设计

### 1. 新组件 `ana::LiveSpectrumPanel`（src/gui/LiveSpectrumPanel.{h,cpp}）

- **数据源**：`AnaPlugAudioProcessor::getScopeOutput(std::vector<float>&)`（已有 API，
  2048 样本，无锁换读缓冲）。**不新建 Timer**：编辑器主 timer（30 Hz）每 tick 调
  `liveSpectrumPanel_.updateFromProcessor(audioProcessor)` 拉最新 buffer（避免多 timer 时序漂移）。
- **FFT**：`juce::dsp::FFT(2048)` + Hann 窗（一次性预计算窗系数表）。每 tick：
  取 scope 2048 点 → 加窗 → performRealOnlyForwardTransform → 幅度谱 1025 bins。
  30 Hz × 2048 点实数 FFT 的 UI 侧开销可忽略（<1% 核）。
- **显示**：
  - 对数频率横轴：20 Hz – 20 kHz，40–64 根柱（按面板宽度自适应，每柱聚合落入其频率区间的
    bin 最大能量）。
  - 幅度：dB 刻度，动态范围默认 -72…0 dBFS（常量，首版不做可调）。
  - 柱体颜色：CyberpunkTheme 紫系（低电平 fg_ 暗淡 → 高电平 cyan_ → 满电平 magenta_ 渐变，
    与现有 MacroKnob 的色彩语义一致）。
  - **峰值保持**：每柱峰值 0.5–1.0 s 指数衰减下落（参考 VisualFeedbackPanel 现有 peak hold 逻辑）。
  - 未加载/静音时：画网格背景 + "-inf dB" 提示文字，**不再显示 "No partials active"**（该文案
    属于 PARTIALS 视图，保留在原处）。
- **线程安全**：沿用 `getScopeOutput` 的双缓冲读发布模式，UI 侧不加新锁。

### 2. 视图模式改造（PluginEditor）

- `viewModeCombo_` 新增第 1 项 **LIVE**，原 1-5 项顺延为 2-6（PARTIALS/WATERFALL/EDITOR/3D/SCOPE）。
- 默认选中 **LIVE**。
- `onViewModeChanged`：LIVE ↔ LiveSpectrumPanel 显隐；其余视图逻辑不变。
- `resized()`：LiveSpectrumPanel 与 feedbackPanel_/waterfallDisplay_/spectrumEditorCanvas_/
  waveformDisplay_ 同区（fbArea.reduced(2)），同一位置互斥显隐。
- **bug 修复**：把 SCOPE 的 scope→WaveformDisplay 推送移出 `isEngineLoaded()` 门
  （数据本就不依赖引擎）。
- 编辑器主 timer（30 Hz）内直接调用 `liveSpectrumPanel_.updateFromProcessor(audioProcessor)`：
  拉取 scope + 在 panel 内做 FFT + repaint。不另开 Timer，避免多个 timer 的时序漂移。

### 3. 数据流

```
processBlock 末尾 captureScopeOutput(buffer)   [已有，不动]
        │ (无锁双缓冲)
        ▼
editor timer 30Hz → liveSpectrumPanel_.updateFromProcessor(p)
                          │ getScopeOutput(dest)
                          ▼
                    FFT(2048, Hann) → 1025 bins → 对数聚合 → paint()
```

## 边界与非目标

- 不改 processBlock / 不加音频线程任何逻辑。
- 不改 PARTIALS/WATERFALL/EDITOR/3D 的现有数据路径与外观。
- 首版不做：频谱参数可调 UI（动态范围/分辨率/窗长）、3D/Waterfall 式实时频谱、
  频谱导出/采样（那些属于 P6 与后续）。
- LIVE 视图不显示采样引擎 partials 信息（那是 PARTIALS 视图的职责）。

## 测试与验收

1. `tests/test_ui_paint.cpp` 增加 LiveSpectrumPanel headless paint 用例：
   - 空数据（未 update）paint 不崩；
   - 注入确定性正弦 scope 数据后 paint 不崩（FFT 路径执行）。
2. 现有 539 用例 0 失败不回归；pluginval（strictness 2）editor 冷/热开合不回归。
3. 手工验收（用户）：不加载采样打开插件 → 中央显示实时频谱，随宿主播放变化；
   加载采样后切 PARTIALS/WATERFALL 行为与现在完全一致。

## 风险

- FFT bins → 对数柱聚合的边界 bin 归属需仔细（20 Hz 下 bin 分辨率 ≈ 23 Hz @2048/48k，
  低频区多 bin 聚合、高频区稀疏插值聚合）——实现时用"落入区间取最大能量"规则，避免闪烁。
- `viewModeCombo_` 项序变化会影响既有 preset/状态记忆吗——该 combo 未接入 APVTS（手动 UI），
  无持久化，安全。
