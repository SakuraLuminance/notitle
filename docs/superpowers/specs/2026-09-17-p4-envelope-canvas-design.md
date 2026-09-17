# P4 — ENV 页 + 可编辑包络画布 设计

日期：2026-09-17
状态：设计已定（用户逐条确认 7 个决策点后批准）
归属：路线图 P4（前置：P1 ✓ P2 ✓ P3 ✓ P6 ✓，见 `docs/superpowers/plans/2026-09-07-p2-p6-roadmap.md`）
参考系：Serum（可拖拽多点包络 + sustain 标记 + 可见曲线）、Phase Plant（包络作为独立模块、参数清晰）、Harmor（包络与调制同屏观念）

## 背景与问题

1. **引擎完整、UI 缺失**：`ana::MultiPointEnvelope`（`src/dsp/MultiPointEnvelope.{h,cpp}`）已支持 ≤32 断点（time/value/curve）、3 种曲线（Linear/Exponential/SCurve）、4 种 loop 模式（None/Forward/PingPong/Sustain）、tempo sync，且有 `test_multi_point_envelope.cpp` 覆盖；但 UI 里**没有任何包络画布**。
2. **两组包络**：`PluginProcessor` 持有 `envPool_[3]`（= 调制源 ENV1/ENV2/ENV3，note-on 时全部 `trigger()`，值进 flat 调制槽）与独立的 `volumeAdsr_`（VCA 音量包络，Sustain 模式、note-off `release()`）。前者经 `ModSource::ENV1..ENV3` 消费，后者在 `PluginProcessor.cpp:898/:934` 乘到输出。
3. **唯一的编辑入口是 4 个滑条**：`ModulationAssignPanel`（MOD 页）顶部固定 4 个 Volume ADSR 旋转滑条；ENV1/ENV2 仅作为指派 source 下拉的选项存在（ENV3 连下拉都没有）。
4. **持久化只存 ADSR 标量**：`PresetManager::serialiseENVConfig` / `serialiseVolumeADSR` 只写 attack/decay/sustain/release，**不存断点列表**；读取时 `setAttack…` 会 `rebuildADSR()` 覆盖断点。因此画布一旦允许任意断点，必须扩展持久化。
5. **引擎 loop 语义偏差**：`advanceEnvelope()` 中 Forward 的回卷区间是 `[loopStart, 末尾断点]`（`wrapRange = totalEndSec - loopStartSec`），PingPong 也在 totalEnd/loopStart 折返；`loopEnd` 仅在 Sustain 模式当 HOLD 用，其余模式只当“是否配置了 loop”的开关。UI 上“loop 终点”对 Forward/PingPong 实际不生效。
6. **并发隐患（既有）**：`envPool_`/`volumeAdsr_` 的断点是 `std::vector<Breakpoint>`，音频线程 `process()` 遍历它，而消息线程的滑条回调 `setAttack…` → `rebuildADSR()`（clear + 4× push_back）会同时改它——现在就有数据竞争。画布会让编辑更频繁，必须先解决。

## 决策记录（用户逐条确认）

| 决策点 | 结论 |
|---|---|
| 第一批覆盖范围 | **Volume ADSR 画布 + ENV1/ENV2**（ENV3 在 Round 2 搭车） |
| 画布与滑条关系 | **并存 + 双向同步**（滑条改动 → `rebuildADSR`；拖画布点 → 反向更新滑条显示派生 A/D/S/R） |
| 断点持久化 | **新增断点列表 + 保留旧 ADSR 标量兼容**（有 points 用 points，否则用标量重建） |
| sustain / release 语义 | **显式 HOLD 标记 + 自由 release 段**（HOLD = `loopEnd`；按住停在此处，note-off 后从它继续走完剩余断点） |
| 画布位置 | **新增第 7 个页签 ENV**（MOD 页不动） |
| 第一批功能面 | **全套**：增删拖点 / 3 曲线 / HOLD / loop 模式 + 可拖 loop 起止 / tempo sync（宿主 BPM）/ 复制·清空·重置 / 悬停读数；顺带把 ENV3 加进槽位与指派下拉 |
| Forward/PingPong 边界 | **修引擎**：改为在 `[loopStart, loopEnd]` 内折返，未配置 loopEnd 仍是 play-once |

## 目标形态

ENV 页左侧是槽位列表 `[VOL][ENV1][ENV2][ENV3]`，右侧是一块大画布：

