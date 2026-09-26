#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <thread>
#include <tlhelp32.h>
#include <vector>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <system_error>
#include <ctime>
#include "LicenseClient.h"
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "psapi.lib")

namespace fs = std::filesystem;

const char* DLL_NAME = "phoenix.dll";
const char* PENDING_DLL_NAME = "phoenix.dll.pending";
const char* TARGET_PROCESS = "ArkAscended.exe";

namespace {
    HANDLE g_LogFile = INVALID_HANDLE_VALUE;
    std::mutex g_LogMutex;
    fs::path g_LogPath;

    std::string CurrentTimestamp() {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        std::ostringstream stream;
        stream << std::setfill('0')
            << std::setw(4) << time.wYear << '-'
            << std::setw(2) << time.wMonth << '-'
            << std::setw(2) << time.wDay << ' '
            << std::setw(2) << time.wHour << ':'
            << std::setw(2) << time.wMinute << ':'
            << std::setw(2) << time.wSecond << '.'
            << std::setw(3) << time.wMilliseconds;
        return stream.str();
    }

    std::string LogFileTimestamp() {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        std::ostringstream stream;
        stream << std::setfill('0')
            << std::setw(4) << time.wYear
            << std::setw(2) << time.wMonth
            << std::setw(2) << time.wDay << '_'
            << std::setw(2) << time.wHour
            << std::setw(2) << time.wMinute
            << std::setw(2) << time.wSecond;
        return stream.str();
    }

    std::string FormatWin32Error(DWORD errorCode) {
        if (errorCode == ERROR_SUCCESS) return "no Win32 error";
        LPWSTR messageBuffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&messageBuffer), 0, nullptr);
        std::wstring wideMessage = length && messageBuffer
            ? std::wstring(messageBuffer, length)
            : L"unable to format this Win32 error";
        if (messageBuffer) LocalFree(messageBuffer);
        while (!wideMessage.empty() &&
            (wideMessage.back() == L'\r' || wideMessage.back() == L'\n' ||
                wideMessage.back() == L' ')) {
            wideMessage.pop_back();
        }
        const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideMessage.data(),
            static_cast<int>(wideMessage.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0) return "unable to encode this Win32 error";
        std::string message(static_cast<size_t>(utf8Length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideMessage.data(),
            static_cast<int>(wideMessage.size()), message.data(), utf8Length,
            nullptr, nullptr);
        return message;
    }

    void WriteConsoleOnly(const std::string& text) {
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!output || output == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(output, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
    }

    void InitializePersistentLog() {
        std::error_code error;
        fs::create_directories("logs", error);
        const std::string filename = "phoenix_loader_" + LogFileTimestamp() + "_PID" +
            std::to_string(GetCurrentProcessId()) + ".log";
        g_LogPath = error ? fs::path(filename) : fs::path("logs") / filename;
        g_LogFile = CreateFileW(g_LogPath.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_LogFile == INVALID_HANDLE_VALUE) {
            const DWORD logError = GetLastError();
            WriteConsoleOnly("[!] Unable to create persistent log: " +
                FormatWin32Error(logError) + " (" + std::to_string(logError) + ")\n");
            return;
        }
    }

    void ClosePersistentLog() {
        std::scoped_lock lock(g_LogMutex);
        if (g_LogFile != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(g_LogFile);
            CloseHandle(g_LogFile);
            g_LogFile = INVALID_HANDLE_VALUE;
        }
    }
}

// --- 工具函数：控制台与持久化日志输出 ---
void WriteToConsoleBuffer(const std::string& text) {
    std::scoped_lock lock(g_LogMutex);
    WriteConsoleOnly(text);
    if (g_LogFile == INVALID_HANDLE_VALUE) return;

    std::string record = "[" + CurrentTimestamp() + "] " + text;
    if (record.empty() || record.back() != '\n') record.push_back('\n');
    DWORD written = 0;
    WriteFile(g_LogFile, record.c_str(), static_cast<DWORD>(record.size()),
        &written, nullptr);
    FlushFileBuffers(g_LogFile);
}

void LogWin32Failure(const std::string& stage, DWORD errorCode) {
    WriteToConsoleBuffer("[-] " + stage + " failed. Win32=" +
        std::to_string(errorCode) + " (" + FormatWin32Error(errorCode) + ")\n");
}

