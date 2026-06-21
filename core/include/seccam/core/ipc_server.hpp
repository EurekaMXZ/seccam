#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace seccam::core {

class IpcServer {
 public:
  using FrameHandler = std::function<std::string(const std::string &)>;

  IpcServer(std::string socket_path, int listen_backlog, FrameHandler handler);
  int serve(const std::atomic<bool> &stop_requested);

 private:
  std::string socket_path_;
  int listen_backlog_ = 16;
  FrameHandler handler_;
};

}  // namespace seccam::core
