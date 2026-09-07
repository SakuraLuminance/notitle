# P1 Live Spectrum Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 中央面板任何状态下显示随最终输出变化的实时频谱（LIVE 成为默认视图）。

**Architecture:** 复用已有的无锁 scope 捕获（`captureScopeOutput`，processBlock 末尾无条件调用）作为数据源；新 `ana::LiveSpectrumPanel` 在编辑器 30 Hz 主 timer 里做 2048 点实数 FFT + 对数频率柱状显示 + 峰值保持；视图模式在 PARTIALS 家族前插入 LIVE 并设为默认；把 SCOPE 推送移出 `isEngineLoaded()` 门。

**Tech Stack:** JUCE 8.0.17 (juce_dsp FFT), Catch2, 现有 CyberpunkTheme。

## Global Constraints

- 本机（F:\anaplug）**无编译工具链**——所有验证 = commit → push → GitHub Actions Run（~20 min）→ 日志取证。TDD 的红绿循环坍缩为"测试与实现同批提交、单轮 CI 验证"。
- push 方式：`git push https://x-access-token:<PAT>@github.com/SakuraLuminance/notitle.git main`（PAT 会话内提供；push 后 `git fetch origin main` 刷新本地跟踪 ref）。
- JUCE 8 `AudioBuffer` 构造不清零——本计划一律用 `std::vector`（值初始化）/显式 clear。
- 命名与风格：`namespace ana`；颜色一律取自 `CyberpunkTheme`；新 .cpp 必须同时加入 **plugin 目标（CMakeLists.txt）与 tests 目标（tests/CMakeLists.txt）**。
- 测试基线：**539 用例 0 失败 + pluginval strictness 2 绿**（Run #102 起的有效基线，不得回归）。
- 提交信息风格：单行长描述、conventional 前缀（feat/fix/ci/test/docs）。

---

### Task 1: `ana::LiveSpectrumPanel` 组件 + 构建接入

**Files:**
- Create: `src/gui/LiveSpectrumPanel.h`
- Create: `src/gui/LiveSpectrumPanel.cpp`
- Modify: `CMakeLists.txt`（plugin 源列表，`src/gui/panels/MasterSection.cpp` 之后）
- Modify: `tests/CMakeLists.txt`（测试源列表，`../src/gui/SpectrumDisplay.cpp` 之后）

**Interfaces:**
- Consumes: `AnaPlugAudioProcessor::getScopeOutput(std::vector<float>&) const`（PluginProcessor.h:346，2048 点）。
- Produces:
  - `void LiveSpectrumPanel::updateFromSamples(const float* data, int numSamples)` — 核心 FFT 路径（测试直接驱动）
  - `void LiveSpectrumPanel::updateFromProcessor(AnaPlugAudioProcessor& p)` — 编辑器 timer 调用
  - paint() 自包含（无外部状态）

- [ ] **Step 1: 写 `src/gui/LiveSpectrumPanel.h`**

```cpp
#pragma once

#include "../../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace ana
{

class LiveSpectrumPanel : public juce::Component
{
public:
    LiveSpectrumPanel();

    void paint(juce::Graphics&) override;
    void resized() override;

    void updateFromProcessor(AnaPlugAudioProcessor& processor);
    void updateFromSamples(const float* data, int numSamples);

private:
    void rebuildBarMap();
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;          // 2048
    static constexpr int numBins = fftSize / 2 + 1;        // 1025
    static constexpr float kMinDb = -72.0f;
    static constexpr float kMaxDb = 0.0f;
    static constexpr float kPeakDecay = 0.95f;             // per tick @30 Hz (~1 s)

    juce::dsp::FFT fftEngine_{ fftOrder };
    std::array<float, fftSize> hannWindow_{};
    std::vector<float> fftBuffer_;                          // 2*fftSize interleaved
    std::vector<float> magnitudes_;                         // numBins linear magnitudes
    std::vector<float> barLevels_;                          // per-bar normalised 0..1
    std::vector<float> peakLevels_;                         // per-bar peak hold
    std::vector<int>   barBinStart_;                        // per-bar first bin
    std::vector<int>   barBinEnd_;                          // per-bar last bin (exclusive)
    std::array<float, 3> freqTickHz_{ { 100.0f, 1000.0f, 10000.0f } };
};

} // namespace ana
```

- [ ] **Step 2: 写 `src/gui/LiveSpectrumPanel.cpp`**

