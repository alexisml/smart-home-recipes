// wiz_udp - control WiZ bulbs from ESPHome over their local UDP API.
//
// Protocol: JSON over UDP port 38899, as used by Home Assistant's WiZ
// integration (pywizlight). Works with bulbs that accept unsigned setPilot;
// newer firmware that requires signed commands is not supported.
// Tested on ESP8266 (Arduino). See README.md for the API and usage.
#pragma once

#include <WiFiUdp.h>
#include <string>
#include <vector>
#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"
#include "esphome/components/light/light_state.h"

namespace wiz_udp {

static const char *const TAG = "wiz_udp";
static const uint16_t BULB_PORT = 38899;
static const uint16_t LOCAL_PORT = 38900;

// WiZ scenes (sceneId) in the order of your light's `effects:` list:
// effect index n (1-based, 0 = no effect) -> SCENES[n - 1]. Name the effects
// however you like; only the order matters. Rhythm (1000), custom (256+) and
// sync (201-204) scenes are left out.
static const int SCENES[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                             20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 40};
static const size_t N_SCENES = sizeof(SCENES) / sizeof(SCENES[0]);

// sceneId for an effect index (0 = no effect / not a scene).
inline int scene_for_effect(uint32_t effect_index) {
  return (effect_index >= 1 && effect_index <= N_SCENES) ? SCENES[effect_index - 1] : 0;
}
// Effect index for a sceneId (0 = not in the list).
inline uint32_t effect_for_scene(int scene) {
  for (size_t i = 0; i < N_SCENES; i++)
    if (SCENES[i] == scene) return (uint32_t) (i + 1);
  return 0;
}

struct Bulb {
  IPAddress ip;
  uint32_t seen = 0;   // millis() of the last getPilot answer (0 = never)
  int8_t state = -1;   // -1 unknown, 0 off, 1 on
  uint8_t dimming = 100;
  uint16_t temp = 0;   // Kelvin, 0 = not in white mode
  bool rgb = false;    // true = colour mode (r/g/b valid)
  int scene = 0;       // WiZ sceneId (0 = none)
  uint8_t r = 0, g = 0, b = 0;
};

inline WiFiUDP &udp() {
  static WiFiUDP u;
  static bool begun = false;
  if (!begun) {
    u.begin(LOCAL_PORT);
    begun = true;
  }
  return u;
}

inline std::vector<Bulb> &bulbs() {
  static std::vector<Bulb> v;
  return v;
}

// "192.168.1.50, 192.168.1.51,..." -> bulb list (keeps known state of unchanged IPs)
inline void set_ips(const std::string &s) {
  std::vector<Bulb> next;
  size_t p = 0;
  while (p <= s.size() && next.size() < 16) {
    size_t q = s.find(',', p);
    if (q == std::string::npos) q = s.size();
    std::string tok = s.substr(p, q - p);
    tok.erase(0, tok.find_first_not_of(" \t"));
    tok.erase(tok.find_last_not_of(" \t") + 1);
    IPAddress ip;
    if (!tok.empty() && ip.fromString(tok.c_str())) {
      Bulb b;
      b.ip = ip;
      for (auto &old : bulbs())
        if (old.ip == ip) b = old;
      next.push_back(b);
    }
    p = q + 1;
  }
  bulbs() = next;
  ESP_LOGI(TAG, "%u bulb(s) configured", (unsigned) bulbs().size());
}

inline void send_to(const IPAddress &ip, const std::string &msg) {
  udp().beginPacket(ip, BULB_PORT);
  udp().write((const uint8_t *) msg.data(), msg.size());
  udp().endPacket();
}

// Same command to every bulb back to back, so they change together.
inline void send_all(const std::string &msg) {
  for (auto &b : bulbs()) send_to(b.ip, msg);
}

static const char *const GET_PILOT = "{\"method\":\"getPilot\",\"params\":{}}";

// Ask every bulb at once.
inline void get_pilot_all() { send_all(GET_PILOT); }

// Drain the socket; record getPilot answers per bulb. Call often (interval).
inline void poll() {
  char buf[512];
  while (udp().parsePacket() > 0) {
    int n = udp().read(buf, sizeof(buf) - 1);
    if (n <= 0) continue;
    buf[n] = 0;
    if (strstr(buf, "\"getPilot\"") == nullptr) continue;  // setPilot acks etc.
    IPAddress from = udp().remoteIP();
    for (auto &b : bulbs()) {
      if (!(b.ip == from)) continue;
      esphome::json::parse_json(std::string(buf), [&b](JsonObject root) -> bool {
        JsonObject r = root["result"];
        if (r.isNull()) return false;
        b.seen = millis();
        if (r["state"].is<bool>()) b.state = r["state"].as<bool>() ? 1 : 0;
        if (r["dimming"].is<int>()) b.dimming = r["dimming"].as<int>();
        b.scene = r["sceneId"].is<int>() ? r["sceneId"].as<int>() : 0;
        if (r["r"].is<int>() && r["g"].is<int>() && r["b"].is<int>()) {
          b.rgb = true;
          b.r = r["r"].as<int>();
          b.g = r["g"].as<int>();
          b.b = r["b"].as<int>();
          b.temp = 0;
        } else if (r["temp"].is<int>()) {
          b.rgb = false;
          b.temp = r["temp"].as<int>();
        }
        return true;
      });
    }
  }
}

// Answered getPilot at or after `since` (millis).
inline bool fresh(const Bulb &b, uint32_t since) {
  return b.seen != 0 && (int32_t) (b.seen - since) >= 0;
}

inline bool any_fresh(uint32_t since) {
  for (auto &b : bulbs())
    if (fresh(b, since)) return true;
  return false;
}

// Every bulb has answered since `since` (e.g. all online after power-up).
inline bool all_fresh(uint32_t since) {
  if (bulbs().empty()) return false;
  for (auto &b : bulbs())
    if (!fresh(b, since)) return false;
  return true;
}

// What every bulb should look like.
struct Target {
  bool on = false;
  int dimming = 100;  // 10..100
  bool rgb = false;
  int r = 255, g = 255, b = 255;
  int temp = 2700;    // Kelvin, white mode
  int scene = 0;      // >0: WiZ scene (effect) instead of colour/white
};

inline Target target_off() { return Target(); }

// brightness 0..1; r/g/b 0..1 when `color`, else mireds for white.
inline Target target_on(float brightness, bool color, float r, float g, float b, float mireds) {
  Target t;
  t.on = true;
  t.dimming = (int) (brightness * 100.0f + 0.5f);
  if (t.dimming < 10) t.dimming = 10;  // WiZ minimum
  if (t.dimming > 100) t.dimming = 100;
  t.rgb = color;
  if (color) {
    t.r = (int) (r * 255.0f + 0.5f);
    t.g = (int) (g * 255.0f + 0.5f);
    t.b = (int) (b * 255.0f + 0.5f);
  } else {
    t.temp = mireds > 0 ? (int) (1000000.0f / mireds + 0.5f) : 2700;
    if (t.temp < 2200) t.temp = 2200;
    if (t.temp > 6500) t.temp = 6500;
  }
  return t;
}

// Target for the current state of a light entity (its effect -> scene).
inline Target target_from(esphome::light::LightState *l) {
  auto &v = l->remote_values;
  if (!v.is_on()) return target_off();
  Target t = target_on(v.get_brightness(), v.get_color_mode() == esphome::light::ColorMode::RGB, v.get_red(),
                       v.get_green(), v.get_blue(), v.get_color_temperature());
  t.scene = scene_for_effect(l->get_current_effect_index());
  return t;
}

inline std::string pilot(const Target &t) {
  char out[160];
  if (!t.on) {
    snprintf(out, sizeof(out), "{\"method\":\"setPilot\",\"params\":{\"state\":false}}");
  } else if (t.scene > 0) {
    snprintf(out, sizeof(out), "{\"method\":\"setPilot\",\"params\":{\"state\":true,\"sceneId\":%d,\"dimming\":%d}}",
             t.scene, t.dimming);
  } else if (t.rgb) {
    snprintf(out, sizeof(out),
             "{\"method\":\"setPilot\",\"params\":{\"state\":true,\"dimming\":%d,\"r\":%d,\"g\":%d,\"b\":%d}}",
             t.dimming, t.r, t.g, t.b);
  } else {
    snprintf(out, sizeof(out), "{\"method\":\"setPilot\",\"params\":{\"state\":true,\"dimming\":%d,\"temp\":%d}}",
             t.dimming, t.temp);
  }
  return std::string(out);
}

// Does the bulb's last report match the target (state, and when on:
// brightness, colour or white temperature)?
inline bool matches(const Bulb &b, const Target &t) {
  if (b.state != (t.on ? 1 : 0)) return false;
  if (!t.on) return true;
  if (abs((int) b.dimming - t.dimming) > 1) return false;
  if (t.scene > 0) return b.scene == t.scene;
  if (t.rgb)
    return b.rgb && abs((int) b.r - t.r) <= 3 && abs((int) b.g - t.g) <= 3 && abs((int) b.b - t.b) <= 3;
  return !b.rgb && abs((int) b.temp - t.temp) <= 50;
}

inline bool confirmed(const Bulb &b, uint32_t since, const Target &t) { return fresh(b, since) && matches(b, t); }

inline bool all_confirmed(uint32_t since, const Target &t) {
  if (bulbs().empty()) return false;
  for (auto &b : bulbs())
    if (!confirmed(b, since, t)) return false;
  return true;
}

// One verification round, all at once: bulbs that answered (since `since`)
// but don't match get the command again; every unconfirmed bulb is asked
// again. A bulb that just came online is caught by its first answer.
inline void verify_round(uint32_t since, const Target &t) {
  std::string cmd = pilot(t);
  for (auto &b : bulbs())
    if (fresh(b, since) && !matches(b, t)) send_to(b.ip, cmd);
  for (auto &b : bulbs())
    if (!confirmed(b, since, t)) send_to(b.ip, GET_PILOT);
}

inline std::string unconfirmed_list(uint32_t since, const Target &t) {
  std::string out;
  for (auto &b : bulbs())
    if (!confirmed(b, since, t)) out += std::string(b.ip.toString().c_str()) + " ";
  return out;
}

// How many configured bulbs answered within `max_age_ms`.
inline int online_count(uint32_t max_age_ms) {
  int n = 0;
  uint32_t now = millis();
  for (auto &b : bulbs())
    if (b.seen != 0 && now - b.seen <= max_age_ms) n++;
  return n;
}

// --- Snapshot / restore (used by calibration) -------------------------------

// Each bulb's last reported state as a Target.
inline Target target_of(const Bulb &b) {
  Target t;
  t.on = b.state == 1;
  t.dimming = b.dimming;
  t.rgb = b.rgb;
  t.r = b.r;
  t.g = b.g;
  t.b = b.b;
  if (b.temp > 0) t.temp = b.temp;
  // Scene 11 is also what plain warm white reports; restore it as white.
  t.scene = (b.scene > 0 && b.scene != 11 && effect_for_scene(b.scene) != 0) ? b.scene : 0;
  return t;
}

inline std::vector<Target> &snapshot() {
  static std::vector<Target> v;
  return v;
}

inline void take_snapshot() {
  snapshot().clear();
  for (auto &b : bulbs()) snapshot().push_back(target_of(b));
}

// One restore round: each bulb that doesn't match its snapshot gets its own
// command, all sent back to back; unconfirmed ones are asked again.
inline bool restore_round(uint32_t since) {
  bool all = true;
  auto &snap = snapshot();
  for (size_t i = 0; i < bulbs().size() && i < snap.size(); i++) {
    auto &b = bulbs()[i];
    if (fresh(b, since) && matches(b, snap[i])) continue;
    all = false;
    send_to(b.ip, pilot(snap[i]));
  }
  for (size_t i = 0; i < bulbs().size() && i < snap.size(); i++)
    if (!(fresh(bulbs()[i], since) && matches(bulbs()[i], snap[i]))) send_to(bulbs()[i].ip, GET_PILOT);
  return all;
}

inline std::string not_fresh_list(uint32_t since) {
  std::string out;
  for (auto &b : bulbs())
    if (!fresh(b, since)) out += std::string(b.ip.toString().c_str()) + " ";
  return out;
}

// Combined view of the bulbs answering within `max_age_ms`:
// on if any is on; attributes from the first one that is on.
struct Group {
  bool known = false, on = false, rgb = false;
  float brightness = 1.0f, r = 1.0f, g = 1.0f, b = 1.0f, mireds = 370.0f;
  int scene = 0;
};

inline Group group(uint32_t max_age_ms) {
  Group gr;
  uint32_t now = millis();
  for (auto &bl : bulbs()) {
    if (bl.seen == 0 || now - bl.seen > max_age_ms || bl.state < 0) continue;
    gr.known = true;
    if (bl.state == 1 && !gr.on) {
      gr.on = true;
      gr.brightness = bl.dimming / 100.0f;
      gr.scene = effect_for_scene(bl.scene) != 0 ? bl.scene : 0;
      if (bl.rgb) {
        gr.rgb = true;
        float m = std::max(bl.r, std::max(bl.g, bl.b));
        if (m < 1) m = 1;
        gr.r = bl.r / m;
        gr.g = bl.g / m;
        gr.b = bl.b / m;
      } else if (bl.temp > 0) {
        gr.mireds = 1000000.0f / bl.temp;
      }
    }
  }
  return gr;
}

}  // namespace wiz_udp
