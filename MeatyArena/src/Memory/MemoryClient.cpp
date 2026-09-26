#include "MemoryClient.h"
#include "../SDK/RuntimeResolver.h"
#include "../SDK/TargetProfile.h"
#include "../Unity/Types.h"
#include "../Core/Logging/Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace
{
    struct PatternByte
    {
        std::uint8_t value = 0;
        bool wildcard = false;
    };

    std::vector<PatternByte> ParsePattern(const char* pattern)
    {
        std::vector<PatternByte> bytes;
        if (!pattern)
            return bytes;
        const char* cursor = pattern;
        while (*cursor)
        {
            while (*cursor && std::isspace(static_cast<unsigned char>(*cursor)))
                ++cursor;
            if (!*cursor)
                break;
            if (*cursor == '?')
            {
                bytes.push_back({0, true});
                while (*cursor == '?')
                    ++cursor;
                continue;
            }
            char* end = nullptr;
            const auto value = std::strtoul(cursor, &end, 16);
            if (end == cursor || value > 0xFF)
                return {};
            bytes.push_back({static_cast<std::uint8_t>(value), false});
            cursor = end;
        }
        return bytes;
    }

    bool Matches(const std::uint8_t* data, const std::vector<PatternByte>& pattern)
    {
        for (std::size_t i = 0; i < pattern.size(); ++i)
            if (!pattern[i].wildcard && data[i] != pattern[i].value)
                return false;
        return true;
    }
} // namespace

MemoryClient::~MemoryClient()
{
    Disconnect();
}

void MemoryClient::ConnectAsync(ConnectionConfig config)
{
    Disconnect();
    cancel_.store(false, std::memory_order_release);
    state_.store(MemoryConnectionState::Connecting, std::memory_order_release);
    SetMessage("Initialising PCLeech/MemProcFS...");
    connectThread_ = std::thread(&MemoryClient::ConnectWorker, this, std::move(config));
}

void MemoryClient::Disconnect()
{
    cancel_.store(true, std::memory_order_release);
    if (connectThread_.joinable())
        connectThread_.join();

    {
        std::lock_guard<std::mutex> lock(handleMutex_);
        CloseHandleLocked();
    }

    processId_.store(0, std::memory_order_release);
    moduleBase_.store(0, std::memory_order_release);
    moduleSize_.store(0, std::memory_order_release);
    keyboardAddress_.store(0, std::memory_order_release);
    keyboardPid_.store(0, std::memory_order_release);
    keyboardState_.fill(0);
    keyboardPressedEdges_.fill(0);
    lastKeyboardRead_ = {};
    {
        std::lock_guard<std::mutex> lock(keyboardMessageMutex_);
        keyboardStatus_ = "Disconnected";
    }
    state_.store(MemoryConnectionState::Disconnected, std::memory_order_release);
    SetMessage("Disconnected");
}

MemoryStatus MemoryClient::GetStatus() const
{
    MemoryStatus status;
    status.state = state_.load(std::memory_order_acquire);
    status.processId = processId_.load(std::memory_order_acquire);
    status.moduleBase = moduleBase_.load(std::memory_order_acquire);
    status.moduleSize = moduleSize_.load(std::memory_order_acquire);
    status.readOperations = readOperations_.load(std::memory_order_relaxed);
    status.readBytes = readBytes_.load(std::memory_order_relaxed);
    status.readFailures = readFailures_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(messageMutex_);
        status.message = message_;
    }
    return status;
}

bool MemoryClient::ReadProcessLocked(DWORD pid, std::uint64_t address, void* buffer, std::size_t size, DWORD flags) const
{
    if (!handle_ || !api_.memReadEx || !pid || !address || !buffer || size == 0 || size > static_cast<std::size_t>(MAXDWORD))
        return false;

    DWORD bytesRead = 0;
    const bool complete = api_.memReadEx(handle_, pid, address, static_cast<PBYTE>(buffer), static_cast<DWORD>(size), &bytesRead, flags) != FALSE && bytesRead == size;
    readOperations_.fetch_add(1, std::memory_order_relaxed);
    readBytes_.fetch_add(bytesRead, std::memory_order_relaxed);
    if (!complete)
        readFailures_.fetch_add(1, std::memory_order_relaxed);
    return complete;
}

