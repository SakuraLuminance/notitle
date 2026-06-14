#pragma once
#include <vector>

namespace ana {

// F6: 调制自由路由 X/Y/Z (Modulation Matrix)

enum class ModSource
{
    LFO1,
    LFO2,
    Env1,
    Env2,
    Velocity,
    ModWheel,
    PitchBend,
    Aftertouch,
    Macro1,
    Macro2,
    Macro3,
    Macro4,
    NumSources
};

enum class ModDest
{
    GlobalPitch,
    FilterCutoff,
    FilterResonance,
    FilterDrive,
    AmpVolume,
    AmpPan,
    FxMix,
    NumDests
};

struct ModRouting
{
    ModSource source;
    ModDest dest;
    float amount = 0.0f; // -1.0 to 1.0
    bool bipolar = false;
    bool active = true;
    int id = 0;
};

class ModulationMatrix
{
public:
    ModulationMatrix() = default;
    ~ModulationMatrix() = default;

    int addRouting(ModSource src, ModDest dest, float amount, bool bipolar = false);
    bool removeRouting(int id);
    void clearAll();

    void setSourceValue(ModSource source, float value);
    float getSourceValue(ModSource source) const;

    float getModulation(ModDest dest) const;

    const std::vector<ModRouting>& getRoutings() const { return routings; }

private:
    std::vector<ModRouting> routings;
    float sourceValues[static_cast<int>(ModSource::NumSources)] = {0.0f};
    int nextId = 1;
};

} // namespace ana
