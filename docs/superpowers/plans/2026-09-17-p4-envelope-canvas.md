# P4 Envelope Canvas Implementation Plan (Round 1)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 新增第 7 页 ENV：可编辑多断点包络画布（VOL/ENV1/ENV2），与 MOD 页 ADSR 滑条双向同步。

**Architecture:** 复用 `ana::MultiPointEnvelope` 作为模型；纯函数 `ana::EnvelopeEditOps` 承担夹取/派生 ADSR/坐标换算（可单测）；`ana::EnvelopeCanvas` 只做交互与渲染；`ana::EnvelopePage` 组装槽位与参数行。音频线程与 UI 编辑用 `juce::SpinLock envLock_` 互斥。

**Tech Stack:** C++17 / JUCE 8 / Catch2 / MSVC /MT；本机无编译链，验证全部走 GitHub Actions。

## Global Constraints

- 本机无 cmake/msbuild/cl/ninja，禁止声称"本地编译通过"；唯一验证 = push 后 GitHub Actions（~20 min/轮）。
- 被测试目标编译的文件**严禁** `#include PluginProcessor.h`（clap 陷阱）。
- 新 `.cpp` 必须同时加进 `CMakeLists.txt` 与 `tests/CMakeLists.txt`。
- 不新增 warning-clean 之外的东西；CI 用 `/W4` 等级（MSVC），避免未使用参数/变量。
- spec：`docs/superpowers/specs/2026-09-17-p4-envelope-canvas-design.md`。

---

### Task 1: 引擎 loop 边界修正 + 新 API

**Files:**
- Modify: `src/dsp/MultiPointEnvelope.h` / `.cpp`
- Modify: `tests/test_multi_point_envelope.cpp`

**Produces:**
- `void setBreakpointCurve(int index, CurveType curve);`
- `double getTimeInSeconds(float breakpointTime) const;`（原 private `timeToSeconds` 转公开）
- `double getTimePositionSeconds() const noexcept;`
- Forward/PingPong 在 `[loopStart, loopEnd]` 折返；未配置 `loopEnd` 仍 play-once。

- [ ] **Step 1: 加失败测试**（追加到 `test_multi_point_envelope.cpp` 末尾）

```cpp
TEST_CASE("MultiPointEnvelope: P4 loop boundaries + new API", "[envelope][p4]")
{
    SECTION("forward loops within [loopStart, loopEnd], not total end")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.2f, 1.0f);
        env.addBreakpoint(0.4f, 0.5f);
        env.addBreakpoint(0.6f, 0.0f);   // outside the loop range
        env.setLoopMode(LoopMode::Forward);
        env.setLoopStart(0);
        env.setLoopEnd(2);
        env.prepare(48000.0);
        env.trigger();

        advance(env, 4800);              // t = 0.1 -> rising segment
        REQUIRE(env.getValue() == Catch::Approx(0.5f).margin(0.01f));

        advance(env, 19200);             // +0.4s = exactly one loop length
        REQUIRE(env.getValue() == Catch::Approx(0.5f).margin(0.01f));
        REQUIRE(env.isActive());

        // Value never enters the 0.4..0.6 region again: peak stays 1.0
        bool sawPeak = false;
        for (int i = 0; i < 200; ++i)
            if (advance(env, 48) > 0.999f) { sawPeak = true; break; }
        REQUIRE(sawPeak);
        REQUIRE(env.getValue() <= 1.0f);
    }

    SECTION("ping-pong reflects at loopEnd, not total end")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.1f, 1.0f);
        env.addBreakpoint(0.2f, 0.0f);
        env.addBreakpoint(0.5f, 0.0f);   // outside loop range
        env.setLoopMode(LoopMode::PingPong);
        env.setLoopStart(0);
        env.setLoopEnd(2);
        env.prepare(48000.0);
        env.trigger();

        advance(env, 4800);              // 0.1s -> peak
        REQUIRE(env.getValue() == Catch::Approx(1.0f).margin(0.01f));
        advance(env, 4800);              // 0.2s -> reflect at loopEnd
        REQUIRE(env.getValue() == Catch::Approx(0.0f).margin(0.01f));
        advance(env, 4800);              // back to 0.1s
        REQUIRE(env.getValue() == Catch::Approx(1.0f).margin(0.01f));
        for (int i = 0; i < 50; ++i) advance(env, 480);
        REQUIRE(env.isActive());
    }

    SECTION("setBreakpointCurve changes the segment shape")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f, CurveType::Linear);
        env.prepare(48000.0);
        env.setBreakpointCurve(1, CurveType::Exponential);
        env.trigger();
        advance(env, 12000);             // 0.25s -> exponential overshoots linear
        REQUIRE(env.getValue() > 0.25f);
    }

    SECTION("getTimeInSeconds honours sync mode")
    {
        MultiPointEnvelope env;
        env.setTempo(120.0);
        env.setBeatDivision(1.0);
        env.setSyncMode(true);
        REQUIRE(env.getTimeInSeconds(2.0f) == Catch::Approx(1.0)); // 2 beats @120bpm
        env.setSyncMode(false);
        REQUIRE(env.getTimeInSeconds(2.0f) == Catch::Approx(2.0));
    }

    SECTION("getTimePositionSeconds tracks playback")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        env.prepare(48000.0);
        env.trigger();
        advance(env, 24000);
        REQUIRE(env.getTimePositionSeconds() == Catch::Approx(0.5).margin(0.01));
    }
}
```

