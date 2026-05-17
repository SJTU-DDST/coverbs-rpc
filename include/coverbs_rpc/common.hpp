#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <rdmapp/qp.h>
#include <rdmapp/scheduler.h>

namespace coverbs_rpc {

using scheduler_factory = std::function<std::shared_ptr<rdmapp::scheduler>()>;

struct ConnConfig {
  uint32_t device_nr = 0;
  uint32_t port_nr = 1;
  uint32_t cq_size = 256;
  rdmapp::qp_config qp_config = rdmapp::default_qp_config();
};

struct RpcConfig {
  uint32_t rdma_device_nr = 0;
  uint32_t rdma_port_nr = 1;
  std::size_t max_inflight = 128;
  std::size_t max_req_payload = 256;
  std::size_t max_resp_payload = 4096;

  auto to_conn_config() const noexcept -> ConnConfig {
    ConnConfig cfg;
    auto const wr_depth = static_cast<uint32_t>(max_inflight + 64);
    cfg.cq_size = wr_depth * 2;
    cfg.qp_config.max_send_wr = wr_depth;
    cfg.qp_config.max_recv_wr = wr_depth;
    cfg.device_nr = rdma_device_nr;
    cfg.port_nr = rdma_port_nr;
    return cfg;
  }
};

namespace detail {

struct RpcHeader {
  uint64_t req_id;
  uint32_t payload_len;
  uint32_t fn_id;
};

constexpr uintptr_t kWaiterEmpty = 0;

auto inline make_req_id(uint64_t seq, uint32_t slot_idx) noexcept -> uint64_t {
  return (seq << 32) | static_cast<uint64_t>(slot_idx);
}

auto inline parse_slot_idx(uint64_t req_id) noexcept -> uint32_t {
  return static_cast<uint32_t>(req_id & 0xFFFFFFFF);
}

} // namespace detail

} // namespace coverbs_rpc
