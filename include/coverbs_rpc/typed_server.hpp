#pragma once

#include "coverbs_rpc/basic_server.hpp"
#include "coverbs_rpc/conn/acceptor.hpp"
#include "coverbs_rpc/detail/traits.hpp"
#include "coverbs_rpc/server_mux.hpp"

#include <cppcoro/async_scope.hpp>
#include <cppcoro/io_service.hpp>
#include <exception>
#include <glaze/glaze.hpp>
#include <memory>

namespace coverbs_rpc {

class typed_server {
public:
  typed_server(cppcoro::io_service &io_service, uint16_t port, TypedRpcConfig config = {},
               std::uint32_t thread_count = 4)
      : config_(config)
      , thread_count_(thread_count)
      , device_(std::make_shared<rdmapp::device>(config.device_nr, config.port_nr))
      , pd_(std::make_shared<rdmapp::pd>(device_))
      , io_service_(io_service)
      , acceptor_(io_service_, port, pd_, nullptr, config.to_conn_config())
      , mux_() {}

  template <auto Handler>
  auto register_handler() -> void {
    static_assert(!detail::is_member_fn_v<Handler>,
                  "for calling with no instance, Handler must not be a member function");
    register_handler_impl<Handler>(Handler);
  }

  template <auto Handler, typename Class>
  auto register_handler(Class *instance) -> void {
    static_assert(detail::is_member_fn_v<Handler>,
                  "for calling with instance, Handler must be a member function");
    auto invoker = [instance](auto &&...args) {
      return std::invoke(Handler, instance, std::forward<decltype(args)>(args)...);
    };
    register_handler_impl<Handler>(invoker);
  }

  auto run() -> cppcoro::task<void> {
    cppcoro::async_scope scope;
    while (true) {
      auto qp = co_await acceptor_.accept();
      get_logger()->info("typed_server: accepted connection");
      scope.spawn(handle_connection(std::move(qp)));
    }
    co_await scope.join();
  }

  ~typed_server() { acceptor_.close(); }

private:
  template <auto Handler, typename Invoker>
  auto register_handler_impl(Invoker invoker) -> void {
    using Req = detail::rpc_req_t<Handler>;
    using Resp = detail::rpc_resp_t<Handler>;
    constexpr uint32_t fn_id = detail::function_id<Handler>;

    auto h = [inv = std::move(invoker)](std::span<std::byte> req_bytes,
                                        std::span<std::byte> resp_bytes) -> std::size_t {
      Req req{};
      auto err = glz::read_beve(req, req_bytes);
      if (err) [[unlikely]] {
        get_logger()->error("typed_server: failed to deserialize request");
        return 0;
      }

      Resp resp;
      if constexpr (detail::is_coro_fn_v<Handler>) {
        // Current architecture doesn't support async handlers yet, but we can add later
        // resp = co_await inv(req);
        std::terminate(); // Not implemented
      } else {
        resp = inv(req);
      }

      auto ec = glz::write_beve(resp, resp_bytes);
      if (ec) [[unlikely]] {
        get_logger()->error("typed_server: failed to serialize response");
        return 0;
      }
      return ec.count;
    };

    mux_.register_handler(fn_id, std::move(h));
  }

  auto handle_connection(std::shared_ptr<rdmapp::qp> qp) -> cppcoro::task<void> {
    basic_server server(qp, mux_, config_, thread_count_);
    try {
      co_await server.run();
    } catch (const std::exception &e) {
      get_logger()->warn("typed_server: connection closed with error: {}", e.what());
    }
  }

  TypedRpcConfig const config_;
  uint32_t const thread_count_;
  std::shared_ptr<rdmapp::device> device_;
  std::shared_ptr<rdmapp::pd> pd_;
  cppcoro::io_service &io_service_;
  qp_acceptor acceptor_;
  basic_mux mux_;
};

} // namespace coverbs_rpc