- [ ] **Step 2: 更新既有失效用例**（`TEST_CASE("MultiPointEnvelope: loop modes")` 的 forward section）

把 `tests/test_multi_point_envelope.cpp` 中：
```cpp
        // After wrapping from 0.6 to 0 and coming up to 0.1 again:
        advance(env, 24000); // advance to 0.6s (end of first cycle)
        advance(env, 4800);  // 0.1s into second cycle
```
改为：
```cpp
        // Advance exactly one loop length (loopEnd = 0.4s) from t=0.1s:
        advance(env, 19200); // 0.4s
        advance(env, 0);
```

- [ ] **Step 3: 实现**（`MultiPointEnvelope.h` 公开区加声明）

```cpp
    void   setBreakpointCurve(int index, CurveType curve);
    double getTimeInSeconds(float breakpointTime) const;
    double getTimePositionSeconds() const noexcept { return timePosSeconds; }
```

`MultiPointEnvelope.cpp`：`timeToSeconds` 定义改名为 `getTimeInSeconds`（内部 3 处调用同步改名），并新增：
```cpp
void MultiPointEnvelope::setBreakpointCurve(int index, CurveType curve)
{
    if (index < 0 || index >= static_cast<int>(breakpoints.size()))
        return;
    breakpoints[static_cast<size_t>(index)].curve = curve;
}
```

`advanceEnvelope` 的边界段替换为：
```cpp
    const bool loopConfigured = loopEndIndex >= 0
        && loopEndIndex < static_cast<int>(breakpoints.size());

    if (loopMode == LoopMode::Sustain && !released && timePosSeconds >= loopEndSec)
    {
        timePosSeconds = loopEndSec;
    }
    else if (loopMode == LoopMode::Forward && loopConfigured)
    {
        if (timePosSeconds >= loopEndSec)
        {
            const double wrapRange = loopEndSec - loopStartSec;
            timePosSeconds = (wrapRange > 0.0)
                ? loopStartSec + std::fmod(timePosSeconds - loopStartSec, wrapRange)
                : loopStartSec;
        }
    }
    else if (loopMode == LoopMode::PingPong && loopConfigured && direction > 0)
    {
        if (timePosSeconds >= loopEndSec)
        {
            direction = -1;
            const double overshoot = timePosSeconds - loopEndSec;
            timePosSeconds = loopEndSec - std::min(overshoot, loopEndSec - loopStartSec);
            if (timePosSeconds < loopStartSec)
                timePosSeconds = loopStartSec;
        }
    }
    else if (timePosSeconds >= totalEndSec)
    {
        handleEnvelopeEnd();
        return;
    }
```
（删除原来的 Forward/PingPong `switch` 分支；`loopEndSec` 定义保持不变。）

- [ ] **Step 4: 提交**

```bash
git add src/dsp/MultiPointEnvelope.h src/dsp/MultiPointEnvelope.cpp tests/test_multi_point_envelope.cpp
git commit -m "fix(envelope): Forward/PingPong loop within [loopStart, loopEnd]; add setBreakpointCurve/getTimeInSeconds/getTimePositionSeconds"
```

---

### Task 2: `ana::EnvelopeEditOps` + 单测

**Files:** Create `src/dsp/EnvelopeEditOps.{h,cpp}`, `tests/test_envelope_edit_ops.cpp`; Modify `CMakeLists.txt`, `tests/CMakeLists.txt`

**Consumes:** Task 1 API。**Produces:** `addPoint/removePoint/movePoint/deriveADSR/timeToX/xToTime/valueToY/yToValue/hitTestPoint/hitTestMarker`

- [ ] **Step 1: 头文件**（含 `DerivedADSR`；`hitTestMarker` 返回 -1/0(loopStart)/1(loopEnd)）

