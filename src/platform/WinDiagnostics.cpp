#include "WinDiagnostics.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#ifdef _WIN32
// Windows SDK extension headers require windows.h first.
// clang-format off
#include <windows.h>
#include <dxgi1_4.h>
#include <tlhelp32.h>
#include <winternl.h>
// clang-format on
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif
namespace drawborad {
namespace {
std::string quote(const std::string& s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            o << "\\\"";
            break;
        case '\\':
            o << "\\\\";
            break;
        case '\n':
            o << "\\n";
            break;
        case '\r':
            o << "\\r";
            break;
        case '\t':
            o << "\\t";
            break;
        default:
            if (c < 32)
                o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c)
                  << std::dec;
            else
                o << char(c);
        }
    }
    o << '"';
    return o.str();
}
#ifdef _WIN32
std::string utf8(const wchar_t* s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string r(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, r.data(), n, nullptr, nullptr);
    r.pop_back();
    return r;
}
std::string luid(LUID id) {
    std::ostringstream o;
    o << std::hex << std::setfill('0') << std::setw(8) << std::uint32_t(id.HighPart) << std::setw(8)
      << id.LowPart;
    return o.str();
}
#endif
} // namespace
DeviceReport collectDeviceReport() {
    DeviceReport r;
    r.logicalThreads = std::thread::hardware_concurrency();
#ifdef _WIN32
    using VersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto nt = GetModuleHandleW(L"ntdll.dll");
    auto version = reinterpret_cast<VersionFn>(GetProcAddress(nt, "RtlGetVersion"));
    RTL_OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (version && version(&v) == 0)
        r.osBuild = "Windows " + std::to_string(v.dwMajorVersion) + "." +
                    std::to_string(v.dwMinorVersion) + "." + std::to_string(v.dwBuildNumber);
    else
        r.warnings.push_back("os.version-unavailable");
    wchar_t cpu[512]{};
    DWORD n = sizeof(cpu);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu, &n) == ERROR_SUCCESS)
        r.cpu = utf8(cpu);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        r.ram = memory.ullTotalPhys;
    r.dpi = GetDpiForSystem();
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power))
        r.powerMode = power.ACLineStatus == 1   ? "AC"
                      : power.ACLineStatus == 0 ? "Battery"
                                                : "Unknown";
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(
            CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        for (UINT i = 0;; i++) {
            IDXGIAdapter1* a = nullptr;
            if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND)
                break;
            if (!a)
                break;
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(a->GetDesc1(&desc))) {
                LARGE_INTEGER d{};
                a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &d);
                r.gpuAdapters.push_back(
                    {utf8(desc.Description), luid(desc.AdapterLuid),
                     std::to_string(d.HighPart) + ":" + std::to_string(d.LowPart),
                     desc.DedicatedVideoMemory, (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0});
            }
            a->Release();
        }
        factory->Release();
    }
    auto cuda = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (cuda) {
        using Init = int(__stdcall*)(unsigned);
        using Count = int(__stdcall*)(int*);
        using Name = int(__stdcall*)(char*, int, int);
        using Luid = int(__stdcall*)(char*, unsigned*, int);
        using Driver = int(__stdcall*)(int*);
        auto init = reinterpret_cast<Init>(GetProcAddress(cuda, "cuInit"));
        auto count = reinterpret_cast<Count>(GetProcAddress(cuda, "cuDeviceGetCount"));
        auto name = reinterpret_cast<Name>(GetProcAddress(cuda, "cuDeviceGetName"));
        auto getluid = reinterpret_cast<Luid>(GetProcAddress(cuda, "cuDeviceGetLuid"));
        auto driver = reinterpret_cast<Driver>(GetProcAddress(cuda, "cuDriverGetVersion"));
        int d = 0;
        if (driver && driver(&d) == 0)
            r.driver = "CUDA driver API " + std::to_string(d);
        int total = 0;
        if (init && count && name && init(0) == 0 && count(&total) == 0) {
            for (int i = 0; i < total && i < 32; i++) {
                char text[256]{};
                if (name(text, sizeof(text), i) != 0)
                    continue;
                LUID l{};
                unsigned mask = 0;
                bool found = getluid && getluid(reinterpret_cast<char*>(&l), &mask, i) == 0;
                r.cudaDevices.push_back({i, text, found ? luid(l) : ""});
            }
        }
        FreeLibrary(cuda);
    } else
        r.warnings.push_back("cuda.driver-unavailable");
    HANDLE snap =
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W m{};
        m.dwSize = sizeof(m);
        if (Module32FirstW(snap, &m)) {
            r.moduleScanCompleted = true;
            do {
                auto s = utf8(m.szModule);
                auto lower = s;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                if (lower.find("webview") != std::string::npos ||
                    lower.find("webengine") != std::string::npos ||
                    lower.find("libcef") != std::string::npos)
                    r.webviewModules.push_back(s);
            } while (Module32NextW(snap, &m));
        }
        CloseHandle(snap);
    }
