// DLL の入口、クラスファクトリ、COM と APO の登録・解除。
#include "EqApo.h"

#include <new>
#include <objbase.h>

long g_objectCount = 0;
long g_lockCount = 0;
static HINSTANCE g_instance = nullptr;

static const wchar_t kClsidKey[] = L"SOFTWARE\\Classes\\CLSID\\{40874C30-A23F-4E8D-B383-B40845BA5584}";
static const wchar_t kInprocKey[] =
    L"SOFTWARE\\Classes\\CLSID\\{40874C30-A23F-4E8D-B383-B40845BA5584}\\InprocServer32";
static const wchar_t kDescription[] = L"akiraak/minitone APO";

class ClassFactory : public IClassFactory
{
public:
    // 静的に 1 つだけ置くので、参照カウントは数えない。
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
    {
        if (ppv == nullptr)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory))
        {
            *ppv = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return 2; }
    STDMETHOD_(ULONG, Release)() override { return 1; }

    STDMETHOD(CreateInstance)(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (ppv == nullptr)
            return E_POINTER;
        *ppv = nullptr;
        // 集約するときは IUnknown を求める決まり。
        if (outer != nullptr && riid != __uuidof(IUnknown))
            return CLASS_E_NOAGGREGATION;

        EqApo* apo = new (std::nothrow) EqApo(outer);
        if (apo == nullptr)
            return E_OUTOFMEMORY;

        HRESULT hr = apo->NonDelegatingQueryInterface(riid, ppv);
        apo->NonDelegatingRelease();
        EqLog(L"CreateInstance: aggregated=%d hr=0x%08X", outer != nullptr ? 1 : 0,
              static_cast<unsigned>(hr));
        return hr;
    }

    STDMETHOD(LockServer)(BOOL lock) override
    {
        if (lock)
            InterlockedIncrement(&g_lockCount);
        else
            InterlockedDecrement(&g_lockCount);
        return S_OK;
    }
};

static ClassFactory g_classFactory;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{
    if (ppv == nullptr)
        return E_POINTER;
    *ppv = nullptr;
    if (clsid != CLSID_EqApo)
        return CLASS_E_CLASSNOTAVAILABLE;
    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    EqLog(L"DllGetClassObject: host=%s", exe);
    return g_classFactory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return (g_objectCount == 0 && g_lockCount == 0) ? S_OK : S_FALSE;
}

static HRESULT SetStringValue(HKEY key, const wchar_t* name, const wchar_t* value)
{
    DWORD bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
    LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), bytes);
    return HRESULT_FROM_WIN32(status);
}

// regsvr32 から呼ばれる。COM のクラスと APO を登録する。管理者の権限が要る。
// 出力デバイスへの取り付け（FxProperties）はここでは行わない。
STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(g_instance, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kClsidKey, 0, nullptr, 0, KEY_WRITE, nullptr,
                                     &key, nullptr);
    if (status != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(status);
    HRESULT hr = SetStringValue(key, nullptr, kDescription);
    RegCloseKey(key);
    if (FAILED(hr))
        return hr;

    status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kInprocKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                             nullptr);
    if (status != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(status);
    hr = SetStringValue(key, nullptr, path);
    if (SUCCEEDED(hr))
        hr = SetStringValue(key, L"ThreadingModel", L"Both");
    RegCloseKey(key);
    if (FAILED(hr))
        return hr;

    return RegisterAPO(EqApo::regProperties);
}

STDAPI DllUnregisterServer()
{
    HRESULT hr = UnregisterAPO(CLSID_EqApo);
    LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidKey);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
        return HRESULT_FROM_WIN32(status);
    return hr;
}
