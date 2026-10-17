#include "volume_controls.h"
#include "volume_command.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace esphome::volume_controls {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

// The keys in the order the manifest lists them (quieter, mute, louder; as channel keys, down and up). Their icons are Material Design Icons of
// Tessera's set. Across the tile they stand in this order, as minus, mute, plus; down a tile that is taller than wide
// they stand the other way, louder at the top.
struct KeyKind {
  const char *option;
  uint32_t glyph;
};
static constexpr KeyKind KINDS[] = {
    {"down", 0xF075E},  // volume-minus
    {"mute", 0xF075F},  // volume-mute
    {"up", 0xF075D},    // volume-plus
};
static constexpr int KIND_COUNT = sizeof(KINDS) / sizeof(KINDS[0]);
enum Kind { DOWN, MUTE, UP };

// The channel style (0.2.0): the same two keys as chevrons, pointing along the row across a tile that is as wide as it is
// high or wider and up and down in a column. chevron-left/right, chevron-up/down.
static uint32_t channel_glyph(Kind kind, bool across) {
  if (across) return kind == UP ? 0xF0142 : 0xF0141;
  return kind == UP ? 0xF0143 : 0xF0140;
}

// How long a key stays lit after a tap: at least this long, and until the next tick after it.
static constexpr uint32_t FLASH_MS = 300;

// The tile: the keys that have an action, in a row across the middle of a tile that is as wide as it is high or wider,
// or in a column down the middle of a taller one.
class VolumeTile : public tessera::Tile {
 public:
  explicit VolumeTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    const char *entity = c.entity ? c.entity : "";
    detail::copy(entity_, sizeof entity_, entity, std::strlen(entity));
    player_ = std::strncmp(entity_, "media_player.", 13) == 0;
    // A tile set up before this option has none, and gets the volume keys it always had.
    channel_ = std::string(c.options["style"] | "volume") == "channel";

    for (int k = 0; k < KIND_COUNT; ++k) {
      if (channel_ && k == MUTE) continue;  // channels have no mute key
      Key &key = keys_[count_];
      const Parsed parsed = parse(c.options[KINDS[k].option] | "", key.command);
      if (parsed == Parsed::EMPTY) continue;
      key.kind = static_cast<Kind>(k);
      key.valid = parsed == Parsed::OK;
      ++count_;
    }

    if (count_ == 0) {
      // Nothing to press yet: say what to do, in the middle.
      hint_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_width(hint_, c.width);
      lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
      ui::set_text(hint_, plugin_->text("no_keys"));
      lv_obj_set_pos(hint_, 0, (c.height - ui::line_height(Font::BODY)) / 2);
      return;
    }

    // Down a column the keys stand the other way round: louder first.
    across_ = c.width >= c.height;
    if (!across_) std::reverse(keys_, keys_ + count_);

    // Keys as large as the tile allows but no more than 14 mm high, and no wider than half again their height: a
    // finger's key, not a slab on a large tile. They share the length of the tile, and the gap narrows before they do.
    auto fit = [&]() {
      if (across_) {
        kw_ = (c.width - (count_ - 1) * gap_) / count_;
        kh_ = c.height;
      } else {
        kh_ = (c.height - (count_ - 1) * gap_) / count_;
        kw_ = c.width;
      }
    };
    gap_ = ui::px(8);
    fit();
    if (count_ > 1 && std::min(kw_, kh_) < ui::mm(9)) {
      gap_ = ui::px(3);
      fit();
    }
    kh_ = std::max(1, std::min(kh_, ui::mm(14)));
    kw_ = std::max(1, std::min(kw_, kh_ * 3 / 2));
    left_ = (c.width - (across_ ? count_ * kw_ + (count_ - 1) * gap_ : kw_)) / 2;
    top_ = (c.height - (across_ ? kh_ : count_ * kh_ + (count_ - 1) * gap_)) / 2;
    const int radius = std::min(ui::px(ui::large() ? 12 : 8), std::min(kw_, kh_) / 2);

    // The largest icon that leaves the key a margin.
    const int room = std::min(kw_, kh_) * 3 / 4;
    const Font face = ui::text_width(ui::icon(0xF040A), Font::ICON) <= room ? Font::ICON : Font::ICON_SMALL;
    const int line = ui::line_height(face);