bool ApplyPendingDLL() {
    std::error_code error;
    if (!fs::exists(PENDING_DLL_NAME, error)) return true;
    WriteToConsoleBuffer("[*] A pending DLL update was found; applying it now...\n");
    if (MoveFileExA(PENDING_DLL_NAME, DLL_NAME,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        WriteToConsoleBuffer("[+] Pending DLL update applied successfully.\n");
        return true;
    }

    const DWORD moveError = GetLastError();
    LogWin32Failure("Apply pending DLL update", moveError);
    WriteToConsoleBuffer(
        "    Fully exit ArkAscended.exe, then start the loader again.\n");
    return false;
}

// --- 业务逻辑：查找 PID ---
DWORD FindProcessId(const char* procName) {
    DWORD processID = 0;
    while (processID == 0) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32);
            if (Process32First(hSnapshot, &pe32)) {
                do {
                    if (_stricmp(pe32.szExeFile, procName) == 0) {
                        processID = pe32.th32ProcessID;
                        break;
                    }
                } while (Process32Next(hSnapshot, &pe32));
            }
            CloseHandle(hSnapshot);
        }
        else {
            LogWin32Failure("CreateToolhelp32Snapshot", GetLastError());
        }
        if (processID == 0) {
            WriteToConsoleBuffer("Waiting for " + std::string(procName) + "...\n");
            Sleep(1000);
        }
    }
    return processID;
}

// --- 业务逻辑：注入 DLL ---
bool InjectDLL(DWORD procID) {
    constexpr DWORD RequiredAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    HANDLE hProcess = OpenProcess(RequiredAccess, FALSE, procID);
    if (!hProcess) {
        LogWin32Failure("OpenProcess", GetLastError());
        WriteToConsoleBuffer("    Check that loader and game use the same privilege level.\n");
        return false;
    }

    char dllPath[MAX_PATH]{};
    const DWORD pathLength = GetFullPathNameA(DLL_NAME, MAX_PATH, dllPath, NULL);
    if (pathLength == 0 || pathLength >= MAX_PATH) {
        const DWORD error = GetLastError();
        CloseHandle(hProcess);
        LogWin32Failure("GetFullPathNameA(DLL)", error);
        return false;
    }
    WriteToConsoleBuffer("[*] DLL path: " + std::string(dllPath) + "\n");

    LPVOID pDllPath = VirtualAllocEx(hProcess, NULL, strlen(dllPath) + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pDllPath) {
        const DWORD error = GetLastError();
        CloseHandle(hProcess);
        LogWin32Failure("VirtualAllocEx", error);
        return false;
    }

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(hProcess, pDllPath, dllPath, strlen(dllPath) + 1,
        &bytesWritten) || bytesWritten != strlen(dllPath) + 1) {
        const DWORD error = GetLastError();
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        LogWin32Failure("WriteProcessMemory", error);
        WriteToConsoleBuffer("    Bytes written: " + std::to_string(bytesWritten) +
            "/" + std::to_string(strlen(dllPath) + 1) + "\n");
        return false;
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (!kernel32) {
        const DWORD error = GetLastError();
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        LogWin32Failure("GetModuleHandleA(kernel32.dll)", error);
        return false;
    }
    LPVOID pLoadLibrary = GetProcAddress(kernel32, "LoadLibraryA");
    if (!pLoadLibrary) {
        const DWORD error = GetLastError();
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        LogWin32Failure("GetProcAddress(LoadLibraryA)", error);
        return false;
    }
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibrary, pDllPath, 0, NULL);
    if (!hThread) {
        const DWORD error = GetLastError();
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        LogWin32Failure("CreateRemoteThread", error);
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(hThread, 30000);
    if (waitResult != WAIT_OBJECT_0) {
        const DWORD error = waitResult == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
        CloseHandle(hThread);
        // The remote thread may still be reading this buffer after a timeout.
        // Leave the small allocation in place instead of freeing live memory.
        CloseHandle(hProcess);
        LogWin32Failure("WaitForSingleObject(remote LoadLibraryA)", error);
        return false;
    }

    DWORD remoteLoadResult = 0;
    if (!GetExitCodeThread(hThread, &remoteLoadResult)) {
        const DWORD error = GetLastError();
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        LogWin32Failure("GetExitCodeThread(remote LoadLibraryA)", error);
        return false;
    }
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    if (remoteLoadResult == 0) {
        WriteToConsoleBuffer(
            "[-] Remote LoadLibraryA returned NULL. The DLL was not loaded.\n"
            "    Likely causes: missing dependency, architecture mismatch, security block, or invalid DLL.\n");
        return false;
    }

    std::ostringstream result;
    result << "[+] Remote LoadLibraryA returned 0x" << std::hex << std::uppercase
        << remoteLoadResult << ".\n";
    WriteToConsoleBuffer(result.str());
    return true;
}

