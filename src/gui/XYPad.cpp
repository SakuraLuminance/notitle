#include "XYPad.h"
#include "CyberpunkTheme.h"
#include <cmath>

namespace ana {

//==============================================================================
XYPad::XYPad()
{
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
    drawCrosshair(g, bounds.reduced(12.0f), pos, CyberpunkTheme::cyan_);

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
    // Draw vertical text by saving/restoring transform
    juce::Graphics::ScopedSaveState saveState(g);
    auto yText = yLabel_ + ": " + juce::String(static_cast<int>(y_.load() * 100.0f));
    auto centre = yLabelBounds.getCentre();
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi,
                                                     centre.x, centre.y));
    g.drawText(yText, yLabelBounds.toNearestInt(), juce::Justification::centred);
}

void XYPad::resized()
{
}

void XYPad::timerCallback()
{
    // If bound to external atomics, read from them to stay in sync
    if (xParam_ != nullptr)
        x_.store(xParam_->load());
    if (yParam_ != nullptr)
        y_.store(yParam_->load());

    // Age the trail points and remove fully faded ones
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
        x_.store(param->load());
}

void XYPad::setYParameter(std::atomic<float>* param, const juce::String& label)
{
    yParam_ = param;
    yLabel_ = label;
    if (param != nullptr)
        y_.store(param->load());
}

//==============================================================================
void XYPad::mouseDown(const juce::MouseEvent& e)
{
    isDragging_ = true;
    setPosition(e.position);
}

void XYPad::mouseDrag(const juce::MouseEvent& e)
{
    setPosition(e.position);
}

void XYPad::mouseUp(const juce::MouseEvent& e)
{
    isDragging_ = false;
    setPosition(e.position);
}

//==============================================================================
juce::Point<float> XYPad::getPosition() const
{
    return { x_.load(), y_.load() };
}

void XYPad::setPosition(juce::Point<float> p)
{
    auto bounds = getLocalBounds().toFloat().reduced(12.0f);
    if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
        return;

    // Convert from pixel to normalized [0,1] coords
    float nx = (p.x - bounds.getX()) / bounds.getWidth();
    float ny = 1.0f - (p.y - bounds.getY()) / bounds.getHeight(); // flip Y so top = 1

    nx = juce::jlimit(0.0f, 1.0f, nx);
    ny = juce::jlimit(0.0f, 1.0f, ny);

    // Update internal atomics
    if (x_.load() != nx)
        x_.store(nx);
    if (y_.load() != ny)
        y_.store(ny);

    // Push to external bound parameters
    if (xParam_ != nullptr)
        xParam_->store(nx);
    if (yParam_ != nullptr)
        yParam_->store(ny);

    // Add to trail
    trail_.push_back({ nx, ny, 0.0f });
    while (static_cast<int>(trail_.size()) > maxTrailLength_)
        trail_.pop_front();
}

//==============================================================================
void XYPad::drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const float gridSpacing = bounds.getWidth() / 8.0f;
    const int numLines = static_cast<int>(bounds.getWidth() / gridSpacing);

    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.04f));

    // Vertical lines
    for (int i = 1; i < numLines; ++i)
    {
        float x = bounds.getX() + static_cast<float>(i) * gridSpacing;
        g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 0.5f);
    }

    // Horizontal lines
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
    // Convert normalized position to pixel
    float px = bounds.getX() + pos.x * bounds.getWidth();
    float py = bounds.getY() + (1.0f - pos.y) * bounds.getHeight();

    // Glow (outer aura)
    g.setColour(colour.withAlpha(0.15f));

    // Horizontal glow bar
    g.drawLine(bounds.getX(), py, bounds.getRight(), py, 4.0f);
    // Vertical glow bar
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
    g.setColour(juce::Colours::white.withAlpha(0.8f));
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
