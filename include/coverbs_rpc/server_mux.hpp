#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string_view>

namespace coverbs_rpc {

class basic_mux {
public:
  using Handler =
      std::function<std::size_t(std::span<std::byte> payload, std::span<std::byte> resp)>;

  auto register_handler(uint32_t fn_id, std::string_view fn_name, Handler h) -> void;

  auto dispatch(uint32_t fn_id, std::span<std::byte> payload, std::span<std::byte> resp) const
      -> std::size_t;

  auto handler_count() const noexcept -> std::size_t;

private:
  std::map<uint32_t, Handler> handlers_;
};

} // namespace coverbs_rpc
