#include "seccam/core/core_service.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "seccam/core/runtime_state.hpp"

namespace seccam::core {

std::unique_ptr<CoreService::Backend> make_core_backend(RuntimeState &runtime_state);

CoreService::CoreService(RuntimeState &runtime_state) : runtime_state_(runtime_state) {}

CoreService::~CoreService() {
  try {
    stop();
  } catch (...) {
  }
}

void CoreService::start(const RuntimeConfig &config) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_) {
    return;
  }

  backend_ = make_core_backend(runtime_state_);
  active_config_ = config;
  runtime_state_.apply_config(config);
  backend_->start(active_config_);
  started_ = true;
}

void CoreService::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!started_ || !backend_) {
    return;
  }

  backend_->stop();
  runtime_state_.mark_components_ready(false, false);
  runtime_state_.set_pipeline_counters(0, 0);
  started_ = false;
}

void CoreService::apply_runtime_config(const RuntimeConfig &config) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!started_) {
    active_config_ = config;
    runtime_state_.apply_config(active_config_);
    return;
  }

  RuntimeConfig previous_config = active_config_;
  backend_->stop();
  runtime_state_.mark_components_ready(false, false);
  runtime_state_.set_pipeline_counters(0, 0);

  try {
    backend_ = make_core_backend(runtime_state_);
    active_config_ = config;
    runtime_state_.apply_config(active_config_);
    backend_->start(active_config_);
  } catch (...) {
    try {
      backend_ = make_core_backend(runtime_state_);
      active_config_ = previous_config;
      runtime_state_.apply_config(active_config_);
      backend_->start(active_config_);
    } catch (...) {
      started_ = false;
    }
    throw;
  }
}

}  // namespace seccam::core
