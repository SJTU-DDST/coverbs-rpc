#include "coverbs_rpc/conn/connector.hpp"
#include "coverbs_rpc/conn/transmission.hpp"
#include "coverbs_rpc/logger.hpp"

#include <cppcoro/io_service.hpp>
#include <cppcoro/net/ipv4_address.hpp>
#include <cppcoro/net/ipv4_endpoint.hpp>
#include <rdmapp/qp.h>

namespace coverbs_rpc {

qp_connector::qp_connector(cppcoro::io_service &io_service, std::shared_ptr<pd> pd,
                           std::shared_ptr<srq> srq, ConnConfig config)
    : pd_(pd)
    , srq_(srq)
    , io_service_(io_service)
    , config_(std::move(config)) {}

auto qp_connector::alloc_cq() noexcept -> std::shared_ptr<cq> {
  auto cq = std::make_shared<rdmapp::cq>(this->pd_->device_ptr(), config_.cq_size);
  pollers_.emplace_back(cq);
  return cq;
}

auto qp_connector::from_socket(cppcoro::net::socket &socket, std::span<std::byte const> userdata)
    -> cppcoro::task<std::shared_ptr<qp_t>> {
  auto cq1 = alloc_cq();
  auto cq2 = alloc_cq();
  auto qp_ptr = std::make_shared<qp_t>(this->pd_, cq1, cq2, srq_, config_.qp_config);
  qp_ptr->user_data().assign(userdata.begin(), userdata.end());
  co_await send_qp(*qp_ptr, socket);

  auto remote_qp = co_await recv_qp(socket);

  qp_ptr->rtr(remote_qp.header.lid, remote_qp.header.qp_num, remote_qp.header.sq_psn,
              remote_qp.header.gid);
  get_logger()->trace("qp: rtr");
  qp_ptr->user_data() = std::move(remote_qp.user_data);
  get_logger()->trace("qp: user_data: size={}", qp_ptr->user_data().size());
  qp_ptr->rts();
  get_logger()->trace("qp: rts");
  co_return qp_ptr;
}

auto qp_connector::connect(std::string_view hostname, uint16_t port,
                           std::span<const std::byte> userdata)
    -> cppcoro::task<std::shared_ptr<qp_t>> {
  auto addr = cppcoro::net::ipv4_address::from_string(hostname);
  if (!addr) {
    throw std::runtime_error("failed to parse hostname as ipv4 address");
  }

  cppcoro::net::socket socket = cppcoro::net::socket::create_tcpv4(io_service_);
  co_await socket.connect(cppcoro::net::ipv4_endpoint(*addr, port));

  get_logger()->info("connector: tcp connected to: {}:{}", hostname, port);
  auto qp = co_await from_socket(socket, userdata);
  get_logger()->info("connector: created qp from tcp connection");
  co_return qp;
}

auto qp_connector::connect(std::string_view hostname, uint16_t port, qp_handshake const &handshake)
    -> cppcoro::task<std::vector<std::shared_ptr<qp_t>>> {
  auto addr = cppcoro::net::ipv4_address::from_string(hostname);
  if (!addr) {
    throw std::runtime_error("failed to parse hostname as ipv4 address");
  }

  cppcoro::net::socket socket = cppcoro::net::socket::create_tcpv4(io_service_);
  co_await socket.connect(cppcoro::net::ipv4_endpoint(*addr, port));

  get_logger()->info("connector: tcp connected to: {}:{}", hostname, port);

  co_await send_handshake(handshake, socket);
  get_logger()->debug("connector: sent handshake");

  std::vector<std::shared_ptr<qp_t>> result;
  result.reserve(handshake.nr_qp);
  for (unsigned int i = 0; i < handshake.nr_qp; i++) {
    result.emplace_back(co_await from_socket(socket, {}));
  }
  get_logger()->info("connector: connect with nr_qp={} sid={}", handshake.nr_qp, handshake.sid);
  co_return result;
}

} // namespace coverbs_rpc
