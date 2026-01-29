#include <coverbs_rpc/logger.hpp>
#include <cppcoro/task.hpp>
#include <rdmapp/qp.h>

int main(int argc [[maybe_unused]], char **argv [[maybe_unused]]) {
  coverbs_rpc::get_logger()->info("hello world!");
  return 0;
}
