#include "coverbs_rpc/conn/acceptor.hpp"
#include "coverbs_rpc/conn/transmission.hpp"
#include "coverbs_rpc/detail/logger.hpp"

#include <cppcoro/io_service.hpp>
#include <cppcoro/net/ipv4_address.hpp>
#include <cppcoro/net/ipv4_endpoint.hpp>
#include <cppcoro/net/socket.hpp>
#include <rdmapp/qp.h>
#include <stdexcept>

namespace coverbs_rpc {

using detail::get_logger;

static auto config_socket(cppcoro::net::socket &socket) {
  int fd = socket.native_handle();
  int opt = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    throw std::runtime_error("confif socket failed");
  }
}

static auto require_scheduler(std::shared_ptr<rdmapp::scheduler> scheduler)
    -> std::shared_ptr<rdmapp::scheduler> {
  if (!scheduler) {
    throw std::invalid_argument("qp_acceptor: scheduler must not be null");
  }
  return scheduler;
}

static auto require_scheduler_factory(scheduler_factory factory) -> scheduler_factory {
  if (!factory) {
    throw std::invalid_argument("qp_acceptor: scheduler factory must not be empty");
  }
  return factory;
}

static auto fixed_scheduler_factory(std::shared_ptr<rdmapp::scheduler> scheduler)
    -> scheduler_factory {
  scheduler = require_scheduler(std::move(scheduler));
  return [scheduler]() { return scheduler; };
}

qp_acceptor::qp_acceptor(cppcoro::io_service &io_service, scheduler_factory scheduler_factory,
                         uint16_t port, std::shared_ptr<pd> pd, std::shared_ptr<srq> srq,
                         ConnConfig config)
    : acceptor_socket_(cppcoro::net::socket::create_tcpv4(io_service))
    , pd_(pd)
    , srq_(srq)
    , scheduler_factory_(require_scheduler_factory(std::move(scheduler_factory)))
    , cq_provider_(pd_->device_ptr())
    , port_(port)
    , io_service_(io_service)
    , config_(std::move(config)) {
  try {
    config_socket(acceptor_socket_);
    acceptor_socket_.bind(cppcoro::net::ipv4_endpoint(cppcoro::net::ipv4_address(), port));
    acceptor_socket_.listen();
    get_logger()->info("qp_acceptor: bind and listen: port={}", port_);
  } catch (std::exception &e) {
    get_logger()->error("qp_acceptor: failed to bind and listen: error={}", e.what());
    std::terminate();
  }
}

qp_acceptor::qp_acceptor(cppcoro::io_service &io_service,
                         std::shared_ptr<rdmapp::scheduler> scheduler, uint16_t port,
                         std::shared_ptr<pd> pd, std::shared_ptr<srq> srq, ConnConfig config)
    : qp_acceptor(io_service, fixed_scheduler_factory(std::move(scheduler)), port, std::move(pd),
                  std::move(srq), std::move(config)) {}

auto qp_acceptor::accept_qp(cppcoro::net::socket &socket, std::shared_ptr<rdmapp::cq> send_cq,
                            std::shared_ptr<rdmapp::cq> recv_cq)
    -> cppcoro::task<std::shared_ptr<qp_t>> {
  auto remote_qp = co_await recv_qp(socket);
  auto local_qp =
      std::make_shared<qp_t>(remote_qp.header.lid, remote_qp.header.qp_num, remote_qp.header.sq_psn,
                             remote_qp.header.gid, pd_, recv_cq, send_cq, srq_, config_.qp_config);
  local_qp->user_data() = std::move(remote_qp.user_data);
  co_await send_qp(*local_qp, socket);
  co_return local_qp;
}

auto qp_acceptor::accept() -> cppcoro::task<std::shared_ptr<qp_t>> {
  cppcoro::net::socket socket = cppcoro::net::socket::create_tcpv4(io_service_);
  co_await acceptor_socket_.accept(socket);
  get_logger()->info("qp_acceptor: accepted connection from {}", socket.remote_endpoint().to_string());
  auto scheduler = make_scheduler();
  co_return co_await accept_qp(socket, alloc_cq(scheduler), alloc_cq(scheduler));
}

auto qp_acceptor::accept_multiple(qp_handshake &handshake)
    -> cppcoro::task<std::vector<std::shared_ptr<qp_t>>> {
  cppcoro::net::socket socket = cppcoro::net::socket::create_tcpv4(io_service_);
  co_await acceptor_socket_.accept(socket);
  get_logger()->info("qp_acceptor: accept client handshake: remote={}",
                     socket.remote_endpoint().to_string());

  handshake = co_await recv_handshake(socket);
  get_logger()->info("qp_acceptor: handshake nr_qp={} sid={}", handshake.nr_qp, handshake.sid);

  std::vector<std::shared_ptr<qp_t>> result;
  result.reserve(handshake.nr_qp);
  for (unsigned int i = 0; i < handshake.nr_qp; i++) {
    auto scheduler = make_scheduler();
    auto cq = alloc_cq(scheduler);
    result.emplace_back(co_await accept_qp(socket, cq, cq));
  }
  get_logger()->info("qp_acceptor: accept nr_qp={} sid={}", handshake.nr_qp, handshake.sid);
  co_return result;
}

auto qp_acceptor::make_scheduler() -> std::shared_ptr<rdmapp::scheduler> {
  return require_scheduler(scheduler_factory_());
}

auto qp_acceptor::alloc_cq(std::shared_ptr<rdmapp::scheduler> scheduler)
    -> std::shared_ptr<rdmapp::cq> {
  return cq_provider_.alloc(config_.cq_size, require_scheduler(std::move(scheduler)));
}

auto qp_acceptor::close() noexcept -> void {
  try {
    acceptor_socket_.close();
    get_logger()->warn("qp_acceptor: closed");
  } catch (std::exception &e) {
    get_logger()->error("qp_acceptor: error close, msg={}", e.what());
  }
}

} // namespace coverbs_rpc