- [ ] **Step 2: 失败测试**（关键断言）

```cpp
TEST_CASE("EnvelopeEditOps: clamping and derived ADSR", "[envelope][editops]")
{
    ana::MultiPointEnvelope env;
    env.prepare(48000.0);
    env.addBreakpoint(0.0f, 0.0f);
    env.addBreakpoint(0.1f, 1.0f);
    env.addBreakpoint(0.3f, 0.5f);
    env.addBreakpoint(0.6f, 0.0f);
    env.setLoopMode(ana::LoopMode::Sustain);
    env.setLoopEnd(2);

    auto adsr = ana::EnvelopeEditOps::deriveADSR(env);
    REQUIRE(adsr.attack  == Catch::Approx(0.1f).margin(0.001f));
    REQUIRE(adsr.decay   == Catch::Approx(0.2f).margin(0.001f));
    REQUIRE(adsr.sustain == Catch::Approx(0.5f).margin(0.001f));
    REQUIRE(adsr.release == Catch::Approx(0.3f).margin(0.001f));

    SECTION("move is clamped between neighbours and 0..1")
    {
        REQUIRE(ana::EnvelopeEditOps::movePoint(env, 1, -5.0f, 2.0f));
        REQUIRE(env.getBreakpoint(1).time  == Catch::Approx(0.0f));
        REQUIRE(env.getBreakpoint(1).value == Catch::Approx(1.0f));
    }

    SECTION("remove keeps at least two points")
    {
        REQUIRE(ana::EnvelopeEditOps::removePoint(env, 1));
        REQUIRE(env.getNumBreakpoints() == 3);
        REQUIRE(ana::EnvelopeEditOps::removePoint(env, 0));
        REQUIRE(ana::EnvelopeEditOps::removePoint(env, 0));
        REQUIRE(env.getNumBreakpoints() == 2);
        REQUIRE_FALSE(ana::EnvelopeEditOps::removePoint(env, 0));
    }

    SECTION("addPoint is a no-op when full")
    {
        for (int i = 0; i < ana::MultiPointEnvelope::maxBreakpoints; ++i)
            ana::EnvelopeEditOps::addPoint(env, 9.0f, 0.5f, ana::CurveType::Linear);
        REQUIRE(env.getNumBreakpoints() == ana::MultiPointEnvelope::maxBreakpoints);
    }
}
```

- [ ] **Step 3: 实现**（`deriveADSR` 用 spec §3 的映射；坐标换算式 `time = (x - area.getX()) / area.getWidth() * maxTime`，值轴反向）

- [ ] **Step 4: 接入 CMake**（`src/dsp/EnvelopeEditOps.cpp` 两处；`test_envelope_edit_ops.cpp` 一处）

- [ ] **Step 5: 提交**

---

### Task 3: `ana::EnvelopeCanvas` + headless 测试

**Files:** Create `src/gui/EnvelopeCanvas.{h,cpp}`, `tests/test_envelope_canvas.cpp`; Modify 两处 CMake

**Consumes:** Task 1/2。**Produces:** `void setEnvelope(ana::MultiPointEnvelope*)`, `void setPlayhead(double seconds, float value)`, `std::function<void()> onEdited`

- [ ] **Step 1: 组件骨架**：`paint`（网格 → 曲线段按 `Breakpoint::curve` → 断点圆点 → HOLD/LOOP START 竖线 → 播放头）、`resized`、`mouseDown/mouseDrag/mouseUp/mouseDoubleClick`（经 `EnvelopeEditOps`）、`setPlayhead`。

- [ ] **Step 2: headless 测试**

```cpp
TEST_CASE("EnvelopeCanvas: paint and edits are headless safe", "[ui][envelope]")
{
    ana::MultiPointEnvelope env;
    env.prepare(48000.0);
    env.addBreakpoint(0.0f, 0.0f);
    env.addBreakpoint(0.2f, 1.0f);
    env.addBreakpoint(0.5f, 0.4f);
    env.addBreakpoint(1.0f, 0.0f);
    env.setLoopMode(ana::LoopMode::Sustain);
    env.setLoopEnd(2);

    ana::EnvelopeCanvas canvas;
    canvas.setBounds(0, 0, 400, 200);
    canvas.setEnvelope(&env);
    canvas.setPlayhead(0.3, 0.8);

    juce::Image img(juce::Image::ARGB, 400, 200, true);
    juce::Graphics g(img);
    REQUIRE_NOTHROW(canvas.paint(g));

    // all loop modes / sync on / no points must paint too
    env.setLoopMode(ana::LoopMode::PingPong);
    env.setSyncMode(true);
    env.setTempo(140.0);
    REQUIRE_NOTHROW(canvas.paint(g));
    ana::MultiPointEnvelope empty;
    canvas.setEnvelope(&empty);
    REQUIRE_NOTHROW(canvas.paint(g));
}
```

