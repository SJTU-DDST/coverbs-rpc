#include "coverbs_rpc/detail/logger.hpp"
#include "coverbs_rpc/runtime.hpp"
#include "coverbs_rpc/typed_server.hpp"

#include <cppcoro/io_service.hpp>
#include <cppcoro/sync_wait.hpp>
#include <numeric>
#include <thread>

#include "typed_rpc_test.hpp"

using namespace coverbs_rpc;
using namespace coverbs_rpc::test;
using coverbs_rpc::detail::get_logger;

namespace coverbs_rpc::test {
template <std::size_t I>
auto RequestHandler<I>::handle(const TypedRequest &req) -> TypedResponse {
  TypedResponse resp;
  resp.id = req.id;
  resp.status = "Success from handler " + std::to_string(I);
  resp.result = req.data;
  for (auto &v : resp.result) {
    v += I;
  }
  if (!req.data.empty()) {
    resp.average = std::accumulate(req.data.begin(), req.data.end(), 0.0) /
                   static_cast<double>(req.data.size());
  } else {
    resp.average = 0.0;
  }
  resp.success = (req.type == static_cast<uint8_t>(I));
  return resp;
}
} // namespace coverbs_rpc::test

template <std::size_t... Is>
void register_handlers(typed_server &server, std::index_sequence<Is...>) {
  (server.register_handler<RequestHandler<Is>::handle>(), ...);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    get_logger()->info("Usage: {} [port]", argv[0]);
    return 1;
  }

  uint16_t port = static_cast<uint16_t>(std::stoi(argv[1]));

  coverbs_rpc::runtime runtime;

  typed_server server(runtime.io_service, runtime.scheduler_factory(), port, kServerRpcConfig);
  register_handlers(server, std::make_index_sequence<kNumHandlers>{});

  get_logger()->info("Typed RPC Mux Server listening on port {}", port);

  try {
    cppcoro::sync_wait(server.run());
  } catch (const std::exception &e) {
    get_logger()->error("Server exception: {}", e.what());
    return 1;
  }

  return 0;
}
