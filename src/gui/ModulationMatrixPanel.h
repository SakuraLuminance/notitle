#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/ModulationMatrix.h"

namespace ana {

// G2: 调制矩阵 (GUI)
class ModulationMatrixPanel : public juce::Component
{
public:
    ModulationMatrixPanel(ModulationMatrix& matrix);
    ~ModulationMatrixPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    ModulationMatrix& modMatrix;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationMatrixPanel)
};

}
