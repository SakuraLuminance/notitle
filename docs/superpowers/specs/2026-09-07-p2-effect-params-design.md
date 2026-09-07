# P2 — 效果器栏参数编辑（Phase Plant 式展开）设计

日期：2026-09-07
状态：已批准（用户确认"批准但标签用英文"）
归属：路线图 P2（docs/superpowers/plans/2026-09-07-p2-p6-roadmap.md）

## 背景

EffectRackComponent 已是 Phase Plant 式骨架（14 类型注册表、加/删/上下移/撤销、bypass/mix/wetLowCut/wetHighCut），但**效果器自身的参数完全没有 UI**——用户"不知道怎么修改参数"。侦察结论：

- 每个 EffectBase 槽位只有通用 4 旋钮；`modSlider_` 是纯视觉死控件。
- 14 个效果类都有干净的 setter 面，但 EffectBase 接口（prepare/process/reset/getState/setState）没有通用参数通道。
- **架构约束（Run #103-105 clap 陷阱）**：EffectRackComponent.h include PluginProcessor.h → 永不进测试目标；参数注册表必须独立成 DSP 侧文件且只依赖 EffectsChain.h（EffectsChain.cpp 本就在测试目标，test-safe）。
- 单位已实锤：Delay=ms/%，Reverb=0-1，Chorus=Hz/%/ms，Compressor=db/倍数/ms，Distortion=0-100/0-200，Limiter=db/ms，Bitcrusher=1-16/1-32，Saturation=%/Hz。

## 目标

点槽位 ▾ 展开**参数编辑行**（横向 mini-knob + 英文标签），拖动即时生效；折叠回 30px 紧凑行。第一批 8 类型，其余 6 类型无参数 UI（显示 "No editable params"）。

## 设计

### 1. EffectBase 通用参数接口（src/dsp/EffectsChain.h）

```cpp
struct EffectParamSpec {
    const char* id;      // "delay_ms"
    const char* label;   // "TIME"（英文）
    float min, max, def;
    float skew;          // <1 = 低值更细；1 = 线性
    bool  isInt;         // 整型滑条（步进 1）
};

class EffectBase {
    ...现有...
    virtual int getNumParams() const                        { return 0; }
    virtual const EffectParamSpec& getParamSpec(int) const  { static EffectParamSpec none{}; return none; }
    virtual float getParamValue(int) const                  { return 0.0f; }
    virtual void  setParamValue(int, float)                 {}
};
```

默认空实现 → 未覆盖的效果类型自动"无参数"，零破坏。

### 2. EffectParamRegistry（src/dsp/effects/EffectParamRegistry.{h,cpp}）

- **适配器统一化**：现有 createEffectByName 里 6 个内联 Adapter（Delay/Reverb/EQ/Chorus/Distortion/AutoTune）**移入本文件**，与 8 个直接 EffectBase 子类（Bitcrusher 等）的适配器并列；每个适配器持 `std::vector<EffectParamSpec> params_` 并把 index→setter 映射（含单位换算，如 UI 0-100 → setFeedback 内部 /100）。
- **getter 补齐**：round-trip 需要 getParamValue 反读真实状态——8 个首选效果类补齐缺失的 inline getter（Delay 4、Reverb 5、Chorus 5、Compressor 6、Distortion 4、Limiter 5、Saturation 4；Bitcrusher 已有 3），纯头文件内联、镜像成员、零行为变化。
- 公开 API（namespace ana）：
  - `int EffectParamRegistry::getNumTypes()`
  - `const juce::StringArray& EffectParamRegistry::getTypeNames()`
  - `std::unique_ptr<EffectBase> EffectParamRegistry::create(const juce::String& typeName)`
  - `const juce::String& EffectParamRegistry::getDisplayName(typeName)`（首版 = 类型名本身）
- EffectSlotWidget 的两个静态方法（getAvailableEffectTypes/createEffectByName）**保留签名**、内部委托到 Registry（零调用点改动；undo 动作照旧工作）。

### 3. 第一批参数表（8 类型，英文标签，范围=侦察实锤值）

