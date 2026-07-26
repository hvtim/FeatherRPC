#include "ComInit.h"

#include <winrt/base.h>

namespace platform_windows {

void EnsureComInitialized() {
    thread_local bool initialized = false;
    if (initialized) {
        return;
    }
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    initialized = true;
}

} // namespace platform_windows
