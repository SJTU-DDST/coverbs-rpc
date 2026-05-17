#include "coverbs_rpc/basic_client.hpp"
#include "coverbs_rpc/detail/logger.hpp"

#include <concurrentqueue.h>
#include <cppcoro/async_scope.hpp>
#include <cppcoro/sync_wait.hpp>
#include <format>
#include <memory>
#include <rdmapp/qp.h>
#include <stdexcept>
#include <thread>
#include <utility>

namespace coverbs_rpc {
using detail::get_logger;

namespace detail {

constexpr uintptr_t kWaiterReady = 1;

struct RpcSlot {
  std::atomic<uintptr_t> waiter{kWaiterEmpty};
  std::byte *response_payload{};
  std::size_t actual_len{};
  uint64_t expected_req_id{};
};

struct RpcResponseAwaitable {
  RpcSlot &slot;
  auto await_ready() const noexcept -> bool {
    return slot.waiter.load(std::memory_order_acquire) == kWaiterReady;
  }
  auto await_suspend(std::coroutine_handle<> h) noexcept -> bool {
    uintptr_t expected = kWaiterEmpty;
    return slot.waiter.compare_exchange_strong(
        expected, uintptr_t(h.address()), std::memory_order_release, std::memory_order_acquire);
  }
  auto await_resume() noexcept -> std::size_t { return slot.actual_len; }
};

static auto pause() noexcept -> void {
#if defined(__i386__) || defined(__x86_64__)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
  __asm__ __volatile__("yield" ::: "memory");
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

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
      , recv_loop_thread_(&basic_client::Impl::run_recv_workers, this) {
    for (uint32_t i = 0; i < config_.max_inflight; ++i) {
      free_slots_.enqueue(i);
    }

    get_logger()->debug("Client initialized with {} slots, send_buf={}, recv_buf={}",
                        config_.max_inflight, send_buffer_size_, recv_buffer_size_);
  }

  void run_recv_workers() {
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
        std::size_t available_payload = nbytes - sizeof(detail::RpcHeader);
        if (payload_len > available_payload || payload_len > config_.max_resp_payload)
            [[unlikely]] {
          get_logger()->error("Client: invalid response payload size: payload_len={} nbytes={}",
                              payload_len, nbytes);
          std::terminate();
        }

        slot.actual_len = payload_len;
        slot.response_payload = buffer_ptr + sizeof(detail::RpcHeader);

        // A response may beat waiter registration once send/recv completions
        // are funneled through the same scheduler thread.
        uintptr_t waiter = slot.waiter.exchange(detail::kWaiterReady, std::memory_order_acq_rel);
        if (waiter > detail::kWaiterReady) {
          auto h = std::coroutine_handle<>::from_address(reinterpret_cast<void *>(waiter));
          h.resume();
        }

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
  std::jthread recv_loop_thread_;
};

basic_client::basic_client(std::shared_ptr<rdmapp::qp> qp, RpcConfig config)
    : impl_(std::make_unique<Impl>(qp, config)) {}

basic_client::~basic_client() = default;

basic_client::call_operation::call_operation(Impl *impl, uint32_t slot_idx) noexcept
    : impl_(impl)
    , slot_idx_(slot_idx) {}

basic_client::call_operation::call_operation(call_operation &&other) noexcept
    : impl_(std::exchange(other.impl_, nullptr))
    , slot_idx_(std::exchange(other.slot_idx_, 0))
    , started_(std::exchange(other.started_, false))
    , waiting_response_(std::exchange(other.waiting_response_, false))
    , completed_(std::exchange(other.completed_, false)) {}

auto basic_client::call_operation::operator=(call_operation &&other) noexcept -> call_operation & {
  if (this != &other) {
    release();
    impl_ = std::exchange(other.impl_, nullptr);
    slot_idx_ = std::exchange(other.slot_idx_, 0);
    started_ = std::exchange(other.started_, false);
    waiting_response_ = std::exchange(other.waiting_response_, false);
    completed_ = std::exchange(other.completed_, false);
  }
  return *this;
}

basic_client::call_operation::~call_operation() { release(); }

auto basic_client::call_operation::request_buffer() noexcept -> std::span<std::byte> {
  std::size_t offset = slot_idx_ * impl_->send_buffer_size_ + sizeof(detail::RpcHeader);
  return {impl_->send_buffer_pool_.data() + offset, impl_->config_.max_req_payload};
}

auto basic_client::call_operation::call(uint32_t fn_id, std::size_t req_size)
    -> cppcoro::task<std::span<std::byte>> {
  if (!impl_) [[unlikely]] {
    throw std::logic_error("basic_client::call_operation is empty");
  }
  if (started_) [[unlikely]] {
    throw std::logic_error("basic_client::call_operation can only be submitted once");
  }
  if (req_size > impl_->config_.max_req_payload) [[unlikely]] {
    throw std::runtime_error(std::format("request payload too large: max={} req={}",
                                         impl_->config_.max_req_payload, req_size));
  }
  started_ = true;

  uint64_t seq = impl_->global_seq_.fetch_add(1);
  uint64_t req_id = detail::make_req_id(seq, slot_idx_);

  detail::RpcSlot &slot = impl_->slots_[slot_idx_];
  slot.waiter.store(detail::kWaiterEmpty);
  slot.response_payload = nullptr;
  slot.actual_len = 0;
  slot.expected_req_id = req_id;

  std::size_t send_offset = slot_idx_ * impl_->send_buffer_size_;
  auto send_slice_mr =
      rdmapp::mr_view(impl_->send_mr_, send_offset, sizeof(detail::RpcHeader) + req_size);

  detail::RpcHeader *header = reinterpret_cast<detail::RpcHeader *>(send_slice_mr.span().data());
  header->req_id = req_id;
  header->payload_len = static_cast<uint32_t>(req_size);
  header->fn_id = fn_id;

  try {
    co_await impl_->qp_->send(send_slice_mr, rdmapp::use_native_awaitable);
    waiting_response_ = true;
    co_await detail::RpcResponseAwaitable{slot};
    waiting_response_ = false;
  } catch (...) {
    release();
    throw;
  }

  completed_ = true;
  co_return std::span{slot.response_payload, slot.actual_len};
}

auto basic_client::call_operation::release() noexcept -> void {
  if (!impl_) {
    return;
  }
  if (!waiting_response_ || completed_) {
    impl_->free_slots_.enqueue(slot_idx_);
  }
  impl_ = nullptr;
}

auto basic_client::prepare_call() -> call_operation {
  uint32_t slot_idx;
  while (!impl_->free_slots_.try_dequeue(slot_idx)) {
    detail::pause();
  }
  return call_operation(impl_.get(), slot_idx);
}

} // namespace coverbs_rpc