```cpp
#include "LiveSpectrumPanel.h"
#include "CyberpunkTheme.h"
#include <cmath>

namespace ana
{

LiveSpectrumPanel::LiveSpectrumPanel()
{
    fftBuffer_.assign(static_cast<size_t>(fftSize) * 2, 0.0f);
    magnitudes_.assign(numBins, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        hannWindow_[(size_t) i] = 0.5f * (1.0f - std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(fftSize - 1)));
    rebuildBarMap();
}

void LiveSpectrumPanel::resized()
{
    rebuildBarMap();
}

void LiveSpectrumPanel::rebuildBarMap()
{
    const int barCount = juce::jlimit(40, 64, getWidth() / 6);
    barLevels_.assign(static_cast<size_t>(barCount), 0.0f);
    peakLevels_.assign(static_cast<size_t>(barCount), 0.0f);
    barBinStart_.assign(static_cast<size_t>(barCount), 0);
    barBinEnd_.assign(static_cast<size_t>(barCount), 1);

    // Nominal 48 kHz bin width for axis mapping (labels are approximate at other rates)
    constexpr float binHz = 48000.0f / static_cast<float>(fftSize);
    constexpr float fMin = 20.0f;
    constexpr float fMax = 20000.0f;
    const float ratio = fMax / fMin;

    for (int b = 0; b < barCount; ++b)
    {
        const float fLo = fMin * std::pow(ratio, static_cast<float>(b) / barCount);
        const float fHi = fMin * std::pow(ratio, static_cast<float>(b + 1) / barCount);
        int lo = static_cast<int>(std::floor(fLo / binHz));
        int hi = static_cast<int>(std::ceil(fHi / binHz));
        lo = juce::jlimit(1, numBins - 1, lo);
        hi = juce::jlimit(lo + 1, numBins, hi);
        barBinStart_[(size_t) b] = lo;
        barBinEnd_[(size_t) b] = hi;
    }
}

void LiveSpectrumPanel::updateFromProcessor(AnaPlugAudioProcessor& processor)
{
    std::vector<float> scope;
    if (processor.getScopeOutput(scope))
        updateFromSamples(scope.data(), static_cast<int>(scope.size()));
}

void LiveSpectrumPanel::updateFromSamples(const float* data, int numSamples)
{
    if (data == nullptr || numSamples <= 0)
        return;

    // Window into the interleaved real-FFT buffer (rest is scratch, zeroed once)
    const int n = juce::jlimit(1, fftSize, numSamples);
    for (int i = 0; i < n; ++i)
        fftBuffer_[(size_t) i] = data[i] * hannWindow_[(size_t) i];
    for (int i = n; i < fftSize; ++i)
        fftBuffer_[(size_t) i] = 0.0f;

    fftEngine_.performRealOnlyForwardTransform(fftBuffer_.data(), true);
    for (int i = 0; i < numBins; ++i)
    {
        const float re = fftBuffer_[(size_t) (2 * i)];
        const float im = fftBuffer_[(size_t) (2 * i + 1)];
        magnitudes_[(size_t) i] = std::sqrt(re * re + im * im);
    }

    // Aggregate bins into log-spaced bars (max energy per band)
    const int barCount = static_cast<int>(barLevels_.size());
    for (int b = 0; b < barCount; ++b)
    {
        float peak = 0.0f;
        for (int i = barBinStart_[(size_t) b]; i < barBinEnd_[(size_t) b]; ++i)
            peak = juce::jmax(peak, magnitudes_[(size_t) i]);
        const float db = juce::Decibels::gainToDecibels(peak, 1.0e-9f);
        barLevels_[(size_t) b] = juce::jlimit(0.0f, 1.0f,
            (db - kMinDb) / (kMaxDb - kMinDb));
    }

    // Peak hold with decay
    for (int b = 0; b < barCount; ++b)
    {
        float& pk = peakLevels_[(size_t) b];
        pk *= kPeakDecay;
        pk = juce::jmax(pk, barLevels_[(size_t) b]);
    }

    repaint();
}

void LiveSpectrumPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll(CyberpunkTheme::bg_);
    CyberpunkTheme::drawGridBackground(g, getLocalBounds());

    const int barCount = static_cast<int>(barLevels_.size());
    if (barCount == 0)
        return;

    const float axisHeight = 12.0f;
    const float plotHeight = bounds.getHeight() - axisHeight;
    const float barW = bounds.getWidth() / static_cast<float>(barCount);

    for (int b = 0; b < barCount; ++b)
    {
        const float t = juce::jlimit(0.0f, 1.0f, barLevels_[(size_t) b]);
        const float h = t * plotHeight;
        const float x = bounds.getX() + b * barW;

        juce::Colour barColour;
        if (t < 0.6f)
            barColour = CyberpunkTheme::fg_.darker(0.4f)
                .interpolatedWith(CyberpunkTheme::cyan_, t / 0.6f);
        else
            barColour = CyberpunkTheme::cyan_
                .interpolatedWith(CyberpunkTheme::magenta_, (t - 0.6f) / 0.4f);

        if (h > 0.5f)
        {
            g.setColour(barColour);
            g.fillRect(x + 0.5f, bounds.getY() + plotHeight - h, barW - 1.0f, h);
        }

        const float pk = juce::jlimit(0.0f, 1.0f, peakLevels_[(size_t) b]);
        if (pk > 0.01f)
        {
            g.setColour(barColour.withAlpha(0.8f));
            g.fillRect(x + 0.5f, bounds.getY() + plotHeight - pk * plotHeight - 2.0f,
                       barW - 1.0f, 2.0f);
        }
    }

    // Frequency axis ticks (nominal 48 kHz mapping)
    g.setFont(CyberpunkTheme::getCyberFont(8.0f, false));
    g.setColour(CyberpunkTheme::fg_.withAlpha(0.4f));
    const float logSpan = std::log10(20000.0f / 20.0f);
    for (const float tick : freqTickHz_)
    {
        const float frac = std::log10(tick / 20.0f) / logSpan;
        const float x = bounds.getX() + frac * bounds.getWidth();
        g.drawText(tick >= 1000.0f
                       ? juce::String(tick / 1000.0f, 0) + "k"
                       : juce::String(tick, 0),
                   juce::Rectangle<float>(x - 12.0f, bounds.getBottom() - axisHeight,
                                          24.0f, axisHeight),
                   juce::Justification::centred);
    }

    // Idle marker when everything is silent
    bool anySignal = false;
    for (const float lv : barLevels_)
        if (lv > 0.01f) { anySignal = true; break; }
    if (!anySignal)
    {
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.35f));
        g.setFont(CyberpunkTheme::getCyberFont(12.0f, false));
        g.drawText("-inf dB", getLocalBounds(),
                   juce::Justification::centred);
    }
}

} // namespace ana
```

