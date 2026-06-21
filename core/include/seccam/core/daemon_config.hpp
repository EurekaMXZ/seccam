#pragma once

#include <string>

#include "seccam/core/types.hpp"

namespace seccam::core {

struct DaemonConfig {
  std::string config_path = "/mnt/data/seccam/seccam-core.ini";
  std::string socket_path = "/var/run/seccam-core.sock";
  int listen_backlog = 16;
  RuntimeConfig runtime = default_runtime_config();
};

DaemonConfig load_daemon_config_or_throw(int argc, char **argv);
void persist_daemon_config_or_throw(const DaemonConfig &config);
void print_usage(const char *argv0);

}  // namespace seccam::core
