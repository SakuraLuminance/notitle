#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/PresetManager.h"

namespace ana {

class PresetBrowserPanel : public juce::Component,
                           public juce::ListBoxModel
{
public:
    PresetBrowserPanel(PresetManager& pm);
    ~PresetBrowserPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ListBoxModel overrides
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    // Listener for preset changes (so main UI can update)
    std::function<void()> onPresetLoaded;

private:
    void updatePresetList();
    void loadSelectedPreset();
    void saveCurrentState();

    PresetManager& presetManager;

    juce::ComboBox categoryBox;
    juce::ListBox presetListBox;
    juce::TextButton loadButton{ "Load" };
    juce::TextButton saveButton{ "Save" };
    juce::TextEditor presetNameEditor;
    juce::Label titleLabel{ {}, "Preset Browser" };

    std::unique_ptr<juce::TextEditor> searchField_;
    juce::StringArray currentPresets;
    static constexpr int starWidth = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowserPanel)
};

} // namespace ana
