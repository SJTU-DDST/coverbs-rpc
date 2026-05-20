#include "coverbs_rpc/typed_client.hpp"

#include <cppcoro/sync_wait.hpp>
#include <rdmapp/device.h>
#include <rdmapp/pd.h>
#include <rdmapp/qp.h>

namespace coverbs_rpc {

typed_client::typed_client(cppcoro::io_service &io_service, scheduler_factory scheduler_factory,
                           std::string_view hostname, uint16_t port, RpcConfig config)
    : typed_client(io_service, std::move(scheduler_factory), hostname, port, config,
                   config.to_conn_config()) {}

typed_client::typed_client(cppcoro::io_service &io_service, scheduler_factory scheduler_factory,
                           std::string_view hostname, uint16_t port, RpcConfig config,
                           ConnConfig conn_config)
    : config_(config)
    , device_(conn_config.device_name == "auto"
                  ? std::make_shared<rdmapp::device>(rdmapp::auto_select)
                  : std::make_shared<rdmapp::device>(conn_config.device_name, conn_config.port_nr))
    , pd_(std::make_shared<rdmapp::pd>(device_))
    , io_service_(io_service)
    , connector_(io_service_, std::move(scheduler_factory), pd_, nullptr, conn_config) {
  qp_ = cppcoro::sync_wait(connector_.connect(hostname, port));
  client_ = std::make_unique<basic_client>(qp_, config_);
}

typed_client::typed_client(cppcoro::io_service &io_service,
                           std::shared_ptr<rdmapp::scheduler> scheduler, std::string_view hostname,
                           uint16_t port, RpcConfig config)
    : typed_client(
          io_service, [scheduler = std::move(scheduler)]() { return scheduler; }, hostname, port,
          config) {}

typed_client::typed_client(cppcoro::io_service &io_service,
                           std::shared_ptr<rdmapp::scheduler> scheduler, std::string_view hostname,
                           uint16_t port, RpcConfig config, ConnConfig conn_config)
    : typed_client(
          io_service, [scheduler = std::move(scheduler)]() { return scheduler; }, hostname, port,
          config, conn_config) {}

} // namespace coverbs_rpc
