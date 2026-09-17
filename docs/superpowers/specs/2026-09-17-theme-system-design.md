# 视觉主题系统（多主题 / visual kei 风格线）设计

日期：2026-09-17 · 状态：已实现（未推送，随本地批次一起验证）

## 目标

在保留现有 Cyberpunk 视觉语言的前提下，提供**多套可切换主题**，每套对应一种完整视觉风格；切换即时生效，不需要重建 UI，并随插件状态持久化。

## 架构

| 部分 | 说明 |
|---|---|
| `src/gui/ThemePalettes.h`（新，header-only） | `ThemePalette { name, bg, fg, accent, accent2, highlight, monoBody }` + 7 套内置调色板 + `count/get/name()` |
| `CyberpunkTheme` | 新增 `applyPalette(int)`（写入 5 个颜色槽 + `monoBody_` + 重新注册 LookAndFeel 颜色）、`getThemeIndex()`、`isMonoBody()`、`remapComponentColours(Component&, oldPalette)`、`matchSlot()` |
| 颜色槽映射 | `bg_`←bg、`fg_`←fg、`cyan_`←accent、`magenta_`←accent2、`yellow_`←highlight —— **沿用历史命名，因此 200+ 处既有调用点零改动** |
| 字体 | `getCyberFont(height, bold)`：`bold || monoBody_` → 等宽（CRT/MONO 主题正文也用等宽） |
| 实时切换 | `remapComponentColours` 递归遍历组件树，把「旧调色板槽位及其 darker/brighter 派生色」改写为新槽位（保持原 alpha）；自绘组件（画布/粒子/包络/图像）在 paint 时读槽位，自动跟随 |
| 持久化 | `PluginProcessor::themeIndex_`（0..7 夹取）随 `get/setStateInformation` 存取；编辑器构造末尾应用 |
| UI | 顶栏 `themeCombo_`（NEON/VAPOR/CRT/ICE/MONO/TOXIC/SAKURA） |

## 调色板

| # | 名称 | bg | fg | accent | accent2 | highlight | 正文等宽 |
|---|---|---|---|---|---|---|---|
| 0 | NEON（默认，= 原值，颜色测试依赖） | 0a0a05 | d0e0c8 | 00ccff | ff00ff | 39ff14 | 否 |
| 1 | VAPOR | 0d0620 | ede4ff | ff2fb9 | 00e5ff | b47cff | 否 |
| 2 | CRT | 0f0b06 | ffd9a0 | ffb000 | ff6a00 | 2ee6c6 | 是 |
| 3 | ICE | 05080f | dce8f5 | 6ec6ff | b388ff | 7dffd4 | 否 |
| 4 | MONO | 0b0b0c | e6e6e6 | ffffff | 9a9a9a | d4d4d4 | 是 |
| 5 | TOXIC | 070b04 | d8f0c0 | a8ff00 | 00ffa3 | ffe600 | 否 |
| 6 | SAKURA | 120a12 | ffe6f2 | ff7ab6 | c77dff | 7be0ff | 否 |

## 语义约定（所有主题共享）

- `accent` = 可交互（按钮/滑块/弧线/焦点）
- `accent2` = 次强调（HOLD 标记、选中态、tooltip 描边）
- `highlight` = 数值/幅度（包络断点、图像热力、粒子）
- `fg_` = 文本；数值读数用 `fg_` 72% 不抢强调色
- 背景层级：`bg_` → `bg_.brighter(0.1~0.2)`（控件底）→ 画布 `bg_.darker(0.25)`

## 新增主题

1. 在 `ThemePalettes.h` 的数组里追加一行（`count` 自动跟随，`themeCombo_` 自动列出）。
2. 若要改调色板数量上限，检查 `PluginProcessor::setThemeIndex` 的夹取范围（当前 0..7）。
3. 无需改动任何面板代码。

## 测试

`tests/test_theme_palettes.cpp`：调色板唯一性与默认值、索引夹取、`applyPalette` 更新槽位/LookAndFeel 颜色/等宽标志、以及 `remapComponentColours` 对「精确槽位色 / darker 派生色 / 自定义色」的处理（自定义色保持不变）。测试用 `ThemeGuard` 在结束时恢复默认调色板，保证 `test_ui_colors` 不受影响。

## 风险

| 风险 | 缓解 |
|---|---|
| 某组件用硬编码色 → 切主题后不跟随 | 已把 GUI 内残留的 9 处灰阶统一到 `fg_.withAlpha(...)`；粒子 hue 与白/黑语义色有意保留 |
| 派生色匹配不到（如 `.withAlpha().darker()` 组合） | 匹配表覆盖 darker 0.15~0.8 / brighter 0.05~0.4 的常用档；未匹配则保持原色（不崩） |
| 主题切换时的性能 | 切换是用户偶发操作，遍历一次组件树 + 重绘，量级毫秒 |
