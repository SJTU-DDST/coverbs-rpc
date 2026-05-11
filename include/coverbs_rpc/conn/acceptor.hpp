#pragma once

#include "coverbs_rpc/common.hpp"
#include "coverbs_rpc/conn/transmission.hpp"
#include "coverbs_rpc/detail/cq_provider.hpp"

#include <cppcoro/net/socket.hpp>
#include <cppcoro/task.hpp>
#include <cstdint>
#include <memory>
#include <rdmapp/cq.h>
#include <rdmapp/device.h>
#include <rdmapp/pd.h>
#include <rdmapp/qp.h>
#include <rdmapp/scheduler.h>

namespace cppcoro {
class io_service;
}

namespace coverbs_rpc {

struct qp_acceptor {
  using pd = rdmapp::pd;
  using cq = rdmapp::cq;
  using srq = rdmapp::srq;
  using qp_t = rdmapp::basic_qp;

  qp_acceptor(cppcoro::io_service &io_service, scheduler_factory scheduler_factory, uint16_t port,
              std::shared_ptr<pd> pd, std::shared_ptr<srq> srq = nullptr, ConnConfig config = {});

  qp_acceptor(cppcoro::io_service &io_service, std::shared_ptr<rdmapp::scheduler> scheduler,
              uint16_t port, std::shared_ptr<pd> pd, std::shared_ptr<srq> srq = nullptr,
              ConnConfig config = {});

  auto accept() -> cppcoro::task<std::shared_ptr<qp_t>>;

  auto accept_multiple(qp_handshake &handshake)
      -> cppcoro::task<std::vector<std::shared_ptr<qp_t>>>;

  auto close() noexcept -> void;

  ~qp_acceptor() = default;

private:
  auto accept_qp(cppcoro::net::socket &socket, std::shared_ptr<rdmapp::cq> send_cq,
                 std::shared_ptr<rdmapp::cq> recv_cq) -> cppcoro::task<std::shared_ptr<qp_t>>;

  auto make_scheduler() -> std::shared_ptr<rdmapp::scheduler>;
  auto alloc_cq(std::shared_ptr<rdmapp::scheduler> scheduler) -> std::shared_ptr<rdmapp::cq>;

  cppcoro::net::socket acceptor_socket_;
  std::shared_ptr<pd> pd_;
  std::shared_ptr<srq> srq_;
  scheduler_factory scheduler_factory_;
  detail::cq_provider cq_provider_;
  uint16_t const port_;
  cppcoro::io_service &io_service_;
  ConnConfig const config_;
};

} // namespace coverbs_rpc