#else
    utsname u{};
    if (uname(&u) == 0)
        r.osBuild = std::string(u.sysname) + " " + u.release;
    std::ifstream cpu("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpu, line)) {
        if (line.starts_with("model name")) {
            auto pos = line.find(':');
            r.cpu = pos == std::string::npos ? line : line.substr(pos + 2);
            break;
        }
    }
    long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0)
        r.ram = std::uint64_t(pages) * std::uint64_t(page);
    r.powerMode = "NotMeasured";
    std::ifstream modules("/proc/self/maps");
    r.moduleScanCompleted = bool(modules);
    while (std::getline(modules, line))
        if (line.find("webview") != std::string::npos ||
            line.find("WebEngine") != std::string::npos || line.find("libcef") != std::string::npos)
            r.webviewModules.push_back(line);
    r.warnings.push_back("Windows/D3D11/CUDA validation not run on this host");
#endif
    if (r.cpu.empty())
        r.cpu = "Unavailable";
    if (r.osBuild.empty())
        r.osBuild = "Unavailable";
    return r;
}
std::string diagnosticJson(const DeviceReport& r) {
    std::ostringstream o;
    o << "{\"schema\":1,\"osBuild\":" << quote(r.osBuild) << ",\"cpu\":" << quote(r.cpu)
      << ",\"ramBytes\":" << r.ram << ",\"logicalThreads\":" << r.logicalThreads
      << ",\"dpi\":" << r.dpi << ",\"powerMode\":" << quote(r.powerMode)
      << ",\"sceneGraph\":" << quote(r.sceneGraph)
      << ",\"activeRenderLuid\":" << (r.activeRenderLuid ? quote(*r.activeRenderLuid) : "null")
      << ",\"driver\":" << quote(r.driver) << ",\"gpuAdapters\":[";
    bool first = true;
    for (auto& a : r.gpuAdapters) {
        if (!first)
            o << ',';
        first = false;
        o << "{\"name\":" << quote(a.name) << ",\"luid\":" << quote(a.luid)
          << ",\"driver\":" << quote(a.driver) << ",\"dedicatedBytes\":" << a.dedicatedBytes
          << ",\"software\":" << (a.software ? "true" : "false") << '}';
    }
    o << "],\"cudaDevices\":[";
    first = true;
    for (auto& a : r.cudaDevices) {
        if (!first)
            o << ',';
        first = false;
        o << "{\"ordinal\":" << a.ordinal << ",\"name\":" << quote(a.name)
          << ",\"luid\":" << quote(a.luid) << '}';
    }
    o << "],\"moduleScanCompleted\":" << (r.moduleScanCompleted ? "true" : "false")
      << ",\"webviewModules\":[";
    first = true;
    for (auto& s : r.webviewModules) {
        if (!first)
            o << ',';
        first = false;
        o << quote(s);
    }
    o << "],\"warnings\":[";
    first = true;
    for (auto& s : r.warnings) {
        if (!first)
            o << ',';
        first = false;
        o << quote(s);
    }
    o << "]}";
    return o.str();
}
} // namespace drawborad
