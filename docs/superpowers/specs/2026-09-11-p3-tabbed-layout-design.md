# P3 — 信息架构分页（Serum 式）设计

日期：2026-09-11
状态：已批准（用户确认布局草图 + "UNISON/VOICE/ARP 并入 MASTER 页" + "多页像 Serum 可切换"）
归属：路线图 P3；其后顺序调整为 **P6（Harmor 式 WAV 导入/采样减法）→ P4 → P5**（用户点名采样减法优先级高）

## 背景与问题

1100×780 里 7 个区域同屏无主次：底部六合一（UNISON/VOICE/ARP/SEQ/SAMPLE/MASTER）挤死；
调制面板被夹在中间压缩；SEQ 没有地位；频谱与主内容抢空间。用户要求 Serum 式分页。

## 目标形态（已画草图获批准）

```
标题栏（不变：标题+PRESET+IMPORT）
├── 频谱区（常驻 ~30%，不随页切换；视图切换下拉移至频谱左上角内嵌）
├── 页签条（水平横排 6 页：TIMBRE FILTER MOD SEQ FX MASTER，主题风格自绘）
├── 内容区 = 当前页（~60%）
└── 状态栏（不动）
```

页面归属：
- **TIMBRE**：TimbreA + TimbreB + BLEND + XY Pad
- **FILTER**：FilterPanel
- **MOD**：调制指派面板（滚动）+ MACRO 4 旋钮（行置顶）
- **SEQ**：SequencerPanel（现底部 18% → 全幅大画布）
- **FX**：FX PRESET 行 + PRISM/BLUR/HARM + VOICE(声部角色)行 + EffectRack
- **MASTER**：MASTER VOL/PAN + UNISON + VOICE/PORTAMENTO + ARP（用户裁定并入）

非变更：状态栏/标题栏/所有控件内部布局/right-click MIDI Learn/timer 同步逻辑（隐藏页照常刷新）。

## 设计

### 1. Regions 重定义（PluginEditor）

```cpp
struct Regions {
    juce::Rectangle<int> titleBar;    // 28px
    juce::Rectangle<int> spectrum;    // 常驻频谱区（含内嵌视图下拉）
    juce::Rectangle<int> tabBar;      // 22px 页签条
    juce::Rectangle<int> content;     // 当前页内容
    juce::Rectangle<int> statusBar;   // 35px
};
```

computeRegions 全重写；paint 边框：频谱/页签条/内容/状态四块（频谱标题栏角标 "SPECTRUM"）。

### 2. 页签组件 `PageTabs`（src/gui/panels/PageTabs.{h,cpp}）

自绘按钮组（6 个 juce::TextButton，toggle 风格，主题紫系），`int activeIndex = 0`，
`std::function<void(int)> onTabChanged;`。不用 juce::TabbedComponent（完全是样式战
斗场，自绘 22 行更可控；editor 非测试编译目标，无 clap 约束）。

### 3. 页面切换（editor）

```cpp
void setActivePage(int page);   // 0..5
// 每页成员清单 setVisible(bool)，缓存 activePage_；resized() 按 activePage 布局.content
```

- 默认页 TIMBRE。
- 中央四个采样视图（feedbackPanel_/waterfall/canvas/waveformDisplay_/liveSpectrumPanel_）从
  "视图模式" 数组降级为**频谱区内显示**（区域从 mainArea 改为 spectrum；原
  `viewModeCombo_` 仍控制这五个视图 — LIVE 默认逻辑不变，只是位置移动 + 常驻不隐藏）。
  频谱区右上角放 viewModeCombo_（18px 内嵌条）。
- 失焦页：所有该页成员 setVisible(false)；bounds 照常（切回时无闪烁）。
- ISO：`timerCallback` 不感知页面（隐藏组件照常 receive 数据——repaint 不可见自动跳过）。

### 4. 各页内容布局（contentArea 百分比）

- TIMBRE：A(22%) | B(22%) | XY(22%) | blend 横条(底部 22px 跨全宽)
- FILTER：filterPanel_.setBounds(content)
- MOD：macroPanel_(顶 64px 全宽) + modViewport_(余下全部)
- SEQ：sequencerPanel_.setBounds(content)
- FX：fxPresetRow(16px) + specRow(18px) + vocalRow(18px) + effectRack_(剩余)
- MASTER：UNISON(30%) | VOICE(26%) | ARP(22%) | MASTER(22%)

各子面板内部布局不变（panels 的 resized 自适应其 bounds——Phase 3 拆分红利）。

### 5. 风险与对策

- mouseDown（MIDI Learn 右击）依赖 event.component 直查 —— 隐藏组件不再接收事件，行为正确。
- PURE 几何：首次切页/极小窗口（pluginval giant-resize）时 setContent 负值 → 每页布局
  用 jmax(1, …) 兜底；频谱高度下限 120。
- 首版不做：页签拖动重排、页签持久化、动画过渡。

## 测试与验收

1. 现有 557 用例 0 失败 + pluginval strictness 3 不回归（editor 开合/resize 已是覆盖面）。
2. 手工：六页均可达且控件全活；频谱常驻；MIDI Learn 在各页可用；页切换不闪/不崩。
3. UI 断言：无新增 headless（PageTabs 逻辑随 editor 验证；不为它单开 exe——组件归 editor，非测试编译单）。

## "算法优化"常设承诺（用户同期要求）

strictness 3→4→5 渐进照走（每批可搭车）；P6 引擎级（Harmor 式减法导入）与其 DSP 质量核查
在 P4/P5 批次专项执行；GUI-FFT/LiveSpectrum 等 UI 侧算法已在其位。
