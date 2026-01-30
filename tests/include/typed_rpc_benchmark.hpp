#pragma once

#include <string>

namespace coverbs_rpc::benchmark {

constexpr int kNumCalls = 200000;
constexpr int kThreads = 4;
constexpr int kReportInterval = 10000;

struct BenchmarkRequest {
  std::string data;
};

struct BenchmarkResponse {
  std::string data;
};

template <std::size_t I>
struct BenchmarkHandler {
  static auto handle(const BenchmarkRequest &req [[maybe_unused]]) -> BenchmarkResponse {
    BenchmarkResponse resp;
    if constexpr (I == 0) {
      resp.data = std::string(256, 'a');
    } else {
      resp.data = std::string(4096, 'b');
    }
    return resp;
  }
};
} // namespace coverbs_rpc::benchmark