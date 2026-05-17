#pragma once
#include "coverbs_rpc/common.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace coverbs_rpc::test {

struct TypedRequest {
  uint64_t id;
  std::string name;
  std::vector<uint32_t> data;
  double score;
  bool flag;
  uint8_t type;
};

struct TypedResponse {
  uint64_t id;
  std::string status;
  std::vector<uint32_t> result;
  double average;
  bool success;
};

constexpr std::size_t kNumHandlers = 10;
constexpr std::size_t kNumCallsPerHandler = 1000;
constexpr std::uint32_t kServerMaxInFlight = 512;
inline const RpcConfig kServerRpcConfig = [] {
  RpcConfig cfg;
  cfg.max_inflight = kServerMaxInFlight;
  cfg.max_req_payload = 1024 * 1024;
  cfg.max_resp_payload = 1024 * 1024;
  return cfg;
}();

constexpr std::uint32_t kClientMaxInFlight = 128;
inline const RpcConfig kClientRpcConfig = [] {
  RpcConfig cfg;
  cfg.max_inflight = kClientMaxInFlight;
  cfg.max_req_payload = 1024 * 1024;
  cfg.max_resp_payload = 1024 * 1024;
  return cfg;
}();

template <std::size_t I>
struct RequestHandler {
  static auto handle(const TypedRequest &req) -> TypedResponse;
};

} // namespace coverbs_rpc::test
