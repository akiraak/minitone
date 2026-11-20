#include "SettingsStore.h"

#include <windows.h>

void EqLog(const wchar_t* format, ...);

namespace eq
{

namespace
{

const DWORD kPollIntervalMs = 250;
const DWORD kMaxFileBytes = 64 * 1024;

SRWLOCK g_lifetimeLock = SRWLOCK_INIT; // Acquire / Release 同士の排他
long g_users = 0;
HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;

SRWLOCK g_settingsLock = SRWLOCK_INIT; // g_settings の排他
Settings g_settings = {};
volatile long g_version = 0;

wchar_t g_path[MAX_PATH] = L"";

// 最後に読んだときのファイルの状態。見張りのスレッド（と最初の読み込み）だけが触る。
bool g_lastExists = false;
FILETIME g_lastWriteTime = {};
DWORD g_lastSize = 0;
bool g_loadedOnce = false;

// 設定ファイルの場所。環境変数 EQAPO_SETTINGS_PATH があればそれ（テスト用）、
// 無ければ %ProgramData%\akiraak-equalizer\config\settings.txt。
void ResolvePath()
{
    DWORD length = GetEnvironmentVariableW(L"EQAPO_SETTINGS_PATH", g_path, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
        return;

    length = GetEnvironmentVariableW(L"ProgramData", g_path, MAX_PATH);
    if (length == 0 || length > MAX_PATH - 48)
    {
        g_path[0] = L'\0';
        return;
    }
    wcscat_s(g_path, L"\\akiraak-equalizer\\config\\settings.txt");
}

void Publish(const Settings& settings)
{
    AcquireSRWLockExclusive(&g_settingsLock);
    g_settings = settings;
    InterlockedIncrement(&g_version);
    ReleaseSRWLockExclusive(&g_settingsLock);
}

// ファイルが前に読んだときから変わっていれば読み直す。
void LoadIfChanged()
{
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    bool exists = g_path[0] != L'\0' && GetFileAttributesExW(g_path, GetFileExInfoStandard, &info) != 0 &&
                  (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;

    if (g_loadedOnce && exists == g_lastExists &&
        (!exists || (CompareFileTime(&info.ftLastWriteTime, &g_lastWriteTime) == 0 &&
                     info.nFileSizeLow == g_lastSize)))
        return;

    if (!exists)
    {
        g_loadedOnce = true;
        g_lastExists = false;
        Publish(BypassSettings());
        EqLog(L"settings: file not found, bypass (%s)", g_path);
        return;
    }

    HANDLE file = CreateFileW(g_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        // 書き込みの最中などで開けない。状態を更新せず、次の回にやり直す。
        return;
    }

    static char buffer[kMaxFileBytes];
    DWORD total = 0;
    bool ok = true;
    while (total < kMaxFileBytes)
    {
        DWORD read = 0;
        if (!ReadFile(file, buffer + total, kMaxFileBytes - total, &read, nullptr))
        {
            ok = false;
            break;
        }
        if (read == 0)
            break;
        total += read;
    }
    CloseHandle(file);
    if (!ok)
        return;

    Settings settings = ParseSettings(buffer, total);
    g_loadedOnce = true;
    g_lastExists = true;
    g_lastWriteTime = info.ftLastWriteTime;
    g_lastSize = info.nFileSizeLow;
    Publish(settings);
    EqLog(L"settings: loaded enabled=%d volume=%d%% bands=%d", settings.enabled ? 1 : 0,
          static_cast<int>(settings.volume * 100.0 + 0.5), settings.bandCount);
}

DWORD WINAPI WatchThread(LPVOID /*parameter*/)
{
    while (WaitForSingleObject(g_stopEvent, kPollIntervalMs) == WAIT_TIMEOUT)
        LoadIfChanged();
    return 0;
}

} // namespace

void StoreAcquire()
{
    AcquireSRWLockExclusive(&g_lifetimeLock);
    if (g_users++ == 0)
    {
        ResolvePath();
        g_loadedOnce = false;
        LoadIfChanged(); // 最初の設定は、この場で読んでおく

        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (g_stopEvent != nullptr)
            g_thread = CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr);
        if (g_thread == nullptr)
            EqLog(L"settings: could not start the watch thread (error=%lu)", GetLastError());
    }
    ReleaseSRWLockExclusive(&g_lifetimeLock);
}

void StoreRelease()
{
    AcquireSRWLockExclusive(&g_lifetimeLock);
    if (--g_users == 0)
    {
        if (g_thread != nullptr)
        {
            SetEvent(g_stopEvent);
            WaitForSingleObject(g_thread, INFINITE);
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
        if (g_stopEvent != nullptr)
        {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
    }
    ReleaseSRWLockExclusive(&g_lifetimeLock);
}

unsigned StoreVersion()
{
    return static_cast<unsigned>(g_version);
}

bool StoreTryRead(Settings* settings, unsigned* version)
{
    if (!TryAcquireSRWLockShared(&g_settingsLock))
        return false;
    *settings = g_settings;
    *version = static_cast<unsigned>(g_version);
    ReleaseSRWLockShared(&g_settingsLock);
    return true;
}

void StoreRead(Settings* settings, unsigned* version)
{
    AcquireSRWLockShared(&g_settingsLock);
    *settings = g_settings;
    *version = static_cast<unsigned>(g_version);
    ReleaseSRWLockShared(&g_settingsLock);
}

} // namespace eq