- 拖动断点改时间/值；双击空白加点；右键删点或改该段曲线；端点与邻居之间有夹取，不会交叉。
- 一条可拖的 **HOLD** 标记（= `loopEnd`）：Sustain 模式下按住键停在此处，note-off 后从它继续走完剩余断点（HOLD 之后的形状就是 Release）。
- 一条可拖的 **LOOP START** 标记（= `loopStart`）；Forward/PingPong 时循环区间为 `[LOOP START, HOLD]`。
- 参数行：loop 模式下拉、sync 开关、宿主 BPM 与 beat division、A/D/S/R 数值读数。
- 画布上有播放头/当前值指示（由 processor 原子发布，音频线程不阻塞）。
- MOD 页那 4 个 Volume ADSR 滑条仍然可用，与 VOL 画布双向同步。

## 设计

### 1. 引擎补充（`MultiPointEnvelope`）

**1a. loop 语义修正**（`src/dsp/MultiPointEnvelope.cpp::advanceEnvelope`）
- Forward：到达 `loopEndSec` 时回卷到 `loopStartSec`；`loopEndIndex < 0`（未配置）时保持 play-once（`handleEnvelopeEnd()`）。回卷区间 `loopEndSec - loopStartSec`，为 0 时停在 loopStart。
- PingPong：在 `loopEndSec` 反向、在 `loopStartSec` 再反向（把现有用 `totalEndSec` 的两处边界换成 `loopEndSec`）。
- Sustain 行为不变：未 release 时停在 `loopEndSec`；release 后继续向 totalEnd 前进并结束。
- 更新 `tests/test_multi_point_envelope.cpp` 中依赖旧回卷点的用例，并新增“Forward/PingPong 在 loopEnd 折返”的显式用例。

**1b. 新增公开 API**

```cpp
void  setBreakpointCurve(int index, CurveType curve);   // 现只能在 addBreakpoint 时指定
double getTimeInSeconds(float breakpointTime) const;    // 把现有 private timeToSeconds 转公开（画布/派生用）
double getTimePositionSeconds() const noexcept;         // 播放头位置（仅测试/持锁路径读；UI 走原子）
```
`getBreakpoints()` 只读访问已由 `getNumBreakpoints()` + `getBreakpoint(i)` 满足，不新增。

**1c. 顺带修正**
- `setRelease` 目前夹 `0..10`，而 `deserialiseVolumeADSR` 允许 `0..30`：统一上限（取 30），避免反序列化后又被二次夹断。

### 2. 并发安全（`PluginProcessor`）

- 新增 `juce::SpinLock envLock_` 与访问器 `juce::SpinLock& getEnvelopeLock()`。
- 音频线程：`processBlock` 中推进 `envPool_[i].process()` / `volumeAdsr_.process()` 的整段持锁（每 block 一次，4 个包络，锁窗口极短）。
- 消息线程：所有断点/loop/sync/ADSR 编辑与 `PresetManager` 的 ENV/VolumeADSR 反序列化都持锁。为此 `PresetManager` 新增 `setEnvLockRef(juce::SpinLock*)`，构造时以 `&envLock_` 注入；未注入时（旧测试路径）退化为无锁，行为不变。
- 同时把这把锁用在既有滑条回调路径上（修掉 5.6 的隐患）。

### 3. `ana::EnvelopeEditOps`（新，纯函数，可单测）

放在 `src/dsp/EnvelopeEditOps.{h,cpp}`，不依赖任何 `Component`（只用 `juce::Rectangle<float>`/`Point` 做换算，测试目标已链接 juce_graphics）。

```cpp
namespace ana::EnvelopeEditOps {

struct DerivedADSR { float attack = 0.0f, decay = 0.0f, sustain = 0.0f, release = 0.0f; };

bool addPoint   (MultiPointEnvelope&, float time, float value, CurveType);
bool removePoint(MultiPointEnvelope&, int index);            // 保底至少 2 个点
bool movePoint  (MultiPointEnvelope&, int index, float time, float value); // 时间夹在邻居之间
DerivedADSR deriveADSR(const MultiPointEnvelope&);           // 见下

float timeToX(const MultiPointEnvelope&, float time, juce::Rectangle<float> area);
float xToTime(const MultiPointEnvelope&, float x,    juce::Rectangle<float> area);
float valueToY(float value, juce::Rectangle<float> area);
float yToValue(float y,     juce::Rectangle<float> area);
int   hitTestPoint (const MultiPointEnvelope&, juce::Point<float>, juce::Rectangle<float>, float radiusPx);
int   hitTestMarker(const MultiPointEnvelope&, juce::Point<float>, juce::Rectangle<float>, float radiusPx); // -1 无, 0=loopStart, 1=loopEnd/HOLD
}
```

