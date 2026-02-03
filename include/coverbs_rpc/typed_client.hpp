#pragma once

#include "coverbs_rpc/basic_client.hpp"
#include "coverbs_rpc/conn/connector.hpp"
#include "coverbs_rpc/detail/traits.hpp"

#include <cppcoro/io_service.hpp>
#include <cppcoro/sync_wait.hpp>
#include <glaze/glaze.hpp>
#include <memory>
#include <stdexcept>
#include <vector>

namespace coverbs_rpc {

class typed_client {
public:
  typed_client(cppcoro::io_service &io_service, std::string_view hostname, uint16_t port,
               TypedRpcConfig config = {});

  template <auto Handler>
  auto call(auto &&req) -> cppcoro::task<detail::rpc_resp_t<Handler>> {
    using Resp = detail::rpc_resp_t<Handler>;
    using Req = detail::rpc_req_t<Handler>;
    static_assert(std::same_as<Req, std::decay_t<decltype(req)>>);
    constexpr uint32_t fn_id = detail::function_id<Handler>;

    std::vector<std::byte> send_buffer(config_.max_req_payload);
    auto ec = glz::write_beve_untagged(req, send_buffer);
    if (ec) [[unlikely]] {
      throw std::runtime_error("typed_client: failed to serialize request");
    }
    std::size_t req_size = ec.count;

    std::vector<std::byte> recv_buffer(config_.max_resp_payload);
    std::size_t resp_size =
        co_await client_->call(fn_id, std::span{send_buffer.data(), req_size}, recv_buffer);

    Resp resp{};
    auto err = glz::read_beve_untagged(resp, std::span{recv_buffer.data(), resp_size});
    if (err) [[unlikely]] {
      throw std::runtime_error("typed_client: failed to deserialize response");
    }

    co_return resp;
  }

private:
  TypedRpcConfig const config_;
  std::shared_ptr<rdmapp::device> device_;
  std::shared_ptr<rdmapp::pd> pd_;
  cppcoro::io_service &io_service_;
  qp_connector connector_;
  std::shared_ptr<rdmapp::qp> qp_;
  std::unique_ptr<basic_client> client_;
};

} // namespace coverbs_rpc
