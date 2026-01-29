#pragma once

#include <cstddef>
#include <cstdint>
#include <rdmapp/qp.h>

namespace coverbs_rpc {

struct ConnConfig {
  uint32_t cq_size = 512;
  rdmapp::qp_config qp_config = rdmapp::default_qp_config();
};

struct RpcConfig {
  std::size_t max_inflight = 512;
  std::size_t max_req_payload = 256;
  std::size_t max_resp_payload = 4096;
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
