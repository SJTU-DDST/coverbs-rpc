#include "coverbs_rpc/typed_client.hpp"

#include <cppcoro/sync_wait.hpp>
#include <rdmapp/device.h>
#include <rdmapp/pd.h>
#include <rdmapp/qp.h>

namespace coverbs_rpc {

typed_client::typed_client(cppcoro::io_service &io_service,
                           std::shared_ptr<rdmapp::scheduler> scheduler,
                           std::string_view hostname, uint16_t port, TypedRpcConfig config)
    : config_(config)
    , device_(std::make_shared<rdmapp::device>(config.device_nr, config.port_nr))
    , pd_(std::make_shared<rdmapp::pd>(device_))
    , io_service_(io_service)
    , connector_(io_service_, std::move(scheduler), pd_, nullptr, config.to_conn_config()) {
  qp_ = cppcoro::sync_wait(connector_.connect(hostname, port));
  client_ = std::make_unique<basic_client>(qp_, config_);
}

} // namespace coverbs_rpc
