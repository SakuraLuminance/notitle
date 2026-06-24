#include "XYPad.h"
#include "CyberpunkTheme.h"
#include "../PluginProcessor.h"
#include <cmath>
#include <algorithm>

namespace ana {

//==============================================================================
static constexpr float cornerShrink = 12.0f;

//==============================================================================
XYPad::XYPad(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    juce::SettableTooltipClient::setTooltip("X: Morph Amount (0-100%) | Y: Modulation Depth (0-100%) | Drag to position, double-click to reset");
    startTimerHz(30);
}

XYPad::~XYPad()
{
    stopTimer();
}

//==============================================================================
void XYPad::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.fillAll(CyberpunkTheme::bg_);

    // Subtle inner glow
    g.setGradientFill(juce::ColourGradient::vertical(
        CyberpunkTheme::bg_.brighter(0.06f), bounds.getY(),
        CyberpunkTheme::bg_, bounds.getBottom()));
    g.fillRect(bounds);

    // Grid
    drawGrid(g, bounds);

    // Draw trail (fading magenta circles)
    drawTrail(g, bounds, trail_, CyberpunkTheme::magenta_);

    // Neon border
    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.15f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 3.0f);
    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Crosshair at current position
    auto pos = juce::Point<float>(x_.load(), y_.load());
    drawCrosshair(g, bounds.reduced(cornerShrink), pos, CyberpunkTheme::cyan_);

    // Corner accents
    auto cornerLen = 10.0f;
    auto b = bounds.reduced(2.0f);
    g.setColour(CyberpunkTheme::cyan_);
    // Top-left
    g.drawLine(b.getX(), b.getY(), b.getX() + cornerLen, b.getY());
    g.drawLine(b.getX(), b.getY(), b.getX(), b.getY() + cornerLen);
    // Bottom-right
    g.drawLine(b.getRight(), b.getBottom(), b.getRight() - cornerLen, b.getBottom());
    g.drawLine(b.getRight(), b.getBottom(), b.getRight(), b.getBottom() - cornerLen);

    // X axis label (bottom center)
    g.setFont(CyberpunkTheme::getCyberFont(10.0f, true));
    g.setColour(CyberpunkTheme::cyan_);
    auto xLabelBounds = bounds.removeFromBottom(14.0f).reduced(2.0f, 0.0f);
    g.drawText(xLabel_ + ": " + juce::String(static_cast<int>(x_.load() * 100.0f)),
               xLabelBounds.toNearestInt(), juce::Justification::centred);

    // Y axis label (right side, rotated)
    g.setFont(CyberpunkTheme::getCyberFont(10.0f, true));
    g.setColour(CyberpunkTheme::magenta_);
    auto yLabelBounds = bounds.removeFromRight(14.0f).reduced(0.0f, 2.0f);
    juce::Graphics::ScopedSaveState saveState(g);
    auto yText = yLabel_ + ": " + juce::String(static_cast<int>(y_.load() * 100.0f));
    auto centre = yLabelBounds.getCentre();
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi,
                                                     centre.x, centre.y));
    g.drawText(yText, yLabelBounds.toNearestInt(), juce::Justification::centred);

    // Y target indicator (top-left inside the pad)
    g.setFont(CyberpunkTheme::getCyberFont(8.0f, false));
    g.setColour(CyberpunkTheme::magenta_.withAlpha(0.6f));
    g.drawText("Y: " + yTargetToString(yTarget_),
               getLocalBounds().reduced(4).removeFromTop(12).toNearestInt(),
               juce::Justification::topRight);
}

void XYPad::resized()
{
}