    for (int i = 0; i < count_; ++i) {
      Key &key = keys_[i];
      key.block = ui::block(c.parent, theme::BUTTON);
      lv_obj_set_size(key.block, kw_, kh_);
      lv_obj_set_pos(key.block, left_ + (across_ ? i * (kw_ + gap_) : 0), top_ + (across_ ? 0 : i * (kh_ + gap_)));
      lv_obj_set_style_radius(key.block, radius, 0);
      key.icon = ui::label(key.block, face, theme::INK);
      lv_obj_set_width(key.icon, kw_);
      lv_obj_set_style_text_align(key.icon, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(key.icon, 0, (kh_ - line) / 2);
      ui::set_text(key.icon, ui::icon(channel_ ? channel_glyph(key.kind, across_) : KINDS[key.kind].glyph));
    }
    paint();
  }

  // The entity's state: a media player that is muted lights the mute key; one that is not there greys the keys.
  void on_state(JsonObjectConst data) override {
    const char *state = data["state"] | "";
    const bool muted = data["attributes"]["is_volume_muted"] | false;
    if (muted == muted_ && std::strcmp(state, state_) == 0) return;
    muted_ = muted;
    detail::copy(state_, sizeof state_, state, std::strlen(state));
    paint();
  }

  void on_tick(uint32_t) override {
    if (flash_ >= 0 && esphome::millis() - flash_at_ >= FLASH_MS) {
      flash_ = -1;
      paint();
    }
  }

  void on_theme() override {
    if (hint_) ui::set_color(hint_, theme::MUTED);
    paint();
  }

  void on_tap() override {
    const int i = key_at(tessera::tap_point());
    if (i < 0) return;
    const Key &key = keys_[i];
    const char *target = key.command.target[0] ? key.command.target : entity_;
    bool sent = false;
    if (key.valid && target[0])
      sent = tessera::action(key.command.action, target, key.command.key[0] ? key.command.key : nullptr,
                             key.command.value);
    flash_ = i;
    flash_ok_ = sent;
    flash_at_ = esphome::millis();
    paint();
  }

 private:
  struct Key {
    Kind kind = DOWN;
    bool valid = false;
    Command command;
    lv_obj_t *block = nullptr, *icon = nullptr;
  };

  // The key under the finger: its own size and half the gap around it, so a tap between two keys still counts for the
  // nearer one. A tap off the ends of the row or column gives -1, and a tile with a single key gives it whenever the
  // point is unknown.
  int key_at(tessera::Point p) const {
    if (count_ == 0) return -1;
    if (p.x < 0 || p.y < 0) return count_ == 1 ? 0 : -1;
    const int along = across_ ? p.x - left_ + gap_ / 2 : p.y - top_ + gap_ / 2;
    if (along < 0) return -1;
    const int i = along / ((across_ ? kw_ : kh_) + gap_);
    return i < count_ ? i : -1;
  }

  // Whether the player is muted: the mute key is lit for as long.
  bool lit(Kind kind) const { return player_ && muted_ && kind == MUTE; }

  void paint() {
    const bool away = std::strcmp(state_, "unavailable") == 0;
    for (int i = 0; i < count_; ++i) {
      Key &key = keys_[i];
      // A tap: the pressed blue while it goes out, red when it could not be sent.
      if (i == flash_) {
        if (flash_ok_) ui::set_fill(key.block, Tone::NORMAL, theme::ACCENT_PRESSED);
        else ui::set_fill(key.block, Tone::ALERT, theme::BUTTON);
        ui::set_color(key.icon, theme::ON_ACCENT);
        continue;
      }
      // What the player does now: a blue key, as a chosen segment is.
      if (key.valid && !away && lit(key.kind)) {
        ui::set_fill(key.block, Tone::NORMAL, theme::ACCENT);
        ui::set_color(key.icon, theme::ON_ACCENT);
        continue;
      }
      ui::set_fill(key.block, Tone::NORMAL, theme::BUTTON);
      if (!key.valid) ui::set_tone(key.icon, Tone::ALERT, theme::INK);
      else if (away) ui::set_color(key.icon, theme::SUBTLE);
      else ui::set_color(key.icon, theme::INK);
    }
  }

  const tessera::Plugin *plugin_;
  Key keys_[KIND_COUNT];
  int count_ = 0;
  bool across_ = true;
  int kw_ = 1, kh_ = 1, gap_ = 0, left_ = 0, top_ = 0;
  char entity_[128] = "";
  char state_[32] = "";
  bool player_ = false;
  bool channel_ = false;
  bool muted_ = false;
  int flash_ = -1;
  bool flash_ok_ = false;
  uint32_t flash_at_ = 0;
  lv_obj_t *hint_ = nullptr;
};

void VolumeControls::setup() {
  add_tile("volume", [this]() { return new VolumeTile(this); });
}

}  // namespace esphome::volume_controls
