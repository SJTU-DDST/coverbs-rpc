#pragma once

#include <atomic>
#include <cppcoro/io_service.hpp>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <rdmapp/scheduler.h>
#include <stdexcept>
#include <thread>

#include "coverbs_rpc/common.hpp"

namespace coverbs_rpc {

class runtime {
public:
  explicit runtime(std::size_t scheduler_thread_count = 1, std::uint32_t io_concurrency_hint = 1)
      : io_service(io_concurrency_hint)
      , scheduler_thread_count_(scheduler_thread_count == 0 ? 1 : scheduler_thread_count) {
    io_worker_ = std::jthread([this]() { io_service.process_events(); });
  }

  runtime(runtime const &) = delete;
  runtime(runtime &&) = delete;
  auto operator=(runtime const &) -> runtime & = delete;
  auto operator=(runtime &&) -> runtime & = delete;

  ~runtime() { stop(); }

  auto make_scheduler() -> std::shared_ptr<rdmapp::scheduler> {
    auto scheduler = std::make_shared<rdmapp::basic_scheduler>();
    std::lock_guard lock(schedulers_mutex_);
    if (stopped_.load(std::memory_order_acquire)) {
      scheduler->stop();
      throw std::runtime_error("coverbs_rpc::runtime has stopped");
    }

    schedulers_.push_back(scheduler);
    for (std::size_t i = 0; i < scheduler_thread_count_; ++i) {
      scheduler_workers_.emplace_back([scheduler]() { scheduler->run(); });
    }
    return scheduler;
  }

  auto scheduler_factory() -> coverbs_rpc::scheduler_factory {
    return [this]() { return make_scheduler(); };
  }

  auto stop() noexcept -> void {
    if (stopped_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    io_service.stop();

    std::lock_guard lock(schedulers_mutex_);
    for (auto &scheduler : schedulers_) {
      scheduler->stop();
    }
  }

  cppcoro::io_service io_service;

private:
  std::atomic_bool stopped_{false};
  std::size_t const scheduler_thread_count_;
  std::mutex schedulers_mutex_;
  std::list<std::shared_ptr<rdmapp::scheduler>> schedulers_;
  std::jthread io_worker_;
  std::list<std::jthread> scheduler_workers_;
};

} // namespace coverbs_rpc
