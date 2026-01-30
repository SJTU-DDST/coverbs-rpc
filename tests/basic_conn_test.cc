#include "coverbs_rpc/conn/acceptor.hpp"
#include "coverbs_rpc/conn/connector.hpp"
#include "coverbs_rpc/logger.hpp"

#include <algorithm>
#include <cassert>
#include <cppcoro/async_scope.hpp>
#include <cppcoro/io_service.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <rdmapp/completion_token.h>
#include <spdlog/fmt/ranges.h>
#include <string>
#include <thread>

constexpr std::size_t kMsgSize = 4096;

cppcoro::task<void> handle_qp(std::shared_ptr<rdmapp::qp> qp) {
  std::byte buffer[kMsgSize];
  std::fill_n(buffer, sizeof(buffer), std::byte(0xAA));
  auto local_mr = qp->pd_ptr()->reg_mr(buffer, kMsgSize);
  co_await qp->send(local_mr, rdmapp::use_native_awaitable);
  coverbs_rpc::get_logger()->info("sent 4K");
  auto [nbytes, _] = co_await qp->recv(local_mr, rdmapp::use_native_awaitable);
  assert(nbytes == kMsgSize);
  coverbs_rpc::get_logger()->info("received 4K: nbytes={} head={::X}", nbytes,
                                  std::span(buffer, 10));
  co_return;
}

cppcoro::task<void> server(coverbs_rpc::qp_acceptor &acceptor) {
  cppcoro::async_scope scope;
  while (true) {
    auto qp = co_await acceptor.accept();
    scope.spawn(handle_qp(qp));
  }
  co_await scope.join();
  co_return;
}

cppcoro::task<void> client(coverbs_rpc::qp_connector &connector, std::string hostname,
                           uint16_t port) {
  auto qp = co_await connector.connect(hostname, port);
  std::byte buffer[kMsgSize];
  std::fill_n(buffer, sizeof(buffer), std::byte(0xAA));
  auto local_mr = qp->pd_ptr()->reg_mr(buffer, kMsgSize);
  auto [nbytes, _] = co_await qp->recv(local_mr, rdmapp::use_native_awaitable);
  assert(nbytes == kMsgSize);
  coverbs_rpc::get_logger()->info("recv 4K: nbytes={} head={::X}", nbytes, std::span(buffer, 10));
  co_await qp->send(local_mr, rdmapp::use_native_awaitable);
  coverbs_rpc::get_logger()->info("sent 4K");
  co_return;
}

int main(int argc, char *argv[]) {
  auto device = std::make_shared<rdmapp::device>(0, 1);
  auto pd = std::make_shared<rdmapp::pd>(device);

  cppcoro::io_service io_service;
  auto looper = std::jthread([&io_service]() { io_service.process_events(); });

  if (argc == 2) {
    coverbs_rpc::qp_acceptor acceptor(io_service, std::stoi(argv[1]), pd);
    cppcoro::sync_wait(server(acceptor));
  } else if (argc == 3) {
    coverbs_rpc::qp_connector connector(io_service, pd);
    cppcoro::sync_wait(client(connector, argv[1], std::stoi(argv[2])));
  } else {
    coverbs_rpc::get_logger()->info(
        "Usage: {} [port] for server and {} [server_ip] [port] for client", argv[0], argv[0]);
  }

  io_service.stop();
  return 0;
}
