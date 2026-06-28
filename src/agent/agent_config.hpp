#pragma once
#include <optional>
#include <string>

namespace draind::agent {

struct AgentConfig {
    std::string lock_cmd;         // shell command to lock the screen; empty = no lock
    std::string before_sleep_cmd; // shell command run just before the system suspends
};

// Returns nullopt if path does not exist.
// Throws std::runtime_error on parse failure.
std::optional<AgentConfig> load_agent_config(const std::string& path);

} // namespace draind::agent
