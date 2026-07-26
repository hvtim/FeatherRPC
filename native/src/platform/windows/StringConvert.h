#pragma once

#include <string>
#include <string_view>

namespace platform_windows {

std::string NarrowFromWide(std::wstring_view wide);
std::wstring WideFromNarrow(std::string_view narrow);

} // namespace platform_windows
