# P2 Effect Params Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 效果器槽位 ▾ 展开专属参数编辑行（第一批 8 类型），拖动即时生效，round-trip 可测。

**Architecture:** EffectBase 增加通用参数虚接口（默认空实现零破坏）；新 `src/dsp/effects/EffectParamRegistry.{h,cpp}` 统一 14 类型适配器（含参数表，test-safe——只依赖 EffectsChain.h，规避 clap 陷阱）；`EffectSlotWidget` 的工厂静态方法委托 Registry；新 `EffectParamPanel` 从活实例读参数表渲染 knob 并直写 setParamValue。

**Tech Stack:** JUCE 8, Catch2, 现有 CyberpunkTheme/UndoableAction 骨架。

## Global Constraints

- 本机无工具链——验证 = push → Run（~20 min）→ 日志取证；TDD 红绿循环坍缩为同批提交单轮 CI。
- **被测试目标编译的文件严禁 include PluginProcessor.h**（clap 陷阱，HANDOFF §0.2）。Registry/Panel/新测试只 include EffectsChain.h + 效果头文件。
- push 用 token URL + `git fetch origin main`；PAT 会话内提供。
- 基线：**541 用例 0 失败 + pluginval strictness 2 绿**（Run #106）。
- 参数标签英文（用户决策）；范围以各 setter 内部 clamp 为准（侦察实锤值见 spec §3）。

---

### Task 1: EffectParamSpec + EffectBase 参数虚接口

**Files:**
- Modify: `src/dsp/EffectsChain.h`（EffectBase 区域）

**Interfaces:**
- Produces: `struct EffectParamSpec { const char* id; const char* label; float min; float max; float def; float skew; bool isInt; }`；EffectBase 4 虚函数（默认空实现）。

- [ ] **Step 1**：在 EffectBase 定义中（getState/setState 之后）追加：

```cpp
    // Generic parameter surface for UI editors (P2). Default: no params.
    virtual int getNumParams() const { return 0; }
    virtual const EffectParamSpec& getParamSpec(int index) const
    {
        static const EffectParamSpec none{};
        (void) index;
        return none;
    }
    virtual float getParamValue(int index) const { (void) index; return 0.0f; }
    virtual void  setParamValue(int index, float value) { (void) index; (void) value; }
```

- [ ] **Step 2**：在 EffectBase 之前声明：

```cpp
struct EffectParamSpec
{
    const char* id = nullptr;
    const char* label = nullptr;
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    float skew = 1.0f;
    bool  isInt = false;
};
```

### Task 2: 8 个效果类补齐 inline getter（.h 内联，零行为变化）

**Files:**
- Modify: `src/dsp/effects/DelayEffect.h`（+getDelayTime/getFeedback/getMix/isPingPong）
- Modify: `src/dsp/effects/ReverbEffect.h`（+getRoomSize/getDamping/getWetLevel/getDryLevel/getWidth）
- Modify: `src/dsp/effects/ChorusEffect.h`（+getRate/getDepth/getCentreDelay/getFeedback/getMix）
- Modify: `src/dsp/effects/CompressorEffect.h`（+getThreshold/getRatio/getAttack/getRelease/getKnee/getMakeupGain）
- Modify: `src/dsp/effects/DistortionEffect.h`（+getDrive/getRange/getBlend/getVolume）
- Modify: `src/dsp/effects/LimiterEffect.h`（+getThreshold/getAttack/getRelease/getLookahead/getMix）
- Modify: `src/dsp/effects/SaturationEffect.h`（+getDrive/getTone/getMix/getGain）
- BitcrusherEffect.h 已有 3 getter（无需改）。

- [ ] **Step 1**：每类在对应 setter 声明后加 inline 定义，直接返回成员（成员名以各 .cpp 为准：delayMs/feedback/mixVal/pingPong；roomSize/damping/wetLevel/dryLevel/width；rate/depth/centreDelay/feedback/mixVal；thresholdDb/ratio/attackMs/releaseMs/kneeDb/makeupGainDb；drive/range/blend/volume；thresholdDb/attackMs/releaseMs/lookaheadMs/mixVal；drive/tone/mixVal/gain?）。**Saturation 的成员名先 grep .cpp 确认再写。**

