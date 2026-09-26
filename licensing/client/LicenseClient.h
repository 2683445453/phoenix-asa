#pragma once
#ifndef PHOENIX_LICENSE_ENABLED
#define PHOENIX_LICENSE_ENABLED 0
#endif

#include <cstdint>
#include <string>
#include <filesystem>

namespace phoenix::license {
    inline constexpr bool Enabled = PHOENIX_LICENSE_ENABLED != 0;
    struct Result {
        bool ok = false;
        bool retryable = false;
        std::string message;
        uint64_t deadline = 0;
        int64_t expiresUtc = 0;
        std::string licenseId;
    };

#if PHOENIX_LICENSE_ENABLED
    // A fresh server response is mandatory in each process. Cached credentials
    // authenticate requests; they never constitute an offline authorization.
    class Client {
    public:
        explicit Client(const std::filesystem::path& directory);
        Result Activate(const std::string& code);
        Result Refresh();
        static std::string DeviceId();
    private:
        Result Request(const std::string& code);
        std::filesystem::path directory_;
    };
    Result VerifyLease(const std::string& token, const std::string& nonce,
        const std::string& device, const std::string& publicKeyHex,
        uint64_t requestStarted, uint64_t receivedAt);
    bool IsAuthorized();
    Result Status();
    bool Start(const std::filesystem::path& directory);
    void SetExpirationCallback(void (*callback)());
    void Stop();
#else
    inline bool IsAuthorized() { return true; }
    inline Result Status() { return { true, false, "Development build: licensing disabled." }; }
    inline void Stop() {}
#endif
}