注意：`-inf dB` 文案画在柱层之上居中（spec 要求 idle 提示）；若与柱重叠视觉可接受（静音时柱高 0）。

- [ ] **Step 3: CMakeLists.txt 接入（plugin 目标）**

在 `src/gui/panels/MasterSection.cpp` 之后加一行：

```
        src/gui/LiveSpectrumPanel.cpp
```

- [ ] **Step 4: tests/CMakeLists.txt 接入（测试目标）**

在 `../src/gui/SpectrumDisplay.cpp` 之后加一行：

```
    ../src/gui/LiveSpectrumPanel.cpp
```

（juce_dsp 已在 tests 目标链接清单中——tests/CMakeLists.txt:186 ✓）

### Task 2: headless UI 测试

**Files:**
- Modify: `tests/test_ui_paint.cpp`

**Interfaces:**
- Consumes: `ana::LiveSpectrumPanel::updateFromSamples(const float*, int)`、paint()。

- [ ] **Step 1: 在 test_ui_paint.cpp 追加 include 与两个用例（文件顶部加 `#include "gui/LiveSpectrumPanel.h"`）**

```cpp
TEST_CASE("LiveSpectrumPanel paint does not crash without data", "[ui][paint]")
{
    ana::LiveSpectrumPanel panel;
    panel.setBounds(0, 0, 200, 200);

    juce::Image image(juce::Image::ARGB, 200, 200, true);
    juce::Graphics g(image);
    REQUIRE_NOTHROW(panel.paint(g));
    REQUIRE(true);
}

TEST_CASE("LiveSpectrumPanel FFT path handles sine input", "[ui][paint]")
{
    ana::LiveSpectrumPanel panel;
    panel.setBounds(0, 0, 200, 200);
    panel.resized();

    std::vector<float> sine(2048, 0.0f);
    for (int i = 0; i < 2048; ++i)
        sine[(size_t) i] = 0.8f * std::sin(2.0f * juce::MathConstants<float>::pi
            * 1000.0f * static_cast<float>(i) / 48000.0f);

    juce::Image image(juce::Image::ARGB, 200, 200, true);
    juce::Graphics g(image);
    REQUIRE_NOTHROW(panel.updateFromSamples(sine.data(), 2048));
    REQUIRE_NOTHROW(panel.paint(g));

    // Zero-length / null guards
    REQUIRE_NOTHROW(panel.updateFromSamples(nullptr, 0));
    REQUIRE_NOTHROW(panel.paint(g));
    REQUIRE(true);
}
```

