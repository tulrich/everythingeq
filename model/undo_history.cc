#include "model/undo_history.h"

#include <algorithm>
#include <utility>

namespace eeq {

void UndoHistory::BeginGesture() { ++gesture_depth_; }

void UndoHistory::EndGesture() {
  if (gesture_depth_ == 0) return;
  if (--gesture_depth_ == 0 && !pending_.empty()) {
    Commit(std::move(pending_));
    pending_.clear();
  }
}

void UndoHistory::EndAllGestures() {
  if (gesture_depth_ == 0) return;
  gesture_depth_ = 1;
  EndGesture();
}

void UndoHistory::Record(int id, double before, double after) {
  if (gesture_depth_ == 0) {
    if (before != after) Commit({{id, before, after}});
    return;
  }
  // Within a gesture keep the first "before" and the last "after" per id.
  auto it = std::find_if(pending_.begin(), pending_.end(),
                         [id](const Edit& e) { return e.id == id; });
  if (it == pending_.end()) {
    pending_.push_back({id, before, after});
  } else {
    it->after = after;
  }
}

void UndoHistory::Commit(Step step) {
  step.erase(std::remove_if(step.begin(), step.end(),
                            [](const Edit& e) { return e.before == e.after; }),
             step.end());
  if (step.empty()) return;
  undo_.push_back(std::move(step));
  while (undo_.size() > max_steps_) undo_.pop_front();
  redo_.clear();
}

std::vector<ParamChange> UndoHistory::Undo() {
  std::vector<ParamChange> out;
  if (undo_.empty()) return out;
  Step step = std::move(undo_.back());
  undo_.pop_back();
  for (auto it = step.rbegin(); it != step.rend(); ++it) {
    out.push_back({it->id, it->before});
  }
  redo_.push_back(std::move(step));
  return out;
}

std::vector<ParamChange> UndoHistory::Redo() {
  std::vector<ParamChange> out;
  if (redo_.empty()) return out;
  Step step = std::move(redo_.back());
  redo_.pop_back();
  for (const Edit& e : step) out.push_back({e.id, e.after});
  undo_.push_back(std::move(step));
  return out;
}

void UndoHistory::Clear() {
  undo_.clear();
  redo_.clear();
  pending_.clear();
  gesture_depth_ = 0;
}

}  // namespace eeq
