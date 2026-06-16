#include "PresetBrowserPanel.h"
#include "CyberpunkTheme.h"

namespace ana {

PresetBrowserPanel::PresetBrowserPanel(PresetManager& pm)
    : presetManager(pm)
{
    addAndMakeVisible(titleLabel);
    titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(categoryBox);
    auto categories = presetManager.getCategories();
    for (int i = 0; i < categories.size(); ++i)
        categoryBox.addItem(categories[i], i + 1);

    categoryBox.onChange = [this] { updatePresetList(); };

    addAndMakeVisible(presetListBox);
    presetListBox.setModel(this);
    presetListBox.setColour(juce::ListBox::backgroundColourId, CyberpunkTheme::bg_.brighter(0.05f));

    addAndMakeVisible(presetNameEditor);
    presetNameEditor.setTextToShowWhenEmpty("Preset Name...", CyberpunkTheme::fg_.darker(0.5f));

    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { loadSelectedPreset(); };

    addAndMakeVisible(saveButton);
    saveButton.onClick = [this] { saveCurrentState(); };

    if (!categories.isEmpty())
        categoryBox.setSelectedItemIndex(0, juce::sendNotificationSync);
}

PresetBrowserPanel::~PresetBrowserPanel()
{
    presetListBox.setModel(nullptr);
}

void PresetBrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(CyberpunkTheme::bg_.brighter(0.1f));
}

void PresetBrowserPanel::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    titleLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(10);

    categoryBox.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(10);

    auto buttonBounds = bounds.removeFromBottom(24);
    saveButton.setBounds(buttonBounds.removeFromRight(60));
    buttonBounds.removeFromRight(10);
    loadButton.setBounds(buttonBounds.removeFromRight(60));
    buttonBounds.removeFromRight(10);
    presetNameEditor.setBounds(buttonBounds);

    bounds.removeFromBottom(10);
    presetListBox.setBounds(bounds);
}

int PresetBrowserPanel::getNumRows()
{
    return currentPresets.size();
}

void PresetBrowserPanel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(CyberpunkTheme::cyan_.withAlpha(0.2f));

    g.setColour(CyberpunkTheme::fg_);
    g.setFont(14.0f);
    
    if (juce::isPositiveAndBelow(rowNumber, currentPresets.size()))
    {
        g.drawText(currentPresets[rowNumber],
                   10, 0, width - 20, height,
                   juce::Justification::centredLeft, true);
    }
}

void PresetBrowserPanel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    juce::ignoreUnused(row);
    // You could populate the presetNameEditor with the selected preset name here if desired
    if (juce::isPositiveAndBelow(row, currentPresets.size()))
    {
        presetNameEditor.setText(currentPresets[row], false);
    }
}

void PresetBrowserPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    juce::ignoreUnused(row);
    loadSelectedPreset();
}

void PresetBrowserPanel::updatePresetList()
{
    auto category = categoryBox.getText();
    currentPresets = presetManager.getPresetList(category);
    presetListBox.updateContent();
    presetListBox.repaint();
}

void PresetBrowserPanel::loadSelectedPreset()
{
    int row = presetListBox.getSelectedRow();
    if (juce::isPositiveAndBelow(row, currentPresets.size()))
    {
        auto presetName = currentPresets[row];
        if (presetManager.loadPreset(presetName))
        {
            if (onPresetLoaded)
                onPresetLoaded();
        }
    }
}

void PresetBrowserPanel::saveCurrentState()
{
    auto name = presetNameEditor.getText();
    if (name.isNotEmpty())
    {
        auto category = categoryBox.getText();
        if (category.isNotEmpty())
        {
            if (presetManager.savePreset(name, category))
            {
                updatePresetList();
            }
        }
    }
}

} // namespace ana