std::uint64_t MemoryClient::FindProcessSignatureLocked(DWORD pid, std::uint64_t start, std::uint64_t size, const std::uint8_t* pattern, const char* mask,
                                                       std::size_t patternSize) const
{
    if (!start || !size || !pattern || !mask || !patternSize || std::strlen(mask) != patternSize)
        return 0;

    constexpr std::size_t chunkSize = 4096;
    std::vector<std::uint8_t> buffer(chunkSize + patternSize - 1);
    for (std::uint64_t offset = 0; offset < size; offset += chunkSize)
    {
        const std::size_t requested = static_cast<std::size_t>((std::min<std::uint64_t>)(buffer.size(), size - offset));
        if (requested < patternSize)
            break;

        std::size_t readable = requested;
        if (!ReadProcessLocked(pid, start + offset, buffer.data(), requested, VMMDLL_FLAG_NOCACHE))
        {
            if (requested <= chunkSize || !ReadProcessLocked(pid, start + offset, buffer.data(), chunkSize, VMMDLL_FLAG_NOCACHE))
                continue;
            readable = chunkSize;
        }

        for (std::size_t candidate = 0; candidate + patternSize <= readable; ++candidate)
        {
            bool matches = true;
            for (std::size_t byte = 0; byte < patternSize; ++byte)
                if (mask[byte] != '?' && buffer[candidate + byte] != pattern[byte])
                {
                    matches = false;
                    break;
                }
            if (matches)
                return start + offset + candidate;
        }
    }
    return 0;
}

