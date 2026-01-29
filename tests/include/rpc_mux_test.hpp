#pragma once
#include "coverbs_rpc/common.hpp"
#include <cstddef>
#include <cstdint>

namespace coverbs_rpc::test {
constexpr std::size_t kNumHandlers = 20;
constexpr std::size_t kNumCallsPerHandler = 1000;
constexpr std::size_t kRequestSize = 128;
constexpr std::size_t kResponseSize = 128;

inline auto get_request_byte(uint32_t fn_id) -> std::byte {
  return static_cast<std::byte>(0x10 + fn_id);
}

inline auto get_response_byte(uint32_t fn_id) -> std::byte {
  return static_cast<std::byte>(0x20 + fn_id);
}

constexpr std::uint32_t kServerMaxInFlight = 1024;
constexpr RpcConfig kServerRpcConfig{.max_inflight = kServerMaxInFlight};

constexpr std::uint32_t kClientMaxInFlight = 512;
constexpr RpcConfig kClientRpcConfig{.max_inflight = kClientMaxInFlight};
} // namespace coverbs_rpc::test
