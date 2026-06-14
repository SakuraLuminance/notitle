#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ana {

//==============================================================================
/**
    Cyberpunk 2077-inspired LookAndFeel for AnaPlug.
    
    Dark background (#0a0a14), cyan/magenta/yellow neon accents,
    sharp geometric borders, futuristic typography, scan-line overlays.
*/
class CyberpunkTheme : public juce::LookAndFeel_V4
{
public:
    enum class ThemeType { Dark, Light };

    CyberpunkTheme()
    {
        setThemeType(ThemeType::Dark);
    }

    //==============================================================================
    static CyberpunkTheme& getInstance()
    {
        static CyberpunkTheme instance;
        return instance;
    }

    //==============================================================================
    void setThemeType(ThemeType type)
    {
        if (type == ThemeType::Light)
        {
            bg_      = juce::Colour(0xf0, 0xf0, 0xf5); // light gray
            fg_      = juce::Colour(0x1a, 0x1a, 0x24); // dark gray
            cyan_    = juce::Colour(0x00, 0xb0, 0xc0); // darker cyan for visibility
            magenta_ = juce::Colour(0xd0, 0x00, 0x45);
            yellow_  = juce::Colour(0xd0, 0xa0, 0x00);
        }
        else
        {
            bg_      = juce::Colour(0x0a, 0x0a, 0x14); // near-black
            fg_      = juce::Colour(0xc0, 0xc0, 0xd0); // light gray
            cyan_    = juce::Colour(0x00, 0xf0, 0xff); // neon cyan
            magenta_ = juce::Colour(0xff, 0x00, 0x55);
            yellow_  = juce::Colour(0xff, 0xd0, 0x00);
        }
        setupColours();
    }