bool MemoryClient::InitializeKeyboard(std::string& error)
{
    constexpr std::uint64_t kernelAddressThreshold = 0x7FFFFFFFFFFFULL;
    static constexpr std::uint8_t win32ksgdSessionSignature[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x8B, 0x04, 0xC8};
    static constexpr std::uint8_t win32kSessionSignature[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0xFF, 0xC9};
    static constexpr std::uint8_t keyStateSignature[] = {0x48, 0x8D, 0x90, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x0F, 0x57, 0xC0};

    std::lock_guard<std::mutex> handleLock(handleMutex_);
    keyboardAddress_.store(0, std::memory_order_release);
    keyboardPid_.store(0, std::memory_order_release);
    keyboardState_.fill(0);
    keyboardPressedEdges_.fill(0);
    lastKeyboardRead_ = {};

    const auto setStatus = [this](const std::string& value)
    {
        std::lock_guard<std::mutex> statusLock(keyboardMessageMutex_);
        keyboardStatus_ = value;
    };
    const auto fail = [&](const std::string& reason)
    {
        error = reason;
        setStatus(reason);
        return false;
    };
    if (!handle_ || !api_.processGetInformationAll || !api_.mapGetModuleFromName || !api_.memReadEx)
        return fail("DMA keyboard APIs are unavailable in vmm.dll");

    ULONG64 windowsBuild = 0;
    if (api_.configGet)
        api_.configGet(handle_, VMMDLL_OPT_WIN_VERSION_BUILD, &windowsBuild);

    PVMMDLL_PROCESS_INFORMATION processInfo = nullptr;
    DWORD processCount = 0;
    if (!api_.processGetInformationAll(handle_, &processInfo, &processCount) || !processInfo || !processCount)
        return fail("Could not enumerate target processes for DMA keyboard input");

    std::vector<DWORD> csrssPids;
    DWORD winlogonPid = 0;
    for (DWORD index = 0; index < processCount; ++index)
    {
        const auto& process = processInfo[index];
        if (_stricmp(process.szName, "csrss.exe") == 0)
            csrssPids.push_back(process.dwPID);
        else if (!winlogonPid && _stricmp(process.szName, "winlogon.exe") == 0)
            winlogonPid = process.dwPID;
    }
    api_.memFree(processInfo);

    const auto isKernelPointer = [](std::uint64_t address)
    {
        return address > kernelAddressThreshold;
    };
    const auto moduleRange = [&](DWORD pid, const char* name, std::uint64_t& base, std::uint64_t& size)
    {
        base = size = 0;
        PVMMDLL_MAP_MODULEENTRY module = nullptr;
        if (!api_.mapGetModuleFromName(handle_, pid, const_cast<LPSTR>(name), &module, VMMDLL_MODULE_FLAG_NORMAL) || !module)
            return false;
        base = module->vaBase;
        size = module->cbImageSize;
        api_.memFree(module);
        return isKernelPointer(base) && size != 0;
    };

    if (windowsBuild == 0 || windowsBuild > 22000)
    {
        for (const DWORD csrssPid : csrssPids)
        {
            const DWORD kernelPid = csrssPid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY;
            std::uint64_t sessionModuleBase = 0, sessionModuleSize = 0;
            bool usingWin32ksgd = moduleRange(kernelPid, "win32ksgd.sys", sessionModuleBase, sessionModuleSize);
            if (!usingWin32ksgd && !moduleRange(kernelPid, "win32k.sys", sessionModuleBase, sessionModuleSize))
                continue;

            const std::uint8_t* sessionPattern = usingWin32ksgd ? win32ksgdSessionSignature : win32kSessionSignature;
            const char* sessionMask = usingWin32ksgd ? "xxx????xxxx" : "xxx????xx";
            const std::size_t sessionPatternSize = usingWin32ksgd ? sizeof(win32ksgdSessionSignature) : sizeof(win32kSessionSignature);
            const std::uint64_t sessionInstruction = FindProcessSignatureLocked(kernelPid, sessionModuleBase, sessionModuleSize, sessionPattern, sessionMask, sessionPatternSize);
            std::int32_t sessionDisplacement = 0;
            if (!sessionInstruction || !ReadProcessLocked(kernelPid, sessionInstruction + 3, &sessionDisplacement, sizeof(sessionDisplacement), VMMDLL_FLAG_NOCACHE))
                continue;

            const std::uint64_t sessionSlotsAddress = sessionInstruction + 7 + static_cast<std::int64_t>(sessionDisplacement);
            std::uint64_t sessionSlots = 0;
            if (!isKernelPointer(sessionSlotsAddress) || !ReadProcessLocked(kernelPid, sessionSlotsAddress, &sessionSlots, sizeof(sessionSlots), VMMDLL_FLAG_NOCACHE) ||
                !isKernelPointer(sessionSlots))
                continue;

            std::uint64_t userSessionState = 0;
            for (std::uint64_t slot = 0; slot < 4 && !userSessionState; ++slot)
            {
                std::uint64_t slotPointer = 0, candidate = 0;
                if (ReadProcessLocked(kernelPid, sessionSlots + slot * sizeof(std::uint64_t), &slotPointer, sizeof(slotPointer), VMMDLL_FLAG_NOCACHE) &&
                    isKernelPointer(slotPointer) && ReadProcessLocked(kernelPid, slotPointer, &candidate, sizeof(candidate), VMMDLL_FLAG_NOCACHE) && isKernelPointer(candidate))
                    userSessionState = candidate;
            }
            if (!userSessionState)
                continue;

            std::uint64_t win32kbase = 0, win32kbaseSize = 0;
            if (!moduleRange(kernelPid, "win32kbase.sys", win32kbase, win32kbaseSize))
                continue;
            const std::uint64_t keyInstruction = FindProcessSignatureLocked(kernelPid, win32kbase, win32kbaseSize, keyStateSignature, "xxx????x????xxx", sizeof(keyStateSignature));
            std::int32_t keyStateOffset = 0;
            if (!keyInstruction || !ReadProcessLocked(kernelPid, keyInstruction + 3, &keyStateOffset, sizeof(keyStateOffset), VMMDLL_FLAG_NOCACHE))
                continue;

            const std::uint64_t keyStateAddress = userSessionState + static_cast<std::int64_t>(keyStateOffset);
            const DWORD keyboardReadPid = (winlogonPid ? winlogonPid : csrssPid) | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY;
            std::array<std::uint8_t, 64> sample{};
            if (!isKernelPointer(keyStateAddress) || !ReadProcessLocked(keyboardReadPid, keyStateAddress, sample.data(), sample.size(), VMMDLL_FLAG_NOCACHE))
                continue;

            keyboardState_ = sample;
            keyboardAddress_.store(keyStateAddress, std::memory_order_release);
            keyboardPid_.store(keyboardReadPid, std::memory_order_release);
            lastKeyboardRead_ = std::chrono::steady_clock::now();
            error.clear();
            setStatus("Ready (Windows session state)");
            return true;
        }
    }

    if ((windowsBuild == 0 || windowsBuild <= 22000) && winlogonPid && api_.mapGetEat)
    {
        const DWORD kernelPid = winlogonPid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY;
        PVMMDLL_MAP_EAT eat = nullptr;
        if (api_.mapGetEat(handle_, kernelPid, const_cast<LPSTR>("win32kbase.sys"), &eat) && eat)
        {
            std::uint64_t keyStateAddress = 0;
            if (eat->dwVersion == VMMDLL_MAP_EAT_VERSION)
                for (DWORD index = 0; index < eat->cMap; ++index)
                    if (eat->pMap[index].uszFunction && std::strcmp(eat->pMap[index].uszFunction, "gafAsyncKeyState") == 0)
                    {
                        keyStateAddress = eat->pMap[index].vaFunction;
                        break;
                    }
            api_.memFree(eat);

            if (!isKernelPointer(keyStateAddress) && api_.pdbLoad && api_.pdbSymbolAddress)
            {
                std::uint64_t win32kbase = 0, win32kbaseSize = 0;
                char pdbName[MAX_PATH]{};
                ULONG64 symbolAddress = 0;
                if (moduleRange(kernelPid, "win32kbase.sys", win32kbase, win32kbaseSize) && api_.pdbLoad(handle_, kernelPid, win32kbase, pdbName) &&
                    api_.pdbSymbolAddress(handle_, pdbName, const_cast<LPSTR>("gafAsyncKeyState"), &symbolAddress))
                    keyStateAddress = symbolAddress;
            }

            std::array<std::uint8_t, 64> sample{};
            if (isKernelPointer(keyStateAddress) && ReadProcessLocked(kernelPid, keyStateAddress, sample.data(), sample.size(), VMMDLL_FLAG_NOCACHE))
            {
                keyboardState_ = sample;
                keyboardAddress_.store(keyStateAddress, std::memory_order_release);
                keyboardPid_.store(kernelPid, std::memory_order_release);
                lastKeyboardRead_ = std::chrono::steady_clock::now();
                error.clear();
                setStatus("Ready (gafAsyncKeyState export)");
                return true;
            }
        }
    }

    return fail("DMA keyboard address could not be resolved; aim input is disabled");
}

