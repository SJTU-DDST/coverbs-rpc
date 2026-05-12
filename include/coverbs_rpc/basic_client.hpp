#pragma once

#include "coverbs_rpc/common.hpp"

#include <cppcoro/task.hpp>
#include <memory>
#include <rdmapp/qp.h>
#include <span>

namespace coverbs_rpc {

class basic_client {
  struct Impl;

public:
  class call_operation {
  public:
    call_operation() noexcept = default;
    call_operation(call_operation const &) = delete;
    auto operator=(call_operation const &) -> call_operation & = delete;
    call_operation(call_operation &&other) noexcept;
    auto operator=(call_operation &&other) noexcept -> call_operation &;
    ~call_operation();

    auto request_buffer() noexcept -> std::span<std::byte>;
    auto call(uint32_t fn_id, std::size_t req_size) -> cppcoro::task<std::span<std::byte>>;

  private:
    friend class basic_client;

    call_operation(Impl *impl, uint32_t slot_idx) noexcept;
    auto release() noexcept -> void;

    Impl *impl_{nullptr};
    uint32_t slot_idx_{0};
    bool started_{false};
    bool waiting_response_{false};
    bool completed_{false};
  };

  basic_client(std::shared_ptr<rdmapp::qp> qp, RpcConfig config = {});
  ~basic_client();

  auto prepare_call() -> call_operation;

private:
  std::unique_ptr<Impl> impl_;
};

} // namespace coverbs_rpc
