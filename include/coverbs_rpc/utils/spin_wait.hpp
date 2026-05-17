#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <cppcoro/awaitable_traits.hpp>
#include <exception>
#include <type_traits>
#include <utility>

namespace coverbs_rpc::utils {

namespace detail {

inline void cpu_relax() noexcept {
#if defined(__i386__) || defined(__x86_64__)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
  __asm__ __volatile__("yield" ::: "memory");
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

struct SpinEvent {
  std::atomic<bool> ready_{false};
  void notify() noexcept { ready_.store(true, std::memory_order_release); }
  void wait() noexcept {
    if (ready_.load(std::memory_order_acquire))
      return;
    while (!ready_.load(std::memory_order_acquire)) {
      cpu_relax();
    }
  }
  bool is_ready() const noexcept { return ready_.load(std::memory_order_acquire); }
};

template <typename T>
struct spin_wait_task;

struct final_awaiter {
  constexpr bool await_ready() const noexcept { return false; }
  template <typename Promise>
  void await_suspend(std::coroutine_handle<Promise> h) noexcept {
    h.promise().event_.notify();
  }
  constexpr void await_resume() const noexcept {}
};

struct spin_promise_base {
  SpinEvent event_;
  std::exception_ptr exception_{nullptr};

  void unhandled_exception() { exception_ = std::current_exception(); }

  auto initial_suspend() noexcept { return std::suspend_always{}; }

  auto final_suspend() noexcept { return final_awaiter{}; }
};

template <typename T>
struct spin_wait_promise : spin_promise_base {
  union {
    T value_;
  };
  bool has_value_ = false;

  spin_wait_promise() noexcept {}
  ~spin_wait_promise() {
    if (has_value_)
      value_.~T();
  }

  spin_wait_task<T> get_return_object();

  template <typename U>
  void return_value(U &&v) {
    new (&value_) T(std::forward<U>(v));
    has_value_ = true;
  }

  T get_result() {
    if (exception_)
      std::rethrow_exception(exception_);
    return std::move(value_);
  }
};

template <typename T>
struct spin_wait_promise<T &&> : spin_promise_base {
  union {
    T value_;
  };
  bool has_value_ = false;

  spin_wait_promise() noexcept {}
  ~spin_wait_promise() {
    if (has_value_)
      value_.~T();
  }

  spin_wait_task<T &&> get_return_object();

  void return_value(T &&v) {
    new (&value_) T(std::move(v));
    has_value_ = true;
  }

  T &&get_result() {
    if (exception_)
      std::rethrow_exception(exception_);
    return std::move(value_);
  }
};

template <typename T>
struct spin_wait_promise<T &> : spin_promise_base {
  T *value_ptr_ = nullptr;

  spin_wait_task<T &> get_return_object();

  void return_value(T &v) { value_ptr_ = std::addressof(v); }

  T &get_result() {
    if (exception_)
      std::rethrow_exception(exception_);
    return *value_ptr_;
  }
};

template <>
struct spin_wait_promise<void> : spin_promise_base {
  spin_wait_task<void> get_return_object();
  void return_void() {}
  void get_result() {
    if (exception_)
      std::rethrow_exception(exception_);
  }
};

// ==========================================
// 3. Task Wrapper (RAII)
// ==========================================
template <typename T>
struct spin_wait_task {
  using promise_type = spin_wait_promise<T>;
  using handle_t = std::coroutine_handle<promise_type>;

  handle_t h_;

  explicit spin_wait_task(handle_t h)
      : h_(h) {}

  ~spin_wait_task() {
    if (h_) {
      h_.promise().event_.wait();
      h_.destroy();
    }
  }

  spin_wait_task(const spin_wait_task &) = delete;
  spin_wait_task(spin_wait_task &&other) noexcept
      : h_(std::exchange(other.h_, nullptr)) {}

  void start() { h_.resume(); }

  decltype(auto) get() {
    h_.promise().event_.wait();
    return h_.promise().get_result();
  }
};

template <typename T>
spin_wait_task<T> spin_wait_promise<T>::get_return_object() {
  return spin_wait_task<T>{std::coroutine_handle<spin_wait_promise<T>>::from_promise(*this)};
}
template <typename T>
spin_wait_task<T &&> spin_wait_promise<T &&>::get_return_object() {
  return spin_wait_task<T &&>{std::coroutine_handle<spin_wait_promise<T &&>>::from_promise(*this)};
}
template <typename T>
spin_wait_task<T &> spin_wait_promise<T &>::get_return_object() {
  return spin_wait_task<T &>{std::coroutine_handle<spin_wait_promise<T &>>::from_promise(*this)};
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

template <typename T>
struct safe_spin_result {
  using type = T;
};
template <typename T>
struct safe_spin_result<T &&> {
  using type = std::remove_reference_t<T>;
};
template <typename T>
using safe_spin_result_t = typename safe_spin_result<T>::type;

} // namespace detail

template <typename Awaitable>
auto spin_wait(Awaitable &&awaitable)
    -> detail::safe_spin_result_t<typename cppcoro::awaitable_traits<Awaitable>::await_result_t> {
  using result_type = typename cppcoro::awaitable_traits<Awaitable>::await_result_t;

  auto task =
      detail::make_spin_wait_task<Awaitable, result_type>(std::forward<Awaitable>(awaitable));
  task.start();
  // 这里的 decltype(auto) task.get() 会返回 result_type (即 T, T& 或 T&&)
  // 但是函数的返回类型已经被我们强制指定为 safe_spin_result_t
  // 如果 task.get() 返回 T&&，这里会隐式执行 T(T&&) 移动构造，
  // 在 task 析构之前完成数据的转移。
  return task.get();
}

} // namespace coverbs_rpc::utils