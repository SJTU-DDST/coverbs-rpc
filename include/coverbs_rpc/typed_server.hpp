#pragma once

#include "coverbs_rpc/conn/acceptor.hpp"
#include "coverbs_rpc/detail/traits.hpp"
#include "coverbs_rpc/server_mux.hpp"

#include <cppcoro/io_service.hpp>
#include <cppcoro/task.hpp>
#include <exception>
#include <glaze/beve/read.hpp>
#include <glaze/glaze.hpp>
#include <memory>
#include <stdexcept>

namespace coverbs_rpc {

class typed_server {
public:
  typed_server(cppcoro::io_service &io_service, scheduler_factory scheduler_factory, uint16_t port,
               RpcConfig config = {});

  typed_server(cppcoro::io_service &io_service, scheduler_factory scheduler_factory, uint16_t port,
               RpcConfig config, ConnConfig conn_config);

  typed_server(cppcoro::io_service &io_service, std::shared_ptr<rdmapp::scheduler> scheduler,
               uint16_t port, RpcConfig config = {});

  typed_server(cppcoro::io_service &io_service, std::shared_ptr<rdmapp::scheduler> scheduler,
               uint16_t port, RpcConfig config, ConnConfig conn_config);

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

  auto run() -> cppcoro::task<void>;

  ~typed_server();

private:
  template <auto Handler, typename Invoker>
  auto register_handler_impl(Invoker invoker) -> void {
    using Req = detail::rpc_req_t<Handler>;
    using Resp = detail::rpc_resp_t<Handler>;
    constexpr uint32_t fn_id = detail::function_id<Handler>;
    constexpr std::string_view fn_name = detail::function_name<Handler>;

    auto h = [inv = std::move(invoker)](std::span<std::byte> req_bytes,
                                        std::span<std::byte> resp_bytes) -> std::size_t {
      Req req{};
      auto err = glz::read_beve_untagged(req, req_bytes);
      if (err) [[unlikely]] {
        throw std::runtime_error("typed_server: failed to deserialize request");
      }

      Resp resp;
      if constexpr (detail::is_coro_fn_v<Handler>) {
        // Current architecture doesn't support async handlers yet, but we can add later
        // resp = co_await inv(req);
        std::terminate(); // Not implemented
        static_assert(!sizeof(Handler));
      } else {
        resp = inv(req);
      }

      auto ec = glz::write_beve_untagged(resp, resp_bytes);
      if (ec) [[unlikely]] {
        throw std::runtime_error("typed_server: failed to serialize response");
      }
      return ec.count;
    };

    mux_.register_handler(fn_id, fn_name, std::move(h));
  }

  auto handle_connection(std::shared_ptr<rdmapp::qp> qp) -> cppcoro::task<void>;

  RpcConfig const config_;
  std::shared_ptr<rdmapp::device> device_;
  std::shared_ptr<rdmapp::pd> pd_;
  cppcoro::io_service &io_service_;
  qp_acceptor acceptor_;
  basic_mux mux_;
};

} // namespace coverbs_rpc
