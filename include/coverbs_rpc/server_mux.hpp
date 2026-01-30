#pragma once

#include "coverbs_rpc/logger.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <span>

namespace coverbs_rpc {

class basic_mux {
public:
  using Handler =
      std::function<std::size_t(std::span<std::byte> payload, std::span<std::byte> resp)>;

  auto register_handler(uint32_t fn_id, Handler h) -> void {
    if (handlers_.find(fn_id) != handlers_.end()) [[unlikely]] {
      get_logger()->critical("server_mux: register the same handler for fn_id {}", fn_id);
      std::terminate();
    }
    handlers_[fn_id] = std::move(h);
  }

  auto dispatch(uint32_t fn_id, std::span<std::byte> payload, std::span<std::byte> resp) const
      -> std::size_t {
    auto it = handlers_.find(fn_id);
    if (it == handlers_.end()) [[unlikely]] {
      get_logger()->error("server_mux: handler not found for fn_id={}", fn_id);
      return 0;
    }
    return it->second(payload, resp);
  }

private:
  std::map<uint32_t, Handler> handlers_;
};

} // namespace coverbs_rpc