bool MemoryClient::IsKeyDown(std::uint32_t virtualKey) const
{
    if (virtualKey >= 256)
        return false;
    if (!KeyboardReady())
        return false;

    std::lock_guard<std::mutex> lock(handleMutex_);
    const auto now = std::chrono::steady_clock::now();
    if (lastKeyboardRead_.time_since_epoch().count() == 0 || now - lastKeyboardRead_ >= std::chrono::milliseconds(4))
        UpdateKeyboardStateLocked(now);
    return (keyboardState_[virtualKey / 4] & (1u << ((virtualKey % 4) * 2))) != 0;
}

bool MemoryClient::UpdateKeyboardStateLocked(std::chrono::steady_clock::time_point now) const
{
    const std::uint64_t address = keyboardAddress_.load(std::memory_order_acquire);
    const DWORD pid = keyboardPid_.load(std::memory_order_acquire);
    if (!address || !pid)
        return false;

    std::array<std::uint8_t, 64> sample{};
    const bool updated = ReadProcessLocked(pid, address, sample.data(), sample.size(), VMMDLL_FLAG_NOCACHE);
    lastKeyboardRead_ = now;
    if (!updated)
        return false;

    for (std::uint32_t virtualKey = 0; virtualKey < 256; ++virtualKey)
    {
        const std::uint8_t keyMask = static_cast<std::uint8_t>(1u << ((virtualKey % 4) * 2));
        const bool wasDown = (keyboardState_[virtualKey / 4] & keyMask) != 0;
        const bool isDown = (sample[virtualKey / 4] & keyMask) != 0;
        if (isDown && !wasDown)
            keyboardPressedEdges_[virtualKey / 8] |= static_cast<std::uint8_t>(1u << (virtualKey % 8));
    }
    keyboardState_ = sample;
    return true;
}

bool MemoryClient::KeyboardReady() const
{
    return keyboardAddress_.load(std::memory_order_acquire) != 0 && keyboardPid_.load(std::memory_order_acquire) != 0;
}

std::uint64_t MemoryClient::KeyboardAddress() const
{
    return keyboardAddress_.load(std::memory_order_acquire);
}

std::string MemoryClient::KeyboardStatus() const
{
    std::lock_guard<std::mutex> lock(keyboardMessageMutex_);
    return keyboardStatus_;
}

void MemoryClient::BeginKeyCapture()
{
    if (!KeyboardReady())
        return;

    std::lock_guard<std::mutex> lock(handleMutex_);
    UpdateKeyboardStateLocked(std::chrono::steady_clock::now());
    keyboardPressedEdges_.fill(0);
}

std::uint32_t MemoryClient::GetFirstPressedKey()
{
    if (!KeyboardReady())
        return 0;

    std::lock_guard<std::mutex> lock(handleMutex_);
    const auto now = std::chrono::steady_clock::now();
    if (lastKeyboardRead_.time_since_epoch().count() == 0 || now - lastKeyboardRead_ >= std::chrono::milliseconds(20))
        UpdateKeyboardStateLocked(now);

    const auto consume = [this](std::uint32_t virtualKey)
    {
        const std::uint8_t mask = static_cast<std::uint8_t>(1u << (virtualKey % 8));
        std::uint8_t& edges = keyboardPressedEdges_[virtualKey / 8];
        if ((edges & mask) == 0)
            return false;
        edges &= static_cast<std::uint8_t>(~mask);
        return true;
    };

    constexpr std::uint32_t preferredModifiers[] = {VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU};
    for (const std::uint32_t key : preferredModifiers)
        if (consume(key))
            return key;

    for (std::uint32_t key = 1; key < 256; ++key)
    {
        if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU)
            continue;
        bool preferred = false;
        for (const std::uint32_t modifier : preferredModifiers)
            preferred = preferred || key == modifier;
        if (!preferred && consume(key))
            return key;
    }

    constexpr std::uint32_t genericModifiers[] = {VK_SHIFT, VK_CONTROL, VK_MENU};
    for (const std::uint32_t key : genericModifiers)
        if (consume(key))
            return key;
    return 0;
}

