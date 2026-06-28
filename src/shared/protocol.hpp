#pragma once
// Encode/decode helpers for the daemon↔agent and daemon↔ctl socket protocol.
// All messages are newline-terminated JSON objects.
// See docs/protocol.md for the full specification.

#include "json.hpp"
#include <string>

namespace draind::proto {

constexpr const char* SOCKET_PATH = "/run/draind/draind.sock";

// ── Message type strings ──────────────────────────────────────────────────────
constexpr const char* T_HELLO      = "hello";
constexpr const char* T_CONFIG     = "config";
constexpr const char* T_IDLE_DIM   = "idle_dim";
constexpr const char* T_IDLE_SLEEP = "idle_sleep";
constexpr const char* T_ACTIVE     = "active";
constexpr const char* T_INHIBIT    = "inhibit";
constexpr const char* T_UNINHIBIT  = "uninhibit";
constexpr const char* T_ACK        = "ack";
constexpr const char* T_LOCK       = "lock";
constexpr const char* T_PRE_SLEEP  = "pre_sleep";
constexpr const char* T_CTL        = "ctl";
constexpr const char* T_CTL_REPLY  = "ctl_reply";

// ── Decode ────────────────────────────────────────────────────────────────────

inline json::Value decode(const std::string& line) { return json::parse(line); }

inline std::string msg_type(const json::Value& v) { return v.str("type"); }

// ── Agent → Daemon ────────────────────────────────────────────────────────────

inline std::string encode_hello(const std::string& session_id, uint32_t uid) {
    json::Value v;
    v["type"]       = T_HELLO;
    v["session_id"] = session_id;
    v["uid"]        = (int64_t)uid;
    return json::dump(v);
}

inline std::string encode_idle_dim() {
    json::Value v;
    v["type"] = T_IDLE_DIM;
    return json::dump(v);
}

inline std::string encode_idle_sleep() {
    json::Value v;
    v["type"] = T_IDLE_SLEEP;
    return json::dump(v);
}

inline std::string encode_active() {
    json::Value v;
    v["type"] = T_ACTIVE;
    return json::dump(v);
}

inline std::string encode_inhibit(const std::string& reason) {
    json::Value v;
    v["type"]   = T_INHIBIT;
    v["reason"] = reason;
    return json::dump(v);
}

inline std::string encode_uninhibit(const std::string& reason) {
    json::Value v;
    v["type"]   = T_UNINHIBIT;
    v["reason"] = reason;
    return json::dump(v);
}

// ── Daemon → Agent ────────────────────────────────────────────────────────────

inline std::string encode_config(int dim_timeout, int sleep_timeout) {
    json::Value v;
    v["type"]          = T_CONFIG;
    v["dim_timeout"]   = dim_timeout;
    v["sleep_timeout"] = sleep_timeout;
    return json::dump(v);
}

inline std::string encode_ack() {
    json::Value v;
    v["type"] = T_ACK;
    return json::dump(v);
}

inline std::string encode_lock() {
    json::Value v;
    v["type"] = T_LOCK;
    return json::dump(v);
}

inline std::string encode_pre_sleep() {
    json::Value v;
    v["type"] = T_PRE_SLEEP;
    return json::dump(v);
}

// ── Ctl → Daemon ──────────────────────────────────────────────────────────────

inline std::string encode_ctl(const std::string& cmd, const std::string& profile = "") {
    json::Value v;
    v["type"] = T_CTL;
    v["cmd"]  = cmd;
    if (!profile.empty())
        v["profile"] = profile;
    return json::dump(v);
}

// ── Daemon → Ctl ──────────────────────────────────────────────────────────────

inline std::string encode_ctl_reply(json::Value fields) {
    fields["type"] = T_CTL_REPLY;
    return json::dump(fields);
}

} // namespace draind::proto