    //==============================================================================
    void setupColours()
    {
        setColour(juce::ResizableWindow::backgroundColourId, bg_);
        setColour(juce::Label::textColourId, fg_);
        setColour(juce::Label::outlineColourId, cyan_.withAlpha(0.3f));
        setColour(juce::TextButton::buttonColourId, cyan_.darker(0.6f));
        setColour(juce::TextButton::buttonOnColourId, cyan_);
        setColour(juce::TextButton::textColourOffId, fg_);
        setColour(juce::TextButton::textColourOnId, bg_);
        setColour(juce::ComboBox::backgroundColourId, bg_.brighter(0.1f));
        setColour(juce::ComboBox::textColourId, fg_);
        setColour(juce::ComboBox::outlineColourId, cyan_.withAlpha(0.4f));
        setColour(juce::ComboBox::buttonColourId, cyan_.darker(0.3f));
        setColour(juce::ComboBox::arrowColourId, cyan_);
        setColour(juce::PopupMenu::backgroundColourId, bg_.brighter(0.1f));
        setColour(juce::PopupMenu::textColourId, fg_);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, cyan_.withAlpha(0.2f));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour(juce::Slider::rotarySliderFillColourId, cyan_);
        setColour(juce::Slider::rotarySliderOutlineColourId, bg_.brighter(0.2f));
        setColour(juce::Slider::thumbColourId, cyan_);
        setColour(juce::Slider::trackColourId, cyan_.darker(0.5f));
        setColour(juce::Slider::backgroundColourId, bg_.brighter(0.1f));
        setColour(juce::Slider::textBoxTextColourId, fg_);
        setColour(juce::Slider::textBoxBackgroundColourId, bg_.brighter(0.05f));
        setColour(juce::Slider::textBoxOutlineColourId, cyan_.withAlpha(0.3f));
        setColour(juce::ListBox::backgroundColourId, bg_);
        setColour(juce::ListBox::textColourId, fg_);
        setColour(juce::ScrollBar::thumbColourId, cyan_.withAlpha(0.5f));
        setColour(juce::TooltipWindow::backgroundColourId, bg_.brighter(0.2f));
        setColour(juce::TooltipWindow::textColourId, fg_);
        setColour(juce::TooltipWindow::outlineColourId, magenta_.withAlpha(0.5f));
        setColour(juce::CaretComponent::caretColourId, cyan_);
    }

    //==============================================================================
    // Custom button drawing
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                               const juce::Colour& bgColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
        auto baseColour = button.getToggleState() ? cyan_ : cyan_.darker(0.7f);

        if (shouldDrawButtonAsDown)
            baseColour = cyan_.brighter(0.3f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.2f);

        // Neon glow effect
        g.setColour(baseColour.withAlpha(0.3f));
        g.fillRoundedRectangle(bounds, 3.0f);
        
        g.setColour(baseColour);
        g.drawRoundedRectangle(bounds, 3.0f, 1.5f);

        // Corner accents
        auto cornerLen = 8.0f;
        g.setColour(cyan_);
        // Top-left
        g.drawLine(bounds.getX(), bounds.getY(), bounds.getX() + cornerLen, bounds.getY());
        g.drawLine(bounds.getX(), bounds.getY(), bounds.getX(), bounds.getY() + cornerLen);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                         bool /*shouldDrawButtonAsHighlighted*/,
                         bool /*shouldDrawButtonAsDown*/) override
    {
        auto font = getCyberFont(13.0f, button.getToggleState());
        g.setFont(font);
        g.setColour(button.getToggleState() ? bg_ : fg_);
        g.drawText(button.getButtonText(), button.getLocalBounds(),
                   juce::Justification::centred);
    }

    //==============================================================================
    // Custom combo box
    void drawComboBox(juce::Graphics& g, int width, int height,
                      bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox& box) override
    {
        auto bounds = box.getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(bg_.brighter(0.15f));
        g.fillRoundedRectangle(bounds, 2.0f);
        g.setColour(cyan_.withAlpha(isButtonDown ? 0.8f : 0.4f));
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
    }

    void drawComboBoxTextWhenNothingSelected(juce::Graphics& g, juce::ComboBox& box, juce::Label& label) override
    {
        g.setColour(fg_.withAlpha(0.4f));
        g.setFont(getCyberFont(12.0f, false));
        g.drawText(box.getTextWhenNothingSelected(), box.getLocalBounds().reduced(4),
                   juce::Justification::centredLeft);
    }

    //==============================================================================
    // Custom rotary slider
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4);
        auto centre = bounds.getCentre();
        auto radius = juce::jmin(bounds.getWidth() * 0.5f, bounds.getHeight() * 0.5f);

        // Outer ring (neon glow)
        g.setColour(bg_.brighter(0.15f));
        g.fillEllipse(centre.getX() - radius, centre.getY() - radius,
                      radius * 2.0f, radius * 2.0f);

        // Arc fill
        auto arcAngle = juce::jmap(sliderPos, 0.0f, 1.0f, startAngle, endAngle);
        auto arcBounds = juce::Rectangle<float>(centre.getX() - radius + 4,
                                                 centre.getY() - radius + 4,
                                                 radius * 2 - 8, radius * 2 - 8);

        juce::Path arc;
        arc.addArc(arcBounds.getX(), arcBounds.getY(),
                   arcBounds.getWidth(), arcBounds.getHeight(),
                   startAngle, arcAngle, true);

        g.setColour(cyan_);
        g.strokePath(arc, juce::PathStrokeType(2.5f));

        // Outer arc glow
        g.setColour(cyan_.withAlpha(0.2f));
        juce::Path glowArc;
        glowArc.addArc(arcBounds.getX() - 2, arcBounds.getY() - 2,
                        arcBounds.getWidth() + 4, arcBounds.getHeight() + 4,
                        startAngle, arcAngle, true);
        g.strokePath(glowArc, juce::PathStrokeType(4.0f));

        // Center dot
        auto angle = juce::jmap(sliderPos, 0.0f, 1.0f, startAngle, endAngle);
        auto dotX = centre.getX() + (radius - 8) * std::cos(angle);
        auto dotY = centre.getY() + (radius - 8) * std::sin(angle);
        g.setColour(cyan_);
        g.fillEllipse(dotX - 3, dotY - 3, 6, 6);

        // Center cap
        g.setColour(bg_.brighter(0.3f));
        g.fillEllipse(centre.getX() - 5, centre.getY() - 5, 10, 10);
        g.setColour(cyan_.withAlpha(0.6f));
        g.drawEllipse(centre.getX() - 5, centre.getY() - 5, 10, 10, 1.0f);
    }

    //==============================================================================
    // Custom linear slider
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float minPos, float maxPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearHorizontal)
        {
            auto trackH = 4.0f;
            auto trackY = y + h * 0.5f - trackH * 0.5f;
            
            // Background track
            g.setColour(bg_.brighter(0.2f));
            g.fillRoundedRectangle(x, trackY, w, trackH, 2.0f);

            // Filled track
            g.setColour(cyan_);
            g.fillRoundedRectangle(x, trackY, sliderPos - x, trackH, 2.0f);

            // Thumb
            g.setColour(cyan_.brighter(0.3f));
            g.fillEllipse(sliderPos - 5, trackY - 3, 10, trackH + 6);
        }
        else
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h,
                                                    sliderPos, minPos, maxPos,
                                                    style, slider);
        }
    }

    //==============================================================================
    // Font
    static juce::Font getCyberFont(float height, bool bold = false)
    {
        // Try to load a monospaced/tech font, fallback to default
        auto typeface = bold ? juce::Font::getDefaultMonospacedFontName()
                             : juce::Font::getDefaultSansSerifFontName();
        return juce::Font(typeface, height, bold ? juce::Font::bold : juce::Font::plain);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return getCyberFont(12.0f, true);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return getCyberFont(12.0f, false);
    }

    juce::Font getLabelFont(juce::Label&) override
    {
        return getCyberFont(12.0f, false);
    }

    //==============================================================================
    // Draw a "cyber-grid" background pattern
    static void drawGridBackground(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        const int gridSpacing = 40;
        g.setColour(cyan_.withAlpha(0.04f));

        for (int x = bounds.getX(); x < bounds.getRight(); x += gridSpacing)
            g.drawVerticalLine(x, bounds.getY(), bounds.getBottom());

        for (int y = bounds.getY(); y < bounds.getBottom(); y += gridSpacing)
            g.drawHorizontalLine(y, bounds.getX(), bounds.getRight());
    }

    // Draw neon panel border
    static void drawPanelBorder(juce::Graphics& g, juce::Rectangle<int> bounds,
                                 const juce::String& title = "",
                                 juce::Colour accent = cyan_)
    {
        auto b = bounds.toFloat().reduced(1.0f);
        
        // Background fill
        g.setColour(juce::Colour(0x0a, 0x0a, 0x14).withAlpha(0.85f));
        g.fillRoundedRectangle(b, 4.0f);

        // Border glow
        g.setColour(accent.withAlpha(0.15f));
        g.drawRoundedRectangle(b.reduced(1), 4.0f, 3.0f);

        // Border line
        g.setColour(accent.withAlpha(0.5f));
        g.drawRoundedRectangle(b, 4.0f, 1.0f);

        // Title
        if (title.isNotEmpty())
        {
            g.setFont(getCyberFont(11.0f, true));
            g.setColour(accent);
            g.drawText("  " + title, b.removeFromTop(18).reduced(4, 0),
                       juce::Justification::centredLeft);
        }
    }

    //==============================================================================
    inline static juce::Colour bg_      { 0x0a, 0x0a, 0x14 };
    inline static juce::Colour fg_      { 0xc0, 0xc0, 0xd0 };
    inline static juce::Colour cyan_    { 0x00, 0xf0, 0xff };
    inline static juce::Colour magenta_ { 0xff, 0x00, 0x55 };
    inline static juce::Colour yellow_  { 0xff, 0xd0, 0x00 };
};

} // namespace ana
