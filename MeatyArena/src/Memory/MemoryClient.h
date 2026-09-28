#pragma once

#include "../App/AppConfig.h"

#include <Windows.h>
#include <winternl.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4200 4201)
#endif
#include <vmmdll.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

enum class MemoryConnectionState : std::uint8_t
{
    Disconnected,
    Connecting,
    WaitingForProcess,
    Resolving,
    Connected,
    Failed
};

struct MemoryStatus
{
    MemoryConnectionState state = MemoryConnectionState::Disconnected;
    std::uint32_t processId = 0;
    std::uint64_t moduleBase = 0;
    std::uint64_t moduleSize = 0;
    std::uint64_t readOperations = 0;
    std::uint64_t readBytes = 0;
    std::uint64_t readFailures = 0;
    std::string message;
};

class MemoryClient
{
public:
    MemoryClient() = default;
    ~MemoryClient();

    MemoryClient(const MemoryClient&) = delete;
    MemoryClient& operator=(const MemoryClient&) = delete;

    void ConnectAsync(ConnectionConfig config);
    void Disconnect();
    MemoryStatus GetStatus() const;

    bool Read(std::uint64_t address, void* buffer, std::size_t size, bool uncached = false) const;
    struct ScatterEntry
    {
        std::uint64_t address;
        void* buffer;
        std::uint32_t size;
    };
    bool ReadScatter(const std::vector<ScatterEntry>& entries, bool uncached = false, std::vector<bool>* completedEntries = nullptr) const;
    bool ModuleBase(const char* moduleName, std::uint64_t& base) const;
    bool ModuleInfo(const char* moduleName, std::uint64_t& base, std::uint64_t& size) const;
    std::vector<std::uint64_t> FindSignatures(const char* moduleName, const char* pattern, std::size_t maxMatches = 64) const;

    bool InitializeKeyboard(std::string& error);
    [[nodiscard]] bool IsKeyDown(std::uint32_t virtualKey) const;
    [[nodiscard]] bool KeyboardReady() const;
    [[nodiscard]] std::uint64_t KeyboardAddress() const;
    [[nodiscard]] std::string KeyboardStatus() const;
    void BeginKeyCapture();
    [[nodiscard]] std::uint32_t GetFirstPressedKey();

    template <typename T> bool TryRead(std::uint64_t address, T& value, bool uncached = false) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "Memory reads require trivially copyable types");
        value = {};
        return Read(address, &value, sizeof(T), uncached);
    }

private:
    struct VmmApi
    {
        HMODULE module = nullptr;
        decltype(&VMMDLL_Initialize) initialize = nullptr;
        decltype(&VMMDLL_Close) close = nullptr;
        decltype(&VMMDLL_PidGetFromName) pidGetFromName = nullptr;
        decltype(&VMMDLL_Map_GetModuleFromNameU) mapGetModuleFromName = nullptr;
        decltype(&VMMDLL_MemFree) memFree = nullptr;
        decltype(&VMMDLL_MemReadEx) memReadEx = nullptr;
        decltype(&VMMDLL_ConfigSet) configSet = nullptr;
        decltype(&VMMDLL_ConfigGet) configGet = nullptr;
        decltype(&VMMDLL_Map_GetPhysMem) mapGetPhysMem = nullptr;
        decltype(&VMMDLL_ProcessGetInformationAll) processGetInformationAll = nullptr;
        decltype(&VMMDLL_Map_GetEATU) mapGetEat = nullptr;
        decltype(&VMMDLL_PdbLoad) pdbLoad = nullptr;
        decltype(&VMMDLL_PdbSymbolAddress) pdbSymbolAddress = nullptr;
        decltype(&VMMDLL_Scatter_Initialize) scatterInitialize = nullptr;
        decltype(&VMMDLL_Scatter_PrepareEx) scatterPrepareEx = nullptr;
        decltype(&VMMDLL_Scatter_ExecuteRead) scatterExecuteRead = nullptr;
        decltype(&VMMDLL_Scatter_CloseHandle) scatterClose = nullptr;
    };

    void ConnectWorker(ConnectionConfig config);
    bool LoadApiLocked();
    void UnloadApiLocked();
    void CloseHandleLocked();
    void SetMessage(std::string message);
    void MaybeRefreshLocked() const;
    bool ReadProcessLocked(DWORD pid, std::uint64_t address, void* buffer, std::size_t size, DWORD flags) const;
    bool UpdateKeyboardStateLocked(std::chrono::steady_clock::time_point now) const;
    std::uint64_t FindProcessSignatureLocked(DWORD pid, std::uint64_t start, std::uint64_t size, const std::uint8_t* pattern, const char* mask, std::size_t patternSize) const;

    mutable std::mutex handleMutex_;
    mutable std::mutex messageMutex_;
    std::thread connectThread_;
    VmmApi api_;
    VMM_HANDLE handle_ = nullptr;
    std::atomic_bool cancel_{false};
    std::atomic<MemoryConnectionState> state_{MemoryConnectionState::Disconnected};
    std::atomic<std::uint32_t> processId_{0};
    std::atomic<std::uint64_t> moduleBase_{0};
    std::atomic<std::uint64_t> moduleSize_{0};
    mutable std::atomic<std::uint64_t> readOperations_{0};
    mutable std::atomic<std::uint64_t> readBytes_{0};
    mutable std::atomic<std::uint64_t> readFailures_{0};
    mutable std::chrono::steady_clock::time_point lastMemoryRefresh_{};
    mutable std::chrono::steady_clock::time_point lastTlbRefresh_{};
    std::atomic<std::uint64_t> keyboardAddress_{0};
    std::atomic<DWORD> keyboardPid_{0};
    mutable std::array<std::uint8_t, 64> keyboardState_{};
    mutable std::array<std::uint8_t, 32> keyboardPressedEdges_{};
    mutable std::chrono::steady_clock::time_point lastKeyboardRead_{};
    mutable std::mutex keyboardMessageMutex_;
    std::string keyboardStatus_ = "Not initialised";
    std::string message_ = "Disconnected";
};
