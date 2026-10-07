#include "offline_guard.hpp"
#include <algorithm>
#include <cwctype>

namespace cc {
bool has_argument(std::span<const std::wstring> arguments, std::wstring_view wanted) {
    return std::find(arguments.begin(), arguments.end(), wanted) != arguments.end();
}
bool offline_lab_allowed(std::wstring_view executable, std::span<const std::wstring> arguments) {
    const auto slash = executable.find_last_of(L"/\\");
    std::wstring name(executable.substr(slash == std::wstring_view::npos ? 0 : slash + 1));
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
    return name == L"cs2.exe" && has_argument(arguments,L"-insecure")
        && has_argument(arguments,L"-countercraft-lab")
        && !has_argument(arguments,L"-secure") && !has_argument(arguments,L"-vulkan")
        && !has_argument(arguments,L"+connect");
}
}
