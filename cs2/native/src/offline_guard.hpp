#pragma once
#include <span>
#include <string>
#include <string_view>

namespace cc {
bool offline_lab_allowed(std::wstring_view executable, std::span<const std::wstring> arguments);
bool has_argument(std::span<const std::wstring> arguments, std::wstring_view wanted);
}
