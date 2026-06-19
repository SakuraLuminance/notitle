#include "PresetBrowserPanel.h"
#include "CyberpunkTheme.h"

namespace ana {

PresetBrowserPanel::PresetBrowserPanel(PresetManager& pm)
    : presetManager(pm)
{
    addAndMakeVisible(titleLabel);
    titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);

    // Search field
    searchField_ = std::make_unique<juce::TextEditor>();
    searchField_->setTextToShowWhenEmpty("Search presets...", CyberpunkTheme::fg_.darker(0.5f));
    searchField_->setSelectAllWhenFocused(true);
    searchField_->onTextChange = [this] { updatePresetList(); };
    addAndMakeVisible(searchField_.get());

    addAndMakeVisible(categoryBox);
    auto categories = presetManager.getCategories();
    for (int i = 0; i < categories.size(); ++i)
        categoryBox.addItem(categories[i], i + 1);
    categoryBox.setTooltip("Filter by preset category");

    categoryBox.onChange = [this] { updatePresetList(); };

    addAndMakeVisible(presetListBox);
    presetListBox.setModel(this);
    presetListBox.setColour(juce::ListBox::backgroundColourId, CyberpunkTheme::bg_.brighter(0.05f));
    presetListBox.setTooltip("Click to load preset");

    addAndMakeVisible(presetNameEditor);
    presetNameEditor.setTextToShowWhenEmpty("Preset Name...", CyberpunkTheme::fg_.darker(0.5f));
    presetNameEditor.setTooltip("Enter a name for your preset");

    addAndMakeVisible(loadButton);
    loadButton.setTooltip("Load selected preset");
    loadButton.onClick = [this] { loadSelectedPreset(); };

    addAndMakeVisible(saveButton);
    saveButton.setTooltip("Save current settings as preset");
    saveButton.onClick = [this] { saveCurrentState(); };

    // Load persisted favorites
    presetManager.loadFavoritesFromFile();

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
    bounds.removeFromTop(6);

    searchField_->setBounds(bounds.removeFromTop(22));
    bounds.removeFromTop(6);

    categoryBox.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(6);

    auto buttonBounds = bounds.removeFromBottom(24);
    saveButton.setBounds(buttonBounds.removeFromRight(60));
    buttonBounds.removeFromRight(10);
    loadButton.setBounds(buttonBounds.removeFromRight(60));
    buttonBounds.removeFromRight(10);
    presetNameEditor.setBounds(buttonBounds);

    bounds.removeFromBottom(6);
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

    if (!juce::isPositiveAndBelow(rowNumber, currentPresets.size()))
        return;

    auto presetName = currentPresets[rowNumber];
    bool isFav = presetManager.isFavorite(presetName);

    // Draw star/favorite icon
    auto starArea = juce::Rectangle<int>(0, 0, starWidth, height);
    g.setColour(isFav ? CyberpunkTheme::yellow_ : CyberpunkTheme::fg_.withAlpha(0.3f));
    g.setFont(14.0f);
    g.drawText(isFav ? "\xe2\x98\x85" : "\xe2\x98\x86",
               starArea, juce::Justification::centred, true);

    // Draw preset name
    g.setColour(CyberpunkTheme::fg_);
    g.setFont(14.0f);
    g.drawText(presetName,
               starWidth + 4, 0, width - starWidth - 8, height,
               juce::Justification::centredLeft, true);
}

void PresetBrowserPanel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (!juce::isPositiveAndBelow(row, currentPresets.size()))
        return;

    // Check if click is in the star column (first starWidth pixels)
    // e.x is relative to the ListBox component
    if (e.x >= 0 && e.x < starWidth)
    {
        auto presetName = currentPresets[row];
        if (presetManager.isFavorite(presetName))
            presetManager.removeFavorite(presetName);
        else
            presetManager.addFavorite(presetName);

        updatePresetList();
        return;
    }

    // Normal click — populate the name editor
    presetNameEditor.setText(currentPresets[row], false);
}

void PresetBrowserPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    juce::ignoreUnused(row);
    loadSelectedPreset();
}

void PresetBrowserPanel::updatePresetList()
{
    auto category = categoryBox.getText();
    auto allPresets = presetManager.getPresetList(category);
    auto searchText = searchField_->getText().trim();

    // Filter by search text (case-insensitive substring match)
    juce::StringArray filtered;
    if (searchText.isEmpty())
    {
        filtered = allPresets;
    }
    else
    {
        auto lowerSearch = searchText.toLowerCase();
        for (const auto& p : allPresets)
            if (p.toLowerCase().contains(lowerSearch))
                filtered.add(p);
    }

    // Separate favorites and non-favorites, then sort alpha within each group
    juce::StringArray favList, nonFavList;
    for (const auto& p : filtered)
    {
        if (presetManager.isFavorite(p))
            favList.add(p);
        else
            nonFavList.add(p);
    }

    favList.sort(true);
    nonFavList.sort(true);

    // Build final list: favorites first, then the rest
    currentPresets = favList;
    currentPresets.addArray(nonFavList);

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
