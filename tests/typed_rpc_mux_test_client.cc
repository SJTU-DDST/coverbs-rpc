#include "coverbs_rpc/detail/logger.hpp"
#include "coverbs_rpc/runtime.hpp"
#include "coverbs_rpc/typed_client.hpp"

#include <chrono>
#include <cppcoro/io_service.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <numeric>
#include <thread>
#include <vector>

#include "typed_rpc_test.hpp"

using namespace coverbs_rpc;
using namespace coverbs_rpc::test;
using coverbs_rpc::detail::get_logger;

namespace {
std::string server_ip = "192.168.98.70";
uint16_t server_port = 9988;
constexpr int kThreads = 4;
constexpr int kReportInterval = 100;
} // namespace

template <std::size_t I>
auto base_test(typed_client &client) -> void {
  get_logger()->info("Starting test for handler {}", I);

  auto start_handler = std::chrono::high_resolution_clock::now();
  for (std::size_t j = 0; j < kNumCallsPerHandler; ++j) {
    TypedRequest req;
    req.id = j;
    req.name = "Request from handler " + std::to_string(I) + " call " + std::to_string(j);
    req.data = {static_cast<uint32_t>(j), static_cast<uint32_t>(j + 1),
                static_cast<uint32_t>(j + 2)};
    req.score = static_cast<double>(j) * 1.1;
    req.flag = (j % 2 == 0);
    req.type = static_cast<uint8_t>(I);

    auto resp = cppcoro::sync_wait(client.call<RequestHandler<I>::handle>(req));

    // Validation
    if (resp.id != req.id) {
      get_logger()->error("ID mismatch at handler {}, call {}: expected {}, got {}", I, j, req.id,
                          resp.id);
      std::terminate();
    }
    if (resp.result.size() != req.data.size()) {
      get_logger()->error("Result size mismatch at handler {}, call {}", I, j);
      std::terminate();
    }
    for (std::size_t k = 0; k < resp.result.size(); ++k) {
      if (resp.result[k] != req.data[k] + I) {
        get_logger()->error("Result value mismatch at handler {}, call {}, index {}", I, j, k);
        std::terminate();
      }
    }
    double expected_avg = std::accumulate(req.data.begin(), req.data.end(), 0.0) /
                          static_cast<double>(req.data.size());
    if (std::abs(resp.average - expected_avg) > 1e-9) {
      get_logger()->error("Average mismatch at handler {}, call {}: expected {}, got {}", I, j,
                          expected_avg, resp.average);
      std::terminate();
    }
    if (!resp.success) {
      get_logger()->error("Success flag mismatch at handler {}, call {}", I, j);
      std::terminate();
    }

    if ((j + 1) % kReportInterval == 0) {
      get_logger()->info("Handler {}: {}/{} calls completed", I, j + 1, kNumCallsPerHandler);
    }
  }
  auto end_handler = std::chrono::high_resolution_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_handler - start_handler).count();
  get_logger()->info("Handler {} successful. Time: {} ms, Avg: {} us/call", I, elapsed,
                     (elapsed * 1000.0) / kNumCallsPerHandler);
}

template <std::size_t... Is>
auto run_all_tests(typed_client &client, std::index_sequence<Is...>) -> void {
  (base_test<Is>(client), ...);
}

cppcoro::task<void> run_test(cppcoro::io_service &io_service,
                             std::shared_ptr<rdmapp::scheduler> scheduler) {
  get_logger()->info("Connecting to {}:{}", server_ip, server_port);
  typed_client client(io_service, std::move(scheduler), server_ip, server_port, kClientRpcConfig);

  get_logger()->info("Running serial tests...");
  run_all_tests(client, std::make_index_sequence<kNumHandlers>{});

  get_logger()->info("Running parallel tests with {} threads...", kThreads);
  std::vector<std::jthread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&client]() {
      // Each thread runs a subset of handlers or all of them.
      // To keep it simple, let each thread run all handlers once.
      run_all_tests(client, std::make_index_sequence<kNumHandlers>{});
    });
  }

  for (auto &t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  get_logger()->info("All typed RPC multiplexing calls successful");
  co_return;
}

int main(int argc, char **argv) {
  if (argc >= 2) {
    server_ip = argv[1];
  }
  if (argc >= 3) {
    server_port = static_cast<uint16_t>(std::stoi(argv[2]));
  }

  coverbs_rpc::runtime runtime;

  try {
    cppcoro::sync_wait(run_test(runtime.io_service, runtime.scheduler));
  } catch (const std::exception &e) {
    get_logger()->error("Exception: {}", e.what());
    runtime.stop();
    return 1;
  }

  runtime.stop();
  return 0;
}
