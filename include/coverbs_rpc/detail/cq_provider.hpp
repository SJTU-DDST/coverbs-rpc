#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <rdmapp/cq.h>
#include <rdmapp/cq_poller.h>
#include <rdmapp/device.h>
#include <rdmapp/scheduler.h>

namespace coverbs_rpc::detail {

class cq_provider {
public:
  cq_provider(std::shared_ptr<rdmapp::device> device,
              std::shared_ptr<rdmapp::scheduler> scheduler)
      : device_(std::move(device))
      , scheduler_(std::move(scheduler)) {}

  auto alloc(std::size_t cq_size) -> std::shared_ptr<rdmapp::cq> {
    owned_cqs_.emplace_back(device_, scheduler_, cq_size);
    return owned_cqs_.back().cq;
  }

private:
  struct bound_cq {
    bound_cq(std::shared_ptr<rdmapp::device> device,
             std::shared_ptr<rdmapp::scheduler> scheduler, std::size_t cq_size)
        : cq(std::make_shared<rdmapp::cq>(std::move(device), cq_size))
        , poller(cq, std::move(scheduler)) {}

    std::shared_ptr<rdmapp::cq> cq;
    rdmapp::native_cq_poller poller;
  };

  std::shared_ptr<rdmapp::device> device_;
  std::shared_ptr<rdmapp::scheduler> scheduler_;
  std::list<bound_cq> owned_cqs_;
};

} // namespace coverbs_rpc::detail
