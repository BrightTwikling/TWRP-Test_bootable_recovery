#include "twrp_operation_executor.h"

twrp_operation_executor& twrp_operation_executor::instance() {
  static twrp_operation_executor executor;
  return executor;
}
