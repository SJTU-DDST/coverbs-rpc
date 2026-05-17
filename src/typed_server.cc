#include "coverbs_rpc/typed_server.hpp"
#include "coverbs_rpc/basic_server.hpp"
#include "coverbs_rpc/detail/logger.hpp"
#include <cppcoro/async_scope.hpp>

namespace coverbs_rpc {

using detail::get_logger;

typed_server::typed_server(cppcoro::io_service &io_service, scheduler_factory scheduler_factory,
                           uint16_t port, RpcConfig config)
    : typed_server(io_service, std::move(scheduler_factory), port, config, config.to_conn_config()) {}

typed_server::typed_server(cppcoro::io_service &io_service, scheduler_factory scheduler_factory,
                           uint16_t port, RpcConfig config, ConnConfig conn_config)
    : config_(config)
    , device_(std::make_shared<rdmapp::device>(conn_config.device_nr, conn_config.port_nr))
    , pd_(std::make_shared<rdmapp::pd>(device_))
    , io_service_(io_service)
    , acceptor_(io_service_, std::move(scheduler_factory), port, pd_, nullptr, conn_config)
    , mux_() {}

typed_server::typed_server(cppcoro::io_service &io_service,
                           std::shared_ptr<rdmapp::scheduler> scheduler, uint16_t port,
                           RpcConfig config)
    : typed_server(
          io_service, [scheduler = std::move(scheduler)]() { return scheduler; }, port, config) {}

typed_server::typed_server(cppcoro::io_service &io_service,
                           std::shared_ptr<rdmapp::scheduler> scheduler, uint16_t port,
                           RpcConfig config, ConnConfig conn_config)
    : typed_server(
          io_service, [scheduler = std::move(scheduler)]() { return scheduler; }, port, config,
          conn_config) {}

auto typed_server::run() -> cppcoro::task<void> {
  cppcoro::async_scope scope;
  while (true) {
    auto qp = co_await acceptor_.accept();
    get_logger()->debug("typed_server: accepted connection");
    scope.spawn(handle_connection(std::move(qp)));
  }
  co_await scope.join();
}

typed_server::~typed_server() { acceptor_.close(); }

auto typed_server::handle_connection(std::shared_ptr<rdmapp::qp> qp) -> cppcoro::task<void> {
  basic_server server(qp, mux_, config_);
  try {
    co_await server.run();
  } catch (const std::exception &e) {
    get_logger()->warn("typed_server: connection closed with error: {}", e.what());
  }
}

} // namespace coverbs_rpc