（test_ui_paint.cpp 已含 `#include <cmath>` 需求——若无则补；文件当前未含 cmath，Step 1 需同时加 `#include <cmath>` 与 `<vector>`。）

### Task 3: 编辑器集成（LIVE 默认视图 + gate 拆除）

**Files:**
- Modify: `src/PluginEditor.h`（include + 成员）
- Modify: `src/PluginEditor.cpp`（ctor 视图区、onViewModeChanged、resized、timerCallback）

**Interfaces:**
- Consumes: Task 1 的 `LiveSpectrumPanel::updateFromProcessor`。

- [ ] **Step 1: PluginEditor.h** — include 区加：

```cpp
#include "gui/LiveSpectrumPanel.h"
```

center 成员区（`ana::VisualFeedbackPanel feedbackPanel_;` 之前）加：

```cpp
    ana::LiveSpectrumPanel liveSpectrumPanel_;
```

- [ ] **Step 2: PluginEditor.cpp ctor 视图区**（当前 49-69 行区域）改为：

```cpp
    //==============================================================================
    // Center 鈥?Visual feedback + view selector
    ANA_CRUMB("ed:center-start");
    addAndMakeVisible(liveSpectrumPanel_);
    addAndMakeVisible(feedbackPanel_);
    feedbackPanel_.setVisible(false);
    addAndMakeVisible(waterfallDisplay_);
    waterfallDisplay_.setVisible(false);
    addAndMakeVisible(spectrumEditorCanvas_);
    spectrumEditorCanvas_.setVisible(false);
    viewModeCombo_.addItem("LIVE", 1);
    viewModeCombo_.addItem("PARTIALS", 2);
    viewModeCombo_.addItem("WATERFALL", 3);
    viewModeCombo_.addItem("EDITOR", 4);
    viewModeCombo_.addItem("3D", 5);
    viewModeCombo_.addItem("SCOPE", 6);
    viewModeCombo_.setSelectedId(1);
    viewModeCombo_.onChange = [this] { onViewModeChanged(); };
    viewModeCombo_.setTooltip("View mode: LIVE/PARTIALS/WATERFALL/EDITOR/3D/SCOPE");
    addAndMakeVisible(viewModeCombo_);

    // Oscilloscope view (hidden by default; data push no longer gated on engine)
    waveformDisplay_ = std::make_unique<ana::WaveformDisplay>();
    addAndMakeVisible(waveformDisplay_.get());
    waveformDisplay_->setVisible(false);
```

（关键变化：`feedbackPanel_.setVisible(false)`——默认视图由 PARTIALS 改为 LIVE。）

- [ ] **Step 3: onViewModeChanged** 改为：

```cpp
void AnaPlugAudioProcessorEditor::onViewModeChanged()
{
    const int mode = viewModeCombo_.getSelectedId();

    // Hide all view panels first
    liveSpectrumPanel_.setVisible(false);
    feedbackPanel_.setVisible(false);
    waterfallDisplay_.setVisible(false);
    spectrumEditorCanvas_.setVisible(false);
    if (waveformDisplay_)
        waveformDisplay_->setVisible(false);

    switch (mode)
    {
        case 1: // LIVE 鈥?real-time output spectrum (always available)
            liveSpectrumPanel_.setVisible(true);
            break;

        case 2: // PARTIALS 鈥?classic bar display
            feedbackPanel_.setVisible(true);
            break;

        case 3: // WATERFALL 鈥?3D waterfall spectral view
            waterfallDisplay_.setVisible(true);
            break;

        case 4: // EDITOR 鈥?2D spectrum editor canvas
            spectrumEditorCanvas_.setVisible(true);
            spectrumEditorCanvas_.set3DEnabled(false);
            break;

        case 5: // 3D 鈥?spectrum editor with OpenGL 3D waterfall
            spectrumEditorCanvas_.setVisible(true);
            spectrumEditorCanvas_.set3DEnabled(true);
            break;

        case 6: // SCOPE 鈥?real-time oscilloscope
            if (waveformDisplay_)
                waveformDisplay_->setVisible(true);
            break;

        default: // fallback to live
            liveSpectrumPanel_.setVisible(true);
            break;
    }
}
```