void XYPad::timerCallback()
{
    const float timerDt = 1.0f / 30.0f;                // ~33ms per tick
    const float rampSec = rampTimeMs_ / 1000.0f;       // ramp duration in seconds
    const float smoothFactor = 1.0f - std::exp(-timerDt / juce::jmax(0.001f, rampSec));

    // --- Smooth interpolation: ramp X toward target ---
    {
        float currentX = x_.load(std::memory_order_relaxed);
        float diffX = targetX_ - currentX;
        if (std::abs(diffX) > 0.0005f)
        {
            float newX = currentX + diffX * smoothFactor;
            x_.store(newX, std::memory_order_relaxed);
            if (xParam_ != nullptr)
                xParam_->store(newX, std::memory_order_relaxed);
        }
        else if (currentX != targetX_)
        {
            x_.store(targetX_, std::memory_order_relaxed);
            if (xParam_ != nullptr)
                xParam_->store(targetX_, std::memory_order_relaxed);
        }
    }

    // --- Smooth interpolation: ramp Y toward target ---
    {
        float currentY = y_.load(std::memory_order_relaxed);
        float diffY = targetY_ - currentY;
        if (std::abs(diffY) > 0.0005f)
        {
            float newY = currentY + diffY * smoothFactor;
            y_.store(newY, std::memory_order_relaxed);
            if (yParam_ != nullptr)
                yParam_->store(newY, std::memory_order_relaxed);
        }
        else if (currentY != targetY_)
        {
            y_.store(targetY_, std::memory_order_relaxed);
            if (yParam_ != nullptr)
                yParam_->store(targetY_, std::memory_order_relaxed);
        }
    }

    // --- Poll MIDI Learn atomics for external changes ---
    {
        float xLearn = xLearnAtomic_.load(std::memory_order_relaxed);
        if (std::abs(xLearn - lastXLearn_) > 0.001f)
        {
            lastXLearn_ = xLearn;
            targetX_ = xLearn;
        }

        float yLearn = yLearnAtomic_.load(std::memory_order_relaxed);
        if (std::abs(yLearn - lastYLearn_) > 0.001f)
        {
            lastYLearn_ = yLearn;
            targetY_ = yLearn;
        }
    }

    // --- Notify listeners when values settle (avoid flooding) ---
    {
        // Use a coarser check: notify roughly every ~100ms during drag
        static int tickCount = 0;
        if (++tickCount % 3 == 0)
            listeners_.call([this](Listener& l) { l.xyPadChanged(this, x_.load(), y_.load()); });
    }

    // --- Age the trail points and remove fully faded ones ---
    for (auto& tp : trail_)
        tp.age = std::min(tp.age + 0.08f, 1.0f);

    while (!trail_.empty() && trail_.front().age >= 1.0f)
        trail_.pop_front();

    repaint();
}

//==============================================================================
void XYPad::setXParameter(std::atomic<float>* param, const juce::String& label)
{
    xParam_ = param;
    xLabel_ = label;
    if (param != nullptr)
    {
        float v = param->load();
        targetX_ = v;
        x_.store(v);
    }
}

void XYPad::setYParameter(std::atomic<float>* param, const juce::String& label)
{
    yParam_ = param;
    yLabel_ = label;
    if (param != nullptr)
    {
        float v = param->load();
        targetY_ = v;
        y_.store(v);
    }
}

//==============================================================================
void XYPad::setYTarget(YTarget target)
{
    yTarget_ = target;
    repaint();
}

juce::String XYPad::yTargetToString(YTarget t)
{
    switch (t)
    {
        case YTarget::Cutoff:    return "CUTOFF";
        case YTarget::Resonance: return "RES";
        case YTarget::Volume:    return "VOL";
        case YTarget::LFORate:   return "LFO RATE";
        case YTarget::LFODepth:  return "LFO DEPTH";
        default:                 return "?";
    }
}

//==============================================================================
void XYPad::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        showContextMenu(e);
        return;
    }

    isDragging_ = true;
    setTargetPosition(e.position);
}

void XYPad::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDragging_)
        return;
    setTargetPosition(e.position);
}

void XYPad::mouseUp(const juce::MouseEvent& e)
{
    if (!isDragging_)
        return;
    isDragging_ = false;
    setTargetPosition(e.position);
}

