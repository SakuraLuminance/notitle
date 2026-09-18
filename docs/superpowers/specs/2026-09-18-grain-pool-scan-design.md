# 颗粒池渲染前缀优化（性能）设计 — 2026-09-18

> 目标：`GranularSynthesizer::process` 每采样只遍历**可能活跃**的粒子槽，输出与优化前**逐位一致**。

| 项 | 值 |
|---|---|
| 改动 | `src/dsp/GranularSynthesizer.h/.cpp` |
| 提交 | `81e4138` |
| 验证 | Run `#35319079605`：Build 0 error、617 用例 0 失败、strictness 5、0 CRASH-OR-FAIL |

## 1. 问题

```cpp
for (int g = 0; g < maxGrains_; ++g)   // 256
{
    auto& grain = grains_[g];
    if (!grain.active) continue;        // 绝大多数采样都在这里 continue
    ...
}
```

- 每采样 256 次迭代 × 48000 = **12.3 M 次/秒**，其中几乎全是"跳过未激活槽"；
- 粒子池 `grains_[256]` 约 10 KB，每采样都要从头摸一遍；
- 默认音色（密度 10 粒/秒 × 50 ms 窗口）同时活跃的粒子**不到 1 个**。

## 2. 方案：活跃前缀 `[0, scanCount_)`

`spawnGrain()` 一直取**最低的空闲槽**，所以粒子永远只占用池子的前缀：

| 事件 | 对 `scanCount_` 的处理 |
|---|---|
| 生成粒子（槽 `slot`） | `scanCount_ = max(scanCount_, slot + 1)` |
| 粒子结束（正常路径 / 静音提前退出分支） | `shrinkScannedGrains()`：从尾部弹出未激活槽 |
| `reset()` | `scanCount_ = 0` |

**不变式**：任何激活粒子的下标 < `scanCount_`。
- 生成只增不减 ✓；
- 收缩只在**尾部未激活**时进行，绝不会跨过激活粒子 ✓；
- 遍历下标顺序与旧实现完全一致 → **求和顺序不变 → 输出逐位不变** ✓。

收缩在采样循环内调用是安全的：它只会把 `scanCount_` 缩到「第一个激活粒子」之前，因此循环条件 `g < scanCount_` 提前结束时，剩余的都是未激活槽。

## 3. 为什么 `getActiveGrainCount()` 保持全池扫描

它是**独立校验路径**：若前缀算错（某些激活粒子落在前缀之外），这些粒子永远不会被推进、永远不会结束，全池计数就会一直 > 0。测试正是靠它抓这类错误。

## 4. 测试

`tests/test_granular_synthesis.cpp` → "GranularSynthesizer empties its grain pool between spawns"：

1. **撑宽池子**：100 ms 窗口 × 200 粒/秒 ≈ 20 个并发 → `getActiveGrainCount() >= 10`（前缀必须真的长起来）；
2. **抽干池子**：切成 5 ms 窗口 + 最低密度（1 粒/秒）→ 2 秒内必须出现 `active == 0`。
   若前缀过短，第 1 步的 20 个粒子会永久激活，第 2 步不可能到 0。
