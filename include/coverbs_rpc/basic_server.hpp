#pragma once

#include "coverbs_rpc/common.hpp"
#include "coverbs_rpc/server_mux.hpp"

#include <cppcoro/static_thread_pool.hpp>
#include <cppcoro/task.hpp>
#include <cstdint>
#include <memory>
#include <rdmapp/mr.h>
#include <rdmapp/qp.h>
#include <vector>

namespace coverbs_rpc {

class basic_server {
public:
  basic_server(std::shared_ptr<rdmapp::qp> qp, basic_mux const &mux, RpcConfig config = {},
               std::uint32_t thread_count = 4);

  auto run() -> cppcoro::task<void>;

private:
  auto server_worker(std::size_t idx) -> cppcoro::task<void>;

  basic_mux const &mux_;
  RpcConfig const config_;
  std::size_t const send_buffer_size_;
  std::size_t const recv_buffer_size_;
  std::shared_ptr<rdmapp::qp> qp_;
  cppcoro::static_thread_pool tp_;

  std::vector<std::byte> recv_buffer_pool_;
  rdmapp::local_mr recv_mr_;
  std::vector<std::byte> send_buffer_pool_;
  rdmapp::local_mr send_mr_;
};

} // namespace coverbs_rpc
