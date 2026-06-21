#include "seccam/core/ipc_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace seccam::core {
namespace {

bool read_full(int fd, void *buffer, std::size_t size) {
  std::size_t offset = 0;
  while (offset < size) {
    const auto read_count =
        ::read(fd, static_cast<char *>(buffer) + offset, size - offset);
    if (read_count == 0) {
      return false;
    }
    if (read_count < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("read failed: ") + std::strerror(errno));
    }
    offset += static_cast<std::size_t>(read_count);
  }
  return true;
}

void write_full(int fd, const void *buffer, std::size_t size) {
  std::size_t offset = 0;
  while (offset < size) {
    const auto write_count =
        ::write(fd, static_cast<const char *>(buffer) + offset, size - offset);
    if (write_count < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
    }
    offset += static_cast<std::size_t>(write_count);
  }
}

bool read_frame(int fd, std::string &payload) {
  std::uint32_t network_size = 0;
  if (!read_full(fd, &network_size, sizeof(network_size))) {
    return false;
  }

  const std::uint32_t payload_size = ntohl(network_size);
  payload.assign(payload_size, '\0');
  if (payload_size == 0) {
    return true;
  }

  if (!read_full(fd, payload.data(), payload_size)) {
    return false;
  }
  return true;
}

void write_frame(int fd, const std::string &payload) {
  const std::uint32_t payload_size = static_cast<std::uint32_t>(payload.size());
  const std::uint32_t network_size = htonl(payload_size);
  write_full(fd, &network_size, sizeof(network_size));
  if (!payload.empty()) {
    write_full(fd, payload.data(), payload.size());
  }
}

void handle_client(int client_fd, const IpcServer::FrameHandler &handler) {
  try {
    std::string payload;
    while (read_frame(client_fd, payload)) {
      const std::string response = handler(payload);
      write_frame(client_fd, response);
      payload.clear();
    }
  } catch (const std::exception &error) {
    std::cerr << "seccam-core client session aborted: " << error.what() << '\n';
  }

  ::close(client_fd);
}

}  // namespace

IpcServer::IpcServer(std::string socket_path, int listen_backlog, FrameHandler handler)
    : socket_path_(std::move(socket_path)),
      listen_backlog_(listen_backlog),
      handler_(std::move(handler)) {}

int IpcServer::serve(const std::atomic<bool> &stop_requested) {
  if (socket_path_.empty()) {
    throw std::runtime_error("socket path is empty");
  }

  const fs::path socket_file(socket_path_);
  if (!socket_file.parent_path().empty()) {
    fs::create_directories(socket_file.parent_path());
  }
  ::unlink(socket_path_.c_str());

  const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    throw std::runtime_error(std::string("socket creation failed: ") + std::strerror(errno));
  }

  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path_.c_str());

  if (::bind(listen_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
    const std::string message = std::string("bind failed: ") + std::strerror(errno);
    ::close(listen_fd);
    ::unlink(socket_path_.c_str());
    throw std::runtime_error(message);
  }

  if (::chmod(socket_path_.c_str(), 0660) < 0) {
    std::cerr << "seccam-core failed to chmod socket: " << std::strerror(errno) << '\n';
  }

  if (::listen(listen_fd, listen_backlog_) < 0) {
    const std::string message = std::string("listen failed: ") + std::strerror(errno);
    ::close(listen_fd);
    ::unlink(socket_path_.c_str());
    throw std::runtime_error(message);
  }

  std::cerr << "seccam-core listening on " << socket_path_ << '\n';

  while (!stop_requested.load()) {
    pollfd poll_fd = {};
    poll_fd.fd = listen_fd;
    poll_fd.events = POLLIN;

    const int poll_result = ::poll(&poll_fd, 1, 1000);
    if (poll_result < 0) {
      if (errno == EINTR) {
        continue;
      }
      const std::string message = std::string("poll failed: ") + std::strerror(errno);
      ::close(listen_fd);
      ::unlink(socket_path_.c_str());
      throw std::runtime_error(message);
    }
    if (poll_result == 0) {
      continue;
    }

    const int client_fd = ::accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "seccam-core accept failed: " << std::strerror(errno) << '\n';
      continue;
    }

    std::thread(handle_client, client_fd, handler_).detach();
  }

  ::close(listen_fd);
  ::unlink(socket_path_.c_str());
  return 0;
}

}  // namespace seccam::core
