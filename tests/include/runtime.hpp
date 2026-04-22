#pragma once

#include <atomic>
#include <cstddef>
#include <cppcoro/io_service.hpp>
#include <memory>
#include <rdmapp/scheduler.h>
#include <thread>
#include <vector>

namespace coverbs_rpc::test {

class runtime {
public:
  explicit runtime(std::size_t scheduler_thread_count = 1)
      : scheduler(std::make_shared<rdmapp::basic_scheduler>()) {
    io_worker_ = std::jthread([this]() { io_service.process_events(); });

    if (scheduler_thread_count == 0) {
      scheduler_thread_count = 1;
    }
    scheduler_workers_.reserve(scheduler_thread_count);
    for (std::size_t i = 0; i < scheduler_thread_count; ++i) {
      scheduler_workers_.emplace_back([scheduler = scheduler]() { scheduler->run(); });
    }
  }

  ~runtime() { stop(); }

  auto stop() noexcept -> void {
    if (stopped_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    io_service.stop();
    scheduler->stop();
  }

  cppcoro::io_service io_service;
  std::shared_ptr<rdmapp::basic_scheduler> scheduler;

private:
  std::atomic_bool stopped_{false};
  std::jthread io_worker_;
  std::vector<std::jthread> scheduler_workers_;
};

} // namespace coverbs_rpc::test
