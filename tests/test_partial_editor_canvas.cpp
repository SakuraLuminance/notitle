#include <catch2/catch_all.hpp>
#include "gui/PartialEditorCanvas.h"
#include <cmath>

// ============================================================================
// Helpers: build test PartialData
// ============================================================================
static ana::PartialData makeTestData(int numFrames, int numPartials,
                                     double sampleRate = 44100.0)
{
    ana::PartialData data;
    data.sampleRate  = sampleRate;
    data.hopSize     = 512.0;
    data.maxPartials = numPartials;

    for (int f = 0; f < numFrames; ++f)
    {
        ana::PartialFrame frame;
        frame.timestamp = static_cast<double>(f) * 0.01;

        for (int p = 0; p < numPartials; ++p)
        {
            float amp  = static_cast<float>(f * numPartials + p)
                       / static_cast<float>(numFrames * numPartials);
            float freq = (static_cast<float>(p) + 0.5f)
                       / static_cast<float>(numPartials)
                       * (static_cast<float>(sampleRate) * 0.5f);
            frame.partials.push_back({ freq, amp, 0.0f });
        }

        data.frames.push_back(std::move(frame));
    }

    return data;
}

// ============================================================================
// Round-trip:  setPartialData  ->  getModifiedPartialData
// ============================================================================
TEST_CASE("PartialEditorCanvas round-trip preserves metadata and amplitudes",
          "[gui][editor]")
{
    ana::PartialEditorCanvas canvas;

    int numFrames   = 10;
    int numPartials = 8;

    auto inputData = makeTestData(numFrames, numPartials);
    canvas.setPartialData(inputData);
    auto outputData = canvas.getModifiedPartialData();

    SECTION("Frame count and partial count are preserved")
    {
        REQUIRE(outputData.frames.size() == static_cast<size_t>(numFrames));
        REQUIRE(outputData.maxPartials == numPartials);

        for (const auto& frame : outputData.frames)
            REQUIRE(frame.partials.size() == static_cast<size_t>(numPartials));
    }

    SECTION("Timestamps are preserved")
    {
        for (int f = 0; f < numFrames; ++f)
        {
            double expectedT = static_cast<double>(f) * 0.01;
            REQUIRE(std::abs(outputData.frames[static_cast<size_t>(f)].timestamp
                             - expectedT) < 1e-9);
        }
    }

    SECTION("Amplitudes are preserved (round-trip)")
    {
        for (int f = 0; f < numFrames; ++f)
        {
            for (int p = 0; p < numPartials; ++p)
            {
                float expectedAmp = static_cast<float>(f * numPartials + p)
                                  / static_cast<float>(numFrames * numPartials);
                float actualAmp = outputData.frames[static_cast<size_t>(f)]
                                                   .partials[static_cast<size_t>(p)]
                                                   .amplitude;
                REQUIRE(actualAmp == Approx(expectedAmp).margin(1e-6f));
            }
        }
    }

    SECTION("Frequencies are in valid range (0 to Nyquist)")
    {
        float nyquist = static_cast<float>(inputData.sampleRate) * 0.5f;
        for (const auto& frame : outputData.frames)
        {
            for (const auto& partial : frame.partials)
            {
                REQUIRE(partial.frequency >= 0.0f);
                REQUIRE(partial.frequency <= nyquist + 1.0f);
            }
        }
    }
}

// ============================================================================
// Undo restores previous state
// ============================================================================
TEST_CASE("PartialEditorCanvas undo restores previous state",
          "[gui][editor][undo]")
{
    ana::PartialEditorCanvas canvas;

    auto data = makeTestData(5, 4);
    canvas.setPartialData(data);

    // Capture original amplitudes
    auto dataBefore = canvas.getModifiedPartialData();

    // Modify via clear
    canvas.clear();

    // Verify canvas is now zeroed
    {
        auto afterClear = canvas.getModifiedPartialData();
        for (const auto& frame : afterClear.frames)
            for (const auto& partial : frame.partials)
                REQUIRE(partial.amplitude == 0.0f);
    }

    // Undo
    canvas.undo();
    auto afterUndo = canvas.getModifiedPartialData();

    SECTION("Undo restores frame count and partial count")
    {
        REQUIRE(afterUndo.frames.size() == dataBefore.frames.size());
        REQUIRE(afterUndo.maxPartials == dataBefore.maxPartials);
    }

    SECTION("Undo restores original amplitudes")
    {
        for (size_t f = 0; f < dataBefore.frames.size(); ++f)
        {
            for (int p = 0; p < dataBefore.maxPartials; ++p)
            {
                float expected = dataBefore.frames[f]
                                       .partials[static_cast<size_t>(p)]
                                       .amplitude;
                float actual   = afterUndo.frames[f]
                                       .partials[static_cast<size_t>(p)]
                                       .amplitude;
                REQUIRE(actual == Approx(expected).margin(1e-6f));
            }
        }
    }

    SECTION("Redo after undo re-applies the change")
    {
        canvas.redo();
        auto afterRedo = canvas.getModifiedPartialData();

        for (const auto& frame : afterRedo.frames)
            for (const auto& partial : frame.partials)
                REQUIRE(partial.amplitude == 0.0f);
    }
}

