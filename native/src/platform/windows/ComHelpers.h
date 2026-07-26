#pragma once

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <optional>
#include <string>

namespace platform_windows {

// RAII wrapper for VARIANT - VariantClear releases any owned BSTR/IDispatch.
struct AutoVariant {
    VARIANT value;

    AutoVariant() { VariantInit(&value); }
    ~AutoVariant() { VariantClear(&value); }

    AutoVariant(const AutoVariant&) = delete;
    AutoVariant& operator=(const AutoVariant&) = delete;

    AutoVariant(AutoVariant&& other) noexcept {
        value = other.value;
        VariantInit(&other.value);
    }
    AutoVariant& operator=(AutoVariant&& other) noexcept {
        if (this != &other) {
            VariantClear(&value);
            value = other.value;
            VariantInit(&other.value);
        }
        return *this;
    }
};

// Late-bound COM property-get (GetIDsOfNames + Invoke) - no type library
// or #import needed, matching how the original C# code used `dynamic`
// (IDispatch under the hood) instead of a generated interop assembly.
std::optional<AutoVariant> GetDispatchProperty(IDispatch* dispatch, const wchar_t* name);

std::string BstrToUtf8(BSTR bstr);
std::string VariantToUtf8String(const VARIANT& v);
int VariantToInt(const VARIANT& v);
double VariantToDouble(const VARIANT& v);

} // namespace platform_windows
