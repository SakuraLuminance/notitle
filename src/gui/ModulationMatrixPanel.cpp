#include "ModulationMatrixPanel.h"
#include "CyberpunkTheme.h"

namespace ana {

ModulationMatrixPanel::ModulationMatrixPanel(ModulationMatrix& matrix)
    : modMatrix(matrix)
{
    // Initialize UI components for modulation routing (placeholder)
}

ModulationMatrixPanel::~ModulationMatrixPanel()
{
}

void ModulationMatrixPanel::paint(juce::Graphics& g)
{
    g.fillAll(CyberpunkTheme::bg_); // Dark purple background
    
    g.setColour(CyberpunkTheme::fg_);
    g.setFont(16.0f);
    g.drawText("Modulation Matrix", getLocalBounds().withSizeKeepingCentre(200, 30), 
               juce::Justification::centred, true);
               
    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.7f));
    g.drawText(juce::String(modMatrix.getRoutings().size()) + " active routings", 
               getLocalBounds().withSizeKeepingCentre(200, 30).translated(0, 30), 
               juce::Justification::centred, true);
}

void ModulationMatrixPanel::resized()
{
    // Layout logic for slots
}

}