**派生 ADSR 映射**（用于画布 → 滑条反向同步，时间统一换算成秒）：
- `holdIndex` = `loopEndIndex >= 0 ? loopEndIndex : n-1`
- `peakIndex` = `[0, holdIndex]` 中 value 最大者
- `attack = t(peak) - t(0)`；`decay = t(hold) - t(peak)`；`sustain = value(hold)`；`release = t(n-1) - t(hold)`
- 4 点 ADSR 形状下该映射精确还原滑条值。

**反向（滑条 → 引擎）**：沿用现有 `setAttack/Decay/Sustain/Release` → `rebuildADSR()`（会清空自由断点，符合已批准的“最后写入者胜”语义）。

### 4. `ana::EnvelopeCanvas`（新，`src/gui/EnvelopeCanvas.{h,cpp}`）

- 绑定 `ana::MultiPointEnvelope* env` + 回调 `std::function<void()> onEdited`、`std::function<void()> onLayoutChanged`。
- 交互：拖断点 / 双击加点 / 右键菜单（删点、Linear|Exponential|SCurve、复制、清空、重置 ADSR）/ 拖 HOLD 与 LOOP START 标记。
- 视觉：网格、曲线段（按 `Breakpoint::curve` 渲染段末曲线）、断点圆点、HOLD/LOOP START 竖线、条内 A/D/S/R 小读数、悬停坐标、播放头（原子）。
- x 轴：sync 关 → 秒（范围 = `max(末点时间, 1.0)` 加 10% 余量）；sync 开 → beats（label 同步切换）。
- 所有编辑经 `EnvelopeEditOps`（夹取/约束集中在一处，便于测试）。
- headless `paint()` 必须安全（测试会直接调 `paint`）。

### 5. `ana::EnvelopePage`（新，`src/gui/EnvelopePage.{h,cpp}`）+ 第 7 页

- 容器：左侧 slot 列表（`VOL/ENV1/ENV2/ENV3`），右侧 `EnvelopeCanvas` + 参数行（loop 模式下拉、sync 开关、BPM/beat division、A/D/S/R 读数、`RESET ADSR` 按钮）。
- `PageTabs` 页名数组 6→7（追加 `"ENV"`）；`PluginEditor::setActivePage` 上限 5→6，并加 ENV 页成员可见性切换与 `resized()` 分支。
- ENV1/ENV2/ENV3 槽位在 Round 1 只开放 VOL/ENV1/ENV2；ENV3 的槽位与指派 source 下拉项在 Round 2 一起加（避免出现能画却无法指派的槽）。

### 6. Processor 接线

- 暴露引用：`ana::MultiPointEnvelope& getEnvelopeSlot(int slot)`（0=VOL→`volumeAdsr_`，1..3=ENV1..3→`envPool_[slot-1]`）。
- 初始化补齐：`envPool_[1]`、`envPool_[2]` 目前**没有默认断点**（只有 `envPool_[0]` 在构造时 `rebuildADSR()`）；三者在构造时统一 `prepare(44100)` + `rebuildADSR()`，否则 ENV2/ENV3 画布是空的、调制源恒为 0。
- tempo sync：`processBlock` 已从宿主取 BPM（`PluginProcessor.cpp:519-524`，目前只喂 `stepSequencer_`）→ 同时 `setTempo(bpm)` 到 4 个包络。
- UI 原子发布（每 block，持 `envLock_`）：`envUITime_[4]`、`envUIValue_[4]`（秒 / 0..1）；VOL 的 value 已有 `volumeAdsrValue_`，统一走新数组以免两套。
- 一个读取器：`int getUiSlotFor(...)` 不需要；画布按 slot 索引直接读 `envUITime_[slot]`。

### 7. 数据流

```
画布拖拽 ──> EnvelopeEditOps(夹取) ──> envLock_ 内改 MultiPointEnvelope
                                              │ (音频线程，持锁)
                                              ├─ envValues_[0..2] ──> flat 调制槽 (depth)
                                              └─ volumeAdsrValue_ ──> VCA 乘子
双向同步：timer 读 deriveADSR() ──> 刷 MOD 页 4 滑条 (dontSendNotification)
          滑条 onValueChange ──> setVolumeAttack… ──> rebuildADSR() ──> 画布重绘
tempo sync：processBlock 宿主 BPM ──> setTempo() ──> x 轴显示 beats
```

