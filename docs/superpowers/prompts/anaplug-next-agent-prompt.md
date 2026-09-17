# AnaPlug 接任 AI · 可粘贴 prompt

> 用法：新会话第一句话把下面代码块整段粘贴。它会让 AI 先读交接文档再动手。

```text
角色：AnaPlug 项目接任 AI。项目在 F:\anaplug（JUCE 8.0.13 合成器插件，VST3+CLAP，Windows x64 /MT，C++17；仓库 SakuraLuminance/notitle，main 直推）。

第一步（必做，不要跳过）：按顺序读
  1) HANDOFF_V3.md            —— 当前权威交接：环境铁律、架构、线程模型、GUI/主题、陷阱清单、文件索引
  2) docs/superpowers/plans/2026-09-07-p2-p6-roadmap.md —— 里程碑总表（P1–P6 + P6b 均已完成）
  3) 相关 spec：docs/superpowers/specs/2026-09-17-{p4-envelope-canvas,p5-frontier-wiring,p6b-time-varying-image,theme-system}-design.md
  4) HANDOFF_V2.md            —— 只当"历史故障取证库"查（红 Run 分析、cdb/pluginval 取证、UAC 安装脚本）
读完先向我复述：当前 HEAD/未推送批次、下一个该做的任务、以及你打算怎么验证。

环境铁律（违反必炸）：
- 本机没有任何编译工具链（无 cmake/msbuild/cl/ninja）。所有验证只能 `git push origin main` 推 GitHub Actions（12–20 分钟/轮）。禁止声称"本地编译/测试通过"。
- 取 CI 日志：`GET /actions/runs/{id}/jobs` → job id → `GET /actions/jobs/{job_id}/logs`；PAT 可用 `"protocol=https`nhost=github.com`n" | git credential fill` 从凭据管理器取出（不落盘）。推送本身直接 `git push origin main` 即可。
- 【最大陷阱】CI 的 Test 步骤带 `continue-on-error: true`——测试挂了 run 也是绿的。每轮必须读日志里的三项：`All tests passed (… in N test cases)`、`Strictness level: 5`、无 `CRASH-OR-FAIL ::`。
- 纪律：改一批 → 推一轮 → 取证 → 再改。失败先拉日志定位，禁止凭空大改。

当前状态（2026-09-17）：
- origin/main = 26d18ae（那轮 Build/Test/pluginval 步骤全 success，但用例计数当时未读到）。
- 本地有 15 个未推送提交（P6b 时变谐波图像、逐帧编辑、IMAGE 画布、EVO 页、一轮性能优化+5 个 bug 修复、多主题视觉系统、约 12 个新测试+1 个基准）。
- 最近一次读到计数的全绿：583 用例 + strictness 5。未推送批次预期 595+。
- 第一个任务：`git push origin main`，然后读日志确认上面三项；若有编译/测试失败，按报错定位修复并重推。

设计方针（必须遵守，详见 HANDOFF_V3 §7）：
- 视觉：只用主题槽位色（CyberpunkTheme::bg_/fg_/cyan_/magenta_/yellow_）与 token（kControlHeight/kSliderWidth/kReadoutWidth/kCanvas*Alpha）；不硬编码颜色；每个连续控件配数值读数（formatPercent/formatNumber/styleReadout）；行高 20；字体只用 getCyberFont；空状态全大写。
- 交互：不接线就不出现在 UI；控件必须有 tooltip；新控件必须落到处理器 API。
- 代码：被测试目标编译的文件严禁 include PluginProcessor.h / PluginEditor.h（clap 陷阱）；新 .cpp 必须同时进 CMakeLists.txt 与 tests/CMakeLists.txt；GUI 只读原子；实时路径禁止分配；锁不可重入（回调前先放锁）。
- 流程：brainstorm（问清需求）→ 简洁 spec（表格化，docs/superpowers/specs/）→ plan → 实现 → 推一轮 → 取证；提交前缀 feat/fix/perf/style/test/docs(scope)。

后续路线（按优先级）：
  1) 先推送并取证当前批次（见上）。
  2) rack 内 MIDI Learn —— 设计已写死在 HANDOFF_V3 §6.2，可直接照做（MidiMapping 加 setter/getter 回调 + EffectParamPanel/EffectSlotWidget/EffectRackComponent 暴露旋钮 + 编辑器轮询注册）。
  3) 枚举参数真菜单 —— 先与我确认范围再设计。
  4) 颗粒 UI（GranularSynthesizer 零 UI）、DNA fittest→当前音色按钮、P6 图像帧索引升级为谐波分箱。
  5) 成品 VST3 安装（需 UAC，脚本在 HANDOFF_V2）。

我的偏好：spec 尽量简洁（表格、少散文）；说"开始完成吧"= 批准并立刻实现；不要反复问我细节，能自己查代码/文档就自己查；重大架构取舍才问我。
```

---

## 维护提示（给人类）

- 每完成一个里程碑：更新 `docs/superpowers/plans/2026-09-07-p2-p6-roadmap.md` 的进度行 + 对应 spec 的"待验证/已完成"一节。
- 每次推送并读到日志后：把 `All tests passed (… in N test cases)` 的 N 与 `Strictness level` 记进 HANDOFF_V3 §0 快照。
- 新主题加在 `src/gui/ThemePalettes.h` 一行即可（UI 自动列出）。