### Task 3: EffectParamRegistry.{h,cpp}（14 适配器统一 + 8 参数表）

**Files:**
- Create: `src/dsp/effects/EffectParamRegistry.h`
- Create: `src/dsp/effects/EffectParamRegistry.cpp`

**Interfaces:**
- Consumes: EffectBase + 各效果类头文件（全部 test-safe）。
- Produces:
  - `namespace ana { struct EffectParamRegistry { static const juce::StringArray& getTypeNames(); static std::unique_ptr<EffectBase> create(const juce::String& typeName); }; }`
  - EffectSlotWidget 静态方法改为委托（签名不变）。

- [ ] **Step 1**：头文件（只 include EffectsChain.h 与 juce_core）：

```cpp
#pragma once

#include "../EffectsChain.h"
#include <juce_core/juce_core.h>

namespace ana
{

struct EffectParamRegistry
{
    static const juce::StringArray& getTypeNames();
    static std::unique_ptr<EffectBase> create(const juce::String& typeName);
};

} // namespace ana
```

- [ ] **Step 2**：cpp —— 把 EffectRackComponent.cpp 里 6 个内联 Adapter（Delay/Reverb/EQ/Chorus/Distortion/AutoTune）与 8 个直接构造（Bitcrusher/Compressor/Flanger/Phaser/RingModulator/StereoWidener/Saturation/Limiter）全部搬进来，统一为 `XxxAdapter : public EffectBase`（成员 `XxxEffect fx;` + prepare/process/reset/getState/setState 转发）。**首批 8 个适配器**各带：

```cpp
    int getNumParams() const override { return (int) table_.size(); }
    const EffectParamSpec& getParamSpec(int i) const override { return table_[(size_t) i]; }
    float getParamValue(int i) const override;
    void  setParamValue(int i, float v) override;
```

get/set 映射（单位换算在适配器内）：

- DelayAdapter：`{delay_ms,"TIME",1,2000,300,0.25f,false}`→fx.setDelayTime/getDelayTime；`{feedback,"FEEDBACK",0,100,45,1,false}`；`{mix,"MIX",0,100,30,1,false}`；`{ping,"PING-PONG",0,1,0,1,true}`→setPingPong(v>0.5)/isPingPong。
- ReverbAdapter：room/damp/wet/dry/width 0-1 默认 0.5/0.5/0.33/0.4/1.0 → setRoomSize/getRoomSize 等。
- ChorusAdapter：rate 0.05-10 def 1 skew0.3；depth 0-100 def 50；cdelay 0-100 def 20；fb 0-99 def 25；mix 0-100 def 50。
- CompressorAdapter：thresh -60..0 def -20；ratio 1-20 def 4；attack 0.1-100 def 10 skew0.3；release 10-1000 def 200 skew0.3；knee 0-10 def 3；makeup 0-24 def 0。
- DistortionAdapter：drive 0-100 def 40；range 0-100 def 50；blend 0-100 def 50；volume 0-200 def 100。
- LimiterAdapter：thresh -30..0 def -3；attack 0.01-10 def 1 skew0.3；release 1-100 def 60；lookahead 0-10 def 2；mix 0-1 def 1。
- BitcrusherAdapter：bits 1-16 def 8 int（setBitDepth/getBitDepth）；downsample 1-32 def 1 int；mix 0-1 def 1。
- SaturationAdapter：drive 0-100 def 30；tone 20-20000 def 8000 skew0.3；mix 0-100 def 100；gain 0-4 def 1。

第二批 6 类型（EQ/AutoTune/Flanger/Phaser/RingModulator/StereoWidener）适配器**不带参数表**（getNumParams 继承默认 0）。

- [ ] **Step 3**：EffectRackComponent.cpp：两个静态方法内部改为委托（`return EffectParamRegistry::getTypeNames();` / `return EffectParamRegistry::create(typeName);`），删除原 6 个内联 Adapter 与直接构造分支（此文件保留 UI/undo 逻辑）。

### Task 4: EffectParamPanel + rack 展开

