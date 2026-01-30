#pragma once

#include "coverbs_rpc/common.hpp"

#include <cppcoro/task.hpp>
#include <memory>
#include <rdmapp/qp.h>
#include <span>

namespace coverbs_rpc {

class basic_client {
public:
  basic_client(std::shared_ptr<rdmapp::qp> qp, RpcConfig config = {});
  ~basic_client();

  auto call(uint32_t fn_id, std::span<const std::byte> req_data, std::span<std::byte> resp_buffer)
      -> cppcoro::task<std::size_t>;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace coverbs_rpc
