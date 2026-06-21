#include <atomic>
#include <csignal>
#include <exception>
#include <iostream>

#include "seccam/core/command_dispatcher.hpp"
#include "seccam/core/core_service.hpp"
#include "seccam/core/daemon_config.hpp"
#include "seccam/core/ipc_server.hpp"
#include "seccam/core/runtime_state.hpp"

namespace {

std::atomic<bool> g_stop_requested{false};

void signal_handler(int signal_number) {
  (void)signal_number;
  g_stop_requested.store(true);
}

}  // namespace

int main(int argc, char **argv) {
  try {
    const seccam::core::DaemonConfig config =
        seccam::core::load_daemon_config_or_throw(argc, argv);

    struct sigaction action = {};
    action.sa_handler = signal_handler;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    seccam::core::RuntimeState runtime_state(config.runtime);
    runtime_state.apply_config(config.runtime);

    seccam::core::DaemonConfig mutable_config = config;
    seccam::core::CoreService core_service(runtime_state);
    core_service.start(config.runtime);

    seccam::core::CommandDispatcher dispatcher(
        runtime_state, mutable_config,
        [&core_service](const seccam::core::RuntimeConfig &runtime_config) {
          core_service.apply_runtime_config(runtime_config);
        });
    seccam::core::IpcServer server(
        config.socket_path, config.listen_backlog,
        [&dispatcher](const std::string &payload) { return dispatcher.dispatch(payload); });

    const int exit_code = server.serve(g_stop_requested);
    core_service.stop();
    return exit_code;
  } catch (const std::exception &error) {
    std::cerr << "seccam-core failed: " << error.what() << '\n';
    return 1;
  }
}
