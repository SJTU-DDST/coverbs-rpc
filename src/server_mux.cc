#include "coverbs_rpc/server_mux.hpp"
#include "coverbs_rpc/detail/logger.hpp"

namespace coverbs_rpc {
using detail::get_logger;

auto basic_mux::register_handler(uint32_t fn_id, std::string_view fn_name, Handler h) -> void {
  if (handlers_.find(fn_id) != handlers_.end()) [[unlikely]] {
    get_logger()->critical("server_mux: register the same handler for fn_id {}", fn_id);
    std::terminate();
  }
  get_logger()->debug("server_mux: register: id={} name={}", fn_id, fn_name);
  handlers_[fn_id] = std::move(h);
}

auto basic_mux::handler_count() const noexcept -> std::size_t { return handlers_.size(); }

auto basic_mux::dispatch(uint32_t fn_id, std::span<std::byte> payload,
                         std::span<std::byte> resp) const -> std::size_t {
  auto it = handlers_.find(fn_id);
  if (it == handlers_.end()) [[unlikely]] {
    get_logger()->error("server_mux: handler not found for fn_id={}", fn_id);
    return 0;
  }
  return it->second(payload, resp);
}

} // namespace coverbs_rpc
