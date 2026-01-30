#include "coverbs_rpc/basic_server.hpp"
#include "coverbs_rpc/common.hpp"
#include "coverbs_rpc/conn/acceptor.hpp"
#include "coverbs_rpc/logger.hpp"

#include <algorithm>
#include <cppcoro/async_scope.hpp>
#include <cppcoro/io_service.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <memory>
#include <thread>

#include "basic_rpc_test.hpp"

using namespace coverbs_rpc;
using namespace coverbs_rpc::test;

auto handle_rpc(std::shared_ptr<rdmapp::qp> qp) -> cppcoro::task<void> {
  basic_mux mux;
  mux.register_handler(
      kTestFnId, [](std::span<std::byte> req, std::span<std::byte> resp) -> std::size_t {
        if (req.size() != kRequestSize) {
          get_logger()->error("Server: unexpected request size: {}", req.size());
          return 0;
        }
        for (auto b : req) {
          if (b != kRequestByte) {
            get_logger()->error("Server: unexpected request data");
            return 0;
          }
        }

        if (resp.size() < kResponseSize) {
          get_logger()->error("Server: response buffer too small: {}", resp.size());
          return 0;
        }
        std::fill_n(resp.data(), kResponseSize, kResponseByte);
        return kResponseSize;
      });

  basic_server server(qp, mux, kServerRpcConfig);
  co_await server.run();
}

auto server_loop(qp_acceptor &acceptor) -> cppcoro::task<void> {
  cppcoro::async_scope scope;
  while (true) {
    auto qp = co_await acceptor.accept();
    get_logger()->info("Server: accepted connection");
    scope.spawn(handle_rpc(qp));
  }
  co_await scope.join();
}

auto main(int argc, char *argv[]) -> int {
  if (argc != 2) {
    get_logger()->info("Usage: {} [port]", argv[0]);
    return 1;
  }

  auto device = std::make_shared<rdmapp::device>(0, 1);
  auto pd = std::make_shared<rdmapp::pd>(device);
  cppcoro::io_service io_service;
  auto looper = std::jthread([&io_service]() { io_service.process_events(); });

  qp_acceptor acceptor(
      io_service, std::stoi(argv[1]), pd, nullptr,
      ConnConfig{.qp_config{.max_send_wr = kServerMaxInFlight, .max_recv_wr = kServerMaxInFlight}});
  get_logger()->info("Server: listening on port {}", argv[1]);
  cppcoro::sync_wait(server_loop(acceptor));

  return 0;
}
