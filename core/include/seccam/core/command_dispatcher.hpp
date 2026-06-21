#pragma once

#include <functional>
#include <mutex>
#include <string>

#include "seccam/core/daemon_config.hpp"
#include "seccam/core/recording_catalog.hpp"
#include "seccam/core/runtime_state.hpp"

namespace seccam::core {

class CommandDispatcher {
 public:
  using ConfigApplyHandler = std::function<void(const RuntimeConfig &)>;

  CommandDispatcher(RuntimeState &runtime_state, DaemonConfig &daemon_config,
                    ConfigApplyHandler config_apply_handler);

  std::string dispatch(const std::string &payload);

 private:
  RuntimeState &runtime_state_;
  DaemonConfig &daemon_config_;
  ConfigApplyHandler config_apply_handler_;
  RecordingCatalog recording_catalog_;
  std::mutex config_mutex_;
};

}  // namespace seccam::core
