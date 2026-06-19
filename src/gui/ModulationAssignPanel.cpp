#include "ModulationAssignPanel.h"

namespace ana {

//==============================================================================
ModulationAssignPanel::ModulationAssignPanel(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    // --- Volume ADSR (pinned at top) ---
    volAdsrHeader_.setText("VOL ADSR", juce::dontSendNotification);
    volAdsrHeader_.setFont(CyberpunkTheme::getCyberFont(10.0f, true));
    volAdsrHeader_.setColour(juce::Label::textColourId, CyberpunkTheme::cyan_);
    volAdsrHeader_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(volAdsrHeader_);

    auto setupAdsrSlider = [this](juce::Slider& slider, juce::Label& label,
                                   const juce::String& name,
                                   double min, double max, double init)
    {
        slider.setRange(min, max, 0.001);
        slider.setValue(init);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setDoubleClickReturnValue(true, init);
        addAndMakeVisible(slider);

        label.setText(name, juce::dontSendNotification);
        label.setFont(CyberpunkTheme::getCyberFont(8.0f, false));
        label.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.7f));
        label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(label);
    };

    setupAdsrSlider(volAttack_, volAttackL_, "A", 0.01, 5.0, 0.01);
    setupAdsrSlider(volDecay_, volDecayL_, "D", 0.01, 5.0, 0.5);
    setupAdsrSlider(volSustain_, volSustainL_, "S", 0.0, 1.0, 0.7);
    setupAdsrSlider(volRelease_, volReleaseL_, "R", 0.01, 10.0, 1.0);

    volAttack_.setTooltip("Volume attack time (0.01-5.0s)");
    volDecay_.setTooltip("Volume decay time (0.01-5.0s)");
    volSustain_.setTooltip("Volume sustain level (0-100%)");
    volRelease_.setTooltip("Volume release time (0.01-10.0s)");

    volAttack_.onValueChange = [this]()
        { processor_.setVolumeAttack(static_cast<float>(volAttack_.getValue())); };
    volDecay_.onValueChange = [this]()
        { processor_.setVolumeDecay(static_cast<float>(volDecay_.getValue())); };
    volSustain_.onValueChange = [this]()
        { processor_.setVolumeSustain(static_cast<float>(volSustain_.getValue())); };
    volRelease_.onValueChange = [this]()
        { processor_.setVolumeRelease(static_cast<float>(volRelease_.getValue())); };

    // --- Helper to build a section from a param list ---
    auto buildSection = [this](SectionData& section, const juce::String& name,
                                const std::vector<std::pair<juce::String, int>>& params)
    {
        // Header label — click to collapse/expand
        section.headerLabel.setText("▼ " + name + " (" + juce::String(params.size()) + ")",
                                     juce::dontSendNotification);
        section.headerLabel.setFont(CyberpunkTheme::getCyberFont(9.0f, true));
        section.headerLabel.setColour(juce::Label::textColourId, CyberpunkTheme::yellow_);
        section.headerLabel.setJustificationType(juce::Justification::centredLeft);
        section.headerLabel.setTooltip("Click to collapse/expand this section");
        section.headerLabel.addMouseListener(this, false);
        addAndMakeVisible(section.headerLabel);

        for (const auto& p : params)
        {
            auto row = std::make_unique<ModRow>();
            row->slotIndex = p.second;

            // Param label
            row->label.setText(p.first, juce::dontSendNotification);
            row->label.setFont(CyberpunkTheme::getCyberFont(9.0f, false));
            row->label.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.85f));
            row->label.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(row->label);

            // Source combo — IDs match ModSource+1
            auto& combo = row->sourceCombo;
            combo.addItem("OFF",    static_cast<int>(ModSource::OFF) + 1);
            combo.addItem("LFO 1",  static_cast<int>(ModSource::LFO1) + 1);
            combo.addItem("LFO 2",  static_cast<int>(ModSource::LFO2) + 1);
            combo.addItem("LFO 3",  static_cast<int>(ModSource::LFO3) + 1);
            combo.addItem("LFO 4",  static_cast<int>(ModSource::LFO4) + 1);
            combo.addItem("ENV 1",  static_cast<int>(ModSource::ENV1) + 1);
            combo.addItem("ENV 2",  static_cast<int>(ModSource::ENV2) + 1);
            combo.addItem("ENV 3",  static_cast<int>(ModSource::ENV3) + 1);
            combo.setSelectedId(static_cast<int>(ModSource::OFF) + 1);
            combo.setTooltip("Select modulation source: OFF/LFO1-4/ENV1-3");
            const int si = p.second;
            combo.onChange = [this, si, &combo]()
            {
                processor_.setModSource(si,
                    static_cast<ModSource>(combo.getSelectedId() - 1));
            };
            addAndMakeVisible(combo);

            // Depth slider
            auto& slider = row->depthSlider;
            slider.setRange(-1.0, 1.0, 0.001);
            slider.setValue(0.0);
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider.setDoubleClickReturnValue(true, 0.0);
            slider.setTooltip("Modulation depth: controls how much the source affects this parameter");
            const int sj = p.second;
            slider.onValueChange = [this, sj, &slider]()
            {
                processor_.setModDepth(sj, static_cast<float>(slider.getValue()));
            };
            addAndMakeVisible(slider);

            section.rows.push_back(std::move(row));
        }
    };

    buildSection(filterSection_, "FILTER",   {{"cutoff", 0}, {"resonance", 1}});
    buildSection(timbreSection_, "TIMBRE", {
        {"sub_a", 2}, {"bright_a", 3}, {"blur_a", 4}, {"hpf_a", 5},
        {"sub_b", 6}, {"bright_b", 7}, {"blur_b", 8}, {"hpf_b", 9}
    });
    buildSection(effectsSection_, "EFFECTS", {
        {"delay_time", 10}, {"reverb_wet", 11},
        {"chorus_depth", 12}, {"phaser_fb", 13}
    });
    buildSection(masterSection_, "MASTER", {{"master_vol", 14}, {"master_pan", 15}});

    // Wire combo onChange to also update depth slider state
    auto wireDepthSync = [this](std::unique_ptr<ModRow>& row)
    {
        row->sourceCombo.onChange = [this, &row = *row]()
        {
            const int id = row.sourceCombo.getSelectedId();
            auto src = static_cast<ModSource>(id - 1);
            processor_.setModSource(row.slotIndex, src);

            if (id <= 1) // OFF → disabled
            {
                row.depthSlider.setEnabled(false);
            }
            else
            {
                row.depthSlider.setEnabled(true);
                if (id <= 5) // LFO 1-4 → bipolar
                    row.depthSlider.setRange(-1.0, 1.0, 0.001);
                else // ENV 1-3 → unipolar
                    row.depthSlider.setRange(0.0, 1.0, 0.001);
            }
        };
        // Apply initial state (OFF by default → disabled)
        row.depthSlider.setEnabled(false);
    };

    for (auto& r : filterSection_.rows)  wireDepthSync(r);
    for (auto& r : timbreSection_.rows)  wireDepthSync(r);
    for (auto& r : effectsSection_.rows) wireDepthSync(r);
    for (auto& r : masterSection_.rows)  wireDepthSync(r);
}

