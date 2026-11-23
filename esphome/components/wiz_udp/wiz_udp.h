// wiz_udp - control WiZ bulbs from ESPHome over their local UDP API.
//
// Protocol: JSON over UDP port 38899, as used by Home Assistant's WiZ
// integration (pywizlight). Works with bulbs that accept unsigned setPilot;
// newer firmware that requires signed commands is not supported.
// Tested on ESP8266 (Arduino). See README.md for the API and usage.
#pragma once

#include <WiFiUdp.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/network/util.h"
#include "esphome/core/component.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"
#ifdef USE_TEXT
#include "esphome/components/text/text.h"
#endif

namespace wiz_udp {

static const char *const TAG = "wiz_udp";
static const uint16_t BULB_PORT = 38899;
static const uint16_t LOCAL_PORT = 38900;

// A WiZ scene shown as an effect on a light. It does nothing on its own:
// selecting it changes the light's state, and your on_state logic sends the
// scene (target_from() reads it back from the effect name).
class SceneEffect : public esphome::light::LightEffect {
 public:
  SceneEffect(const char *name, int scene) : LightEffect(name), scene_(scene) {}
  void apply() override {}
  int scene() const { return this->scene_; }

 protected:
  int scene_;
};

inline std::vector<SceneEffect *> &scene_effects() {
  static std::vector<SceneEffect *> v;
  return v;
}

// sceneId for an effect name ("None" / unknown -> 0).
inline int scene_for_effect_name(const char *name) {
  for (auto *e : scene_effects())
    if (strcmp(e->get_name().c_str(), name) == 0) return e->scene();
  return 0;
}
// Effect name for a sceneId (nullptr = no effect for it).
inline const char *effect_name_for_scene(int scene) {
  for (auto *e : scene_effects())
    if (e->scene() == scene) return e->get_name().c_str();
  return nullptr;
}

// Scene 11 ("warm white") is also what a bulb reports in plain warm white,
// e.g. right after power-up, so it is treated as white, not as a scene.
static const int SCENE_WARM_WHITE = 11;
inline bool is_scene(int scene) { return scene > 0 && scene != SCENE_WARM_WHITE && effect_name_for_scene(scene) != nullptr; }

// Scenes that change their own brightness (wake-up, bedtime, pulse, alarm,
// dim-to-warm): confirmed by scene id only, since their reported dimming
// moves and resending would restart the animation.
inline bool scene_varies_brightness(int scene) {
  return scene == 9 || scene == 10 || scene == 31 || scene == 35 || scene == 40;
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
  bool has_mac = false;  // MAC given in the list (ip@mac)
  uint8_t mac[6] = {0};
  uint32_t announced = 0;  // millis() of the last unicast ARP announcement to it
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

// Bulb list from "ip, ip, ..." - each entry optionally "ip@mac" (MAC as
// 12 hex digits, colons allowed). Keeps the known state of unchanged IPs.
// The MAC is only needed for bulbs that never answer this device's ARP
// requests (see arp_prime()).
inline bool parse_mac(const std::string &txt, uint8_t out[6]) {
  int n = 0, hi = -1;
  for (char c : txt) {
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else if (c == ':' || c == '-') continue;
    else return false;
    if (hi < 0) {
      hi = v;
    } else {
      if (n >= 6) return false;
      out[n++] = (uint8_t) (hi << 4 | v);
      hi = -1;
    }
  }
  return n == 6 && hi < 0;
}

inline void set_ips(const std::string &s) {
  std::vector<Bulb> next;
  size_t p = 0;
  while (p <= s.size() && next.size() < 16) {
    size_t q = s.find(',', p);
    if (q == std::string::npos) q = s.size();
    std::string tok = s.substr(p, q - p);
    tok.erase(0, tok.find_first_not_of(" \t"));
    tok.erase(tok.find_last_not_of(" \t") + 1);
    std::string mac_txt;
    size_t at = tok.find('@');
    if (at != std::string::npos) {
      mac_txt = tok.substr(at + 1);
      tok = tok.substr(0, at);
    }
    IPAddress ip;
    if (!tok.empty() && ip.fromString(tok.c_str())) {
      Bulb b;
      b.ip = ip;
      for (auto &old : bulbs())
        if (old.ip == ip) b = old;
      b.has_mac = !mac_txt.empty() && parse_mac(mac_txt, b.mac);
      if (!mac_txt.empty() && !b.has_mac) ESP_LOGW(TAG, "bad MAC '%s' for %s", mac_txt.c_str(), tok.c_str());
      next.push_back(b);
    }
    p = q + 1;
  }
  bulbs() = next;
  ESP_LOGI(TAG, "%u bulb(s) configured", (unsigned) bulbs().size());
}

// --- ARP ------------------------------------------------------------------------
// Before the first packet to a bulb, lwIP must learn its MAC with a broadcast
// "who has <ip>?". Some (older, power-saving) WiZ bulbs never answer that
// from a device on their own subnet, so every packet to them is dropped
// before it leaves - while hosts reaching them through a router work fine.
// arp_known() shows whether the MAC is resolved; for bulbs listed as ip@mac,
// arp_prime() feeds lwIP that bulb's ARP reply itself, so sends go out as
// unicast straight away, and arp_announce() tells the bulb our MAC so its
// answers come back. (This core's lwIP has static ARP entries compiled
// out, so the entry is refreshed before sends instead.)

inline bool arp_known(const IPAddress &ip) {
  struct netif *nif = netif_default;
  if (nif == nullptr) return false;
  ip4_addr_t a;
  a.addr = (uint32_t) ip;
  struct eth_addr *eth = nullptr;
  const ip4_addr_t *ip_ret = nullptr;
  return etharp_find_addr(nif, &a, &eth, &ip_ret) >= 0;
}

inline void arp_prime(const Bulb &b) {
  struct netif *nif = netif_default;
  if (!b.has_mac || nif == nullptr || arp_known(b.ip)) return;
  struct pbuf *p = pbuf_alloc(PBUF_RAW, 42, PBUF_RAM);
  if (p == nullptr) return;
  uint8_t *f = (uint8_t *) p->payload;
  memcpy(f, nif->hwaddr, 6);  // Ethernet: to us, from the bulb, type ARP
  memcpy(f + 6, b.mac, 6);
  f[12] = 0x08;
  f[13] = 0x06;
  uint8_t *a = f + 14;  // ARP reply: <bulb ip> is at <bulb mac>
  a[0] = 0x00;
  a[1] = 0x01;
  a[2] = 0x08;
  a[3] = 0x00;
  a[4] = 6;
  a[5] = 4;
  a[6] = 0x00;
  a[7] = 0x02;
  memcpy(a + 8, b.mac, 6);
  uint32_t sip = (uint32_t) b.ip;
  memcpy(a + 14, &sip, 4);
  memcpy(a + 18, nif->hwaddr, 6);
  uint32_t tip = ip4_addr_get_u32(netif_ip4_addr(nif));
  memcpy(a + 24, &tip, 4);
  // Straight into lwIP's Ethernet input, as the ESP8266 glue does for every
  // received frame (netif->input is a stub there). It takes the pbuf.
  ethernet_input(p, nif);
}

// The other direction: the bulb must also know OUR MAC to answer, and its
// broadcast "who has <our ip>?" doesn't reach us either. A bulb learns the
// asker's MAC from any ARP request addressed to it, so send it a real ARP
// request ("who has <bulb ip>? tell <our ip>") - but unicast to its MAC,
// which does get through. Its reply also gives lwIP a genuine entry.
// Refreshed every ARP_ANNOUNCE_MS (ARP caches expire; one bulb in testing
// forgot us within seconds), and before every retry of a command. An unsolicited ARP
// reply was tried first and WiZ bulbs ignored it.
static const uint32_t ARP_ANNOUNCE_MS = 15000;
// A bulb that hasn't answered recently (e.g. still booting after its power
// came on) gets the request every ARP_RETRY_MS instead, so it's reachable
// within seconds of coming online rather than up to a minute later.
static const uint32_t ARP_RETRY_MS = 3000;
static const uint32_t ARP_QUIET_MS = 10000;

inline uint32_t &arp_sent() {
  static uint32_t n = 0;
  return n;
}
inline uint32_t &arp_failed() {
  static uint32_t n = 0;
  return n;
}

inline void arp_announce(Bulb &b, bool force = false) {
  struct netif *nif = netif_default;
  if (!b.has_mac || nif == nullptr) return;
  uint32_t now = millis();
  bool answering = b.seen != 0 && now - b.seen < ARP_QUIET_MS;
  if (!force && b.announced != 0 && now - b.announced < (answering ? ARP_ANNOUNCE_MS : ARP_RETRY_MS)) return;
  // Allocated like lwIP's own ARP packets (PBUF_LINK: headroom for the
  // Ethernet header and whatever the WiFi driver prepends), and sent through
  // ethernet_output(). A bare PBUF_RAW frame handed to linkoutput corrupted
  // the heap on the ESP8266 (crash in pbuf_free after transmit).
  struct pbuf *p = pbuf_alloc(PBUF_LINK, 28, PBUF_RAM);
  if (p == nullptr) return;
  uint8_t *a = (uint8_t *) p->payload;  // ARP request: who has <bulb ip>? tell <our ip>
  a[0] = 0x00;
  a[1] = 0x01;  // Ethernet
  a[2] = 0x08;
  a[3] = 0x00;  // IPv4
  a[4] = 6;
  a[5] = 4;
  a[6] = 0x00;
  a[7] = 0x01;  // request
  memcpy(a + 8, nif->hwaddr, 6);
  uint32_t sip = ip4_addr_get_u32(netif_ip4_addr(nif));
  memcpy(a + 14, &sip, 4);
  memset(a + 18, 0, 6);
  uint32_t tip = (uint32_t) b.ip;
  memcpy(a + 24, &tip, 4);
  struct eth_addr src, dst;
  memcpy(src.addr, nif->hwaddr, 6);
  memcpy(dst.addr, b.mac, 6);  // unicast to the bulb
  if (ethernet_output(nif, p, &src, &dst, 0x0806) == ERR_OK)  // ETHTYPE_ARP
    arp_sent()++;
  else
    arp_failed()++;
  pbuf_free(p);
  b.announced = millis();
}

// Counters for diagnostics (status_text()).
struct Stats {
  uint32_t tx = 0, tx_fail = 0;              // packets sent / sends the stack refused
  uint32_t rx = 0, rx_matched = 0;           // getPilot answers received / matched to a bulb
  uint32_t rx_unknown = 0, rx_bad = 0;       // from an address not in the list / unparseable
};
inline Stats &stats() {
  static Stats st;
  return st;
}

inline void poll();  // defined below

// Commands go out as fast as the ESP8266 allows. Its WiFi transmit queue is
// tiny: UDP packets sent back to back are dropped silently (in testing only
// the first of a burst of six went out). So consecutive sends are spaced
// SEND_GAP_MS apart - about 25 ms for six bulbs, "at once" to the eye - and
// answers are drained while waiting. A send the stack reports as failed is
// retried once.
static const uint32_t SEND_GAP_MS = 4;

inline void send_to(const IPAddress &ip, const std::string &msg) {
  static uint32_t last = 0;
  while (millis() - last < SEND_GAP_MS) {
    delay(1);  // lets the WiFi stack run
    poll();
  }
  for (auto &b : bulbs()) {
    if (!(b.ip == ip)) continue;
    arp_prime(b);
    arp_announce(b);
  }
  for (int attempt = 0; attempt < 2; attempt++) {
    udp().beginPacket(ip, BULB_PORT);
    udp().write((const uint8_t *) msg.data(), msg.size());
    stats().tx++;
    if (udp().endPacket()) break;
    stats().tx_fail++;
    delay(5);
    poll();
  }
  last = millis();
}

// Same command to every bulb in one quick sweep, so they change together.
inline void send_all(const std::string &msg) {
  for (auto &b : bulbs()) send_to(b.ip, msg);
}

static const char *const GET_PILOT = "{\"method\":\"getPilot\",\"params\":{}}";

// --- Status queries: sequential, from the component's loop -------------------
// Action commands go out in one fast sweep; status questions don't need to.
// ask()/ask_all() only mark bulbs, and the component's loop sends one
// getPilot every STATUS_GAP_MS, so answers arrive one by one (the ESP8266
// core buffers only 4 unread packets) and never delay a command.
static const uint32_t STATUS_GAP_MS = 60;

inline uint32_t &ask_mask() {
  static uint32_t m = 0;
  return m;
}
inline void ask(size_t i) {
  if (i < 32) ask_mask() |= (1u << i);
}
inline void ask_bulb(const IPAddress &ip) {
  for (size_t i = 0; i < bulbs().size(); i++)
    if (bulbs()[i].ip == ip) ask(i);
}
// Ask every bulb for its state (queued, one at a time).
inline void get_pilot_all() {
  for (size_t i = 0; i < bulbs().size(); i++) ask(i);
}
// Called from the loop: sends the next queued question if it's time.
inline void ask_next() {
  static uint32_t last = 0;
  static size_t cursor = 0;
  size_t n = bulbs().size();
  ask_mask() &= (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1);
  if (ask_mask() == 0 || millis() - last < STATUS_GAP_MS) return;
  for (size_t k = 0; k < n; k++) {
    size_t i = (cursor + k) % n;
    if (ask_mask() & (1u << i)) {
      ask_mask() &= ~(1u << i);
      cursor = i + 1;
      last = millis();
      send_to(bulbs()[i].ip, GET_PILOT);
      return;
    }
  }
}

// Drain the socket; record getPilot answers per bulb. Call often (interval).
inline void poll() {
  char buf[512];
  while (udp().parsePacket() > 0) {
    int n = udp().read(buf, sizeof(buf) - 1);
    if (n <= 0) continue;
    buf[n] = 0;
    if (strstr(buf, "\"getPilot\"") == nullptr) continue;  // setPilot acks etc.
    stats().rx++;
    IPAddress from = udp().remoteIP();
    bool known = false;
    for (auto &b : bulbs()) {
      if (!(b.ip == from)) continue;
      known = true;
      bool ok = esphome::json::parse_json(std::string(buf), [&b](JsonObject root) -> bool {
        JsonObject r = root["result"];
        if (r.isNull()) return false;
        b.seen = millis();
        if (r["state"].is<bool>()) b.state = r["state"].as<bool>() ? 1 : 0;
        if (r["dimming"].is<int>()) b.dimming = r["dimming"].as<int>();
        b.scene = r["sceneId"].is<int>() ? r["sceneId"].as<int>() : 0;
        if (r["r"].is<int>() && r["g"].is<int>() && r["b"].is<int>() &&
            (r["r"].as<int>() > 0 || r["g"].as<int>() > 0 || r["b"].as<int>() > 0)) {
          b.rgb = true;
          b.r = r["r"].as<int>();
          b.g = r["g"].as<int>();
          b.b = r["b"].as<int>();
          b.temp = 0;
        } else if (r["temp"].is<int>()) {
          b.rgb = false;
          b.temp = r["temp"].as<int>();
        } else {
          // Neither colour nor white reported (some scenes): don't keep stale
          // values. Scene 11 is warm white, 2700 K.
          b.rgb = false;
          b.temp = b.scene == SCENE_WARM_WHITE ? 2700 : 0;
        }
        return true;
      });
      if (ok)
        stats().rx_matched++;
      else
        stats().rx_bad++;
    }
    if (!known) {
      stats().rx_unknown++;
      ESP_LOGW(TAG, "answer from %s, which is not in the bulb list", from.toString().c_str());
    }
  }
}

// One-line diagnostics: traffic counters and, per bulb, seconds since its
// last answer ("-" = never, "?" appended = its MAC isn't resolved), e.g.
// "tx 120/0 rx 40 ok 40 unk 0 bad 0 | .35 2s .36 -?".
inline std::string status_text() {
  auto &st = stats();
  char head[112];
  snprintf(head, sizeof(head), "tx %u/%u rx %u ok %u unk %u bad %u arp %u/%u |", (unsigned) st.tx,
           (unsigned) st.tx_fail, (unsigned) st.rx, (unsigned) st.rx_matched, (unsigned) st.rx_unknown,
           (unsigned) st.rx_bad, (unsigned) arp_sent(), (unsigned) arp_failed());
  std::string out(head);
  uint32_t now = millis();
  for (auto &b : bulbs()) {
    char part[24];
    const char *arp = arp_known(b.ip) ? "" : "?";  // "?" = MAC not resolved
    if (b.seen == 0)
      snprintf(part, sizeof(part), " .%u -%s", (unsigned) b.ip[3], arp);
    else
      snprintf(part, sizeof(part), " .%u %us%s", (unsigned) b.ip[3], (unsigned) ((now - b.seen) / 1000), arp);
    out += part;
  }
  return out;
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
  t.scene = scene_for_effect_name(l->get_effect_name().c_str());
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
// Same colour, compared as proportions (hue/saturation) so bulbs that scale
// or round their channels still match. All-zero channels never match.
inline bool same_color(int r1, int g1, int b1, int r2, int g2, int b2) {
  int m1 = std::max(r1, std::max(g1, b1)), m2 = std::max(r2, std::max(g2, b2));
  if (m1 <= 0 || m2 <= 0) return false;
  return fabsf((float) r1 / m1 - (float) r2 / m2) <= 0.1f && fabsf((float) g1 / m1 - (float) g2 / m2) <= 0.1f &&
         fabsf((float) b1 / m1 - (float) b2 / m2) <= 0.1f;
}

inline bool matches(const Bulb &b, const Target &t) {
  if (b.state != (t.on ? 1 : 0)) return false;
  if (!t.on) return true;
  // Scenes: scene id plus brightness (±2, a little looser than colours since
  // resending a scene restarts its animation).
  if (t.scene > 0)
    return b.scene == t.scene && (scene_varies_brightness(t.scene) || abs((int) b.dimming - t.dimming) <= 2);
  if (abs((int) b.dimming - t.dimming) > 1) return false;
  if (t.rgb) return b.rgb && same_color(b.r, b.g, b.b, t.r, t.g, t.b);
  return !b.rgb && abs((int) b.temp - t.temp) <= 50;
}

inline bool confirmed(const Bulb &b, uint32_t since, const Target &t) { return fresh(b, since) && matches(b, t); }

inline bool all_confirmed(uint32_t since, const Target &t) {
  if (bulbs().empty()) return false;
  for (auto &b : bulbs())
    if (!confirmed(b, since, t)) return false;
  return true;
}

// One verification round. A bulb that answers in the wrong state gets the
// command again right away. A silent bulb (no answer since the command) may
// be booting, or getting commands while its answers are lost, so it gets the
// command again - preceded by a fresh unicast ARP request - only every 4th
// round (about once a second): enough for a lost command, without flooding
// the ESP8266's small transmit queue or restarting a scene's animation 4x a
// second. Every unconfirmed bulb is also queued to be asked again.
inline void verify_round(uint32_t since, const Target &t) {
  std::string cmd = pilot(t);
  static uint32_t round = 0;
  round++;
  for (auto &b : bulbs()) {
    if (confirmed(b, since, t)) continue;
    if (fresh(b, since)) {
      send_to(b.ip, cmd);  // answering, wrong state
    } else if (round % 4 == 0) {
      arp_announce(b, true);  // silent
      send_to(b.ip, cmd);
    }
  }
  for (size_t i = 0; i < bulbs().size(); i++)
    if (!confirmed(bulbs()[i], since, t)) ask(i);
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
  t.scene = is_scene(b.scene) ? b.scene : 0;
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
    if (!(fresh(bulbs()[i], since) && matches(bulbs()[i], snap[i]))) ask(i);
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
  int scene = 0;      // running scene (is_scene)
  int raw_scene = 0;  // sceneId as reported, including 11
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
      gr.scene = is_scene(bl.scene) ? bl.scene : 0;
      gr.raw_scene = bl.scene;
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

// --- Quiet light updates ------------------------------------------------------
// Internal updates of the light (sync, mirroring a relay, ending an effect)
// go through perform_quietly(). on_state still fires (synchronously, inside
// perform), so gate your user-command logic with `if (!wiz_udp::quiet())`.

inline bool &quiet_flag() {
  static bool q = false;
  return q;
}
inline bool quiet() { return quiet_flag(); }
inline void perform_quietly(esphome::light::LightCall &call) {
  quiet_flag() = true;
  call.perform();
  quiet_flag() = false;
}

// --- One command at a time, sent to all, then verified ------------------------

struct Command {
  Target target;
  uint32_t started = 0;
  uint32_t window = 0;
};
inline Command &command() {
  static Command c;
  return c;
}

// Send `t` to every bulb now; verification runs for `window_ms`.
inline void begin_command(const Target &t, uint32_t window_ms) {
  auto &c = command();
  c.target = t;
  c.started = millis();
  c.window = window_ms;
  send_all(pilot(t));
}
inline bool command_confirmed() { return all_confirmed(command().started, command().target); }
// One verification round. True while the command is still pending
// (not confirmed by every bulb and inside its window): call every ~250 ms.
inline bool command_step() {
  auto &c = command();
  if (command_confirmed() || millis() - c.started >= c.window) return false;
  verify_round(c.started, c.target);
  return true;
}
inline std::string command_unconfirmed() { return unconfirmed_list(command().started, command().target); }
inline uint32_t command_age() { return millis() - command().started; }

// --- Mirror the bulbs into a light --------------------------------------------
// Quietly sets the light to what the bulbs show: on if any answering bulb is
// on, brightness/colour/white or scene of the first lit one. A cut relay
// (relay_on false) shows as off. Returns true if the light was changed.
inline bool sync_light(esphome::light::LightState *l, bool relay_on, uint32_t max_age_ms = 10000) {
  using esphome::light::ColorMode;
  Group g = group(max_age_ms);
  if (relay_on && !g.known) return false;
  bool want = relay_on && g.on;
  auto &v = l->remote_values;
  int cur_raw = scene_for_effect_name(l->get_effect_name().c_str());
  int cur_scene = is_scene(cur_raw) ? cur_raw : 0;
  bool differs = want != v.is_on();
  // "Warm white" (11) picked as an effect and the bulbs report scene 11:
  // keep the effect, only follow brightness.
  if (want && !differs && cur_raw == SCENE_WARM_WHITE && g.raw_scene == SCENE_WARM_WHITE) {
    differs = fabsf(std::max(0.1f, g.brightness) - std::max(0.1f, v.get_brightness())) > 0.02f;
    if (!differs) return false;
    auto call = l->make_call();
    call.set_brightness(g.brightness);
    perform_quietly(call);
    return true;
  }
  if (want && !differs) {
    float bri = std::max(0.1f, g.brightness);
    if (g.scene > 0) {
      differs = cur_scene != g.scene || fabsf(bri - std::max(0.1f, v.get_brightness())) > 0.02f;
    } else {
      bool is_rgb = v.get_color_mode() == ColorMode::RGB;
      float mireds = std::min(1000000.0f / 2200.0f, std::max(1000000.0f / 6500.0f, g.mireds));
      differs = cur_scene != 0 || fabsf(bri - std::max(0.1f, v.get_brightness())) > 0.02f || g.rgb != is_rgb ||
                (g.rgb ? (fabsf(g.r - v.get_red()) > 0.02f || fabsf(g.g - v.get_green()) > 0.02f ||
                          fabsf(g.b - v.get_blue()) > 0.02f)
                       : fabsf(mireds - v.get_color_temperature()) > 5.0f);
    }
  }
  if (!differs) return false;
  auto call = l->make_call();
  call.set_state(want);
  if (want) {
    call.set_brightness(g.brightness);
    const char *eff = g.scene > 0 ? effect_name_for_scene(g.scene) : nullptr;
    if (eff != nullptr) {
      call.set_effect(eff);  // (no transition: ESPHome rejects it with an effect)
    } else {
      call.set_effect("None");
      if (g.rgb) {
        call.set_color_mode(ColorMode::RGB);
        call.set_red(g.r);
        call.set_green(g.g);
        call.set_blue(g.b);
      } else {
        call.set_color_mode(ColorMode::COLOR_TEMPERATURE);
        call.set_color_temperature(g.mireds);
      }
    }
  }
  perform_quietly(call);
  return true;
}

// --- The component ------------------------------------------------------------
// Reads bulb answers in its loop, keeps the bulb list in sync with an optional
// text entity, and adds the WiZ scenes as effects to an optional light.
class WizUdp : public esphome::Component {
 public:
#ifdef USE_TEXT
  void set_bulbs_text(esphome::text::Text *t) { this->bulbs_text_ = t; }
#endif
  void add_scene(int scene, const char *name) {
    auto *e = new SceneEffect(name, scene);  // NOLINT: lives for the program
    scene_effects().push_back(e);
    this->effects_.push_back(e);
  }
  // Call after all add_scene() and before the light's setup(). The light keeps
  // a pointer to effects_, which never changes after this.
  void attach_scenes(esphome::light::LightState *l) { l->add_effects(this->effects_.data(), this->effects_.size()); }

  void setup() override {
#ifdef USE_TEXT
    if (this->bulbs_text_ != nullptr) {
      set_ips(this->bulbs_text_->state);
      this->bulbs_text_->add_on_state_callback([](const std::string &x) { set_ips(x); });
    }
#endif
  }
  void loop() override {
    if (!esphome::network::is_connected()) return;
    poll();
    ask_next();
  }
  float get_setup_priority() const override { return esphome::setup_priority::AFTER_WIFI; }

 protected:
  std::vector<esphome::light::LightEffect *> effects_;
#ifdef USE_TEXT
  esphome::text::Text *bulbs_text_{nullptr};
#endif
};

}  // namespace wiz_udp
