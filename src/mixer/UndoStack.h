// Undo/redo over full mixer states (small states, so copies keep it simple and exact).
#pragma once
#include <vector>
#include "MixerModel.h"

namespace wm::mixer {

class UndoStack {
public:
    void push(const State& s) { undo_.push_back(s); redo_.clear(); if (undo_.size() > 200) undo_.erase(undo_.begin()); }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    // `current` is the state being replaced; returns the state to apply.
    State undo(const State& current) { State s = undo_.back(); undo_.pop_back(); redo_.push_back(current); return s; }
    State redo(const State& current) { State s = redo_.back(); redo_.pop_back(); undo_.push_back(current); return s; }

private:
    std::vector<State> undo_, redo_;
};

}  // namespace wm::mixer
