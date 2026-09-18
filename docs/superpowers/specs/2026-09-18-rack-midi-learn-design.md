# rack 内 MIDI Learn（效果器参数可学习）设计

日期：2026-09-18 · 状态：实现中（本批一次推一轮验证）· 归属：P2 遗留项（HANDOFF_V3 §6.2 已定设计）

## 问题

`MidiLearn` 的映射目标只有 `std::atomic<float>*`。静态滑条（timbre/filter/macro/master 等）背后都有处理器侧原子，所以能学；
**rack 里的效果器参数**走 `EffectBase::get/setParamValue`（普通 float 成员），没有原子可指，于是 20 个效果器的专属旋钮目前**完全无法 MIDI Learn**。

## 决策

| 决策点 | 结论 |
|---|---|
| 目标抽象 | `MidiMapping` 增加 `std::function<void(float)> targetSetter` / `std::function<float()> targetGetter`；**`targetParam != nullptr` 时原子优先**，既有路径行为零变化 |
| 回调是否持久化 | 否（与 `targetParam` 一样是运行期指针/闭包），状态仍只存 cc/paramId/min/max/global；加载后由编辑器重连 |
| 槽位身份 | setter 捕获 `processor*` + 槽位下标 + 参数下标，**调用时按下标解析**（先判 `getNumEffects()` 边界），因此不持有 `EffectBase*`，效果器被删/重排不会悬垂 |
| 参数 id | `fx{slot}_p{index}`（与既有 `macro_*`/`seq_*` 风格一致） |
| 注册时机 | 编辑器 timer 每 tick 扫描 rack 的**已展开**面板旋钮：新增则注册、消失（折叠/重建/删除）则注销；旋钮由消息线程创建销毁，无跨线程指针 |
| 实时安全 | 音频线程只做 `std::function` 调用 + 一次 `EffectBase::setParamValue`（同为音频线程上下文的普通 float 写），无分配；捕获对象为指针+整数，不触发堆分配 |
| 测试目标限制 | 回调 lambda 定义在 `PluginEditor.cpp`（**不入测试目标**）；`MidiLearn.{h,cpp}` 保持只依赖 JUCE 基础模块，单测直接测回调分支 |

## 改动清单

| 文件 | 改动 |
|---|---|
| `src/dsp/MidiLearn.{h,cpp}` | `MidiMapping` 加 setter/getter；`addMapping/startLearn/reconnectTarget` 各加"回调目标"重载；写值统一走私有 `applyMappingValue()`；新增 `getMappingValue(paramId, out)` 供 UI 轮询 |
| `src/gui/EffectParamPanel.{h,cpp}` | 暴露 `getEffect()`、`visitKnobs(fn)`、`getNumKnobs()`；旋钮 tooltip 追加 "Right-click: MIDI Learn" |
| `src/gui/EffectRackComponent.{h,cpp}` | `EffectSlotWidget::getParamPanel()`；`EffectRackComponent::visitSlots(fn)` |
| `src/PluginEditor.{h,cpp}` | `MidiLearnSliderInfo` 加 setter/getter；`setupMidiLearnForEffectKnob()`；`refreshEffectKnobMidiLearn()`（在 `updateMidiLearnState()` 开头调用）；右键菜单与轮询支持回调目标 |
| `tests/test_midi_learn.cpp` | 8 个回调目标用例（映射/范围/优先级/learn/重连/持久化后空目标安全/getMappingValue） |

## 验收

- rack 内任意效果器旋钮右键 → MIDI Learn → 动硬件旋钮 → 参数跟随，且旋钮位置同步刷新。
- 折叠面板 / 删除槽位 / 重排槽位后不再有任何对已销毁旋钮的引用（无崩溃、无残留监听）。
- 既有 595+ 用例全绿 + pluginval strictness 5（原子目标路径行为不变）。
