# AnaPlug

Windows x64 合成器插件（VST3 + CLAP）：把采样分析成**谐波图像**，用实时加法合成 / 重合成去雕塑它——配 20 个效果器、调制矩阵、包络画布、DNA 进化、频谱冻结、粒子视图与多套视觉主题。

- 技术栈：JUCE 8.0.13（FetchContent）、clap-juce-extensions、Catch2 v3.5.2、C++17、MSVC `/MT`
- 目标：`AnaPlug`（插件）、`AnaPlugTests`（单测 + 基准）

## GRAIN 页（颗粒层）使用说明

切到 **GRAIN** 页签（第 9 页），先 `LOAD SAMPLE` 载入采样，然后：

| 控件 | 作用 |
|---|---|
| `GRAIN` | 颗粒层总开关（关掉后仍保留设置） |
| `MIX` | 颗粒层混入主输出的比例 |
| `SIZE` | 单颗粒子时长 1–100 ms（决定音粒的粗细） |
| `DENSITY` | 每秒生成粒子数 1–1000（越高越接近持续音） |
| `SPACE` | 读头在采样中的位置（0% 起点，100% 终点） |
| `PITCH` | 粒子移调 ±24 半音（读头速度） |
| `WINDOW` | 粒子窗形状：HANN / TRI / GAUSS / SINC（越靠后越硬） |
| `MOD` + `DEPTH` + `RATE` | 读头位置调制：OFF / LFO / ENV / RND；DEPTH 是采样长度的比例，RATE 是速度 |
| `SPREAD` | 每颗粒子的随机声像散布（0% 全在中间，100% 铺满左右） |
| `REVERSE` | 该比例的粒子倒放（跨度与时长不变，只是反向读） |
| `JITTER` | 间距随机化：平均速率不变，只让粒子不再是钟表式等距（越大越像云雾） |

**粒子云**（页面下半）：顶部窄条是采样包络，下面是正在发声的粒子——横轴 = 读头位置，纵轴 = 粒子年龄（新粒子在顶部、逐渐下沉），条宽 = 粒子时长，颜色 = 声像（青=左、品红=右），条端竖线 = 粒子方向，黄色竖线 = `SPACE` 基准位置。

每个旋钮都支持**右键 → MIDI Learn**（参数 id：`grain_mix` / `grain_size` / `grain_density` / `grain_position` / `grain_pitch` / `grain_mod_depth` / `grain_mod_rate` / `grain_spread` / `grain_reverse` / `grain_jitter`）；所有参数随工程状态保存与恢复。
## FX 页（效果器链）使用说明

切到 **FX** 页签（第 5 页）：顶行是效果预设下拉框与 **UNDO / REDO**（效果链的增删与排序可撤销——`+ ADD EFFECT` 加上、`X` 删掉、上下箭头换位都会进撤销栈，按钮在无可撤/可重做时自动置灰）。

链里的每个槽位：`▾` 展开该效果器的全部参数旋钮与枚举菜单（枚举是真下拉菜单，不是旋钮）、`BYP` 旁通、`LO`/`HI` 湿声带通（4 阶，约 24 dB/oct）、`MIX` 干湿比。参数旋钮与菜单都支持右键 MIDI Learn，控件 tooltip 会列出枚举的每个取值含义。

**所有控件的 tooltip 都会显示**：插件自己持有 JUCE 的 TooltipWindow（没有它，`setTooltip` 写了也不会弹——这是之前所有提示都静默失效的原因）。

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
> & tools/ci-annotations.ps1 -Sha <sha>   # 构建错误 / 用例数 / 断言数 / strictness / CRASH-OR-FAIL
> & tools/ci-status.ps1 -Sha <sha>        # 上面这些 + 每个步骤的成败（末尾自动调用前一个脚本）
> ```

## 上手 / 继续开发

| 文档 | 内容 |
|---|---|
| **`HANDOFF_V3.md`** | **当前权威交接**：环境铁律、架构与线程模型、GUI/主题系统、设计方针、陷阱清单、关键文件索引、路线 |
| `docs/superpowers/prompts/anaplug-next-agent-prompt.md` | 给下一个 AI 的可粘贴接任 prompt |
| `docs/superpowers/plans/2026-09-07-p2-p6-roadmap.md` | 里程碑总表（P1–P6 已完成；另含 P6b 时变图像、多主题、rack 内 MIDI Learn、颗粒层 + GRAIN 页（粒子云 + 右键 MIDI Learn）、图像谐波分箱、帧混合曲线、枚举真菜单、颗粒池性能优化） |
| `tools/ci-status.ps1` | 读 CI 各步骤成败，末尾调用下面这个脚本打印 runner 注解 |
| `tools/ci-annotations.ps1` | 只打印 runner 注解（构建错误 / 用例数 / 断言数 / strictness / CRASH-OR-FAIL）——**本机唯一可用的取证通道** |
| `tools/install-vst3.ps1` | 安装成品 VST3 到系统目录（自提权；自动找 Downloads 里最新的 artifact zip，可选清 REAPER 缓存） |
| `docs/superpowers/specs/*.md` | 每个功能的设计（简洁表格化） |
| `HANDOFF_V2.md` | 历史故障取证库（58 次 CI 失败分析、cdb/pluginval 取证、安装脚本） |

## 设计要点（详见 V3 §7）

- 颜色只走主题槽位（`src/gui/ThemePalettes.h` 一行即可加主题），画布/控件用 `CyberpunkTheme` 的 token
- 每个连续控件配数值读数；行高 20；字体只用 `getCyberFont`
- **不接线就不出现在 UI**；被测试目标编译的文件禁止 include `PluginProcessor.h`（clap 陷阱）
- 流程：brainstorm → 简洁 spec → plan → 实现 → 推一轮 CI → 读日志取证
