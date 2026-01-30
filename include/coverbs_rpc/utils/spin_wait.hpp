#pragma once

#include <atomic>
#include <coroutine>
#include <cppcoro/awaitable_traits.hpp>
#include <exception>
#include <type_traits>
#include <utility>

namespace coverbs_rpc::utils {

inline void cpu_relax() noexcept { __builtin_ia32_pause(); }

template <typename T>
struct spin_wait_task;

// Base promise to handle synchronization
struct spin_wait_promise_base {
  std::atomic<bool> is_ready{false};
  std::exception_ptr exception{nullptr};

  void unhandled_exception() { exception = std::current_exception(); }
};

template <typename T>
struct spin_wait_promise : spin_wait_promise_base {
  union {
    T value;
  };
  bool has_value = false;

  spin_wait_promise() noexcept {}
  ~spin_wait_promise() {
    if (has_value)
      value.~T();
  }

  spin_wait_task<T> get_return_object();

  auto initial_suspend() noexcept { return std::suspend_always{}; }

  auto final_suspend() noexcept {
    is_ready.store(true, std::memory_order_release);
    return std::suspend_always{};
  }

  template <typename U>
  void return_value(U &&v) {
    new (&value) T(std::forward<U>(v));
    has_value = true;
  }

  T get_result() {
    if (exception)
      std::rethrow_exception(exception);
    return std::move(value);
  }
};

template <typename T>
struct spin_wait_promise<T &&> : spin_wait_promise_base {
  union {
    T value;
  };
  bool has_value = false;

  spin_wait_promise() noexcept {}
  ~spin_wait_promise() {
    if (has_value)
      value.~T();
  }

  spin_wait_task<T &&> get_return_object();

  auto initial_suspend() noexcept { return std::suspend_always{}; }
  auto final_suspend() noexcept {
    is_ready.store(true, std::memory_order_release);
    return std::suspend_always{};
  }

  void return_value(T &&v) {
    new (&value) T(std::move(v));
    has_value = true;
  }

  T &&get_result() {
    if (exception)
      std::rethrow_exception(exception);
    return std::move(value);
  }
};

template <typename T>
struct spin_wait_promise<T &> : spin_wait_promise_base {
  T *value_ptr = nullptr;

  spin_wait_task<T &> get_return_object();

  auto initial_suspend() noexcept { return std::suspend_always{}; }

  auto final_suspend() noexcept {
    is_ready.store(true, std::memory_order_release);
    return std::suspend_always{};
  }

  void return_value(T &v) { value_ptr = std::addressof(v); }

  T &get_result() {
    if (exception)
      std::rethrow_exception(exception);
    return *value_ptr;
  }
};

template <>
struct spin_wait_promise<void> : spin_wait_promise_base {
  spin_wait_task<void> get_return_object();

  auto initial_suspend() noexcept { return std::suspend_always{}; }

  auto final_suspend() noexcept {
    is_ready.store(true, std::memory_order_release);
    return std::suspend_always{};
  }

  void return_void() {}

  void get_result() {
    if (exception)
      std::rethrow_exception(exception);
  }
};

// The Coroutine Handle Wrapper
template <typename T>
struct spin_wait_task {
  using promise_type = spin_wait_promise<T>;
  using handle_t = std::coroutine_handle<promise_type>;

  handle_t handle;

  explicit spin_wait_task(handle_t h)
      : handle(h) {}

  ~spin_wait_task() {
    if (handle)
      handle.destroy();
  }

  spin_wait_task(const spin_wait_task &) = delete;
  spin_wait_task &operator=(const spin_wait_task &) = delete;
  spin_wait_task(spin_wait_task &&other) noexcept
      : handle(std::exchange(other.handle, nullptr)) {}

  void start() { handle.resume(); }

  bool done() const noexcept { return handle.promise().is_ready.load(std::memory_order_acquire); }

  decltype(auto) result() { return handle.promise().get_result(); }
};

// Link promise to task
template <typename T>
spin_wait_task<T> spin_wait_promise<T>::get_return_object() {
  return spin_wait_task<T>{std::coroutine_handle<spin_wait_promise<T>>::from_promise(*this)};
}
template <typename T>
spin_wait_task<T &> spin_wait_promise<T &>::get_return_object() {
  return spin_wait_task<T &>{std::coroutine_handle<spin_wait_promise<T &>>::from_promise(*this)};
}
template <typename T>
spin_wait_task<T &&> spin_wait_promise<T &&>::get_return_object() {
  return spin_wait_task<T &&>{std::coroutine_handle<spin_wait_promise<T &&>>::from_promise(*this)};
}
inline spin_wait_task<void> spin_wait_promise<void>::get_return_object() {
  return spin_wait_task<void>{std::coroutine_handle<spin_wait_promise<void>>::from_promise(*this)};
}

template <typename Awaitable, typename ResultType>
spin_wait_task<ResultType> make_spin_wait_task(Awaitable awaitable) {
  if constexpr (std::is_void_v<ResultType>) {
    co_await std::move(awaitable);
  } else {
    co_return co_await std::move(awaitable);
  }
}

template <typename Awaitable>
auto spin_wait(Awaitable &&awaitable) {
  using result_type = typename cppcoro::awaitable_traits<Awaitable>::await_result_t;

  auto task = make_spin_wait_task<Awaitable, result_type>(std::forward<Awaitable>(awaitable));

  task.start();

  while (!task.done()) {
    cpu_relax();
  }

  return task.result();
}

} // namespace coverbs_rpc::utils