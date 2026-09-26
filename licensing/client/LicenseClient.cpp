#include "LicenseClient.h"
#if PHOENIX_LICENSE_ENABLED
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <LicensePublicKey.h>
#include "vendor/json.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

namespace phoenix::license {
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
constexpr size_t MaxResponse = 16384;
constexpr uint64_t MaxLeaseSeconds = 600;

struct Internet {
    HINTERNET value = nullptr;
    explicit Internet(HINTERNET handle) : value(handle) {}
    ~Internet() { if (value) WinHttpCloseHandle(value); }
    operator HINTERNET() const { return value; }
};
struct Algorithm {
    BCRYPT_ALG_HANDLE value = nullptr;
    explicit Algorithm(LPCWSTR name) {
        if (BCryptOpenAlgorithmProvider(&value, name, nullptr, 0) < 0)
            throw std::runtime_error("crypto_unavailable");
    }
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};
struct Key {
    BCRYPT_KEY_HANDLE value = nullptr;
    ~Key() { if (value) BCryptDestroyKey(value); }
};
struct LocalBlob {
    DATA_BLOB value{};
    ~LocalBlob() {
        if (value.pbData) { SecureZeroMemory(value.pbData, value.cbData); LocalFree(value.pbData); }
    }
};

bool IsHex(const std::string& value, size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
Bytes Unhex(const std::string& text) {
    if (!IsHex(text, text.size()) || text.size() % 2) throw std::runtime_error("bad_hex");
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    Bytes result(text.size() / 2);
    for (size_t i = 0; i < result.size(); ++i) result[i] = (digit(text[i*2]) << 4) | digit(text[i*2+1]);
    return result;
}
std::string Hex(const unsigned char* data, size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(size*2, '0');
    for (size_t i = 0; i < size; ++i) { result[i*2] = digits[data[i] >> 4]; result[i*2+1] = digits[data[i] & 15]; }
    return result;
}
Bytes Hash(const std::string& text) {
    Algorithm algorithm(BCRYPT_SHA256_ALGORITHM);
    Bytes result(32);
    if (BCryptHash(algorithm.value, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())), static_cast<ULONG>(text.size()),
        result.data(), static_cast<ULONG>(result.size())) < 0) throw std::runtime_error("hash_failed");
    return result;
}
std::string Nonce() {
    std::array<unsigned char, 32> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("random_failed");
    return Hex(bytes.data(), bytes.size());
}
Bytes Base64Url(const std::string& text) {
    if (text.empty() || text.size() > MaxResponse || text.size() % 4 == 1) throw std::runtime_error("bad_base64");
    std::string encoded = text;
    for (char& c : encoded) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            throw std::runtime_error("bad_base64");
    }
    while (encoded.size() % 4) encoded += '=';
    DWORD size = 0;
    if (!CryptStringToBinaryA(encoded.c_str(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr))
        throw std::runtime_error("bad_base64");
    Bytes result(size);
    if (!CryptStringToBinaryA(encoded.c_str(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, result.data(), &size, nullptr, nullptr))
        throw std::runtime_error("bad_base64");
    result.resize(size);
    return result;
}
Json Parse(const std::string& text) {
    if (text.size() > MaxResponse) throw std::runtime_error("response_too_large");
    std::vector<std::set<std::string>> keys;
    return Json::parse(text, [&](int depth, Json::parse_event_t event, Json& parsed) {
        if (depth > 8) throw std::runtime_error("json_depth");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key && !keys.back().insert(parsed.get<std::string>()).second)
            throw std::runtime_error("duplicate_field");
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}
int64_t Integer(const Json& object, const char* key) {
    const auto& field = object.at(key);
    if (!field.is_number_integer() || (field.is_number_unsigned() && field.get<uint64_t>() > INT64_MAX))
        throw std::runtime_error("invalid_integer");
    return field.get<int64_t>();
}
std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("file_unavailable");
    std::array<char, MaxResponse + 1> buffer{};
    input.read(buffer.data(), buffer.size());
    const size_t count = static_cast<size_t>(input.gcount());
    if (count > MaxResponse) throw std::runtime_error("file_too_large");
    return std::string(buffer.data(), count);
}
std::string ServiceUrl(const std::filesystem::path& directory) {
    auto config = directory / "license-server.json";
    if (!std::filesystem::exists(config)) throw std::runtime_error("missing_service_config");
    auto settings = Parse(ReadFile(config));
    if (!settings.is_object() || settings.size() != 1) throw std::runtime_error("invalid_service_config");
    std::string url = settings.at("url").get<std::string>();
    if (url.empty() || url.size() > 2048 || !std::all_of(url.begin(), url.end(), [](unsigned char c) { return c >= 33 && c < 127; }))
        throw std::runtime_error("invalid_service_url");
    return url;
}
std::string Entropy(const std::string& url) {
    return std::string("phoenix/session/v1/") + PHOENIX_LICENSE_PUBLIC_KEY_HEX + "/" + url;
}
std::string LoadSession(const std::filesystem::path& directory, const std::string& url) {
    auto file = directory / ".license" / "session.bin";
    if (!std::filesystem::exists(file)) return {};
    std::string protectedData = ReadFile(file), entropy = Entropy(url);
    DATA_BLOB input{static_cast<DWORD>(protectedData.size()), reinterpret_cast<BYTE*>(protectedData.data())};
    DATA_BLOB salt{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())};
    LocalBlob output;
    if (!CryptUnprotectData(&input, nullptr, &salt, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.value))
        throw std::runtime_error("cached_session_unavailable");
    std::string result(reinterpret_cast<char*>(output.value.pbData), output.value.cbData);
    if (!IsHex(result, 64)) throw std::runtime_error("invalid_cached_session");
    return result;
}
void SaveSession(const std::filesystem::path& directory, const std::string& url, std::string session) {
    std::string entropy = Entropy(url);
    DATA_BLOB input{static_cast<DWORD>(session.size()), reinterpret_cast<BYTE*>(session.data())};
    DATA_BLOB salt{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())};
    LocalBlob output;
    bool encrypted = CryptProtectData(&input, L"phoenix session", &salt, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.value) != FALSE;
    SecureZeroMemory(session.data(), session.size());
    if (!encrypted) throw std::runtime_error("session_save_failed");
    auto folder = directory / ".license";
    std::filesystem::create_directories(folder);
    auto target = folder / "session.bin";
    auto temporary = folder / ("session-" + Nonce() + ".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<char*>(output.value.pbData), output.value.cbData);
        file.close();
        if (!file) { std::filesystem::remove(temporary); throw std::runtime_error("session_save_failed"); }
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("session_save_failed");
    }
}
struct Response { DWORD status; std::string body; };
Response Post(const std::string& url, const wchar_t* path, const std::string& body) {
    std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) throw std::runtime_error("invalid_service_url");
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring basePath(parts.lpszUrlPath ? parts.lpszUrlPath : L"", parts.dwUrlPathLength);
    bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    bool localHttp = false;
