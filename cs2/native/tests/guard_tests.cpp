#include "offline_guard.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <Windows.h>

int wmain(int argc, wchar_t** argv) {
    try {
        auto allowed = [](std::wstring_view exe, std::initializer_list<std::wstring> args) {
            const std::vector<std::wstring> values(args); return cc::offline_lab_allowed(exe,values);
        };
        auto require = [](bool value) { if (!value) throw std::runtime_error("Offline guard test failed"); };
        require(allowed(L"D:\\game\\CS2.exe",{L"-insecure",L"-countercraft-lab"}));
        require(!allowed(L"cs2.exe",{L"-countercraft-lab"}));
        require(!allowed(L"cs2.exe",{L"-insecure"}));
        require(!allowed(L"cs2.exe",{L"-insecure-ish",L"-countercraft-lab"}));
        require(!allowed(L"cs2.exe",{L"text -insecure",L"-countercraft-lab"}));
        require(!allowed(L"notcs2.exe",{L"-insecure",L"-countercraft-lab"}));
        require(!allowed(L"cs2.exe",{L"-insecure",L"-countercraft-lab",L"-secure"}));
        require(!allowed(L"cs2.exe",{L"-insecure",L"-countercraft-lab",L"-vulkan"}));
        require(!allowed(L"cs2.exe",{L"-insecure",L"-countercraft-lab",L"+connect"}));
        require(argc == 2);
        const auto addon = LoadLibraryW(argv[1]);
        require(addon != nullptr);
        const auto init = reinterpret_cast<bool(*)(HMODULE,HMODULE)>(GetProcAddress(addon,"AddonInit"));
        const auto uninit = reinterpret_cast<void(*)(HMODULE,HMODULE)>(GetProcAddress(addon,"AddonUninit"));
        require(init && uninit);
        // Actual DLL initialization refuses this non-CS2 process without calling ReShade/network APIs.
        require(!init(addon,nullptr)); uninit(addon,nullptr);
        require(FreeLibrary(addon) != FALSE);
        std::cout << "{\"offlineGuardTests\":\"passed\",\"groups\":10}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
