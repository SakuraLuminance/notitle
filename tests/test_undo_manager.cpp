#include <catch2/catch_all.hpp>
#include "dsp/UndoManager.h"

using namespace ana;

//==============================================================================
// Helper: creates a command that increments/decrements a shared counter.
//==============================================================================
static std::unique_ptr<UndoCommand> makeCounterCmd(int* value, int delta,
                                                    const juce::String& desc = {})
{
    auto cmd = std::make_unique<UndoCommand>();
    cmd->description = desc;
    cmd->execute = [value, delta]() { *value += delta; };
    cmd->undo    = [value, delta]() { *value -= delta; };
    return cmd;
}

//==============================================================================
TEST_CASE("UndoManager - basic undo/redo", "[undo][basic]")
{
    UndoManager um;
    int value = 0;

    // Execute three increments.
    um.execute(makeCounterCmd(&value, 1, "Inc A"));
    REQUIRE(value == 1);
    REQUIRE(um.canUndo());
    REQUIRE_FALSE(um.canRedo());
    REQUIRE(um.getNumUndoSteps() == 1);
    REQUIRE(um.getNumRedoSteps() == 0);

    um.execute(makeCounterCmd(&value, 5, "Inc B"));
    REQUIRE(value == 6);
    REQUIRE(um.getNumUndoSteps() == 2);

    um.execute(makeCounterCmd(&value, 10, "Inc C"));
    REQUIRE(value == 16);
    REQUIRE(um.getNumUndoSteps() == 3);

    // Undo once: back to 6.
    um.undo();
    REQUIRE(value == 6);
    REQUIRE(um.getNumUndoSteps() == 2);
    REQUIRE(um.getNumRedoSteps() == 1);
    REQUIRE(um.canRedo());

    // Undo again: back to 1.
    um.undo();
    REQUIRE(value == 1);
    REQUIRE(um.getNumUndoSteps() == 1);
    REQUIRE(um.getNumRedoSteps() == 2);

    // Redo once: back to 6.
    um.redo();
    REQUIRE(value == 6);
    REQUIRE(um.getNumUndoSteps() == 2);
    REQUIRE(um.getNumRedoSteps() == 1);

    // Redo again: back to 16.
    um.redo();
    REQUIRE(value == 16);
    REQUIRE(um.getNumUndoSteps() == 3);
    REQUIRE(um.getNumRedoSteps() == 0);
}

//==============================================================================
TEST_CASE("UndoManager - undo clears redo stack on new execute",
          "[undo][redo_clear]")
{
    UndoManager um;
    int value = 0;

    um.execute(makeCounterCmd(&value, 1));
    REQUIRE(value == 1);

    um.execute(makeCounterCmd(&value, 2));
    REQUIRE(value == 3);

    // Undo twice.
    um.undo();
    REQUIRE(value == 1);
    REQUIRE(um.getNumRedoSteps() == 1);

    um.undo();
    REQUIRE(value == 0);
    REQUIRE(um.getNumRedoSteps() == 2);

    // New execute: redo stack must be cleared.
    um.execute(makeCounterCmd(&value, 100));
    REQUIRE(value == 100);
    REQUIRE(um.getNumRedoSteps() == 0);
    REQUIRE(um.getNumUndoSteps() == 1);

    // Redo should not be available.
    REQUIRE_FALSE(um.canRedo());
}