bool MemoryClient::Read(std::uint64_t address, void* buffer, std::size_t size, bool uncached) const
{
    if (!address || !buffer || size == 0 || size > static_cast<std::size_t>(MAXDWORD))
        return false;

    std::lock_guard<std::mutex> lock(handleMutex_);
    MaybeRefreshLocked();
    const DWORD pid = processId_.load(std::memory_order_acquire);
    if (!handle_ || !pid)
    {
        readFailures_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    DWORD bytesRead = 0;
    DWORD flags = VMMDLL_FLAG_ZEROPAD_ON_FAIL;
    if (uncached)
        flags |= VMMDLL_FLAG_NOCACHE;

    if (!api_.memReadEx)
    {
        readFailures_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const bool ok = api_.memReadEx(handle_, pid, address, static_cast<PBYTE>(buffer), static_cast<DWORD>(size), &bytesRead, flags) != FALSE;
    readOperations_.fetch_add(1, std::memory_order_relaxed);
    readBytes_.fetch_add(bytesRead, std::memory_order_relaxed);
    if (!ok || bytesRead != size)
        readFailures_.fetch_add(1, std::memory_order_relaxed);
    return ok && bytesRead == size;
}

bool MemoryClient::ReadScatter(const std::vector<ScatterEntry>& entries, bool uncached, std::vector<bool>* completedEntries) const
{
    if (completedEntries)
        completedEntries->assign(entries.size(), false);
    if (entries.empty())
        return true;
    std::lock_guard<std::mutex> lock(handleMutex_);
    MaybeRefreshLocked();
    const DWORD pid = processId_.load(std::memory_order_acquire);
    if (!handle_ || !pid || !api_.scatterInitialize)
        return false;
    const DWORD flags = uncached ? VMMDLL_FLAG_NOCACHE : 0;
    auto scatter = api_.scatterInitialize(handle_, pid, flags);
    if (!scatter)
        return false;
    std::vector<DWORD> lengths(entries.size());
    std::vector<bool> prepared(entries.size(), false);
    bool anyPrepared = false;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        if (!Unity::IsValidAddress(entry.address) || !entry.buffer || !entry.size)
            continue;
        prepared[i] = api_.scatterPrepareEx(scatter, entry.address, entry.size, static_cast<PBYTE>(entry.buffer), &lengths[i]) != FALSE;
        anyPrepared = anyPrepared || prepared[i];
    }
    const bool executed = anyPrepared && api_.scatterExecuteRead(scatter) != FALSE;
    bool complete = executed && anyPrepared;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const bool entryComplete = prepared[i] && executed && lengths[i] == entries[i].size;
        if (completedEntries)
            (*completedEntries)[i] = entryComplete;
        readOperations_.fetch_add(1, std::memory_order_relaxed);
        readBytes_.fetch_add(lengths[i], std::memory_order_relaxed);
        if (!entryComplete)
        {
            readFailures_.fetch_add(1, std::memory_order_relaxed);
            complete = false;
        }
    }
    api_.scatterClose(scatter);
    return complete;
}

bool MemoryClient::ModuleBase(const char* moduleName, std::uint64_t& base) const
{
    std::uint64_t size = 0;
    return ModuleInfo(moduleName, base, size);
}

bool MemoryClient::ModuleInfo(const char* moduleName, std::uint64_t& base, std::uint64_t& size) const
{
    base = 0;
    size = 0;
    std::lock_guard<std::mutex> lock(handleMutex_);
    MaybeRefreshLocked();
    const DWORD pid = processId_.load(std::memory_order_acquire);
    if (!handle_ || !pid || !moduleName)
        return false;
    PVMMDLL_MAP_MODULEENTRY module = nullptr;
    if (!api_.mapGetModuleFromName(handle_, pid, const_cast<LPSTR>(moduleName), &module, VMMDLL_MODULE_FLAG_NORMAL))
        return false;
    base = module->vaBase;
    size = module->cbImageSize;
    api_.memFree(module);
    return Unity::IsValidAddress(base) && size != 0;
}

