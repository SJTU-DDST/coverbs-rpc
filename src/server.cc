#include "coverbs_rpc/server.hpp"
#include "coverbs_rpc/logger.hpp"

#include <cppcoro/when_all.hpp>
#include <exception>

namespace coverbs_rpc {

Server::Server(std::shared_ptr<rdmapp::qp> qp, RpcConfig config,
               std::uint32_t thread_count)
    : config_(config),
      send_buffer_size_(config_.max_resp_payload + sizeof(detail::RpcHeader)),
      recv_buffer_size_(config_.max_req_payload + sizeof(detail::RpcHeader)),
      qp_(qp), tp_(thread_count),
      recv_buffer_pool_(config_.max_inflight * recv_buffer_size_),
      recv_mr_(qp->pd_ptr()->reg_mr(recv_buffer_pool_.data(),
                                    recv_buffer_pool_.size())),
      send_buffer_pool_(config_.max_inflight * send_buffer_size_),
      send_mr_(qp->pd_ptr()->reg_mr(send_buffer_pool_.data(),
                                    send_buffer_pool_.size())) {
  get_logger()->info("Server initialized with {} slots, thread_count={}",
                     config_.max_inflight, thread_count);
}

auto Server::register_handler(uint32_t fn_id, Handler h) -> void {
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  bool ok = handlers_.find(fn_id) == handlers_.end();
  assert(ok);
  if (!ok) [[unlikely]] {
    get_logger()->critical("Server: register the same handler");
    std::terminate();
  }
  handlers_[fn_id] = std::move(h);
}

auto Server::run() -> cppcoro::task<void> {
  std::vector<cppcoro::task<void>> workers;
  workers.reserve(config_.max_inflight);

  for (std::size_t i = 0; i < config_.max_inflight; ++i) {
    workers.emplace_back(server_worker(i));
  }

  co_await cppcoro::when_all(std::move(workers));
}

auto Server::server_worker(std::size_t idx) -> cppcoro::task<void> {
  std::size_t const recv_offset = idx * recv_buffer_size_;
  std::size_t const send_offset = idx * send_buffer_size_;

  auto recv_mr = rdmapp::mr_view(recv_mr_, recv_offset, recv_buffer_size_);
  auto send_mr = rdmapp::mr_view(send_mr_, send_offset, send_buffer_size_);

  auto const resp_payload_span = std::span<std::byte>(
      static_cast<std::byte *>(send_mr.addr()) + sizeof(detail::RpcHeader),
      config_.max_resp_payload);

  while (true) {
    auto [nbytes, _] =
        co_await qp_->recv(recv_mr, rdmapp::use_native_awaitable);

    if (nbytes < sizeof(detail::RpcHeader)) [[unlikely]] {
      get_logger()->warn("Server: received too small packet: {}", nbytes);
      continue;
    }

    co_await tp_.schedule();

    auto *header = reinterpret_cast<detail::RpcHeader *>(recv_mr.addr());
    auto payload = std::span<std::byte>(
        static_cast<std::byte *>(recv_mr.addr()) + sizeof(detail::RpcHeader),
        header->payload_len);
    auto *resp_header = reinterpret_cast<detail::RpcHeader *>(send_mr.addr());

    std::size_t resp_payload_len = 0;
    if (auto it = handlers_.find(header->fn_id); it != handlers_.end()) {
      resp_payload_len = it->second(payload, resp_payload_span);
    } else [[unlikely]] {
      get_logger()->error("Server: handler not found for fn_id={}",
                          header->fn_id);
      continue;
    }

    resp_header->req_id = header->req_id;
    resp_header->payload_len = static_cast<uint32_t>(resp_payload_len);

    std::size_t resp_len = sizeof(detail::RpcHeader) + resp_payload_len;
    auto send_view = rdmapp::mr_view(send_mr_, send_offset, resp_len);

    try {
      co_await qp_->send(send_view, rdmapp::use_native_awaitable);
    } catch (const std::exception &e) {
      get_logger()->error("Server: send reply failed: {}", e.what());
    }
  }
}

} // namespace coverbs_rpc
