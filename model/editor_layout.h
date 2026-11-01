#ifndef EVERYTHINGEQ_MODEL_EDITOR_LAYOUT_H_
#define EVERYTHINGEQ_MODEL_EDITOR_LAYOUT_H_

#include <array>
#include <vector>

namespace eeq {

// Responsive editor geometry (ABOUT.md section 4), kept free of the UI
// framework so it can be tested.  The editor positions its controls from
// these rectangles and hides what a tier leaves out.

struct Rect {
  float l = 0.f, t = 0.f, r = 0.f, b = 0.f;

  float w() const { return r - l; }
  float h() const { return b - t; }
  bool empty() const { return w() <= 0.f || h() <= 0.f; }
  bool Contains(const Rect& o) const {
    return o.l >= l - 0.01f && o.r <= r + 0.01f && o.t >= t - 0.01f &&
           o.b <= b + 0.01f;
  }
  bool Overlaps(const Rect& o) const {
    return o.l < r && l < o.r && o.t < b && t < o.b;
  }
};

// What the window shows, from largest to smallest.  A window is in the
// smaller of its width and height tiers.
enum class LayoutTier {
  kMinimum,  // The EQ graph only.
  kSmall,    // Band ribbon, preset name and the graph.
  kCompact,  // Plus meters, fader and band strip; dynamics in a drawer.
  kFull,     // Everything, with the dynamics panel docked at the right.
};

inline constexpr int kDefaultEditorWidth = 1200;
inline constexpr int kDefaultEditorHeight = 720;
inline constexpr int kMinEditorWidth = 360;
inline constexpr int kMinEditorHeight = 220;
inline constexpr int kMaxEditorWidth = 2000;
inline constexpr int kMaxEditorHeight = 1300;

LayoutTier TierFor(float width, float height);

// The selected band's strip under the graph: type and routing on the left,
// three dials, then the Dyn A and Dyn B assignment cards.
struct StripLayout {
  Rect shape;  // Panel holding the info column and the dials.
  Rect info;
  std::array<Rect, 3> dials;
  std::array<Rect, 2> cards;
  bool compact = false;  // Fewer rows in the info column and cards.
};

struct EditorLayout {
  LayoutTier tier = LayoutTier::kFull;
  float width = 0.f, height = 0.f;

  bool show_top = false;
  Rect ribbon, preset;
  bool ribbon_power = false;  // Bypass dots in the ribbon cells.
  bool preset_undo = false;   // Undo/Redo beside the preset name.

  Rect graph;
  bool show_meters = false;
  Rect in_meter, out_meter, fader;

  bool show_strip = false;
  StripLayout strip;

  bool show_footer = false;
  Rect footer;

  // The dynamics panel: docked (full tier) or a drawer over the graph
  // (compact tier, while open).
  bool show_panel = false;
  bool panel_overlay = false;
  Rect panel;
  bool show_drawer_tab = false;
  Rect drawer_tab;
};

EditorLayout ComputeEditorLayout(float width, float height, bool drawer_open);

// The dynamics panel's contents: the routing map, then the selected group's
// header, an optional warning, the detector scope when there is room, and
// its foldable sections of two-column fields.
struct PanelSection {
  int rows = 0;           // Field rows when open.
  bool want_open = true;  // The user's choice.
  int fold_priority = 0;  // Folded first (lowest) when space runs out.
};

struct PanelLayout {
  Rect title;  // "ROUTING".
  Rect map;
  float map_block = 26.f;  // Height of one band's row pair in the map.
  bool show_legend = false;
  Rect legend;
  Rect divider;
  Rect header;
  bool show_warning = false;
  Rect warning;
  bool show_scope = false;
  Rect scope;
  bool show_sc_meter = false;
  Rect sc_meter;
  struct Section {
    Rect header;
    bool open = false;
    std::vector<Rect> cells;  // Row-major, two per row, when open.
  };
  std::vector<Section> sections;
};

// Map geometry shared by the layout and the routing map control.
inline constexpr float kMapHeaderHeight = 81.f;  // Source lines and heads.
inline constexpr float kMapMinBlock = 20.f;
inline constexpr float kMapMaxBlock = 26.f;
float MapHeight(float block);

PanelLayout ComputePanelLayout(const Rect& panel,
                               const std::vector<PanelSection>& sections,
                               bool warning, bool sc_meter);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_EDITOR_LAYOUT_H_