std::vector<std::uint64_t> MemoryClient::FindSignatures(const char* moduleName, const char* pattern, std::size_t maxMatches) const
{
    std::vector<std::uint64_t> matches;
    const auto parsed = ParsePattern(pattern);
    if (parsed.empty() || maxMatches == 0)
        return matches;

    std::uint64_t moduleBase = 0, moduleSize = 0;
    if (!ModuleInfo(moduleName, moduleBase, moduleSize))
        return matches;

    struct Region
    {
        std::uint64_t address;
        std::size_t size;
    };
    std::vector<Region> regions;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (Read(moduleBase, &dos, sizeof(dos), true) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && static_cast<std::uint64_t>(dos.e_lfanew) + sizeof(nt) < moduleSize &&
        Read(moduleBase + static_cast<std::uint32_t>(dos.e_lfanew), &nt, sizeof(nt), true) && nt.Signature == IMAGE_NT_SIGNATURE)
    {
        const auto sectionBase = moduleBase + static_cast<std::uint32_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
        const auto sectionCount = (std::min)(nt.FileHeader.NumberOfSections, static_cast<WORD>(96));
        std::vector<IMAGE_SECTION_HEADER> sections(sectionCount);
        if (sectionCount && Read(sectionBase, sections.data(), sections.size() * sizeof(IMAGE_SECTION_HEADER), true))
        {
            for (const auto& section : sections)
            {
                if ((section.Characteristics & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE)) == 0)
                    continue;
                const auto size = static_cast<std::size_t>((std::max)(section.Misc.VirtualSize, section.SizeOfRawData));
                if (!size || static_cast<std::uint64_t>(section.VirtualAddress) + size > moduleSize)
                    continue;
                regions.push_back({moduleBase + section.VirtualAddress, size});
            }
        }
    }
    if (regions.empty())
        regions.push_back({moduleBase, static_cast<std::size_t>(moduleSize)});

    constexpr std::size_t chunkSize = 1024 * 1024;
    const std::size_t overlap = parsed.size() > 1 ? parsed.size() - 1 : 0;
    std::unordered_set<std::uint64_t> unique;
    for (const auto& region : regions)
    {
        for (std::size_t offset = 0; offset < region.size && matches.size() < maxMatches;)
        {
            const auto bytesToRead = (std::min)(chunkSize + overlap, region.size - offset);
            std::vector<std::uint8_t> buffer(bytesToRead);

            (void)Read(region.address + offset, buffer.data(), buffer.size(), true);
            for (std::size_t i = 0; i + parsed.size() <= buffer.size() && matches.size() < maxMatches; ++i)
            {
                if (!Matches(buffer.data() + i, parsed))
                    continue;
                const auto address = region.address + offset + i;
                if (unique.insert(address).second)
                    matches.push_back(address);
            }
            if (bytesToRead <= overlap)
                break;
            offset += bytesToRead - overlap;
        }
    }
    return matches;
}

