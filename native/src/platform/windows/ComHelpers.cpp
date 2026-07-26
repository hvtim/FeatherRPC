#include "ComHelpers.h"

namespace platform_windows {

std::optional<AutoVariant> GetDispatchProperty(IDispatch* dispatch, const wchar_t* name) {
    if (!dispatch) {
        return std::nullopt;
    }

    DISPID dispid;
    LPOLESTR nameCopy = const_cast<LPOLESTR>(name); // GetIDsOfNames never modifies it despite the non-const signature
    if (FAILED(dispatch->GetIDsOfNames(IID_NULL, &nameCopy, 1, LOCALE_USER_DEFAULT, &dispid))) {
        return std::nullopt;
    }

    AutoVariant result;
    DISPPARAMS params{};
    if (FAILED(dispatch->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &params, &result.value,
            nullptr, nullptr))) {
        return std::nullopt;
    }

    return result;
}

std::string BstrToUtf8(BSTR bstr) {
    if (!bstr) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(len - 1), '\0'); // len includes the null terminator
    WideCharToMultiByte(CP_UTF8, 0, bstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

std::string VariantToUtf8String(const VARIANT& v) {
    if (v.vt == VT_BSTR) {
        return BstrToUtf8(v.bstrVal);
    }
    return {};
}

int VariantToInt(const VARIANT& v) {
    switch (v.vt) {
        case VT_I4: return v.lVal;
        case VT_I2: return v.iVal;
        case VT_R8: return static_cast<int>(v.dblVal);
        case VT_R4: return static_cast<int>(v.fltVal);
        default: return 0;
    }
}

double VariantToDouble(const VARIANT& v) {
    switch (v.vt) {
        case VT_R8: return v.dblVal;
        case VT_R4: return v.fltVal;
        case VT_I4: return v.lVal;
        case VT_I2: return v.iVal;
        default: return 0.0;
    }
}

} // namespace platform_windows
