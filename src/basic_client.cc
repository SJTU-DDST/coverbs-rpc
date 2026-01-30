#include "coverbs_rpc/basic_client.hpp"
#include "coverbs_rpc/logger.hpp"

#include <concurrentqueue.h>
#include <cppcoro/async_scope.hpp>
#include <cppcoro/sync_wait.hpp>
#include <memory>
#include <rdmapp/qp.h>

namespace coverbs_rpc {

namespace detail {

struct RpcSlot {
  std::atomic<uintptr_t> waiter{kWaiterEmpty};
  std::span<std::byte> user_resp_buffer{};
  std::size_t actual_len{};
  uint64_t expected_req_id{};
};

struct RpcResponseAwaitable {
  RpcSlot &slot;
  constexpr auto await_ready() const noexcept -> bool { return false; }
  void await_suspend(std::coroutine_handle<> h) noexcept {
    slot.waiter.store(uintptr_t(h.address()));
  }
  auto await_resume() noexcept -> std::size_t { return slot.actual_len; }
};

static auto pause() noexcept -> void { __builtin_ia32_pause(); }

} // namespace detail

struct basic_client::Impl {
  Impl(std::shared_ptr<rdmapp::qp> qp, RpcConfig config)
      : config_(config)
      , send_buffer_size_(config_.max_req_payload + sizeof(detail::RpcHeader))
      , recv_buffer_size_(config_.max_resp_payload + sizeof(detail::RpcHeader))
      , qp_(qp)
      , send_buffer_pool_(config_.max_inflight * send_buffer_size_)
      , send_mr_(qp->pd_ptr()->reg_mr(send_buffer_pool_.data(), send_buffer_pool_.size()))
      , recv_buffer_pool_(config_.max_inflight * recv_buffer_size_)
      , recv_mr_(qp->pd_ptr()->reg_mr(recv_buffer_pool_.data(), recv_buffer_pool_.size()))
      , slots_(config_.max_inflight)
      , free_slots_(config_.max_inflight * 2)
      , worker_(&basic_client::Impl::start_recv_workers, this) {
    for (uint32_t i = 0; i < config_.max_inflight; ++i) {
      free_slots_.enqueue(i);
    }

    get_logger()->info("Client initialized with {} slots, send_buf={}, recv_buf={}",
                       config_.max_inflight, send_buffer_size_, recv_buffer_size_);
  }

  void start_recv_workers() {
    cppcoro::async_scope scope;
    for (std::size_t i = 0; i < config_.max_inflight; ++i) {
      scope.spawn(recv_worker(i));
    }
    cppcoro::sync_wait(scope.join());
  }

  auto recv_worker(std::size_t worker_idx) -> cppcoro::task<void> {
    get_logger()->debug("Client: recv_worker[{}] started", worker_idx);
    std::size_t offset = worker_idx * recv_buffer_size_;
    while (true) {
      auto recv_slice_mr = rdmapp::mr_view(recv_mr_, offset, recv_buffer_size_);
      try {
        auto [nbytes, _] = co_await qp_->recv(recv_slice_mr, rdmapp::use_native_awaitable);

        if (nbytes < sizeof(detail::RpcHeader)) [[unlikely]] {
          get_logger()->warn("Client: received too small packet: {}", nbytes);
          continue;
        }

        auto buffer_ptr = recv_buffer_pool_.data() + offset;
        auto header = reinterpret_cast<detail::RpcHeader *>(buffer_ptr);

        uint64_t recv_id = header->req_id;
        uint32_t slot_idx = detail::parse_slot_idx(recv_id);

        if (slot_idx >= config_.max_inflight) [[unlikely]] {
          get_logger()->error("Client: invalid slot_idx decoded: {}", slot_idx);
          continue;
        }

        detail::RpcSlot &slot = slots_[slot_idx];

        if (slot.expected_req_id != recv_id) [[unlikely]] {
          get_logger()->error("Client: mismatch req_id: expected={} get={}", slot.expected_req_id,
                              recv_id);
          std::terminate();
        }

        std::size_t payload_len = header->payload_len;
        std::size_t copy_len = std::min((std::size_t)payload_len, slot.user_resp_buffer.size());

        std::copy_n(buffer_ptr + sizeof(detail::RpcHeader), copy_len, slot.user_resp_buffer.data());

        slot.actual_len = copy_len;

        uintptr_t w;
        while ((w = slot.waiter.load()) == detail::kWaiterEmpty) {
          detail::pause();
        }
        auto h = std::coroutine_handle<>::from_address(reinterpret_cast<void *>(w));
        h.resume();

      } catch (const std::exception &e) {
        get_logger()->error("Client: recv worker error: {}", e.what());
        break;
      }
    }
  }

  RpcConfig const config_;
  std::size_t const send_buffer_size_;
  std::size_t const recv_buffer_size_;

  std::shared_ptr<rdmapp::qp> qp_;

  std::vector<std::byte> send_buffer_pool_;
  rdmapp::local_mr send_mr_;
  std::vector<std::byte> recv_buffer_pool_;
  rdmapp::local_mr recv_mr_;

  std::vector<detail::RpcSlot> slots_;
  moodycamel::ConcurrentQueue<uint32_t> free_slots_;

  std::atomic<uint64_t> global_seq_{0};
  std::jthread worker_;
};

basic_client::basic_client(std::shared_ptr<rdmapp::qp> qp, RpcConfig config)
    : impl_(std::make_unique<Impl>(qp, config)) {}

basic_client::~basic_client() = default;

auto basic_client::call(uint32_t fn_id, std::span<const std::byte> req_data,
                        std::span<std::byte> resp_buffer) -> cppcoro::task<std::size_t> {
  if (req_data.size() > impl_->config_.max_req_payload) {
    throw std::runtime_error("request payload too large");
  }

  uint32_t slot_idx;
  while (!impl_->free_slots_.try_dequeue(slot_idx)) {
    detail::pause();
  }

  uint64_t seq = impl_->global_seq_.fetch_add(1);
  uint64_t req_id = detail::make_req_id(seq, slot_idx);

  detail::RpcSlot &slot = impl_->slots_[slot_idx];
  slot.waiter.store(detail::kWaiterEmpty);
  slot.user_resp_buffer = resp_buffer;
  slot.expected_req_id = req_id;

  std::size_t send_offset = slot_idx * impl_->send_buffer_size_;
  auto send_slice_mr =
      rdmapp::mr_view(impl_->send_mr_, send_offset, sizeof(detail::RpcHeader) + req_data.size());

  detail::RpcHeader *header = reinterpret_cast<detail::RpcHeader *>(send_slice_mr.span().data());
  header->req_id = req_id;
  header->payload_len = static_cast<uint32_t>(req_data.size());
  header->fn_id = fn_id;

  std::copy_n(req_data.data(), req_data.size(),
              send_slice_mr.span().data() + sizeof(detail::RpcHeader));

  std::size_t nbytes = 0;
  try {
    co_await impl_->qp_->send(send_slice_mr, rdmapp::use_native_awaitable);
    nbytes = co_await detail::RpcResponseAwaitable{slot};
  } catch (const std::exception &e) {
    get_logger()->error("Client: RPC failed: {}", e.what());
  }

  impl_->free_slots_.enqueue(slot_idx);
  co_return nbytes;
}

} // namespace coverbs_rpc
