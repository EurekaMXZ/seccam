#pragma once

#include <memory>
#include <mutex>

#include "seccam/core/types.hpp"

namespace seccam::core {

class RuntimeState;

class CoreService {
 public:
  class Backend {
   public:
    virtual ~Backend() = default;

    virtual void start(const RuntimeConfig &config) = 0;
    virtual void stop() = 0;
  };

  explicit CoreService(RuntimeState &runtime_state);
  ~CoreService();

  CoreService(const CoreService &) = delete;
  CoreService &operator=(const CoreService &) = delete;

  void start(const RuntimeConfig &config);
  void stop();
  void apply_runtime_config(const RuntimeConfig &config);

 private:
  RuntimeState &runtime_state_;
  std::unique_ptr<Backend> backend_;
  RuntimeConfig active_config_;
  bool started_ = false;
  std::mutex mutex_;
};

}  // namespace seccam::core
