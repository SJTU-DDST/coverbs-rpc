#include "coverbs_rpc/logger.hpp"
#include "coverbs_rpc/typed_server.hpp"

#include <cppcoro/io_service.hpp>
#include <cppcoro/sync_wait.hpp>
#include <thread>

#include "typed_rpc_benchmark.hpp"

using namespace coverbs_rpc;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    get_logger()->info("Usage: {} [port]", argv[0]);
    return 1;
  }

  uint16_t port = static_cast<uint16_t>(std::stoi(argv[1]));

  cppcoro::io_service io_service;
  auto looper = std::jthread([&io_service]() { io_service.process_events(); });

  TypedRpcConfig config;
  config.max_inflight = 1024;
  config.max_req_payload = 8192;
  config.max_resp_payload = 8192;

  typed_server server(io_service, port, config, 4);
  server.register_handler<benchmark::BenchmarkHandler<0>::handle>();
  server.register_handler<benchmark::BenchmarkHandler<1>::handle>();

  get_logger()->info("Typed RPC Benchmark Server listening on port {}", port);

  try {
    cppcoro::sync_wait(server.run());
  } catch (const std::exception &e) {
    get_logger()->error("Server exception: {}", e.what());
    return 1;
  }

  return 0;
}
