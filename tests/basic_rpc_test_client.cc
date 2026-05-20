#include "coverbs_rpc/basic_client.hpp"
#include "coverbs_rpc/common.hpp"
#include "coverbs_rpc/conn/connector.hpp"
#include "coverbs_rpc/detail/logger.hpp"
#include "coverbs_rpc/runtime.hpp"

#include "rdmapp/device.h"
#include <algorithm>
#include <cppcoro/io_service.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <cppcoro/when_all.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "basic_rpc_test.hpp"

using namespace coverbs_rpc;
using namespace coverbs_rpc::test;
using coverbs_rpc::detail::get_logger;

namespace {
std::string server_ip = "192.168.98.70"; // default
uint16_t server_port = 9988;             // default
constexpr int kCallCnt = 100000;
constexpr int kReportInterval = 10000;
} // namespace

cppcoro::task<void> run_rpc_test(basic_client &client, int num_calls) {
  for (int i = 0; i < num_calls; ++i) {
    auto op = client.prepare_call();
    auto req_buffer = op.request_buffer();
    std::fill_n(req_buffer.data(), kRequestSize, kRequestByte);

    auto resp = co_await op.call(kTestFnId, kRequestSize);

    if (resp.size() != kResponseSize) {
      get_logger()->error("Response length mismatch: expected {}, got {}", kResponseSize,
                          resp.size());
      exit(1);
    }

    for (auto b : resp) {
      if (b != kResponseByte) {
        get_logger()->error("Response data mismatch");
        exit(1);
      }
    }
    if ((i + 1) % kReportInterval == 0) {
      get_logger()->info("Completed {}/{} RPC calls", i + 1, num_calls);
    }
  }
  co_return;
}

cppcoro::task<void> run_test(cppcoro::io_service &io_service, scheduler_factory scheduler_factory,
                             std::shared_ptr<rdmapp::pd> pd) {
  qp_connector connector(io_service, std::move(scheduler_factory), pd, nullptr,
                         ConnConfig{.cq_size = kClientMaxInFlight * 2,
                                    .qp_config{.max_send_wr = kClientMaxInFlight * 2,
                                               .max_recv_wr = kClientMaxInFlight * 2}});
  auto qp = co_await connector.connect(server_ip, server_port);
  basic_client client(qp, kClientRpcConfig);

  const int kNumCalls = 1000;
  get_logger()->info("Step 1: Sequential test, calling RPC {} times...", kNumCalls);
  co_await run_rpc_test(client, kNumCalls);
  get_logger()->info("Step 1: All {} RPC calls successful", kNumCalls);

  const int kNumConcurrentTasks = 4;
  const int kCallsPerTask = kCallCnt;
  get_logger()->info("Step 2: Concurrent test, calling RPC {} times with {} tasks...",
                     kNumConcurrentTasks * kCallsPerTask, kNumConcurrentTasks);
  std::vector<cppcoro::task<void>> tasks;
  for (int i = 0; i < kNumConcurrentTasks; ++i) {
    tasks.push_back(run_rpc_test(client, kCallsPerTask));
  }
  co_await cppcoro::when_all(std::move(tasks));
  get_logger()->info("Step 2: All concurrent RPC calls successful");

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
