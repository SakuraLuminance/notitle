# AnaPlug

Windows x64 合成器插件（VST3 + CLAP）：把采样分析成**谐波图像**，用实时加法合成 / 重合成去雕塑它——配 20 个效果器、调制矩阵、包络画布、DNA 进化、频谱冻结、粒子视图与多套视觉主题。

- 技术栈：JUCE 8.0.13（FetchContent）、clap-juce-extensions、Catch2 v3.5.2、C++17、MSVC `/MT`
- 目标：`AnaPlug`（插件）、`AnaPlugTests`（单测 + 基准）

## 构建（需要 VS2022 + CMake）

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CI（GitHub Actions，`.github/workflows/cmake.yml`）会构建插件、跑 `AnaPlugTests`、用 pluginval（strictness 5）校验 VST3，并上传 VST3/CLAP 成品 artifact（`AnaPlug-windows-latest`）。

> ⚠️ CI 的 `Test` 步骤带 `continue-on-error: true`：**测试失败 run 依然是绿的**。
> 而且这台开发机**无法下载 CI 日志/artifact**（本机 DNS 把 `*.blob.core.windows.net` 应答成保留段 198.18.x.x），
> 唯一可读通道是 workflow 自己写出的 **runner 注解**：
>
> ```powershell
> & tools/ci-status.ps1 -Sha <sha>   # 构建错误 / named tests / executed cases / failed cases / strictness / CRASH-OR-FAIL
> ```

## 上手 / 继续开发

| 文档 | 内容 |
|---|---|
| **`HANDOFF_V3.md`** | **当前权威交接**：环境铁律、架构与线程模型、GUI/主题系统、设计方针、陷阱清单、关键文件索引、路线 |
| `docs/superpowers/prompts/anaplug-next-agent-prompt.md` | 给下一个 AI 的可粘贴接任 prompt |
| `docs/superpowers/plans/2026-09-07-p2-p6-roadmap.md` | 里程碑总表（P1–P6 已完成；另含 P6b 时变图像、多主题、rack 内 MIDI Learn、颗粒层 + GRAIN 页、图像谐波分箱、帧混合曲线、枚举真菜单、颗粒池性能优化） |
| `tools/ci-status.ps1` | 读 CI 步骤结果 + runner 注解（本机唯一可用的取证通道） |
| `tools/install-vst3.ps1` | 安装成品 VST3 到系统目录（自提权；自动找 Downloads 里最新的 artifact zip，可选清 REAPER 缓存） |
| `docs/superpowers/specs/*.md` | 每个功能的设计（简洁表格化） |
| `HANDOFF_V2.md` | 历史故障取证库（58 次 CI 失败分析、cdb/pluginval 取证、安装脚本） |

## 设计要点（详见 V3 §7）

- 颜色只走主题槽位（`src/gui/ThemePalettes.h` 一行即可加主题），画布/控件用 `CyberpunkTheme` 的 token
- 每个连续控件配数值读数；行高 20；字体只用 `getCyberFont`
- **不接线就不出现在 UI**；被测试目标编译的文件禁止 include `PluginProcessor.h`（clap 陷阱）
- 流程：brainstorm → 简洁 spec → plan → 实现 → 推一轮 CI → 读日志取证
