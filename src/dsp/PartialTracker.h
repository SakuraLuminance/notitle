#pragma once
#include "AudioFileData.h"
#include "STFTConfig.h"
#include "PartialData.h"

namespace ana {

class PartialTracker
{
public:
    PartialTracker();
    ~PartialTracker();

    PartialData trackPartials(
        const AudioFileData& audio,
        const STFTConfig& config);
};

} // namespace ana