//==============================================================================
ModulationAssignPanel::~ModulationAssignPanel()
{
    volAdsrHeader_.removeMouseListener(this);
    filterSection_.headerLabel.removeMouseListener(this);
    timbreSection_.headerLabel.removeMouseListener(this);
    effectsSection_.headerLabel.removeMouseListener(this);
    masterSection_.headerLabel.removeMouseListener(this);
}

//==============================================================================
void ModulationAssignPanel::resized()
{
    auto area = getLocalBounds().reduced(3, 2);

    // --- Volume ADSR (pinned) ---
    {
        auto adsrArea = area.removeFromTop(34);
        volAdsrHeader_.setBounds(adsrArea.removeFromLeft(56));
        auto sliderRow = adsrArea.reduced(0, 2);
        const int sw = sliderRow.getWidth() / 4;

        auto placeAdsr = [&](juce::Slider& s, juce::Label& l)
        {
            auto cell = sliderRow.removeFromLeft(sw).reduced(1, 0);
            l.setBounds(cell.removeFromTop(10));
            s.setBounds(cell.reduced(0, 1));
        };

        placeAdsr(volAttack_,  volAttackL_);
        placeAdsr(volDecay_,   volDecayL_);
        placeAdsr(volSustain_, volSustainL_);
        placeAdsr(volRelease_, volReleaseL_);
    }

    // --- Collapsible sections ---
    auto layoutSection = [this](SectionData& section, juce::Rectangle<int>& a)
    {
        section.headerLabel.setBounds(a.removeFromTop(18).reduced(0, 2));
        if (!section.collapsed)
        {
            for (auto& row : section.rows)
            {
                auto rowArea = a.removeFromTop(28);
                row->label.setBounds(rowArea.removeFromLeft(72).reduced(2, 0));
                row->sourceCombo.setBounds(rowArea.removeFromLeft(84).reduced(2, 3));
                row->depthSlider.setBounds(rowArea.reduced(2, 3));
            }
        }
    };

    layoutSection(filterSection_, area);
    layoutSection(timbreSection_, area);
    layoutSection(effectsSection_, area);
    layoutSection(masterSection_, area);
}

