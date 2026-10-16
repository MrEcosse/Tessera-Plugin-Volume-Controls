#pragma once
// What a key of the tile performs, read from the words a person typed for it in the editor. No LVGL and no Tessera
// here, so a test on a computer can run it. The same parser as the transport controls plugin's.
//
//   media_player.media_play                     the action, on the tile's entity
//   remote.send_command command=EJECT           the action with one field, on the tile's entity
//   script.turn_on script.eject_disc            the action on an entity of its own
//   remote.send_command remote.hall command=up  both
//
// Words are separated by spaces. The field's value runs to the end, so it may hold spaces itself.
#include <cstddef>
#include <cstring>

namespace esphome::volume_controls {

struct Command {
  char action[65] = "";   // domain.service
  char target[65] = "";   // an entity of its own, "" for the tile's
  char key[33] = "";      // the field's name, "" for none
  char value[65] = "";    // its value
};

enum class Parsed { EMPTY, OK, INVALID };

namespace detail {
inline bool word_char(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
// domain.name: lowercase letters, digits and _, one dot, both sides filled in, the domain not starting with a digit.
inline bool dotted(const char *s, size_t n) {
  if (n < 3 || n > 64) return false;
  size_t dot = n;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] == '.') {
      if (dot != n) return false;
      dot = i;
    } else if (!word_char(s[i])) {
      return false;
    }
  }
  return dot != n && dot > 0 && dot + 1 < n && !(s[0] >= '0' && s[0] <= '9');
}
inline void copy(char *to, size_t size, const char *from, size_t n) {
  if (n >= size) n = size - 1;
  std::memcpy(to, from, n);
  to[n] = '\0';
}
inline bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
}  // namespace detail

// EMPTY: nothing typed, the key is left out. INVALID: something typed that is not an action (the key shows, in the
// colour of something wrong, and a tap sends nothing).
inline Parsed parse(const char *text, Command &out) {
  using namespace detail;
  out = Command{};
  if (!text) return Parsed::EMPTY;
  const char *p = text;
  const char *end = text + std::strlen(text);
  while (p < end && space(*p)) ++p;
  while (end > p && space(end[-1])) --end;
  if (p == end) return Parsed::EMPTY;

  // The action.
  const char *w = p;
  while (p < end && !space(*p)) ++p;
  if (!dotted(w, p - w)) return Parsed::INVALID;
  copy(out.action, sizeof out.action, w, p - w);
  while (p < end && space(*p)) ++p;
  if (p == end) return Parsed::OK;

  // An entity of its own, when the next word is not a field.
  w = p;
  while (p < end && !space(*p)) ++p;
  if (!std::memchr(w, '=', p - w)) {
    if (!dotted(w, p - w)) return Parsed::INVALID;
    copy(out.target, sizeof out.target, w, p - w);
    while (p < end && space(*p)) ++p;
    if (p == end) return Parsed::OK;
    w = p;
  }

  // The field: name=value, the value to the end.
  const char *eq = static_cast<const char *>(std::memchr(w, '=', end - w));
  if (!eq || eq == w || eq - w > 32) return Parsed::INVALID;
  for (const char *c = w; c < eq; ++c)
    if (!word_char(*c)) return Parsed::INVALID;
  const char *v = eq + 1;
  while (v < end && space(*v)) ++v;
  if (v == end || end - v > 64) return Parsed::INVALID;
  copy(out.key, sizeof out.key, w, eq - w);
  copy(out.value, sizeof out.value, v, end - v);
  return Parsed::OK;
}

}  // namespace esphome::volume_controls
