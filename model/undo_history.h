#ifndef EVERYTHINGEQ_MODEL_UNDO_HISTORY_H_
#define EVERYTHINGEQ_MODEL_UNDO_HISTORY_H_

#include <cstddef>
#include <deque>
#include <vector>

namespace eeq {

struct ParamChange {
  int id;
  double value;
};

// Bounded undo/redo history of parameter edits made in the plugin UI (host
// automation is not recorded).  UI thread only.
//
// Edits between BeginGesture() and EndGesture() (e.g. one drag) form a single
// step; an edit outside a gesture is a step of its own.  Recording a new step
// clears the redo history.
class UndoHistory {
 public:
  explicit UndoHistory(size_t max_steps = 200) : max_steps_(max_steps) {}

  void BeginGesture();
  void EndGesture();
  void Record(int id, double before, double after);

  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }

  // Return the parameter values to apply.  Applying them must not be
  // recorded (call them outside Record()).
  std::vector<ParamChange> Undo();
  std::vector<ParamChange> Redo();

  void Clear();

 private:
  struct Edit {
    int id;
    double before;
    double after;
  };
  using Step = std::vector<Edit>;

  void Commit(Step step);

  size_t max_steps_;
  std::deque<Step> undo_;
  std::deque<Step> redo_;
  Step pending_;
  int gesture_depth_ = 0;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_UNDO_HISTORY_H_
