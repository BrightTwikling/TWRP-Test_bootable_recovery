#ifndef TWRP_OPERATION_EXECUTOR_H
#define TWRP_OPERATION_EXECUTOR_H

#include <mutex>
#include <utility>

// Storage and crypto share one transaction domain. Execute on the caller's
// thread: moving nested callbacks to a worker changes existing lock ordering.
// Recursive acquisition permits PartitionManager -> TWPartition -> manager.
class twrp_operation_executor {
 public:
  static twrp_operation_executor& instance();
  using guard = std::lock_guard<std::recursive_mutex>;
  std::recursive_mutex& mutex() { return mutex_; }

  template <typename Function>
  decltype(auto) run(Function&& function) {
    guard lock(mutex_);
    return std::forward<Function>(function)();
  }

 private:
  std::recursive_mutex mutex_;
};

#endif  // TWRP_OPERATION_EXECUTOR_H