| 类型 | 参数（id / label / range / def / skew） |
|---|---|
| Delay | delay_ms "TIME" 1-2000/300/0.25 · feedback "FEEDBACK" 0-100/45 · mix "MIX" 0-100/30 · ping "PING-PONG" 0-1/0 int |
| Reverb | room "ROOM SIZE" 0-1/0.5 · damp "DAMPING" 0-1/0.5 · wet "WET" 0-1/0.33 · dry "DRY" 0-1/0.4 · width "WIDTH" 0-1/1 |
| Chorus | rate "RATE" 0.05-10/1 skew0.3 · depth "DEPTH" 0-100/50 · cdelay "DELAY" 0-100/20 · fb "FEEDBACK" 0-99/25 · mix "MIX" 0-100/50 |
| Compressor | thresh "THRESHOLD" -60-0/-20 · ratio "RATIO" 1-20/4 · attack "ATTACK" 0.1-100/10 skew0.3 · release "RELEASE" 10-1000/200 skew0.3 · knee "KNEE" 0-10/3 · makeup "MAKEUP" 0-24/0 |
| Distortion | drive "DRIVE" 0-100/40 · range "RANGE" 0-100/50 · blend "BLEND" 0-100/50 · volume "LEVEL" 0-200/100 |
| Limiter | thresh "THRESHOLD" -30-0/-3 · attack "ATTACK" 0.01-10/1 skew0.3 · release "RELEASE" 1-100/60 · lookahead "LOOKAHEAD" 0-10/2 · mix "MIX" 0-1/1 |
| Bitcrusher | bits "BIT DEPTH" 1-16/8 int · downsample "DOWNSAMPLE" 1-32/1 int · mix "MIX" 0-1/1 |
| Saturation | drive "DRIVE" 0-100/30 · tone "TONE" 20-20000/8000 skew0.3 · mix "MIX" 0-100/100 · gain "GAIN" 0-4/1 |

（默认值对齐各 setXxx 的现网默认/测试期望；实现时若发现 setter 内部 clamp 与表不一致，以 setter clamp 为准反向修表。）

### 4. UI：EffectParamPanel + rack 展开

- `EffectParamPanel : juce::Component`（src/gui/EffectParamPanel.{h,cpp}）：ctor(`EffectBase* liveEffect`)；从 liveEffect 读 getNumParams/getParamSpec/getParamValue 建 knob 行（juce::Slider RotaryVerticalDrag + 9px 英文 label）；onValueChange → `liveEffect->setParamValue(i, v)`（直接写 DSP，与现有 mix 通道同模式）。每 knob 宽 46px、行高 58px；行数=ceil(n / max(1, width/46))。
- `EffectSlotWidget`：**移除死控件 modSlider_**，其位置放展开按钮（▾/▸）；新增 `bool expanded_`；展开时槽高 = 30(头行) + panel 高度，EffectParamPanel 置于头行下方。EffectRackComponent::resized 按各槽 expanded 状态累计高度。
- 生命周期：panel 持 EffectBase* 指向链内活实例；rebuildSlots（加/删/移/撤销）时**展开状态重置**（最简；记入已知限制）。
- Rack 的 add/reorder/remove/undo 路径不动（继续 EffectBase*）。

### 5. 非目标（首版）

- MIDI Learn 注册（rack 内 slider 接编辑器 MIDI Learn 属 P3+ 事项）
- 枚举参数的菜单控件（Distortion type、RingMod waveform、Saturation mode 等首版不暴露或用 int 滑条——首批表未含枚举项）
- EQ(9 参数)/AutoTune/Flanger/Phaser/RingMod/StereoWidener 的参数表（第二批）
- 参数面板的定时回读（预设加载后的 rack 重建本就重建 panel；不另做 polling）

## 测试

1. **tests/test_effect_params.cpp（新文件）**：8 类型 × round-trip：`EffectParamRegistry::create(type)` → `getNumParams() > 0` → 对每个参数 `setParamValue(mid)` → `getParamValue()` 一致 → `getState()` → 新实例 `setState()` → `getParamValue()` 保持。另 1 用例：14 类型工厂表完整（getTypeNames == 14 且 create 全部非空）。
2. **test_ui_paint.cpp 追加**：EffectParamPanel headless paint（用 Registry 创建的 Delay 实例，空/满数据 paint 不崩）。
3. 既有 541 用例 0 失败 + pluginval strictness 2 不回归。

## 验收

加 Delay → ▾ 展开 → 拖 TIME/FEEDBACK 立即可闻生效 → 折叠 → 上/下移/删除/撤销全部不崩且参数保持；preset round-trip 系列仍绿。

## 风险

- Reverb 的 0-1 参数若 setter 语义与预期不符（值域 0-1 有限定）→ 以 setter clamp 为准修表（首轮 CI 取证兜底）。
- getState/setState round-trip 若某类型丢参数 → 该类型 round-trip 测试会红，按 forensics 定位（已知 AutoTune clamp 到 [0,20] 家族语义，与 §5B 一致）。
- rack 高度动态化的 resized 数学（展开/折叠切换）需仔细；viewport 滚动已具备。