//==============================================================================
juce::Point<float> XYPad::mouseToNormalized(juce::Point<float> mousePos) const
{
    auto bounds = getLocalBounds().toFloat().reduced(cornerShrink);
    if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
        return { 0.5f, 0.5f };

    float nx = (mousePos.x - bounds.getX()) / bounds.getWidth();
    float ny = 1.0f - (mousePos.y - bounds.getY()) / bounds.getHeight(); // flip Y

    return { juce::jlimit(0.0f, 1.0f, nx),
             juce::jlimit(0.0f, 1.0f, ny) };
}

void XYPad::setTargetPosition(juce::Point<float> p)
{
    auto norm = mouseToNormalized(p);

    // Set target (smooth interpolation will ramp toward it)
    targetX_ = norm.x;
    targetY_ = norm.y;

    // Add to trail
    trail_.push_back({ targetX_, targetY_, 0.0f });
    while (static_cast<int>(trail_.size()) > maxTrailLength_)
        trail_.pop_front();

    // Immediate listener notification for responsive UI
    listeners_.call([this](Listener& l) {
        l.xyPadChanged(this, targetX_, targetY_);
    });
}

//==============================================================================
void XYPad::showContextMenu(const juce::MouseEvent& e)
{
    juce::PopupMenu menu;

    // --- Y Target submenu ---
    juce::PopupMenu yTargetMenu;
    auto addYItem = [&](YTarget t) {
        yTargetMenu.addItem(yTargetToString(t), true, yTarget_ == t,
                            [this, t]() { setYTarget(t); });
    };
    addYItem(YTarget::Cutoff);
    addYItem(YTarget::Resonance);
    addYItem(YTarget::Volume);
    addYItem(YTarget::LFORate);
    addYItem(YTarget::LFODepth);
    menu.addSubMenu("Y Target", yTargetMenu);
    menu.addSeparator();

    // --- MIDI Learn X ---
    auto& midiLearn = processor_.getMidiLearn();
    bool xMapped = false;
    bool yMapped = false;
    int xCC = -1, yCC = -1;
    bool xGlobal = false, yGlobal = false;
    for (const auto& m : midiLearn.getMappings())
    {
        if (m.parameterId == getXParamId()) { xMapped = true; xCC = m.ccNumber; xGlobal = m.isGlobal; }
        if (m.parameterId == getYParamId()) { yMapped = true; yCC = m.ccNumber; yGlobal = m.isGlobal; }
    }

    if (midiLearn.isLearning())
    {
        juce::PopupMenu::Item item;
        item.itemID = 1000;
        item.text = "MIDI Learn (in progress...)";
        item.isEnabled = false;
        item.isTicked = false;
        menu.addItem(std::move(item));
    }
    else
    {
        {
            juce::PopupMenu::Item item;
            item.itemID = 1001;
            item.text = "MIDI Learn X" + (xMapped ? juce::String(" [CC ") + juce::String(xCC) + "]" : "");
            item.action = [this]() { startLearnX(); };
            menu.addItem(std::move(item));
        }

        {
            juce::PopupMenu::Item item;
            item.itemID = 1002;
            item.text = "MIDI Learn Y" + (yMapped ? juce::String(" [CC ") + juce::String(yCC) + "]" : "");
            item.action = [this]() { startLearnY(); };
            menu.addItem(std::move(item));
        }

        if (xMapped)
        {
            {
                juce::PopupMenu::Item item;
                item.itemID = 1003;
                item.text = "Clear X Mapping (CC " + juce::String(xCC) + ")";
                item.action = [this, xCC]() { processor_.getMidiLearn().removeMapping(xCC); };
                menu.addItem(std::move(item));
            }
            {
                juce::PopupMenu::Item item;
                item.itemID = 1004;
                item.text = "Global X (survives presets)";
                item.isTicked = xGlobal;
                item.action = [this, xGlobal]() { processor_.getMidiLearn().setMappingGlobal(getXParamId(), !xGlobal); };
                menu.addItem(std::move(item));
            }
        }

        if (yMapped)
        {
            {
                juce::PopupMenu::Item item;
                item.itemID = 1005;
                item.text = "Clear Y Mapping (CC " + juce::String(yCC) + ")";
                item.action = [this, yCC]() { processor_.getMidiLearn().removeMapping(yCC); };
                menu.addItem(std::move(item));
            }
            {
                juce::PopupMenu::Item item;
                item.itemID = 1006;
                item.text = "Global Y (survives presets)";
                item.isTicked = yGlobal;
                item.action = [this, yGlobal]() { processor_.getMidiLearn().setMappingGlobal(getYParamId(), !yGlobal); };
                menu.addItem(std::move(item));
            }
        }
    }

    menu.showMenuAsync(juce::PopupMenu::Options());
}

