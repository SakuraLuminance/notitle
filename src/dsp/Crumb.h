#pragma once
#include <cstdio>

// Temporary crash-localisation instrumentation (Phase 0 diagnostics).
// Prints a phase marker to stderr so CI logs reveal the last executed
// phase before a hard crash (segfaults leave no other trace).
// Remove or compile out once the instantiation crash is fixed.
#define ANA_CRUMB(label)                                    \
    do                                                      \
    {                                                       \
        std::fprintf(stderr, "[AnaPlug] %s\n", label);      \
        std::fflush(stderr);                                \
    } while (false)