//==============================================================================
TEST_CASE("UndoManager - max depth eviction (FIFO)", "[undo][max_depth]")
{
    UndoManager um(3);  // max depth of 3

    int value = 0;

    // Push 4 commands.  The oldest (inc=1) should be evicted.
    um.execute(makeCounterCmd(&value, 1));
    REQUIRE(value == 1);
    REQUIRE(um.getNumUndoSteps() == 1);

    um.execute(makeCounterCmd(&value, 2));
    REQUIRE(value == 3);
    REQUIRE(um.getNumUndoSteps() == 2);

    um.execute(makeCounterCmd(&value, 3));
    REQUIRE(value == 6);
    REQUIRE(um.getNumUndoSteps() == 3);

    um.execute(makeCounterCmd(&value, 4));
    REQUIRE(value == 10);
    REQUIRE(um.getNumUndoSteps() == 3);  // still at 3 (evicted oldest)

    // Undo three times — should land at 3 (not 1, because +1 was evicted).
    um.undo();
    REQUIRE(value == 6);
    REQUIRE(um.getNumUndoSteps() == 2);

    um.undo();
    REQUIRE(value == 3);
    REQUIRE(um.getNumUndoSteps() == 1);

    um.undo();
    REQUIRE(value == 1);  // +1 is still in the actual value computation
                           // but +1's step was evicted.  Wait — let's check.
                           // Actually, the undo of +4 gives 6, undo of +3 gives 3,
                           // undo of +2 gives 1.  But +1 was evicted, so
                           // after these 3 undos, undo stack is empty.
    REQUIRE(um.getNumUndoSteps() == 0);
    REQUIRE_FALSE(um.canUndo());

    // The current value is 1 because:
    //   executed: +1, +2, +3, +4  → value = 10
    //   evicted:  +1               → logically value would be 10-1=9
    //   undo +4:  value -= 4       → 5
    //   undo +3:  value -= 3       → 2
    //   undo +2:  value -= 2       → 0
    //   Actually, the evicted step is +1 which is still applied to value
    //   but its undo is lost. So after undoing all 3 remaining steps,
    //   value should be 10-4-3-2 = 1.
    //   Correct: value == 1.

    REQUIRE(value == 1);
}

//==============================================================================
TEST_CASE("UndoManager - composite commands", "[undo][composite]")
{
    UndoManager um;
    int value = 0;

    // Build a composite with 3 sub-operations.
    um.beginComposite("Batch Add");
    um.execute(makeCounterCmd(&value, 1));
    REQUIRE(value == 1);
    REQUIRE(um.getNumUndoSteps() == 0);  // still in composite, not pushed

    um.execute(makeCounterCmd(&value, 2));
    REQUIRE(value == 3);

    um.execute(makeCounterCmd(&value, 3));
    REQUIRE(value == 6);
    um.endComposite();

    // After endComposite, the composite is pushed as one step.
    REQUIRE(um.getNumUndoSteps() == 1);
    REQUIRE(um.getNumRedoSteps() == 0);

    // Undo the composite — should reverse all three sub-operations.
    um.undo();
    REQUIRE(value == 0);
    REQUIRE(um.getNumUndoSteps() == 0);
    REQUIRE(um.getNumRedoSteps() == 1);

    // Redo the composite — should replay all three.
    um.redo();
    REQUIRE(value == 6);
    REQUIRE(um.getNumUndoSteps() == 1);
    REQUIRE(um.getNumRedoSteps() == 0);
}

//==============================================================================
TEST_CASE("UndoManager - composite with no sub-ops is discarded",
          "[undo][composite][empty]")
{
    UndoManager um;
    um.beginComposite("Empty Group");
    um.endComposite();

    // Nothing was added, so no step should exist.
    REQUIRE(um.getNumUndoSteps() == 0);
    REQUIRE_FALSE(um.canUndo());
}

//==============================================================================
TEST_CASE("UndoManager - descriptions", "[undo][descriptions]")
{
    UndoManager um;

    // No steps → empty descriptions.
    REQUIRE(um.getUndoDescription().isEmpty());
    REQUIRE(um.getRedoDescription().isEmpty());

    um.execute(makeCounterCmd(nullptr, 0, "First Op"));
    REQUIRE(um.getUndoDescription() == "First Op");
    REQUIRE(um.getRedoDescription().isEmpty());

    um.execute(makeCounterCmd(nullptr, 0, "Second Op"));
    REQUIRE(um.getUndoDescription() == "Second Op");

    // Undo → undo description changes, redo description appears.
    um.undo();
    REQUIRE(um.getUndoDescription() == "First Op");
    REQUIRE(um.getRedoDescription() == "Second Op");

    // Undo again.
    um.undo();
    REQUIRE(um.getUndoDescription().isEmpty());
    REQUIRE(um.getRedoDescription() == "First Op");

    // Redo → combo descriptions update.
    um.redo();
    REQUIRE(um.getUndoDescription() == "First Op");
    REQUIRE(um.getRedoDescription() == "Second Op");
}

