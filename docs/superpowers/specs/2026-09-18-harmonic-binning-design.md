# 谐波分箱（P6 遗留：图像跨帧索引）

**状态**：设计定稿，待实现
**日期**：2026-09-18
**背景**：`buildImageFramesFromPartialData()` 目前把每帧的 peak 列表**按索引**拷进
`PartialDataSIMD`（peak 检测按频率升序排序），于是"索引 i"在不同帧里指的是
**不同的谐波**。帧插值（`AdditiveBank` floor/ceil）因此会在同一索引上把两个
不相干的谐波交叉淡化——听感是频率滑动/相位抵消，而不是自然的音色演变。

## 1. 目标

| 目标 | 判定标准 |
|---|---|
| 索引语义稳定 | 帧 N 与帧 N+1 的索引 i 指同一个谐波号（H(i+1)） |
| 行为可预测 | 同一 f0 轴下重放同一图像，分箱结果稳定（往返幂等） |
| 退化安全 | 无音高/静音/噪声帧退化为"按索引拷贝"（= 现有行为） |
| 零音频线程成本 | 分箱只在消息线程（载入/编辑）执行；音频线程仍只读帧数组 |

## 2. 设计

新增 header-only `src/dsp/HarmonicBinning.h`（测试目标安全：只依赖
`PartialData.h` / `PartialDataSIMD.h`）。

| 元素 | 签名 | 语义 |
|---|---|---|
| 单帧 f0 估计 | `static float estimateFundamental(const std::vector<Partial>&, float minShare = 0.25f)` | 取"幅度 ≥ 本帧最强峰 × minShare"的**最低**峰频率；整帧最强峰 < 0.02 时返回 0 |
| 共享 f0 | `static float estimateSharedFundamental(const std::vector<PartialFrame>&)` | 各帧 f0 估计的**中位数**（忽略 0），保证所有帧共用一根轴 |
| 单帧分箱 | `static void binInto(const std::vector<Partial>&, float f0, int binCount, PartialDataSIMD& out)` | bin = round(f/f0) − 1，越界丢弃；bin 冲突保留幅度大者；超出最高 bin 的泛音并入最后一箱 |
| 退化路径 | 同上，`f0 <= 0` | 按索引拷贝（钳位到 binCount） |

### 2.1 接入点

| 文件 | 改动 |
|---|---|
| `src/dsp/HarmonicBinning.h` | 新增（上文 API） |
| `src/PluginProcessor.cpp` `buildImageFramesFromPartialData()` | 先算共享 f0（存入 `imageFundamental_`），每帧改走 `binInto()`；`f.maxPartials` 保持 `kMaxPartials` |
| `src/PluginProcessor.h` | `float getImageFundamental() const`（原子）+ `const std::vector<ana::PartialDataSIMD>& getImageFrames() const`（消息线程读，音频线程只读） |
| `src/gui/PartialEditorCanvas.{h,cpp}` | `setHarmonicAxis(bool, float f0)`：Y 轴标签由"线性 Hz"改为 `H1..Hn`（悬停显示 H 号 + 隐含 Hz）；`getModifiedPartialData()` 的写回频率由 `(p+0.5)/n·nyquist` 改为 `(p+1)·f0` |
| `src/PluginEditor.cpp` | 用 `getImageFrames()`（而非原始分析帧）喂画布，并传 `getImageFundamental()` |

### 2.2 为什么用"共享 f0 + 中位数"

逐帧各自估 f0 会让帧与帧之间的 bin 宽度不同，插值时仍会串谐波；取中位数对
个别帧的检测失败（漏检基频、八度误判）不敏感。若某帧估到 f0/2，它只会把
偶次谐波放进偶数 bin，其余帧不受影响。

## 3. 风险与取舍

| 风险 | 处理 |
|---|---|
| 非谐波素材（噪声/打击）分箱后谐波化 | 无 f0 时走索引拷贝；有 f0 时频谱结构不变（频率仍是真实峰值频率），只是**索引**变了 |
| f0 误判为低八度 → bin 数翻倍 | 上限 `kMaxPartials`；超出部分并入最后一箱，不丢能量 |
| 画布往返（编辑→写回→再分箱）不幂等 | `getModifiedPartialData()` 用与画布同一 f0 生成频率，保证落回同一 bin |

## 4. 测试（`tests/test_harmonic_binning.cpp`，注册进 `tests/CMakeLists.txt`）

| 用例 | 断言 |
|---|---|
| 基频估计 | 谐波序列取最低强峰；静音返回 0 |
| 完美谐波序列分箱 | 100/200/300 Hz → bin 0/1/2，频率与幅度保持 |
| 冲突与越界 | 同 bin 保留强者；低于 0.5·f0 丢弃；超出轴顶并入末箱 |
| 退化路径 | f0 = 0 → 逐索引拷贝，activeCount 不变 |
| 跨帧一致性（回归） | 两帧 peak 数量不同，同一谐波仍落同一索引 |

## 验证

| 项 | 值 |
|---|---|
| Run | `#35315329172`（`a6e5f3b`） |
| Build | 0 条 error |
| 用例 | **611 执行 / 0 失败**（runner 注解，见 HANDOFF_V3 §1） |
| pluginval | `Strictness level: 5` SUCCESS |
| forensics | 0 CRASH-OR-FAIL，7 SKIP-UNMATCHED |
| 备注 | 该 run 的树已包含本批次；首次验证 #35313451841（69169ca）亦全绿（当时注解尚未拆分用例计数） |