//==============================================================================
void ModulationAssignPanel::paint(juce::Graphics& g)
{
    // Subtle separators between sections
    auto drawSep = [&](int y)
    {
        g.setColour(CyberpunkTheme::cyan_.withAlpha(0.07f));
        g.drawHorizontalLine(y, 0, getWidth());
    };

    auto secH = [this](const SectionData& s) -> int
    {
        return s.collapsed ? 18 : (18 + static_cast<int>(s.rows.size()) * 28);
    };

    int y = 36;
    drawSep(y);
    y += secH(filterSection_);
    drawSep(y);
    y += secH(timbreSection_);
    drawSep(y);
    y += secH(effectsSection_);
    drawSep(y);
    // master section - no separator after last
}

//==============================================================================
void ModulationAssignPanel::mouseUp(const juce::MouseEvent& event)
{
    auto toggle = [this](SectionData& section)
    {
        section.collapsed = !section.collapsed;

        auto txt = section.headerLabel.getText();
        if (section.collapsed)
            txt = txt.replaceFirstOccurrenceOf("▼", "▶");
        else
            txt = txt.replaceFirstOccurrenceOf("▶", "▼");
        section.headerLabel.setText(txt, juce::dontSendNotification);

        // Update total content height — this triggers resized() via setSize()
        setSize(getWidth(), calcContentHeight());
    };

    if      (event.eventComponent == &filterSection_.headerLabel)   toggle(filterSection_);
    else if (event.eventComponent == &timbreSection_.headerLabel)   toggle(timbreSection_);
    else if (event.eventComponent == &effectsSection_.headerLabel)  toggle(effectsSection_);
    else if (event.eventComponent == &masterSection_.headerLabel)   toggle(masterSection_);
}

//==============================================================================
int ModulationAssignPanel::calcContentHeight() const
{
    auto secH = [this](const SectionData& s) -> int
    {
        return s.collapsed ? 18 : (18 + static_cast<int>(s.rows.size()) * 28);
    };

    return 36                        // Volume ADSR
         + secH(filterSection_)
         + secH(timbreSection_)
         + secH(effectsSection_)
         + secH(masterSection_)
         + 8;                        // bottom padding
}

//==============================================================================
void ModulationAssignPanel::syncFromProcessor()
{
    auto syncRow = [this](const std::unique_ptr<ModRow>& row)
    {
        const auto& slot = processor_.getModSlot(row->slotIndex);

        const int expectedId = static_cast<int>(slot.mod.source) + 1;
        if (row->sourceCombo.getSelectedId() != expectedId)
            row->sourceCombo.setSelectedId(expectedId, juce::dontSendNotification);

        const double depth = static_cast<double>(slot.mod.depth);
        if (std::abs(row->depthSlider.getValue() - depth) > 0.001)
            row->depthSlider.setValue(depth, juce::dontSendNotification);
    };

    for (auto& r : filterSection_.rows)  syncRow(r);
    for (auto& r : timbreSection_.rows)  syncRow(r);
    for (auto& r : effectsSection_.rows) syncRow(r);
    for (auto& r : masterSection_.rows)  syncRow(r);

    auto syncAdsr = [](juce::Slider& slider, float val)
    {
        if (std::abs(static_cast<float>(slider.getValue()) - val) > 0.001f)
            slider.setValue(static_cast<double>(val), juce::dontSendNotification);
    };
    syncAdsr(volAttack_,  processor_.getVolumeAttack());
    syncAdsr(volDecay_,   processor_.getVolumeDecay());
    syncAdsr(volSustain_, processor_.getVolumeSustain());
    syncAdsr(volRelease_, processor_.getVolumeRelease());
}

} // namespace ana
