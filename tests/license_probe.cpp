#include "LicenseClient.h"
#include "vendor/json.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>

using nlohmann::json;
int report(const phoenix::license::Result& result) {
    std::cout << json{{"ok", result.ok}, {"retryable", result.retryable},
        {"message", result.message}, {"expires", result.expiresUtc}}.dump() << '\n';
    return result.ok ? 0 : 6;
}
int main(int argc, char** argv) {
    if (argc < 2) return 7;
    std::string action = argv[1];
    if (action == "device") { std::cout << phoenix::license::Client::DeviceId(); return 0; }
    if (argc < 3) return 7;
    if (action == "verify") {
        std::ifstream file(argv[2]);
        auto fixtures = json::parse(file);
        for (auto& fixture : fixtures) {
            auto result = phoenix::license::VerifyLease(fixture["token"], fixture["nonce"],
                fixture["device"], fixture["public"], fixture["start"], fixture["now"]);
            if (result.ok != fixture["expected"].get<bool>()) { std::cerr << fixture["name"]; return 1; }
        }
        std::cout << fixtures.size() << " native signature/claim checks passed.\n";
        return 0;
    }
    if (action == "deny-dll") {
        std::wstring path = std::filesystem::absolute(argv[2]).wstring();
        HMODULE module = LoadLibraryW(path.c_str());
        if (!module) return 8;
        for (int i = 0; i < 100; ++i) {
            Sleep(100);
            if (!GetModuleHandleW(path.c_str())) { std::cout << "Unlicensed DLL unloaded before game initialization.\n"; return 0; }
        }
        return 9;
    }
    phoenix::license::Client client(argv[2]);
    if (action == "activate") {
        std::string code;
        std::getline(std::cin, code);
        return report(client.Activate(code));
    }
    if (action == "check") return report(client.Refresh());
    if (action == "watch" && argc == 4) {
        if (!phoenix::license::Start(argv[2])) return report(phoenix::license::Status());
        const auto end = GetTickCount64() + std::stoul(argv[3]) * 1000ULL;
        while (GetTickCount64() < end && phoenix::license::IsAuthorized()) Sleep(20);
        auto result = phoenix::license::Status();
        phoenix::license::Stop();
        return report(result);
    }
    return 7;
}
