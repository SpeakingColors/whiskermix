// Undo/redo over full mixer states (small states, so copies keep it simple and exact).
#pragma once
#include <vector>
#include "MixerModel.h"
#include "RoutingMatrix.h"

namespace wm::mixer {

// Mixer state plus routing: what undo and snapshots restore.
struct Workspace { State state; RoutingMatrix routing; };

template <class T>
class BasicUndoStack {
public:
    void push(const T& s) { undo_.push_back(s); redo_.clear(); if (undo_.size() > 200) undo_.erase(undo_.begin()); }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    // `current` is the state being replaced; returns the state to apply.
    T undo(const T& current) { T s = undo_.back(); undo_.pop_back(); redo_.push_back(current); return s; }
    T redo(const T& current) { T s = redo_.back(); redo_.pop_back(); undo_.push_back(current); return s; }

private:
    std::vector<T> undo_, redo_;
};

using UndoStack = BasicUndoStack<State>;
using WorkspaceUndo = BasicUndoStack<Workspace>;

}  // namespace wm::mixer