void MemoryClient::ConnectWorker(ConnectionConfig config)
{
    {
        std::lock_guard<std::mutex> lock(handleMutex_);
        if (!LoadApiLocked())
        {
            state_.store(MemoryConnectionState::Failed, std::memory_order_release);
            SetMessage("vmm.dll could not be loaded. Copy the matching MemProcFS runtime beside MeatyArena.exe.");
            return;
        }
    }

    std::vector<std::string> argumentStorage = {"", "-norefresh", "-device", config.deviceUri, "-waitinitialize"};
    if (config.debugOutput)
    {
        argumentStorage.emplace_back("-v");
        argumentStorage.emplace_back("-printf");
    }
    std::filesystem::path memoryMapPath = config.memoryMapPath;
    if (memoryMapPath.is_relative())
    {
        std::array<wchar_t, MAX_PATH> executable{};
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length)
            memoryMapPath = std::filesystem::path(std::wstring(executable.data(), length)).parent_path() / memoryMapPath;
    }
    const auto initialize = [this](std::vector<std::string>& storage)
    {
        std::vector<LPCSTR> arguments;
        arguments.reserve(storage.size());
        for (std::string& argument : storage)
            arguments.push_back(argument.c_str());
        return api_.initialize(static_cast<DWORD>(arguments.size()), arguments.data());
    };

    if (config.useMemoryMap && !memoryMapPath.empty() && !std::filesystem::is_regular_file(memoryMapPath))
    {
        SetMessage("Generating mmap.txt from the target system...");
        Log::Write("Memory: no memory map found; generating " + memoryMapPath.string());
        VMM_HANDLE discoveryHandle = initialize(argumentStorage);
        PVMMDLL_MAP_PHYSMEM physicalMap = nullptr;
        bool generated = discoveryHandle && api_.mapGetPhysMem && api_.mapGetPhysMem(discoveryHandle, &physicalMap) != FALSE && physicalMap &&
                         physicalMap->dwVersion == VMMDLL_MAP_PHYSMEM_VERSION && physicalMap->cMap != 0;
        if (generated)
        {
            const auto temporaryPath = std::filesystem::path(memoryMapPath.string() + ".tmp");
            std::error_code directoryError;
            if (const auto parent = memoryMapPath.parent_path(); !parent.empty())
                std::filesystem::create_directories(parent, directoryError);
            std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
            output << std::uppercase << std::hex << std::setfill('0');
            for (DWORD i = 0; output && i < physicalMap->cMap; ++i)
            {
                const auto& entry = physicalMap->pMap[i];
                if (!entry.cb)
                    continue;
                output << std::setw(16) << entry.pa << " - " << std::setw(16) << (entry.pa + entry.cb - 1) << "\r\n";
            }
            output.flush();
            generated = output.good();
            output.close();
            if (generated)
                generated = MoveFileExW(temporaryPath.c_str(), memoryMapPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
            if (!generated)
            {
                std::error_code removeError;
                std::filesystem::remove(temporaryPath, removeError);
            }
        }
        if (physicalMap)
            api_.memFree(physicalMap);
        if (discoveryHandle)
            api_.close(discoveryHandle);
        if (!generated)
        {
            state_.store(MemoryConnectionState::Failed, std::memory_order_release);
            SetMessage("Memory-map generation failed. Check DMA access, then delete mmap.txt and retry.");
            return;
        }
        Log::Write("Memory: generated " + memoryMapPath.string());
    }

    if (config.useMemoryMap && !memoryMapPath.empty() && std::filesystem::is_regular_file(memoryMapPath))
    {
        argumentStorage.emplace_back("-memmap");
        argumentStorage.emplace_back(memoryMapPath.string());
    }
    else if (config.useMemoryMap)
        Log::Write("Memory: map file not found; continuing without " + memoryMapPath.string());

    VMM_HANDLE newHandle = initialize(argumentStorage);
    if (!newHandle)
    {
        state_.store(MemoryConnectionState::Failed, std::memory_order_release);
        SetMessage("MemProcFS initialisation failed. Check runtime DLLs and DMA hardware.");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(handleMutex_);
        if (cancel_.load(std::memory_order_acquire))
        {
            api_.close(newHandle);
            return;
        }
        handle_ = newHandle;
        lastMemoryRefresh_ = std::chrono::steady_clock::now();
        lastTlbRefresh_ = lastMemoryRefresh_;
    }

    state_.store(MemoryConnectionState::WaitingForProcess, std::memory_order_release);
    SetMessage("Waiting for target process...");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds((std::max)(1, config.waitForProcessSeconds));
    auto nextFullRefresh = std::chrono::steady_clock::now();
    DWORD pid = 0;
    while (!cancel_.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
    {
        {
            std::lock_guard<std::mutex> lock(handleMutex_);
            if (handle_ && api_.pidGetFromName(handle_, const_cast<LPSTR>(TargetProfile::ProcessName), &pid))
                break;
            if (handle_ && api_.configSet && std::chrono::steady_clock::now() >= nextFullRefresh)
            {
                api_.configSet(handle_, VMMDLL_OPT_REFRESH_ALL, 1);
                nextFullRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (cancel_.load(std::memory_order_acquire))
        return;
    if (!pid)
    {
        state_.store(MemoryConnectionState::Failed, std::memory_order_release);
        SetMessage("Target process was not found before the timeout.");
        return;
    }

    const std::string moduleName = TargetProfile::GameAssemblyModule;
    PVMMDLL_MAP_MODULEENTRY module = nullptr;
    {
        std::lock_guard<std::mutex> lock(handleMutex_);
        if (!handle_ || !api_.mapGetModuleFromName(handle_, pid, const_cast<LPSTR>(moduleName.c_str()), &module, VMMDLL_MODULE_FLAG_NORMAL))
        {
            state_.store(MemoryConnectionState::Failed, std::memory_order_release);
            SetMessage("Target module was not found: " + moduleName);
            return;
        }
    }

    processId_.store(pid, std::memory_order_release);
    moduleBase_.store(module->vaBase, std::memory_order_release);
    moduleSize_.store(module->cbImageSize, std::memory_order_release);
    api_.memFree(module);

    state_.store(MemoryConnectionState::Resolving, std::memory_order_release);
    SetMessage("Resolving live IL2CPP and Unity offsets...");
    std::string resolverError;
    if (!RuntimeResolver::Initialize(*this, resolverError))
    {
        state_.store(MemoryConnectionState::Failed, std::memory_order_release);
        SetMessage("Runtime resolution failed: " + resolverError);
        return;
    }
    std::string keyboardError;
    bool keyboardReady = false;
    for (int attempt = 0; attempt < 3 && !keyboardReady; ++attempt)
    {
        keyboardReady = InitializeKeyboard(keyboardError);
        if (keyboardReady || attempt == 2)
            break;
        {
            std::lock_guard<std::mutex> lock(handleMutex_);
            if (handle_ && api_.configSet)
                api_.configSet(handle_, VMMDLL_OPT_REFRESH_ALL, 1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (keyboardReady)
        Log::Write("Input: DMA keyboard ready at 0x" +
                   [&]
                   {
                       std::ostringstream stream;
                       stream << std::hex << std::uppercase << KeyboardAddress();
                       return stream.str();
                   }());
    else
        Log::Write("Input: " + keyboardError);
    state_.store(MemoryConnectionState::Connected, std::memory_order_release);
    SetMessage(std::string("Connected read-only to ") + TargetProfile::ProcessName);
}

void MemoryClient::CloseHandleLocked()
{
    if (handle_)
    {
        api_.close(handle_);
        handle_ = nullptr;
    }
    UnloadApiLocked();
}

bool MemoryClient::LoadApiLocked()
{
    if (api_.module)
        return true;

    api_.module = LoadLibraryW(L"vmm.dll");
    if (!api_.module)
        return false;

    const auto load = [this](const char* name)
    {
        return GetProcAddress(api_.module, name);
    };
    api_.initialize = reinterpret_cast<decltype(api_.initialize)>(load("VMMDLL_Initialize"));
    api_.close = reinterpret_cast<decltype(api_.close)>(load("VMMDLL_Close"));
    api_.pidGetFromName = reinterpret_cast<decltype(api_.pidGetFromName)>(load("VMMDLL_PidGetFromName"));
    api_.mapGetModuleFromName = reinterpret_cast<decltype(api_.mapGetModuleFromName)>(load("VMMDLL_Map_GetModuleFromNameU"));
    api_.memFree = reinterpret_cast<decltype(api_.memFree)>(load("VMMDLL_MemFree"));
    api_.memReadEx = reinterpret_cast<decltype(api_.memReadEx)>(load("VMMDLL_MemReadEx"));
    api_.configSet = reinterpret_cast<decltype(api_.configSet)>(load("VMMDLL_ConfigSet"));
    api_.configGet = reinterpret_cast<decltype(api_.configGet)>(load("VMMDLL_ConfigGet"));
    api_.mapGetPhysMem = reinterpret_cast<decltype(api_.mapGetPhysMem)>(load("VMMDLL_Map_GetPhysMem"));
    api_.processGetInformationAll = reinterpret_cast<decltype(api_.processGetInformationAll)>(load("VMMDLL_ProcessGetInformationAll"));
    api_.mapGetEat = reinterpret_cast<decltype(api_.mapGetEat)>(load("VMMDLL_Map_GetEATU"));
    api_.pdbLoad = reinterpret_cast<decltype(api_.pdbLoad)>(load("VMMDLL_PdbLoad"));
    api_.pdbSymbolAddress = reinterpret_cast<decltype(api_.pdbSymbolAddress)>(load("VMMDLL_PdbSymbolAddress"));
    api_.scatterInitialize = reinterpret_cast<decltype(api_.scatterInitialize)>(load("VMMDLL_Scatter_Initialize"));
    api_.scatterPrepareEx = reinterpret_cast<decltype(api_.scatterPrepareEx)>(load("VMMDLL_Scatter_PrepareEx"));
    api_.scatterExecuteRead = reinterpret_cast<decltype(api_.scatterExecuteRead)>(load("VMMDLL_Scatter_ExecuteRead"));
    api_.scatterClose = reinterpret_cast<decltype(api_.scatterClose)>(load("VMMDLL_Scatter_CloseHandle"));
    if (!api_.initialize || !api_.close || !api_.pidGetFromName || !api_.mapGetModuleFromName || !api_.memFree || !api_.memReadEx || !api_.configSet || !api_.mapGetPhysMem ||
        !api_.scatterInitialize || !api_.scatterPrepareEx || !api_.scatterExecuteRead || !api_.scatterClose)
    {
        UnloadApiLocked();
        return false;
    }
    return true;
}

void MemoryClient::MaybeRefreshLocked() const
{
    if (!handle_ || !api_.configSet)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (lastMemoryRefresh_.time_since_epoch().count() == 0 || now - lastMemoryRefresh_ >= std::chrono::milliseconds(300))
    {
        api_.configSet(handle_, VMMDLL_OPT_REFRESH_FREQ_MEM_PARTIAL, 1);
        lastMemoryRefresh_ = now;
    }
    if (lastTlbRefresh_.time_since_epoch().count() == 0 || now - lastTlbRefresh_ >= std::chrono::seconds(2))
    {
        api_.configSet(handle_, VMMDLL_OPT_REFRESH_FREQ_TLB_PARTIAL, 1);
        lastTlbRefresh_ = now;
    }
}

void MemoryClient::UnloadApiLocked()
{
    if (api_.module)
        FreeLibrary(api_.module);
    api_ = {};
}

void MemoryClient::SetMessage(std::string message)
{
    {
        std::lock_guard<std::mutex> lock(messageMutex_);
        if (message_ == message)
            return;
        message_ = message;
    }
    Log::Write("Memory: " + message);
}