#if PHOENIX_LICENSE_LOCAL
    localHttp = parts.nScheme == INTERNET_SCHEME_HTTP && host == L"127.0.0.1";
#endif
    if ((!secure && !localHttp) || host.empty() || parts.dwUserNameLength || parts.dwPasswordLength ||
        parts.dwExtraInfoLength || (!basePath.empty() && basePath != L"/")) throw std::runtime_error("unsafe_service_url");
    Internet session(WinHttpOpen(L"phoenix-license/1", localHttp ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value || !WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000)) throw std::runtime_error("network_error");
    Internet connect(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
    if (!connect.value) throw std::runtime_error("network_error");
    Internet request(WinHttpOpenRequest(connect, L"POST", path, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request.value) throw std::runtime_error("network_error");
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects))) throw std::runtime_error("network_error");
    if (!WinHttpSendRequest(request, L"Content-Type: application/json\r\n", static_cast<DWORD>(-1),
        const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request, nullptr)) throw std::runtime_error("network_error");
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("network_error");
    std::string result;
    char buffer[2048];
    DWORD received;
    const auto started = GetTickCount64();
    for (;;) {
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &received)) throw std::runtime_error("network_error");
        if (!received) break;
        result.append(buffer, received);
        if (result.size() > MaxResponse || GetTickCount64() - started > 10000) throw std::runtime_error("invalid_response");
    }
    return {status, std::move(result)};
}

