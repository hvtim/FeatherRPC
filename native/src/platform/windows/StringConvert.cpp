#include "StringConvert.h"

#include <windows.h>

namespace platform_windows {

std::string NarrowFromWide(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), len, nullptr, nullptr);
    return result;
}

std::wstring WideFromNarrow(std::string_view narrow) {
    if (narrow.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()), result.data(), len);
    return result;
}

} // namespace platform_windows
