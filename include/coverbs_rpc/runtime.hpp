#pragma once

#include <atomic>
#include <cppcoro/io_service.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <rdmapp/scheduler.h>
#include <thread>
#include <vector>

namespace coverbs_rpc {

class runtime {
public:
  explicit runtime(std::size_t scheduler_thread_count = 1, std::uint32_t io_concurrency_hint = 1)
      : io_service(io_concurrency_hint)
      , scheduler(std::make_shared<rdmapp::basic_scheduler>()) {
    io_worker_ = std::jthread([this]() { io_service.process_events(); });

    if (scheduler_thread_count == 0) {
      scheduler_thread_count = 1;
    }
    scheduler_workers_.reserve(scheduler_thread_count);
    for (std::size_t i = 0; i < scheduler_thread_count; ++i) {
      scheduler_workers_.emplace_back([scheduler = scheduler]() { scheduler->run(); });
    }
  }

  runtime(runtime const &) = delete;
  runtime(runtime &&) = delete;
  auto operator=(runtime const &) -> runtime & = delete;
  auto operator=(runtime &&) -> runtime & = delete;

  ~runtime() { stop(); }

  auto stop() noexcept -> void {
    if (stopped_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    io_service.stop();
    scheduler->stop();
  }

  cppcoro::io_service io_service;
  std::shared_ptr<rdmapp::scheduler> scheduler;

private:
  std::atomic_bool stopped_{false};
  std::jthread io_worker_;
  std::vector<std::jthread> scheduler_workers_;
};

} // namespace coverbs_rpc
