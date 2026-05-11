# coverbs-rpc

`coverbs-rpc` is a high-performance C++23 RPC framework that combines the power of **Coroutines** and **RDMA Verbs**. It is designed for low-latency, high-throughput distributed systems, providing a modern and type-safe interface for RDMA-based communication.

## Key Features

- **High Performance**: Built on top of RDMA (Remote Direct Memory Access) for ultra-low latency and high throughput.
- **Modern C++**: Leverages C++23 features, including native coroutine support for clean, asynchronous code.
- **Asynchronous I/O**: Fully asynchronous architecture using `cppcoro` for efficient resource utilization.
- **Type-Safe API**: Easy-to-use typed RPC interface with automatic serialization/deserialization.
- **Fast Serialization**: Uses [Glaze](https://github.com/stephenberry/glaze) for extremely fast binary serialization (`beve`).
- **Service Multiplexing**: Support for multiple RPC services over a single RDMA connection.
- **Lock-free Internal Queues**: Uses `concurrentqueue` for high-performance internal task management.

## Prerequisites

- **OS**: Linux
- **Hardware**: RDMA-capable NIC (Mellanox/NVIDIA ConnectX, etc.) or Soft-RoCE.
- **Software Dependencies**:
    - `libibverbs`
    - `xmake` (Build system)
    - Dependencies managed by xmake: [rdmapp](https://github.com/SJTU-DDST/rdmapp), `cppcoro`, `glaze`, `concurrentqueue`.

## Building

`coverbs-rpc` uses `xmake` as its build system.

```bash
# Build the library
xmake

# Build with tests
xmake f --tests=y
xmake
```

## Quick Start

### 1. Define Request and Response Structs

```cpp
#include <string>

struct EchoReq {
  std::string msg;
};

struct EchoResp {
  std::string msg;
};
```

### 2. Implement a Handler

Handlers can be plain functions or member functions.

```cpp
auto echo(const EchoReq &req) -> EchoResp { 
    return EchoResp{.msg = "Echo: " + req.msg}; 
}
```

### 3. Server Setup

```cpp
#include <coverbs_rpc/runtime.hpp>
#include <coverbs_rpc/typed_server.hpp>
#include <cppcoro/sync_wait.hpp>
#include <memory>
#include <rdmapp/scheduler.h>

cppcoro::task<void> run_server(cppcoro::io_service &io_service,
                               coverbs_rpc::scheduler_factory scheduler_factory,
                               uint16_t port) {
    coverbs_rpc::TypedRpcConfig config;
    coverbs_rpc::typed_server server(io_service, std::move(scheduler_factory), port, config);
    
    // Register the handler
    server.register_handler<echo>();
    
    co_await server.run();
}

int main() {
    coverbs_rpc::runtime runtime;
    cppcoro::sync_wait(run_server(runtime.io_service, runtime.scheduler_factory(), 12345));
    runtime.stop();
    return 0;
}
```

### 4. Client Usage

```cpp
#include "coverbs_rpc/typed_client.hpp"
#include "coverbs_rpc/runtime.hpp"
#include <memory>
#include <rdmapp/scheduler.h>

cppcoro::task<void> run_client(cppcoro::io_service &io_service,
                               coverbs_rpc::scheduler_factory scheduler_factory,
                               std::string hostname, uint16_t port) {
    coverbs_rpc::typed_client client(io_service, std::move(scheduler_factory), hostname, port);
    
    EchoReq req{.msg = "Hello coverbs-rpc!"};
    
    // Make a type-safe RPC call
    auto resp = co_await client.call<echo>(req);
    
    std::cout << "Received: " << resp.msg << std::endl;
}
```

## Project Structure

- `include/coverbs_rpc/`: Core header files.
    - `runtime.hpp`: Owns the TCP `io_service` thread and RDMA scheduler threads.
    - `typed_client.hpp` / `typed_server.hpp`: High-level type-safe RPC API.
    - `basic_client.hpp` / `basic_server.hpp`: Lower-level RPC primitives.
    - `conn/`: RDMA connection management (acceptor, connector).
- `src/`: Implementation files.
- `tests/`: Unit tests and benchmarks.

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [rdmapp](https://github.com/SJTU-DDST/rdmapp): Modern C++ RDMA library.
- [cppcoro](https://github.com/andreasbuhr/cppcoro): A library of C++ coroutine abstractions.
- [glaze](https://github.com/stephenberry/glaze): Extremely fast serialization library.