- [ ] **Step 3: 提交**

---

### Task 4: Processor 支持（锁 / 槽位 / 原子 / BPM）

**Files:** Modify `src/PluginProcessor.h/.cpp`, `src/dsp/PresetManager.h`（加 `setEnvLockRef`）

**Produces:**
- `juce::SpinLock& getEnvelopeLock()`
- `ana::MultiPointEnvelope& getEnvelopeSlot(int slot)`（0=VOL, 1..3=ENV1..3）
- `std::atomic<double> envUITime_[4]` / `std::atomic<float> envUIValue_[4]`（`getEnvUITime(slot)`/`getEnvUIValue(slot)`）

- [ ] **Step 1: 头文件**：加 `#include "dsp/EnvelopeEditOps.h"`（不需要）——只加成员与 3 个方法声明；`envLock_` 放 `envPool_` 附近。
- [ ] **Step 2: cpp**
  - 构造函数：`envPool_[1].prepare(44100); envPool_[1].rebuildADSR();`（同 `[2]`）。
  - `processBlock`：`envPool_`/`volumeAdsr_` 的 process 前后用 `juce::SpinLock::ScopedLockType envLockScope(envLock_);` 包住 `envPool_[i].process()`/`volumeAdsr_.process()` 那两段，并写入 `envUITime_/envUIValue_`；宿主 BPM 处补 `for (auto& e : envPool_) e.setTempo(bpm); volumeAdsr_.setTempo(bpm);`。
  - 既有 ADSR setter（`setVolumeAttack…`、`setEnvelopeAttack…`）内部加锁。
- [ ] **Step 3: PresetManager**：`setEnvLockRef(juce::SpinLock*)`；`serialise/deserialise` ENVConfig 与 VolumeADSR 内部若锁指针非空则持锁（Round 2 才写断点，本轮只加锁引用）。
- [ ] **Step 4: 提交**

---

### Task 5: ENV 页 + 第 7 页签 + 滑条双向同步

**Files:** Create `src/gui/EnvelopePage.{h,cpp}`; Modify `src/gui/panels/PageTabs.h`, `src/PluginEditor.h/.cpp`, `src/gui/ModulationAssignPanel.{h,cpp}`, 两处 CMake

**Consumes:** Task 1–4。

- [ ] **Step 1: PageTabs 6→7**（names 加 `"ENV"`，三处 `6`→`7`，`setActive` 上限 `>6`）。
- [ ] **Step 2: `EnvelopePage`**：slot 按钮（VOL/ENV1/ENV2）、`EnvelopeCanvas`、参数行（loop 模式下拉、sync 开关、A/D/S/R 读数、RESET ADSR 按钮）；`syncFromProcessor()` 供 editor timer 调用。
- [ ] **Step 3: PluginEditor**：成员 `ana::EnvelopePage envPage_;` + `#include`；`setActivePage` 上限 5→6、`pageTabs_` 已自动；`resized()` 加 `case 6: envPage_.setBounds(ca);`；可见性切换；timer 里 `envPage_.syncFromProcessor();` 与 VOL 滑条同步（`timbreBlendSlider_` 附近的 sync 块旁，加 `&ModulationAssignPanel::getVolAttackSlider()` 等 4 个 getter）。
- [ ] **Step 4: ModulationAssignPanel** 暴露 `juce::Slider& getVolAttackSlider()…getVolReleaseSlider()`，回调保持现状（现已写 processor）。
- [ ] **Step 5: 提交并推送**

```bash
git push https://x-access-token:<PAT>@github.com/SakuraLuminance/notitle.git main
```

- [ ] **Step 6: 拉 CI 结果**（`Invoke-RestMethod` 打 GitHub REST；token 用 GCM 缓存），确认 570+ 用例全绿 + pluginval strictness 5。

---

## Self-Review

- **spec 覆盖**：引擎 API/loop 修正 → T1；EditOps+派生 ADSR → T2；Canvas → T3；锁/槽位/BPM/默认断点 → T4；第 7 页+同步 → T5；持久化/ENV3 → Round 2（spec §8 已声明分批）。
- **占位符**：无 TBD/TODO；未贴全的仅是 GUI paint 细节与 EditOps 实现体（步骤已给算式与签名）。
- **类型一致性**：`getEnvelopeSlot(int)`、`deriveADSR`、`setEnvelope`、`onEdited` 全计划统一命名。
