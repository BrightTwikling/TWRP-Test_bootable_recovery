#ifndef GUI2_BACKEND_TWRP_OPERATION_H
#define GUI2_BACKEND_TWRP_OPERATION_H

#include <string>

namespace gui2_backend {

// Shared recovery job lifecycle: busy flag, progress reset, performance
// workload and completion vibration. Backend status owns the result.
void operation_start(const std::string& operation_name);
void operation_end(int operation_status);

// operation_end also wakes the screen through the GUI2 screen backend. The
// jobs end on worker threads, so the UI thread picks that up here.
bool take_operation_ended();

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_OPERATION_H
