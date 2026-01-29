#pragma once
#include "coverbs_rpc/common.hpp"
#include <cstddef>
#include <cstdint>

namespace coverbs_rpc::test {
constexpr std::uint32_t kTestFnId = 1;
constexpr std::size_t kRequestSize = 128;
constexpr std::size_t kResponseSize = 4096;
constexpr std::byte kRequestByte{0x11};
constexpr std::byte kResponseByte{0x22};

constexpr std::uint32_t kServerMaxInFlight = 512;
constexpr RpcConfig kServerRpcConfig{.max_inflight = kServerMaxInFlight};

constexpr std::uint32_t kClientMaxInFlight = 256;
constexpr RpcConfig kClientRpcConfig{.max_inflight = kClientMaxInFlight};
} // namespace coverbs_rpc::test