std::atomic<uint64_t> deadline{0};
std::atomic<bool> stopped{true};
std::atomic<void(*)()> expirationCallback{nullptr};
std::mutex stateMutex, waitMutex;
std::condition_variable wake;
Result current;
std::thread worker;
void Publish(const Result& result) {
    { std::lock_guard lock(stateMutex); current = result; }
    deadline.store(result.ok ? result.deadline : 0, std::memory_order_release);
}
}

Result VerifyLease(const std::string& token, const std::string& nonce, const std::string& device,
    const std::string& publicKeyHex, uint64_t requestStarted, uint64_t receivedAt) {
    try {
        if (token.size() > MaxResponse || !IsHex(publicKeyHex, 128) || receivedAt < requestStarted) throw std::runtime_error("invalid_lease");
        const auto first = token.find('.'), second = token.find('.', first == std::string::npos ? 0 : first + 1);
        if (first == std::string::npos || second == std::string::npos || token.find('.', second + 1) != std::string::npos) throw std::runtime_error("invalid_lease");
        auto headerBytes = Base64Url(token.substr(0, first));
        auto header = Parse(std::string(headerBytes.begin(), headerBytes.end()));
        if (header != Json({{"alg","ES256"},{"typ","JWT"},{"kid","phoenix-v1"}})) throw std::runtime_error("invalid_lease");
        auto signature = Base64Url(token.substr(second + 1));
        if (signature.size() != 64) throw std::runtime_error("invalid_lease");
        auto keyBytes = Unhex(publicKeyHex);
        BCRYPT_ECCKEY_BLOB keyHeader{ BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32 };
        Bytes blob(sizeof(keyHeader) + keyBytes.size());
        memcpy(blob.data(), &keyHeader, sizeof(keyHeader));
        memcpy(blob.data() + sizeof(keyHeader), keyBytes.data(), keyBytes.size());
        Algorithm algorithm(BCRYPT_ECDSA_P256_ALGORITHM);
        Key key;
        if (BCryptImportKeyPair(algorithm.value, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key.value,
            blob.data(), static_cast<ULONG>(blob.size()), 0) < 0) throw std::runtime_error("invalid_lease");
        auto hash = Hash(token.substr(0, second));
        if (BCryptVerifySignature(key.value, nullptr, hash.data(), static_cast<ULONG>(hash.size()),
            signature.data(), static_cast<ULONG>(signature.size()), 0) < 0) throw std::runtime_error("invalid_lease");
        auto payload = Base64Url(token.substr(first + 1, second - first - 1));
        auto claims = Parse(std::string(payload.begin(), payload.end()));
        if (claims.at("product") != "phoenix" || claims.at("nonce") != nonce || claims.at("device") != device ||
            Integer(claims, "version") != 1 || !IsHex(claims.at("license_id").get<std::string>(), 32) ||
            !IsHex(claims.at("session_id").get<std::string>(), 32)) throw std::runtime_error("invalid_lease");
        const auto serverTime = Integer(claims, "server_time"), expires = Integer(claims, "exp"), licenseExpires = Integer(claims, "license_expires_at");
        const auto serverTimeMs = Integer(claims, "server_time_ms");
        if (serverTime <= 0 || serverTime != Integer(claims, "iat") || expires <= serverTime ||
            serverTimeMs <= 0 || serverTimeMs / 1000 != serverTime ||
            expires - serverTime > MaxLeaseSeconds || licenseExpires < expires) throw std::runtime_error("invalid_lease");
        const uint64_t duration = static_cast<uint64_t>(expires - serverTime) * 1000 - serverTimeMs % 1000;
        if (receivedAt - requestStarted >= duration || requestStarted > UINT64_MAX - duration) throw std::runtime_error("expired_response");
        return { true, false, "authorized", requestStarted + duration, licenseExpires, claims.at("license_id").get<std::string>() };
    } catch (...) { return {false, false, "invalid_or_expired_lease"}; }
}