// ============================================================================
// Normalize scales amplitudes to 0-1 range
// ============================================================================
TEST_CASE("PartialEditorCanvas normalize scales correctly",
          "[gui][editor][normalize]")
{
    ana::PartialEditorCanvas canvas;

    // Data with max amplitude well below 1.0
    auto data = makeTestData(5, 4);
    canvas.setPartialData(data);

    // Manually scale all amplitudes down so max is ~0.5
    {
        auto modifiedData = canvas.getModifiedPartialData();
        float origMax = 0.0f;
        for (const auto& frame : modifiedData.frames)
            for (const auto& partial : frame.partials)
                origMax = std::max(origMax, partial.amplitude);

        REQUIRE(origMax > 0.0f);   // original has some content
        REQUIRE(origMax < 1.0f);   // not already at 1
    }

    canvas.normalize();

    auto normalizedData = canvas.getModifiedPartialData();

    SECTION("All amplitudes are in [0, 1]")
    {
        for (const auto& frame : normalizedData.frames)
        {
            for (const auto& partial : frame.partials)
            {
                REQUIRE(partial.amplitude >= 0.0f);
                REQUIRE(partial.amplitude <= 1.0f);
            }
        }
    }

    SECTION("Max amplitude is 1.0 (or very close)")
    {
        float maxAmp = 0.0f;
        for (const auto& frame : normalizedData.frames)
            for (const auto& partial : frame.partials)
                maxAmp = std::max(maxAmp, partial.amplitude);

        REQUIRE(maxAmp == Approx(1.0f).margin(1e-6f));
    }

    SECTION("Relative ratios are preserved")
    {
        // Compare before/after: for any two cells, the ratio should be the same
        auto dataBefore = makeTestData(5, 4);
        canvas.setPartialData(dataBefore);

        // Peak amplitude in original
        float beforePeak = 0.0f;
        for (const auto& frame : dataBefore.frames)
            for (const auto& partial : frame.partials)
                beforePeak = std::max(beforePeak, partial.amplitude);

        canvas.normalize();
        auto afterNorm = canvas.getModifiedPartialData();

        for (size_t f = 0; f < dataBefore.frames.size(); ++f)
        {
            for (int p = 0; p < dataBefore.maxPartials; ++p)
            {
                float original = dataBefore.frames[f]
                                       .partials[static_cast<size_t>(p)]
                                       .amplitude;
                float expected = original / beforePeak;
                float actual   = afterNorm.frames[f]
                                       .partials[static_cast<size_t>(p)]
                                       .amplitude;
                REQUIRE(actual == Approx(expected).margin(1e-6f));
            }
        }
    }
}

// ============================================================================
// Clear resets all amplitudes to zero
// ============================================================================
TEST_CASE("PartialEditorCanvas clear zeros all amplitudes",
          "[gui][editor]")
{
    ana::PartialEditorCanvas canvas;
    canvas.setPartialData(makeTestData(5, 4));
    canvas.clear();

    auto data = canvas.getModifiedPartialData();

    for (const auto& frame : data.frames)
        for (const auto& partial : frame.partials)
            REQUIRE(partial.amplitude == 0.0f);
}

// ============================================================================
// Empty data handling
// ============================================================================
TEST_CASE("PartialEditorCanvas handles empty data gracefully",
          "[gui][editor]")
{
    ana::PartialEditorCanvas canvas;

    SECTION("Default constructed canvas returns empty data")
    {
        auto data = canvas.getModifiedPartialData();
        REQUIRE(data.frames.empty());
        REQUIRE(data.maxPartials == 0);
    }

    SECTION("Setting then clearing produces empty output")
    {
        canvas.setPartialData(makeTestData(3, 4));
        canvas.setPartialData(ana::PartialData{});
        auto data = canvas.getModifiedPartialData();
        REQUIRE(data.frames.empty());
    }
}

// ============================================================================
// Multiple undo / redo stack discipline
// ============================================================================
TEST_CASE("PartialEditorCanvas undo stack depth is limited",
          "[gui][editor][undo]")
{
    ana::PartialEditorCanvas canvas;
    canvas.setPartialData(makeTestData(3, 3));

    // Perform more edits than the stack can hold
    int numEdits = 25;
    for (int i = 0; i < numEdits; ++i)
    {
        canvas.clear();
    }

    // Undo as many times as possible - should stop at some point
    // without crashing
    for (int i = 0; i < numEdits + 5; ++i)
    {
        // Should not crash regardless of how many times we undo
        canvas.undo();
    }

    // After all undos, we should have some state (the oldest or initial)
    auto data = canvas.getModifiedPartialData();
    REQUIRE(data.maxPartials == 3);
    REQUIRE(data.frames.size() == 3);
}