### 8. 持久化（`PresetManager`）

- `ENVConfig/ENV` 与 `VolumeADSR` 树新增：
  - `points`：每点一个子节点 `P`，属性 `time`、`value`、`curve`（int 枚举）；
  - `loopMode`、`loopStart`、`loopEnd`；
  - `sync`、`tempo`、`beatDiv`。
- **继续双写** `attack/decay/sustain/release`（= `deriveADSR` 的结果）。
- 反序列化：若 `points` 存在且 ≥2，则按断点重建并恢复 loop/sync；否则走原 ADSR 路径。旧预设 → 完全兼容。
- 反序列化在 `envLock_` 内执行。

## 测试计划

| 层 | 内容 |
|---|---|
| 引擎 | Forward/PingPong 在 `loopEnd` 折返（含未配置 = play-once）；Sustain HOLD + release 续走不变；`setBreakpointCurve`；`getTimeInSeconds` sync 换算；`getTimePositionSeconds`。 |
| EditOps | 加点/删点（保底 2 点）/移动夹取（不越邻居、不越 0..1）；`deriveADSR` 对 4 点 ADSR 精确还原、对自由形状单调合理；像素↔时间/值双向换算；hitTest。 |
| UI | `EnvelopeCanvas.paint`、`EnvelopePage.paint`、`resized()` 无数据/满断点/各 loop 模式/ sync 开关下不崩；程序化编辑序列（加点→拖→删→重置）后状态自洽。 |
| 持久化 | 断点+loop+sync round-trip 一致；ADSR-only 旧树加载后断点为 4 点标准形状且标量一致。 |
| 回归 | 现有 570 用例全绿；pluginval strictness 5。 |

**测试禁忌**：被测试目标编译的文件严禁 `#include PluginProcessor.h`（clap 陷阱）；`EnvelopePage` 若需 processor 则只测 `EnvelopeCanvas` + EditOps（processor 相关留在 pluginval/集成层）。

## 分批（2 轮 CI）

**Round 1 — 画布可用**
1. `MultiPointEnvelope`：loop 折返修正 + `setBreakpointCurve` + `getTimeInSeconds`（公开）+ `getTimePositionSeconds` + `setRelease` 上限统一；更新/新增单测。
2. `EnvelopeEditOps` + 单测。
3. `EnvelopeCanvas`（完整交互与渲染）+ headless 测试。
4. `EnvelopePage` + `PageTabs` 7 页 + `PluginEditor` 接线（VOL/ENV1/ENV2）。
5. `envLock_` 并发锁；`envPool_[1]/[2]` 默认断点补齐；宿主 BPM → `setTempo`；UI 原子发布。
6. MOD 页滑条 ↔ VOL 画布双向同步。

**Round 2 — 完整与持久**
1. 断点/loop/sync 持久化 + 旧预设兼容 + round-trip 测试。
2. ENV3 槽位 + 指派 source 下拉项。
3. 参数行收尾（BPM/beat division 显示、悬停读数、复制/清空/重置按钮）。
4. 顺手：roadmap 标记 P4 完成。

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 改 `advanceEnvelope` 影响既有 envelope 行为 | 修正只在 `loopConfigured` 且 Forward/PingPong 时改变回卷边界；先改单测再改实现，Sustain/None 路径零改动。 |
| 加锁拖慢音频线程 | `process()` 每 block 一次、锁内只有 4 个包络的数值推进；编辑与音频锁窗口互斥概率极低。 |
| 画布 ↔ 滑条同步抖动（互相触发） | 一律 `dontSendNotification` 写控件；只在差值 > 1e-3 时写。 |
| 7 页签布局挤压 | ENV 页只占 content 区；`PageTabs` 已按页名等分，第 7 个名字宽度足够（“ENV” 3 字符）。 |
| 断点持久化破坏旧 preset | 双写 + “有 points 才用 points” + 旧树 round-trip 测试。 |

## 有意不做（YAGNI）

- LFO 波形画布（路线图列为可选；P4 专注包络）。
- 撤销/重做栈（未要求；先靠右键重置 + 预设）。
- 包络段拖拽整体平移、多选、复制粘贴单点。
- ENV3 之外的额外槽位（需同时扩 `ModulationEngine`/`ModSource` 枚举与 UI，单独评估）。