**Files:**
- Create: `src/gui/EffectParamPanel.h` / `.cpp`
- Modify: `src/gui/EffectRackComponent.h`（EffectSlotWidget：删 modSlider_、加 expandButton_/paramPanel_/expanded_；rack：expandedHeights_）
- Modify: `src/gui/EffectRackComponent.cpp`（按钮接线、resized 数学、rebuild 重置展开态）

**Interfaces:**
- Consumes: EffectBase 参数接口、EffectParamRegistry（panel 内只在 EffectBase* 为空时显示 "No editable params"）。
- Produces: `EffectParamPanel(EffectBase* liveEffect)`；`int getPreferredHeight(int width) const`。

- [ ] **Step 1**：EffectParamPanel（哑组件，仅 include EffectsChain.h + theme + Registry 头——**严禁 PluginProcessor.h**）：ctor 建 knob 数组（RotaryVerticalDrag、46×38、label 9px 下方、setRange(min,max)、setSkewFactor、isInt→setRange(...,1.0)；双击回默认；初值 getParamValue）；onValueChange→setParamValue；`paint` 空态写 "No editable params"。`getPreferredHeight(width)`：rows=ceil(n/max(1,width/46)) → rows*58+6；n==0 → 18。
- [ ] **Step 2**：EffectSlotWidget：modSlider_ 删除；expandButton_（"▾"/"▸"，cyber 风格，onClick 切 expanded_ 并 `if (onExpandToggled) onExpandToggled(slotIndex_);`）；resized 分两段：头行（原布局，改在 `area` 上）当 expanded 时 `area = area.removeFromTop(30)`，剩余给 paramPanel_；`setExpanded(bool)` 创建/销毁 panel（make_unique<EffectParamPanel>(&chain.getEffect(slotIndex_))，addAndMakeVisible）。
- [ ] **Step 3**：EffectRackComponent：`std::vector<bool> expanded_` 与 slots_ 平行；resized 内容高 = Σ(expanded ? 30 + panelHeight : 30)；槽 setBounds 用各自高度；onExpandToggled → resized()；rebuildSlots 重置 expanded_ 全 false；syncFromProcessor 照旧（面板不参与定时同步）。

### Task 5: CMake 接入

- [ ] plugin（CMakeLists.txt）：`src/dsp/effects/EffectParamRegistry.cpp`、`src/gui/EffectParamPanel.cpp`
- [ ] tests（tests/CMakeLists.txt）：`../src/dsp/effects/EffectParamRegistry.cpp`、`../src/gui/EffectParamPanel.cpp`

### Task 6: 测试

**Files:**
- Create: `tests/test_effect_params.cpp`
- Modify: `tests/test_ui_paint.cpp`（+1 用例）

- [ ] test_effect_params.cpp（新）：8 类型 × round-trip（create→n>0→逐参 set(mid)→get 一致→getState→新实例 setState→get 保持）+ 1 用例工厂完整性（getTypeNames()==14、create 全非空）。预期 +9 用例。
- [ ] test_ui_paint.cpp：EffectParamPanel headless paint（Delay 实例，空/满 paint 不崩）。预期 +1。

### Task 7: 自审 + CI

- [ ] 自审：Registry/Panel/新测试文件 grep 确认**零 PluginProcessor.h include**；表值 vs setter clamp 一致；resized 数学（展开/折叠）纸面推演；undo 动作不感知 expanded。
- [ ] commit + push + Run 轮询 + 取证（预期用例数 = 541 + 10 = **551**）。
- [ ] 绿 → artifacts 覆盖 `artifacts/`、UAC 重装 VST3、清 REAPER 缓存；strictness 2→3 合并推（单独一档变更）。

### Task 8: 文档

- [ ] HANDOFF §0.1 加 Run 行；路线图 P2 标完成；记忆更新。

## Self-Review

1. Spec 覆盖：接口(T1)/getter(T2)/注册表+表(T3)/UI+展开(T4)/构建(T5)/测试(T6) ✓；非目标未混入 ✓。
2. Placeholder：无 TBD；全部代码骨架与表值已给；Saturation 成员名以 grep 为准是**执行步骤**而非占位。
3. 类型一致：EffectParamSpec 字段 T1 定义、T3/T4 使用一致；Registry API 名与 T3 头一致。
