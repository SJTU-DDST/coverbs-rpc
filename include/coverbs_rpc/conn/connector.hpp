#pragma once

#include "coverbs_rpc/common.hpp"
#include "coverbs_rpc/conn/transmission.hpp"
#include "coverbs_rpc/detail/cq_provider.hpp"

#include <cppcoro/net/socket.hpp>
#include <cppcoro/task.hpp>
#include <cstdint>
#include <memory>
#include <rdmapp/cq.h>
#include <rdmapp/pd.h>
#include <rdmapp/qp.h>
#include <rdmapp/scheduler.h>

namespace cppcoro {
class io_service;
}

namespace coverbs_rpc {
struct qp_connector {
  using pd = rdmapp::pd;
  using cq = rdmapp::cq;
  using srq = rdmapp::srq;
  using qp_t = rdmapp::basic_qp;

  qp_connector(cppcoro::io_service &io_service, scheduler_factory scheduler_factory,
               std::shared_ptr<pd> pd, std::shared_ptr<srq> srq = nullptr, ConnConfig config = {});

  qp_connector(cppcoro::io_service &io_service, std::shared_ptr<rdmapp::scheduler> scheduler,
               std::shared_ptr<pd> pd, std::shared_ptr<srq> srq = nullptr, ConnConfig config = {});

  auto connect(std::string_view hostname, uint16_t port, std::span<const std::byte> userdata = {})
      -> cppcoro::task<std::shared_ptr<qp_t>>;

  auto connect(std::string_view hostname, uint16_t port, qp_handshake const &handshake)
      -> cppcoro::task<std::vector<std::shared_ptr<qp_t>>>;

private:
  auto from_socket(cppcoro::net::socket &socket, std::span<std::byte const> userdata)
      -> cppcoro::task<std::shared_ptr<qp_t>>;

  auto make_scheduler() -> std::shared_ptr<rdmapp::scheduler>;
  auto alloc_cq(std::shared_ptr<rdmapp::scheduler> scheduler) -> std::shared_ptr<cq>;

  std::shared_ptr<pd> pd_;
  std::shared_ptr<srq> srq_;
  scheduler_factory scheduler_factory_;
  detail::cq_provider cq_provider_;
  cppcoro::io_service &io_service_;
  ConnConfig const config_;
};

} // namespace coverbs_rpc
