#include "coverbs_rpc/basic_client.hpp"
#include "coverbs_rpc/common.hpp"
#include "coverbs_rpc/conn/connector.hpp"
#include "coverbs_rpc/detail/logger.hpp"
#include "coverbs_rpc/runtime.hpp"

#include <chrono>
#include <cppcoro/io_service.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "rpc_mux_test.hpp"

using namespace coverbs_rpc;
using namespace coverbs_rpc::test;
using coverbs_rpc::detail::get_logger;

namespace {
std::string server_ip = "192.168.98.70"; // default
uint16_t server_port = 9988;             // default
constexpr int kThreads = 4;
constexpr int kReportInterval = 10000;
} // namespace

auto base_test(basic_client &client) -> void {
  get_logger()->info("Starting base test: {} handlers, {} calls each", kNumHandlers,
                     kNumCallsPerHandler);

  auto start_all = std::chrono::high_resolution_clock::now();
  size_t total_calls = 0;
  auto last_check = std::chrono::high_resolution_clock::now();

  for (uint32_t i = 0; i < kNumHandlers; ++i) {
    std::vector<std::byte> req_data(kRequestSize, get_request_byte(i));
    auto expected_resp_byte = get_response_byte(i);

    for (std::size_t j = 0; j < kNumCallsPerHandler; ++j) {
      auto op = client.prepare_call();
      auto req_buffer = op.request_buffer();
      std::copy_n(req_data.data(), req_data.size(), req_buffer.data());

      auto resp = cppcoro::sync_wait(op.call(i, req_data.size()));

      if (resp.size() != kResponseSize) {
        get_logger()->error("Response length mismatch at handler {}, call {}: "
                            "expected {}, got {}",
                            i, j, kResponseSize, resp.size());
        std::terminate();
      }

      for (auto b : resp) {
        if (b != expected_resp_byte) {
          get_logger()->error("Response data mismatch at handler {}, call {}", i, j);
          std::terminate();
        }
      }

      total_calls++;
      if (total_calls % kReportInterval == 0) {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(now - last_check).count();
        get_logger()->info("Intermediate Latency (last {} calls): {} us (avg {} us/call)",
                           kReportInterval, elapsed, elapsed / (1.0 * kReportInterval));
        last_check = now;
      }
    }
    get_logger()->info("Handler {} successful", i);
  }

  auto end_all = std::chrono::high_resolution_clock::now();
  auto total_elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(end_all - start_all).count();
  get_logger()->info("Base test successful. Total calls: {}, Total time: {} us, Avg Latency: {} us",
                     total_calls, total_elapsed, total_elapsed / (double)total_calls);
}

cppcoro::task<void> run_test(cppcoro::io_service &io_service, scheduler_factory scheduler_factory,
                             std::shared_ptr<rdmapp::pd> pd) {
  get_logger()->info("Running serial base test...");
  qp_connector connector(io_service, std::move(scheduler_factory), pd, nullptr,
                         ConnConfig{.cq_size = kClientMaxInFlight * 2,
                                    .qp_config{.max_send_wr = kClientMaxInFlight * 2,
                                               .max_recv_wr = kClientMaxInFlight * 2}});
  auto qp = co_await connector.connect(server_ip, server_port);
  basic_client client(qp, kClientRpcConfig);

  base_test(client);

  get_logger()->info("Running parallel base tests with {} threads...", kThreads);
  std::vector<std::jthread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&client]() { base_test(client); });
  }

  for (auto &t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  get_logger()->info("All RPC multiplexing calls successful");
  co_return;
}

int main(int argc, char **argv) {
  if (argc >= 2) {
    server_ip = argv[1];
  }
  if (argc >= 3) {
    server_port = std::stoi(argv[2]);
  }

  get_logger()->info("Connecting to {}:{}", server_ip, server_port);

  auto device = std::make_shared<rdmapp::device>(rdmapp::auto_select);
  auto pd = std::make_shared<rdmapp::pd>(device);
  coverbs_rpc::runtime runtime;

  try {
    cppcoro::sync_wait(run_test(runtime.io_service, runtime.scheduler_factory(), pd));
  } catch (const std::exception &e) {
    get_logger()->error("Exception: {}", e.what());
    runtime.stop();
    return 1;
  }

  runtime.stop();
  return 0;
}