void XYPad::startLearnX()
{
    processor_.getMidiLearn().startLearn(getXParamId(), &xLearnAtomic_, 0.0f, 1.0f);
}

void XYPad::startLearnY()
{
    processor_.getMidiLearn().startLearn(getYParamId(), &yLearnAtomic_, 0.0f, 1.0f);
}

//==============================================================================
void XYPad::drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const float gridSpacing = bounds.getWidth() / 8.0f;
    const int numLines = static_cast<int>(bounds.getWidth() / gridSpacing);

    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.12f));

    for (int i = 1; i < numLines; ++i)
    {
        float x = bounds.getX() + static_cast<float>(i) * gridSpacing;
        g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 0.5f);
    }

    for (int i = 1; i < numLines; ++i)
    {
        float y = bounds.getY() + static_cast<float>(i) * gridSpacing;
        g.drawLine(bounds.getX(), y, bounds.getRight(), y, 0.5f);
    }

    // Center crosshair guides (subtle)
    float cx = bounds.getCentreX();
    float cy = bounds.getCentreY();
    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.08f));
    g.drawLine(cx, bounds.getY(), cx, bounds.getBottom(), 1.0f);
    g.drawLine(bounds.getX(), cy, bounds.getRight(), cy, 1.0f);
}

void XYPad::drawCrosshair(juce::Graphics& g, juce::Rectangle<float> bounds,
                            juce::Point<float> pos, juce::Colour colour)
{
    float px = bounds.getX() + pos.x * bounds.getWidth();
    float py = bounds.getY() + (1.0f - pos.y) * bounds.getHeight();

    // Glow (outer aura)
    g.setColour(colour.withAlpha(0.15f));
    g.drawLine(bounds.getX(), py, bounds.getRight(), py, 4.0f);
    g.drawLine(px, bounds.getY(), px, bounds.getBottom(), 4.0f);

    // Main crosshair lines
    g.setColour(colour.withAlpha(0.7f));
    g.drawLine(bounds.getX(), py, bounds.getRight(), py, 1.5f);
    g.drawLine(px, bounds.getY(), px, bounds.getBottom(), 1.5f);

    // Center target ring
    g.setColour(colour.withAlpha(0.6f));
    g.drawEllipse(px - 6.0f, py - 6.0f, 12.0f, 12.0f, 1.5f);

    // Outer ring glow
    g.setColour(colour.withAlpha(0.2f));
    g.drawEllipse(px - 10.0f, py - 10.0f, 20.0f, 20.0f, 2.0f);

    // Center dot
    g.setColour(colour);
    g.fillEllipse(px - 2.5f, py - 2.5f, 5.0f, 5.0f);

    // Inner bright core
    g.setColour(CyberpunkTheme::fg_.withAlpha(0.8f));
    g.fillEllipse(px - 1.0f, py - 1.0f, 2.0f, 2.0f);
}

void XYPad::drawTrail(juce::Graphics& g, juce::Rectangle<float> bounds,
                       const std::deque<TrailPoint>& trail, juce::Colour colour)
{
    if (trail.empty())
        return;

    for (const auto& tp : trail)
    {
        float px = bounds.getX() + tp.x * bounds.getWidth();
        float py = bounds.getY() + (1.0f - tp.y) * bounds.getHeight();
        float alpha = 1.0f - tp.age;
        float radius = 3.0f + (1.0f - tp.age) * 3.0f;

        g.setColour(colour.withAlpha(alpha * 0.5f));
        g.fillEllipse(px - radius, py - radius, radius * 2.0f, radius * 2.0f);
    }
}

} // namespace ana
