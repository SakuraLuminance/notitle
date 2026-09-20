# AnaPlug 接任 AI · 可粘贴 prompt

> 用法：新会话第一句话把下面代码块整段粘贴。它会让 AI 先读交接文档再动手。
> 最后更新：2026-09-19（对应 `origin/main = 3a9ba45`）。

```text
角色：AnaPlug 项目接任 AI。项目在 F:\anaplug（JUCE 8.0.13 合成器插件，VST3 + CLAP + Standalone，Windows x64、MSVC /MT、C++17；仓库 SakuraLuminance/notitle，main 直推）。你拥有全部工具权限：不要反问细节，能自己读代码/文档/CI 就自己查；只有重大架构取舍才问。

第一步（必做，不要跳过）：按顺序读
  1) HANDOFF_V3.md              —— 当前权威交接：§0 快照 = 现状、§1 环境铁律、§7 设计方针、文件索引
  2) AUDIT_ALGORITHMS_V1.md     —— 算法/漏洞审计：已修 6 类、已核实未修清单、19 个零引用模块
  3) docs/superpowers/plans/2026-09-07-p2-p6-roadmap.md —— 里程碑总表（P1–P6、P6b 均已完成）
  4) docs/superpowers/specs/2026-09-17-*.md —— 相关设计 spec
  5) HANDOFF_V2.md              —— 只当历史故障取证库查（红 Run 分析、cdb/pluginval 取证流程）
读完先向我复述：当前 HEAD / 未推送批次、下一个该做的任务、你打算怎么验证。

环境（2026-09-19 实测，全部已验证可用）：
- 唯一权威验证 = `git push origin main` → GitHub Actions（12–25 分钟/轮）。CI 的 Test 步骤是 `continue-on-error: true`（测试挂了 run 也绿），**Build 步骤才是真门禁**；用例计数从 `ui-audit-publish` check-run 的 `### TESTS` 段读。步骤日志与 artifact 都走 `*.blob.core.windows.net`，本机**不通**（TLS 握手失败，DNS 被解析到 198.18.x.x）。
- 本机可用的验证工具（都在仓库外 F:\anaplug-local，能省整轮 CI，但不是产品证据）：
    · `zigcheck.ps1 <文件…>`   —— clang 前端检查（-c，不链接）。改完任何 .cpp/.h 先跑它，退出码 = 失败文件数。
    · `build-tests.ps1 [-Clean] [-Filter <tags>]` —— 编译并运行 Catch2 全量/按标签。**改了头文件必须 -Clean**：对象与头文件 ABI 不一致不会报编译错，而是运行期读到随机内存（断言里出现 10421304982 这种 size 就是它）。脚本已按最新头文件时间自动让对象失效。
    · `build-audit.ps1`        —— 重建并运行 UI 自检（59 张截图 + 逐控件几何检查），产出 F:\anaplug-local\audit-local\index.html 画廊。
    · zig 默认开 UBSan，而 mingw 的 WIN32_FIND_DATAW 只有 4 字节对齐 → 已在脚本里加 `-fno-sanitize=undefined`；改过脚本后需要 `-Clean` 重建一次才生效。
- 测试/安装闭环（一条命令，不需要浏览器、不需要 UAC）：
    `powershell -ExecutionPolicy Bypass -File tools\fetch-latest-vst3.ps1 -ClearReaperCache`
    CI 每次成功构建都会把 VST3 bundle 以单提交分支的形式重发到 `ci-artifacts`（root commit，历史永远只有 1 个提交，不会让仓库每轮涨 17 MB），旁边有 `build.json` 记录 sha / run / 大小；脚本取回、校验、装进 `C:\Program Files\Common Files\VST3`（该目录本机可写，不需要 UAC）。`-NoInstall` 只下载，`-ArtifactApi` 走官方 artifact（能连通 blob 的机器上更快）。
- PAT 取法：把 `protocol=https` 与 `host=github.com` 两行（各带换行）交给 `git credential fill` —— 本机 PowerShell 管道会丢数据，必须用临时文件重定向（`tools\ci-status.ps1` 里就是这么写的，照抄即可）。看 CI 概览：`tools\ci-status.ps1 -Sha <sha>`。

铁律（违反必炸）：
- 纪律：改一批 → 推一轮 → 取证 → 再改。**禁止**把本地编译/测试通过当作产品证据；CI 才是判决。
- 先取证再动手：每次开工先 `git log -1 --oneline`、`git status --porcelain`；不 force push；不改 CI 门禁的判定条件（`UI audit must be clean` 是硬门禁）。
- 一个 run 在跑的时候不要连续推第二轮（两个 run 会抢着往 `ci-reports` 发布，必有一个失败）。
- `Select-Object -First N` 会掐断上游管道并丢掉后面所有输出，脚本里慎用。
- 未接线就不出现在 UI；每个控件必须有 tooltip；GUI 只读原子；实时路径禁止分配；被测试目标编译的文件严禁 include PluginProcessor.h / PluginEditor.h（clap 陷阱）。
- 提交前缀 feat/fix/perf/style/test/docs/ci(scope)，主题小写英文；每批写清改了什么、为什么、怎么验证。

当前状态（2026-09-19）：
- origin/main = `3a9ba45`（feat(ci): 把构建好的 VST3 发到本地取得到的地方）。**未推送批次：无**。
- 最近一次全绿：`d2334d9`（run #197）—— Build success、`UI AUDIT: 59 snapshots, 2707 visible controls, 0 findings`、pluginval `Strictness level: 5` success；算法审计的修复批次就在这一轮里。
- UI 自检现状：0 findings；仅剩 2 条 advisory（`cramped=128`、`invisible-control=2`，MOD 页 900×660）尚未清零、尚未纳入门禁。

下一个该做的任务（按优先级，详见 AUDIT_ALGORITHMS_V1.md §5）：
  1) **PitchCorrector 流式 STFT（critical）**：每个 process() 重建累加器、且只合成完全落在块内的帧，宿主块 512 < 窗口 2048 时整块输出静音（AutoTuneEffect / VocalProcessor 路径）。`tests/test_pitch_corrector.cpp` 里已有 `[!mayfail]` 用例复现（实测 peak = 0.0），实现完把标签摘掉。
  2) 重合成分支接进 FX 链（`PluginProcessor.cpp` 的 925 / 979 / 1024 三处）。
  3) EffectsChain / GranularSynthesizer::sourceBuffer / rack 索引的跨线程访问收敛。
  4) MultibandProcessor 无调用者 = 空操作：接线或删除，二选一。
  5) 19 个零引用模块（约上万行）：接线或移出构建，不要留着冒充已实现功能。
  6) UI 自检 advisory 清零并升级为门禁。

我的偏好：spec 尽量简洁（表格、少散文）；说开始完成吧 = 批准并立刻实现；不要反复问我细节；重大架构取舍才问我。
```

---

## 维护提示（给人类）

- 每轮推送并读到 CI 结论后：把 run 号、用例数、`Strictness level`、UI 自检 `findings` 数记进 `HANDOFF_V3.md` §0 快照，并把本文件的当前状态 / 下一个该做的任务两节改写。
- 每完成一个里程碑：更新 `docs/superpowers/plans/2026-09-07-p2-p6-roadmap.md` 的进度行 + 对应 spec 的待验证/已完成一节。
- 新主题加在 `src/gui/ThemePalettes.h` 一行即可（UI 自动列出）。
- 要把最新构建装进宿主：`tools\fetch-latest-vst3.ps1 -ClearReaperCache`（详见 `HANDOFF_V3.md` §1 的成品安装与本地取构建一行）。