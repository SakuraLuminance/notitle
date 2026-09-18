# 枚举参数真菜单（P2 遗留）设计 — 2026-09-18

> 用户已确认范围：**仅 UI 真菜单（选项 A）**——把 rack 里的枚举参数从"旋钮 + tooltip"换成真下拉菜单，**不新增宿主可自动化的 choice 参数**，不动参数体系与状态格式。

## 1. 现状

FX 页 rack 的每个效果都把参数画成旋钮（`EffectParamPanel`）。枚举参数（失真 TYPE、环形调制 WAVEFORM、饱和 MODE、延迟 PING-PONG、某效果的 ON）只是"整数步进的旋钮 + tooltip 里写 0=.. 1=.."。用户看到的是个可以停在"2.7"的旋钮，实际只有 3~5 个合法取值。

## 2. 判据（哪些参数变菜单）

| 条件 | 结果 |
|---|---|
| `values != nullptr && isInt == true` | **真菜单**（`EffectParamSpec::isChoice()`） |
| `values != nullptr && isInt == false` | 仍是旋钮（`WIDTH` 的 `"0=MONO 0.5=100% 1=200%"` 是连续量锚点，不是枚举） |
| `values == nullptr` | 仍是旋钮（`BITS`/`DOWNSAMPLE`/`STAGES` 等纯数量） |

本轮给两个 0/1 参数补上标签（它们是布尔选择，不是数量）：延迟 `ping`、某效果 `enabled` → `"0=OFF 1=ON"`。

## 3. 标签解析

`EffectParamSpec::getChoiceLabels()`（`EffectsChain.cpp`，测试目标里也有它）：

| 输入 | 输出 |
|---|---|
| `"0=SOFT 1=HARD 2=TUBE"` | `{SOFT, HARD, TUBE}` |
| `"2=SQUARE 0=SINE 1=TRI"` | `{SINE, TRI, SQUARE}`（**按索引排序**，与书写顺序无关） |
| `nullptr` / `""` / `"garbage"` / `"=SINE 0="` | `{}`（畸形项跳过，不产生空菜单项） |

## 4. UI 与布局

| 项 | 决定 |
|---|---|
| 菜单行 | 每个枚举参数独占一行：`menuRowH = 20`，左侧标签 `menuLabelW = 72`，右侧下拉占满剩余宽度 |
| 旋钮 | 其余参数照旧 46×58 网格，排在菜单行**下方** |
| 高度 | `getPreferredHeight(width) = menus * 20 + knobRows * 58 + 6`（rack 用它给 slot 留高度，菜单行不会被裁掉） |
| tooltip | 菜单：`LABEL\n0=.. 1=..`；旋钮：原样再加 `Right-click: MIDI Learn` |
| 写值 | `onChange` → `effect->setParamValue(paramIndex, selectedId - 1)`（**菜单 id = 参数值 + 1**） |

### 索引安全（本轮修掉的隐患）

面板里现在有两种控件，**旋钮顺序不再等于参数顺序**。因此新增 `knobParamIndex_` / `menuParamIndex_` 两张表：

- 旋钮的 `onValueChange` 改用**自己的指针**取值（不再用参数索引去索引 `knobs_`）；
- `visitKnobs` 回调给编辑器的是**参数索引**，MIDI Learn 的 `fx{slot}_p{n}` 语义不变。

## 5. 同步方向

菜单值可能被别的路径改掉（预设载入、状态恢复、MIDI Learn 映射写入）。编辑器 timer 里 `refreshEffectMenus()` 从效果值**回写**菜单选中项（`dontSendNotification`，绝不反向写回效果）。菜单本身不参与 MIDI Learn（没有旋钮可映射）；被映射的是同一个参数时，菜单会跟着动。

## 6. 不做

- 不新增 `AudioProcessorParameter`（宿主自动化/名称显示）——本轮范围外；
- 不给 0/1 参数做 ToggleButton（保持"枚举=菜单"一条规则，少一种控件形态）；
- 不改 `EffectBase` 的参数接口（只用既有的 `EffectParamSpec`）。

## 7. 测试

`tests/test_effect_param_menus.cpp`（新增）：

1. 解析：顺序、乱序、连续锚点（`isChoice()==false` 但标签可解析）、`nullptr`、畸形串。
2. 注册表不变式：遍历 14 种效果，每个 `isChoice()` 参数必须 ① 标签 ≥2 项 ② 项数 == `max+1` ③ 每一项 `setParamValue(c)` → `getParamValue()` 都能原值回读 ④ 无空标签/重复标签。
3. 面板：`RingModulator` → 菜单数 = 枚举参数数、旋钮数 = 其余参数数、菜单项文本 `SINE/TRI/SQUARE`、`setSelectedId(3)` 写回参数值 2、`getPreferredHeight()` 留出菜单行高度。