Client::Client(const std::filesystem::path& directory) : directory_(directory) {}
std::string Client::DeviceId() {
    wchar_t machine[256]{};
    DWORD size = sizeof(machine);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, machine, &size) != ERROR_SUCCESS)
        throw std::runtime_error("device_id_unavailable");
    std::string input = "phoenix/device/v1/";
    for (wchar_t c : std::wstring(machine)) {
        if (c > 127) throw std::runtime_error("device_id_unavailable");
        input += static_cast<char>(c);
    }
    auto hash = Hash(input);
    return Hex(hash.data(), hash.size());
}
Result Client::Activate(const std::string& code) { return Request(code); }
Result Client::Refresh() { return Request({}); }
Result Client::Request(const std::string& code) {
    try {
        auto url = ServiceUrl(directory_);
        const std::string nonce = Nonce(), device = DeviceId();
        Json body{{"product","phoenix"},{"nonce",nonce},{"device",device}};
        if (!code.empty()) body["code"] = code;
        else {
            auto session = LoadSession(directory_, url);
            if (session.empty()) return {false, false, "activation_required"};
            body["session"] = session;
            SecureZeroMemory(session.data(), session.size());
        }
        const auto started = GetTickCount64();
        auto response = Post(url, code.empty() ? L"/v1/session/refresh" : L"/v1/activate", body.dump());
        auto result = Parse(response.body);
        if (response.status != 200) {
            const std::set<std::string> known{"invalid_code","invalid_session","device_mismatch","revoked","expired","clock_rollback","bad_request"};
            auto error = result.value("error", "service_error");
            // Service rollback is a hard denial, not a reason to extend a cached lease.
            return {false, response.status >= 500 && error != "clock_rollback", known.contains(error) ? error : "service_error"};
        }
        auto verified = VerifyLease(result.at("lease").get<std::string>(), nonce, device,
            PHOENIX_LICENSE_PUBLIC_KEY_HEX, started, GetTickCount64());
        if (verified.ok && !code.empty()) {
            auto session = result.at("session").get<std::string>();
            if (!IsHex(session, 64)) return {false, false, "invalid_response"};
            SaveSession(directory_, url, session);
            SecureZeroMemory(session.data(), session.size());
        }
        return verified;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message == "network_error") return {false, true, "service_unavailable"};
        const std::set<std::string> known{"missing_service_config","unsafe_service_url","invalid_service_url","session_save_failed","cached_session_unavailable","device_id_unavailable"};
        return {false, false, known.contains(message) ? message : "invalid_configuration_or_response"};
    }
}

bool IsAuthorized() { return GetTickCount64() < deadline.load(std::memory_order_acquire); }
Result Status() {
    std::lock_guard lock(stateMutex);
    Result result = current;
    if (result.ok && !IsAuthorized()) { result.ok = false; result.message = "lease_expired"; }
    return result;
}
bool Start(const std::filesystem::path& directory) {
    Stop();
    auto initial = Client(directory).Refresh();
    Publish(initial);
    if (!initial.ok) return false;
    stopped.store(false);
    try {
        worker = std::thread([directory] {
            Client client(directory);
            uint64_t nextRefresh = 0, nextCallback = 0;
            while (!stopped.load()) {
                const auto now = GetTickCount64();
                if (!IsAuthorized()) {
                    if (now >= nextCallback) {
                        if (auto callback = expirationCallback.load()) callback();
                        nextCallback = now + 1000;
                    }
                } else if (now >= nextRefresh) {
                    auto result = client.Refresh();
                    if (stopped.load()) break;
                    if (result.ok || !result.retryable) Publish(result);
                    const auto currentDeadline = deadline.load(), currentTime = GetTickCount64();
                    const auto remaining = currentDeadline > currentTime ? currentDeadline - currentTime : 0;
                    nextRefresh = GetTickCount64() + (result.ok ? std::clamp<uint64_t>(remaining / 3, 500, 180000) : 1000);
                }
                std::unique_lock lock(waitMutex);
                wake.wait_for(lock, std::chrono::milliseconds(100), [] { return stopped.load(); });
            }
        });
    } catch (...) {
        stopped.store(true);
        Publish({false, false, "license_worker_failed"});
        return false;
    }
    return true;
}
void SetExpirationCallback(void (*callback)()) { expirationCallback.store(callback); }
void Stop() {
    stopped.store(true);
    expirationCallback.store(nullptr);
    wake.notify_all();
    if (worker.joinable()) worker.join();
    deadline.store(0);
}
}
#endif