//==============================================================================
TEST_CASE("UndoManager - composite descriptions", "[undo][composite][description]")
{
    UndoManager um;

    um.beginComposite("Grouped Action");
    um.execute(makeCounterCmd(nullptr, 1, "Sub A"));
    um.execute(makeCounterCmd(nullptr, 2, "Sub B"));
    um.endComposite();

    REQUIRE(um.getUndoDescription() == "Grouped Action");
}

//==============================================================================
TEST_CASE("UndoManager - clear resets everything", "[undo][clear]")
{
    UndoManager um;
    int value = 0;

    um.execute(makeCounterCmd(&value, 10));
    um.execute(makeCounterCmd(&value, 20));
    REQUIRE(value == 30);

    um.undo();
    REQUIRE(value == 10);

    um.clear();
    REQUIRE(value == 10);       // value unchanged by clear
    REQUIRE_FALSE(um.canUndo());
    REQUIRE_FALSE(um.canRedo());
    REQUIRE(um.getNumUndoSteps() == 0);
    REQUIRE(um.getNumRedoSteps() == 0);
    REQUIRE(um.getUndoDescription().isEmpty());
    REQUIRE(um.getRedoDescription().isEmpty());
}

//==============================================================================
TEST_CASE("UndoManager - nested composite is no-op", "[undo][composite][nested]")
{
    UndoManager um;
    int value = 0;

    um.beginComposite("Outer");
    um.execute(makeCounterCmd(&value, 1));

    // Nested beginComposite is silently ignored.
    um.beginComposite("Inner (should be ignored)");
    um.execute(makeCounterCmd(&value, 2));
    um.endComposite();  // This ends the inner no-op — still in outer composite.

    um.execute(makeCounterCmd(&value, 3));
    um.endComposite();  // Ends the outer composite.

    // All three sub-ops should be in one composite step.
    REQUIRE(um.getNumUndoSteps() == 1);
    REQUIRE(value == 6);  // 1 + 2 + 3

    um.undo();
    REQUIRE(value == 0);  // All reversed as one step.
}

//==============================================================================
TEST_CASE("UndoManager - unlimited depth when maxDepth is 0",
          "[undo][max_depth][unlimited]")
{
    UndoManager um(0);
    int value = 0;

    // Push 256 commands (default is 128, but we set 0 = unlimited).
    for (int i = 0; i < 256; ++i)
        um.execute(makeCounterCmd(&value, 1));

    REQUIRE(value == 256);
    REQUIRE(um.getNumUndoSteps() == 256);

    // Undo all 256 should bring us back to 0.
    for (int i = 0; i < 256; ++i)
        um.undo();

    REQUIRE(value == 0);
    REQUIRE(um.getNumUndoSteps() == 0);
    REQUIRE(um.getNumRedoSteps() == 256);
}

//==============================================================================
TEST_CASE("UndoManager - null command is safely ignored", "[undo][safety]")
{
    UndoManager um;

    // Should not crash.
    um.execute(nullptr);
    REQUIRE(um.getNumUndoSteps() == 0);
    REQUIRE_FALSE(um.canUndo());
}

//==============================================================================
TEST_CASE("UndoManager - undo and redo on empty stacks is safe",
          "[undo][safety]")
{
    UndoManager um;

    // Should not crash or assert.
    um.undo();
    um.redo();

    REQUIRE(um.getNumUndoSteps() == 0);
    REQUIRE(um.getNumRedoSteps() == 0);
}