#if PHOENIX_LICENSE_ENABLED
std::string ReadActivationCode() {
    WriteToConsoleBuffer("请输入激活码（输入不回显）：");
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD previous = 0;
    const bool console = GetConsoleMode(input, &previous) != FALSE;
    if (console) SetConsoleMode(input, previous & ~ENABLE_ECHO_INPUT);
    char buffer[128]{};
    std::cin.getline(buffer, sizeof(buffer));
    if (console) SetConsoleMode(input, previous);
    WriteConsoleOnly("\n");
    std::string result = std::cin ? buffer : "";
    SecureZeroMemory(buffer, sizeof(buffer));
    return result;
}

void PrintLicenseResult(const phoenix::license::Result& result) {
    if (!result.ok) {
        const std::pair<const char*, const char*> explanations[] = {
            {"activation_required", "尚未激活"}, {"invalid_code", "激活码不正确"},
            {"expired", "使用期限已到"}, {"lease_expired", "授权凭证已到期"},
            {"revoked", "此授权已被停用"}, {"invalid_session", "会话已失效，请重新激活"},
            {"device_mismatch", "激活码已绑定其他设备"}, {"service_unavailable", "无法连接授权服务"},
            {"clock_rollback", "授权服务检测到系统时间回拨"},
            {"missing_service_config", "缺少授权服务地址配置"},
            {"invalid_or_expired_lease", "授权签名无效或响应已过期"},
            {"cached_session_unavailable", "无法读取当前用户的授权缓存"}
        };
        std::string explanation = result.message;
        for (const auto& entry : explanations) if (result.message == entry.first) { explanation = entry.second; break; }
        WriteToConsoleBuffer("[-] 授权验证失败：" + explanation +
            "\n    请检查本地授权服务，或使用 --activate 重新激活。\n");
        return;
    }
    __time64_t expires = result.expiresUtc;
    tm local{};
    char date[64]{};
    if (_localtime64_s(&local, &expires) == 0) strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &local);
    WriteToConsoleBuffer("[+] 授权有效，到期时间（本机时区）：" + std::string(date) + "\n");
}
#endif

// --- 主程序 ---
int main(int argc, char* argv[]) {
	SetConsoleTitleW(L"phoenix");
    SetConsoleOutputCP(CP_UTF8);
    char loaderPath[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, loaderPath, MAX_PATH) > 0) {
        fs::path loaderDirectory = fs::path(loaderPath).parent_path();
        SetCurrentDirectoryA(loaderDirectory.string().c_str());
    }
    InitializePersistentLog();
    struct LogGuard {
        ~LogGuard() { ClosePersistentLog(); }
    } logGuard;
    if (g_LogFile != INVALID_HANDLE_VALUE) {
        WriteToConsoleBuffer("[*] Persistent log: " + g_LogPath.string() + "\n");
    }
    else {
        WriteToConsoleBuffer("[!] Persistent logging is unavailable for this run.\n");
    }
    WriteToConsoleBuffer("[*] Loader PID: " + std::to_string(GetCurrentProcessId()) + "\n");

    const std::vector<std::string> args(argv + 1, argv + argc);
    const auto hasArg = [&args](const std::string& value) {
        return std::find(args.begin(), args.end(), value) != args.end();
    };

    if (hasArg("--help") || hasArg("-h")) {
        WriteToConsoleBuffer(
            "Usage: phoenix.exe [--check] [--help]\n"
            "  (no option)      Use the local DLL and wait for ArkAscended.exe.\n"
            "  --update         Unavailable until a phoenix update service is configured.\n"
            "  --download-only  Unavailable until a phoenix update service is configured.\n"
            "  --check          Verify that the local DLL exists and exit.\n");
#if PHOENIX_LICENSE_ENABLED
        WriteToConsoleBuffer(
            "  --activate       输入激活码并保存加密会话，不加载游戏。\n"
            "  --license-check  联网验证授权并显示到期时间，不加载游戏。\n"
            "  --license-watch SECONDS  在指定秒数内持续验证，到期时退出。\n"
            "  本版本用于本地授权验证，尚未加壳。\n");
#else
        WriteToConsoleBuffer("  Development build: licensing is disabled.\n");
#endif
        return 0;
    }

    if (hasArg("--update") || hasArg("--download-only")) {
        WriteToConsoleBuffer(
            "[-] No phoenix update service is configured.\n"
            "    Install a complete phoenix package from your software provider.\n");
        return 5;
    }

    if (!hasArg("--check") && !ApplyPendingDLL()) {
        return 4;
    }

    if (!fs::exists(DLL_NAME) || !fs::is_regular_file(DLL_NAME)) {
        WriteToConsoleBuffer("[-] Local DLL not found next to phoenix.exe.\n");
        WriteToConsoleBuffer("    Place phoenix.exe and phoenix.dll from the same package together.\n");
        return 2;
    }

    if (hasArg("--check")) {
        std::error_code fileError;
        const auto dllSize = fs::file_size(DLL_NAME, fileError);
        WriteToConsoleBuffer("[+] Local DLL is present" +
            (fileError ? std::string(".\n") :
                " (" + std::to_string(dllSize) + " bytes).\n"));
        return 0;
    }

