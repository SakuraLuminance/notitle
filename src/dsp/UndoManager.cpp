#include "UndoManager.h"

namespace ana {

//==============================================================================
UndoManager::UndoManager(int maxDepth)
    : maxDepth_(maxDepth)
{
}

UndoManager::~UndoManager()
{
    delete currentComposite_;
}

//==============================================================================
void UndoManager::execute(std::unique_ptr<UndoCommand> cmd)
{
    if (! cmd)
        return;

    // Capture the callbacks before moving the command.
    auto undoFn   = cmd->undo;
    auto redoFn   = cmd->execute;
    auto desc     = cmd->description;

    if (inComposite_ && currentComposite_ != nullptr)
    {
        // Append to the current composite step.
        currentComposite_->undoActions.push_back(std::move(undoFn));
        currentComposite_->redoActions.push_back(std::move(redoFn));

        // Execute the command immediately.
        if (currentComposite_->redoActions.back())
            currentComposite_->redoActions.back()();
    }
    else
    {
        // Push a new standalone undo step.
        UndoStep step;
        step.undoActions.push_back(std::move(undoFn));
        step.redoActions.push_back(std::move(redoFn));
        step.description = desc;

        undoStack_.push_back(std::move(step));

        // Run the command's execute action.
        if (undoStack_.back().redoActions.back())
            undoStack_.back().redoActions.back()();

        // Clear the redo stack — a new action invalidates all redos.
        redoStack_.clear();

        // Enforce maximum depth.
        enforceMaxDepth();
    }
}

//==============================================================================
void UndoManager::undo()
{
    if (undoStack_.empty())
        return;

    UndoStep step = std::move(undoStack_.back());
    undoStack_.pop_back();

    // Execute undo actions in reverse order so the last operation within
    // a composite is undone first.
    for (auto it = step.undoActions.rbegin();
         it != step.undoActions.rend();
         ++it)
    {
        if (*it)
            (*it)();
    }

    // Push onto the redo stack so it can be redone later.
    redoStack_.push_back(std::move(step));
}

//==============================================================================
void UndoManager::redo()
{
    if (redoStack_.empty())
        return;

    UndoStep step = std::move(redoStack_.back());
    redoStack_.pop_back();

    // Execute redo actions in forward order.
    for (auto& action : step.redoActions)
    {
        if (action)
            action();
    }

    // Push back onto the undo stack.
    undoStack_.push_back(std::move(step));

    // Enforce maximum depth after pushing to undo stack.
    enforceMaxDepth();
}

//==============================================================================
juce::String UndoManager::getUndoDescription() const
{
    if (undoStack_.empty())
        return {};

    return undoStack_.back().description;
}

juce::String UndoManager::getRedoDescription() const
{
    if (redoStack_.empty())
        return {};

    return redoStack_.back().description;
}

//==============================================================================
void UndoManager::clear()
{
    undoStack_.clear();
    redoStack_.clear();
    delete currentComposite_;
    currentComposite_ = nullptr;
    inComposite_ = false;
}

//==============================================================================
void UndoManager::beginComposite(const juce::String& description)
{
    // Nested composites are not supported — silently ignore.
    if (inComposite_)
        return;

    currentComposite_ = new UndoStep();  // Will be owned by undoStack_
    currentComposite_->description = description;
    inComposite_ = true;
}

void UndoManager::endComposite()
{
    if (! inComposite_ || currentComposite_ == nullptr)
        return;

    // If nothing was added during the composite, discard the empty step.
    if (currentComposite_->undoActions.empty())
    {
        delete currentComposite_;
        currentComposite_ = nullptr;
        inComposite_ = false;
        return;
    }

    UndoStep step = std::move(*currentComposite_);
    delete currentComposite_;
    currentComposite_ = nullptr;
    inComposite_ = false;

    undoStack_.push_back(std::move(step));
    redoStack_.clear();
    enforceMaxDepth();
}

//==============================================================================
void UndoManager::enforceMaxDepth()
{
    if (maxDepth_ <= 0)
        return;

    while (static_cast<int>(undoStack_.size()) > maxDepth_)
        undoStack_.erase(undoStack_.begin());
}

} // namespace ana
