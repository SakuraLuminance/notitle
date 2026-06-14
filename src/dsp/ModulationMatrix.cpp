#include "ModulationMatrix.h"
#include <algorithm>

namespace ana {

int ModulationMatrix::addRouting(ModSource src, ModDest dest, float amount, bool bipolar)
{
    ModRouting r;
    r.source = src;
    r.dest = dest;
    r.amount = std::max(-1.0f, std::min(1.0f, amount));
    r.bipolar = bipolar;
    r.active = true;
    r.id = nextId++;
    routings.push_back(r);
    return r.id;
}

bool ModulationMatrix::removeRouting(int id)
{
    auto it = std::remove_if(routings.begin(), routings.end(),
                             [id](const ModRouting& r) { return r.id == id; });
    if (it != routings.end())
    {
        routings.erase(it, routings.end());
        return true;
    }
    return false;
}

void ModulationMatrix::clearAll()
{
    routings.clear();
}

void ModulationMatrix::setSourceValue(ModSource source, float value)
{
    int idx = static_cast<int>(source);
    if (idx >= 0 && idx < static_cast<int>(ModSource::NumSources))
    {
        sourceValues[idx] = value;
    }
}

float ModulationMatrix::getSourceValue(ModSource source) const
{
    int idx = static_cast<int>(source);
    if (idx >= 0 && idx < static_cast<int>(ModSource::NumSources))
    {
        return sourceValues[idx];
    }
    return 0.0f;
}

float ModulationMatrix::getModulation(ModDest dest) const
{
    float total = 0.0f;
    for (const auto& r : routings)
    {
        if (r.active && r.dest == dest)
        {
            float val = sourceValues[static_cast<int>(r.source)];
            
            // If the routing expects a bipolar source but the source is unipolar,
            // or vice versa, handle mapping here if necessary.
            // For now, simple direct multiplication.
            total += val * r.amount;
        }
    }
    // Clamp total modulation to typical bounds if necessary, 
    // but usually let the consumer handle mapping.
    return total;
}

} // namespace ana