#if PHOENIX_LICENSE_ENABLED
    const auto licenseDirectory = fs::current_path();
    phoenix::license::Client licenseClient(licenseDirectory);
    if (hasArg("--activate")) {
        auto code = ReadActivationCode();
        if (code.empty()) return 6;
        auto result = licenseClient.Activate(code);
        SecureZeroMemory(code.data(), code.size());
        PrintLicenseResult(result);
        return result.ok ? 0 : 6;
    }
    if (hasArg("--license-watch")) {
        auto position = std::find(args.begin(), args.end(), "--license-watch");
        unsigned long seconds = 0;
        try {
            if (++position == args.end() || position->empty() || position->find_first_not_of("0123456789") != std::string::npos) return 7;
            seconds = std::stoul(*position);
            if (seconds < 1 || seconds > 3600) return 7;
        } catch (...) { return 7; }
        if (!phoenix::license::Start(licenseDirectory)) { PrintLicenseResult(phoenix::license::Status()); return 6; }
        PrintLicenseResult(phoenix::license::Status());
        const auto end = GetTickCount64() + seconds * 1000ULL;
        while (GetTickCount64() < end && phoenix::license::IsAuthorized()) Sleep(100);
        const auto result = phoenix::license::Status();
        phoenix::license::Stop();
        PrintLicenseResult(result);
        return result.ok ? 0 : 6;
    }
    auto authorization = licenseClient.Refresh();
    if (!hasArg("--license-check") && authorization.message == "activation_required") {
        auto code = ReadActivationCode();
        if (!code.empty()) authorization = licenseClient.Activate(code);
        SecureZeroMemory(code.data(), code.size());
    }
    PrintLicenseResult(authorization);
    if (!authorization.ok || hasArg("--license-check")) return authorization.ok ? 0 : 6;
#else
    if (hasArg("--activate") || hasArg("--license-check") || hasArg("--license-watch")) {
        WriteToConsoleBuffer("[-] This is a development build. Use phoenix-local-license for license validation.\n");
        return 7;
    }
#endif

    WriteToConsoleBuffer(">>> Using local DLL. Waiting for game...\n");
    DWORD procID = FindProcessId(TARGET_PROCESS);
#if PHOENIX_LICENSE_ENABLED
    authorization = licenseClient.Refresh();
    if (!authorization.ok) { PrintLicenseResult(authorization); return 6; }
#endif
    WriteToConsoleBuffer("[+] Target found! PID: " + std::to_string(procID) + "\n");

    WriteToConsoleBuffer("[*] Injecting DLL...\n");
    if (InjectDLL(procID)) {
        WriteToConsoleBuffer("[+] Injection successful!\n");
    }
    else {
        WriteToConsoleBuffer("[-] Injection failed!\n");
        WriteToConsoleBuffer(">>> Done. Exiting in 5s...\n");
        Sleep(5000);
        return 3;
    }

    WriteToConsoleBuffer(">>> Done. Exiting in 5s...\n");
    Sleep(5000);
    return 0;
}
