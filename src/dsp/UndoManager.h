#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

namespace ana {

//==============================================================================
/**
    A single undoable action backed by execute/undo callbacks.

    Each Command knows how to perform and how to reverse one operation.
    Callers create commands on the heap and pass ownership via
    UndoManager::execute().

    @see UndoManager
*/
struct UndoCommand
{
    /** Invoked to perform (or re-perform) the action. */
    std::function<void()> execute;

    /** Invoked to reverse the action. */
    std::function<void()> undo;

    /** Human-readable label (e.g. "Move Slider", "Change Filter Type"). */
    juce::String description;
};

//==============================================================================
/**
    Command-pattern undo/redo manager with configurable depth and
    composite (grouped) undo steps.

    Usage:
        UndoManager um(32);
        um.execute(std::make_unique<UndoCommand>(
            [&]() { slider.setValue(0.5f); },
            [&]() { slider.setValue(oldValue); },
            "Move Slider"));

        if (um.canUndo())  um.undo();
        if (um.canRedo())  um.redo();

    Composite commands group multiple individual execute() calls into
    one atomic undo step:
        um.beginComposite("Batch Edit");
        um.execute(std::make_unique<UndoCommand>(...));  // part 1
        um.execute(std::make_unique<UndoCommand>(...));  // part 2
        um.endComposite();
        // Now undo() rolls back both parts as a single step.

    @see UndoCommand
*/
class UndoManager
{
public:
    /** Creates an undo manager.
        @param maxDepth  Maximum number of undo steps retained.
                         Older steps are evicted (FIFO) when exceeded.
                         Default is 128.  Pass 0 for unlimited.
    */
    explicit UndoManager(int maxDepth = 128);

    ~UndoManager() = default;

    //==============================================================================
    /** Executes a command and pushes it onto the undo stack.
        The redo stack is cleared after a new execute.
        @param cmd  Command to execute.  Ownership is transferred.
    */
    void execute(std::unique_ptr<UndoCommand> cmd);

    /** Undoes the most recent command (or composite group). */
    void undo();

    /** Redoes the most recently undone command (or composite group). */
    void redo();

    //==============================================================================
    /** Returns true if there is at least one step to undo. */
    bool canUndo() const noexcept { return ! undoStack_.empty(); }

    /** Returns true if there is at least one step to redo. */
    bool canRedo() const noexcept { return ! redoStack_.empty(); }

    /** Returns the description of the next undo step, or empty string. */
    juce::String getUndoDescription() const;

    /** Returns the description of the next redo step, or empty string. */
    juce::String getRedoDescription() const;

    //==============================================================================
    /** Removes all undo and redo steps. */
    void clear();

    /** Returns the number of available undo steps. */
    int getNumUndoSteps() const noexcept
    {
        return static_cast<int>(undoStack_.size());
    }

    /** Returns the number of available redo steps. */
    int getNumRedoSteps() const noexcept
    {
        return static_cast<int>(redoStack_.size());
    }

    //==============================================================================
    /** Begins a composite undo step.
        All execute() calls between beginComposite() and endComposite()
        are grouped as a single undo step.  Nested composites are not
        supported — beginComposite() while already in a composite is a
        no-op.
    */
    void beginComposite(const juce::String& description);

    /** Ends a composite undo step and pushes it onto the undo stack.
        Must be preceded by a matching beginComposite().
    */
    void endComposite();

private:
    //==============================================================================
    /** Internal representation of one undoable step (single or composite). */
    struct UndoStep
    {
        /** Actions to run when undoing (in reverse order for composites). */
        std::vector<std::function<void()>> undoActions;

        /** Actions to run when redoing (in forward order for composites). */
        std::vector<std::function<void()>> redoActions;

        /** Human-readable description. */
        juce::String description;
    };

    std::vector<UndoStep> undoStack_;
    std::vector<UndoStep> redoStack_;
    int maxDepth_;

    // Composite state
    UndoStep* currentComposite_ = nullptr;
    bool inComposite_ = false;

    /** Enforces max depth by removing the oldest undo steps. */
    void enforceMaxDepth();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UndoManager)
};

} // namespace ana
