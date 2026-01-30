#include "coverbs_rpc/logger.hpp"
#include "coverbs_rpc/typed_client.hpp"
#include "coverbs_rpc/utils/spin_wait.hpp"

#include <chrono>
#include <cppcoro/io_service.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <thread>
#include <vector>

#include "typed_rpc_benchmark.hpp"

using namespace coverbs_rpc;

template <std::size_t I>
void run_bench(typed_client &client, int threads_count, const std::string &label) {
  auto work = [&](int calls, int idx = 0) {
    benchmark::BenchmarkRequest req;
    req.data = std::string(256, 'c');
    auto start = std::chrono::high_resolution_clock::now();
    auto last_report_time = start;
    for (int i = 0; i < calls; ++i) {
      auto resp = utils::spin_wait(client.call<benchmark::BenchmarkHandler<I>::handle>(req));
      if ((i + 1) % benchmark::kReportInterval == 0) {
        auto now = std::chrono::high_resolution_clock::now();
        auto interval_us =
            std::chrono::duration_cast<std::chrono::microseconds>(now - last_report_time).count();
        get_logger()->info("Case {}-{} ({} threads): Progress {}/{}, Interval Latency = {:.2f} us",
                           label, idx, threads_count, i + 1, calls,
                           (double)interval_us / benchmark::kReportInterval);
        last_report_time = now;
      }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double avg_latency = (double)elapsed_us / calls;
    get_logger()->info("Case {}-{} ({} threads): Total Average Latency = {:.2f} us", label, idx,
                       threads_count, avg_latency);
  };

  // Warmup
  work(100);

  if (threads_count == 1) {
    work(benchmark::kNumCalls);
  } else {
    std::vector<std::jthread> threads;
    for (int t = 0; t < threads_count; ++t) {
      threads.emplace_back(work, benchmark::kNumCalls / threads_count, t);
    }
  }
}

int main(int argc, char **argv) {
  std::string server_ip = "192.168.98.70";
  uint16_t server_port = 9988;
  if (argc >= 2)
    server_ip = argv[1];
  if (argc >= 3)
    server_port = static_cast<uint16_t>(std::stoi(argv[2]));

  cppcoro::io_service io_service;
  auto looper = std::jthread([&io_service]() { io_service.process_events(); });

  TypedRpcConfig config;
  config.max_inflight = 512;
  config.max_req_payload = 8192;
  config.max_resp_payload = 8192;

  try {
    typed_client client(io_service, server_ip, server_port, config);

    get_logger()->info("Starting benchmarks...");

    // Case 1: 256B req, 256B resp
    run_bench<0>(client, 1, "1 (256B/256B)");
    run_bench<0>(client, benchmark::kThreads, "1 (256B/256B)");

    // Case 2: 256B req, 4KB resp
    run_bench<1>(client, 1, "2 (256B/4KB)");
    run_bench<1>(client, benchmark::kThreads, "2 (256B/4KB)");
    get_logger()->info("Done.");
  } catch (const std::exception &e) {
    get_logger()->error("Exception: {}", e.what());
    return 1;
  }

  return 0;
}