- [ ] **Step 4: resized() center 区** — 在 `feedbackPanel_.setBounds(fbArea.reduced(2));` 之前加：

```cpp
    liveSpectrumPanel_.setBounds(fbArea.reduced(2));
```

- [ ] **Step 5: timerCallback** — 把 SCOPE 推送块**移出** `isEngineLoaded()` 门，并在 timer 顶部（`updateMidiLearnState();` 之后）加：

```cpp
    // Live spectrum always tracks the final output (independent of the sample engine)
    liveSpectrumPanel_.updateFromProcessor(audioProcessor);
```

结构变化前（现状）：

```cpp
    if (audioProcessor.isEngineLoaded())
    {
        ...partials block...
        // Push scope buffer data to WaveformDisplay when SCOPE mode is active
        if (waveformDisplay_ && waveformDisplay_->isVisible())
        { ... }
    }
```

变化后：

```cpp
    liveSpectrumPanel_.updateFromProcessor(audioProcessor);

    // Push scope buffer data to WaveformDisplay when SCOPE mode is active
    // (scope capture is unconditional in processBlock; no engine dependency)
    if (waveformDisplay_ && waveformDisplay_->isVisible())
    {
        std::vector<float> scopeData;
        if (audioProcessor.getScopeOutput(scopeData))
        {
            waveformDisplay_->setSamples(scopeData);
            waveformDisplay_->setPlaybackPosition(
                static_cast<double>(audioProcessor.getPlaybackPosition()
                                    % audioProcessor.kScopeBufferSize));
        }
    }
    if (audioProcessor.isEngineLoaded())
    {
        ...partials block unchanged...
    }
```

### Task 4: 自审 + 提交推送 + CI 取证

- [ ] **Step 1: 自审清单**（本机无编译器，逐条人眼核对）
  - `performRealOnlyForwardTransform` 前.fftBuffer_ 前半已写入、后半 scratch 保留 0 ✓
  - `resized()` 在 ctor 内首次 setSize 时执行 → `rebuildBarMap()` 用 `getWidth()=0` → barCount=jlimit(40,64,0)=40，不崩（下限兜底）✓
  - `updateFromSamples` 的 null/0 守卫 ✓
  - `numBins` 越界：binHz=48000/2048≈23.4，20 kHz → bin 854 < 1024 ✓（48k 标称；96k 宿主时 20 kHz→bin 427，更安全）
  - combo 项 id 1-6 与 switch case 一致 ✓
  - `feedbackPanel_` 默认隐藏后，timer 里 `feedbackPanel_.updatePartials()` 照旧调用（隐藏也安全）✓
  - 测试目标：LiveSpectrumPanel.cpp 已加、无新外部依赖 ✓
- [ ] **Step 2: commit（单提交，feat 前缀，正文写明 scope 复用与 gate 修复）**
- [ ] **Step 3: push（token URL）→ `git fetch origin main` → 记录 Run 号 → 轮询至完成（90 s 间隔）**
- [ ] **Step 4: 取证**：绿 → 拉日志确认 `All tests passed`（预期 541 用例：539+2）与 `pluginval exit: 0`；红 → jobs/logs + test-forensics artifact 按 §6 工作流定位
- [ ] **Step 5: 绿后下载 VST3+CLAP artifact 覆盖 `artifacts/`（新一轮成品），重装到 `C:\Program Files\Common Files\VST3\`（UAC），清除 reaper-vstplugins64.ini 的 AnaPlug 缓存条目**

### Task 5: 文档与状态

- [ ] HANDOFF_V2 §0.1 加 Run 行；P1 标记完成
- [ ] 更新项目记忆（路线图进度）
- [ ] 提交 docs（可与下一批（P2）同推）

## Self-Review

1. **Spec coverage**: LIVE 默认视图 ✓（Task 3 Step 2）；新组件+FFT+峰值保持+主题色 ✓（Task 1）；SCOPE gate 拆除 ✓（Task 3 Step 5）；"No partials active" 保留在 PARTIALS ✓（未改动）；headless 测试 ✓（Task 2）；无音频线程改动 ✓；48k 标称轴标注已声明近似 ✓。
2. **Placeholder scan**: 无 TBD/TODO；所有代码块完整。
3. **Type consistency**: `updateFromSamples(const float*, int)` / `updateFromProcessor(AnaPlugAudioProcessor&)` 在 Task 1 定义、Task 2/3 引用一致；`fftOrder=11` 与 `fftSize=2048` 一致（spec 说 2048 点 ✓）。
