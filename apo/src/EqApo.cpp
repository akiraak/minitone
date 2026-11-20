#include "EqApo.h"

#include "SettingsStore.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// {40874C30-A23F-4E8D-B383-B40845BA5584}
const CLSID CLSID_EqApo =
    {0x40874c30, 0xa23f, 0x4e8d, {0xb3, 0x83, 0xb4, 0x08, 0x45, 0xba, 0x55, 0x84}};

// SDK の CRegAPOProperties<1> は長さ 0 の配列を持つ。その警告だけを止める。
#pragma warning(push)
#pragma warning(disable : 4815)
const CRegAPOProperties<1> EqApo::regProperties(
    CLSID_EqApo,
    L"akiraak/equalizer",
    L"MIT License",
    1, 0,
    __uuidof(IAudioProcessingObject));
#pragma warning(pop)

// ログ。%ProgramData%\akiraak-equalizer\equalizer_apo.log に追記する。
// このフォルダは登録のスクリプトが作り、audiodg.exe（LocalService）が書けるようにする。
// 書けなければ何もしない。リアルタイムのスレッド（APOProcess）からは呼ばないこと。
void EqLog(const wchar_t* format, ...)
{
    wchar_t path[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"ProgramData", path, MAX_PATH);
    if (len == 0 || len > MAX_PATH - 48)
        return;
    wcscat_s(path, L"\\akiraak-equalizer\\equalizer_apo.log");

    wchar_t message[512];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t line[600];
    int chars = _snwprintf_s(line, _countof(line), _TRUNCATE,
                             L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu %s\r\n",
                             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                             now.wSecond, now.wMilliseconds, GetCurrentProcessId(), message);
    if (chars <= 0)
        return;

    char utf8[1800];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, chars, utf8, sizeof(utf8), nullptr, nullptr);
    if (bytes <= 0)
        return;

    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, utf8, static_cast<DWORD>(bytes), &written, nullptr);
    CloseHandle(file);
}

EqApo::EqApo(IUnknown* outer)
    : CBaseAudioProcessingObject(regProperties),
      refCount_(1),
      outer_(outer),
      seenVersion_(0),
      sampleRate_(0.0),
      channels_(0)
{
    InterlockedIncrement(&g_objectCount);
    eq::StoreAcquire();
}

EqApo::~EqApo()
{
    eq::StoreRelease();
    InterlockedDecrement(&g_objectCount);
}

STDMETHODIMP EqApo::QueryInterface(REFIID riid, void** ppv)
{
    if (outer_ != nullptr)
        return outer_->QueryInterface(riid, ppv);
    return NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP_(ULONG) EqApo::AddRef()
{
    if (outer_ != nullptr)
        return outer_->AddRef();
    return NonDelegatingAddRef();
}

STDMETHODIMP_(ULONG) EqApo::Release()
{
    if (outer_ != nullptr)
        return outer_->Release();
    return NonDelegatingRelease();
}

HRESULT STDMETHODCALLTYPE EqApo::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    if (ppv == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        // 集約のときに外側が持つ IUnknown。ここだけは外側へ回さない。
        *ppv = static_cast<INonDelegatingUnknown*>(this);
        NonDelegatingAddRef();
        return S_OK;
    }

    if (riid == __uuidof(IAudioProcessingObject))
        *ppv = static_cast<IAudioProcessingObject*>(this);
    else if (riid == __uuidof(IAudioProcessingObjectRT))
        *ppv = static_cast<IAudioProcessingObjectRT*>(this);
    else if (riid == __uuidof(IAudioProcessingObjectConfiguration))
        *ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
    else if (riid == __uuidof(IAudioSystemEffects))
        *ppv = static_cast<IAudioSystemEffects*>(this);
    else if (riid == __uuidof(IAudioSystemEffects2))
        *ppv = static_cast<IAudioSystemEffects2*>(this);
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // インターフェースのポインタ経由の AddRef は、集約されていれば外側へ回る。
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE EqApo::NonDelegatingAddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&refCount_));
}

ULONG STDMETHODCALLTYPE EqApo::NonDelegatingRelease()
{
    long count = InterlockedDecrement(&refCount_);
    if (count == 0)
        delete this;
    return static_cast<ULONG>(count);
}

STDMETHODIMP EqApo::Initialize(UINT32 cbDataSize, BYTE* pbyData)
{
    HRESULT hr = CBaseAudioProcessingObject::Initialize(cbDataSize, pbyData);
    if (FAILED(hr) && hr != APOERR_ALREADY_INITIALIZED)
    {
        // 基底クラスが知らない初期化データを渡されても動けるようにする。
        // この APO は初期化データの中身を使わない。
        HRESULT first = hr;
        hr = CBaseAudioProcessingObject::Initialize(0, nullptr);
        EqLog(L"Initialize: base failed 0x%08X with cb=%u, retry without data -> 0x%08X",
              static_cast<unsigned>(first), cbDataSize, static_cast<unsigned>(hr));
    }

    EqLog(L"Initialize: cb=%u hr=0x%08X", cbDataSize, static_cast<unsigned>(hr));
    return hr;
}

STDMETHODIMP EqApo::LockForProcess(UINT32 u32NumInputConnections,
                                   APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                                   UINT32 u32NumOutputConnections,
                                   APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
    HRESULT hr = CBaseAudioProcessingObject::LockForProcess(
        u32NumInputConnections, ppInputConnections, u32NumOutputConnections, ppOutputConnections);

    if (SUCCEEDED(hr))
    {
        // 音の処理が始まる前に、いまの設定でフィルタを用意しておく。
        channels_ = GetSamplesPerFrame();
        sampleRate_ = static_cast<double>(GetFramesPerSecond());
        eq::Settings settings;
        eq::StoreRead(&settings, &seenVersion_);
        processor_.Configure(settings, sampleRate_, channels_);

        EqLog(L"LockForProcess: ok channels=%u rate=%d bytesPerSample=%u bypassed=%d", channels_,
              static_cast<int>(sampleRate_), GetBytesPerSampleContainer(), processor_.IsBypassed() ? 1 : 0);
    }
    else
    {
        EqLog(L"LockForProcess: failed hr=0x%08X", static_cast<unsigned>(hr));
    }
    return hr;
}

STDMETHODIMP EqApo::IsInputFormatSupported(IAudioMediaType* pOutputFormat,
                                           IAudioMediaType* pRequestedInputFormat,
                                           IAudioMediaType** ppSupportedInputFormat)
{
    HRESULT hr = CBaseAudioProcessingObject::IsInputFormatSupported(
        pOutputFormat, pRequestedInputFormat, ppSupportedInputFormat);
    EqLog(L"IsInputFormatSupported: hr=0x%08X", static_cast<unsigned>(hr));
    return hr;
}

STDMETHODIMP EqApo::IsOutputFormatSupported(IAudioMediaType* pInputFormat,
                                            IAudioMediaType* pRequestedOutputFormat,
                                            IAudioMediaType** ppSupportedOutputFormat)
{
    HRESULT hr = CBaseAudioProcessingObject::IsOutputFormatSupported(
        pInputFormat, pRequestedOutputFormat, ppSupportedOutputFormat);
    EqLog(L"IsOutputFormatSupported: hr=0x%08X", static_cast<unsigned>(hr));
    return hr;
}

STDMETHODIMP EqApo::GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE /*Event*/)
{
    if (ppEffectsIds == nullptr || pcEffects == nullptr)
        return E_POINTER;
    // Windows に知らせる効果の一覧。いまは何も知らせない。
    *ppEffectsIds = nullptr;
    *pcEffects = 0;
    return S_OK;
}

#pragma AVRT_CODE_BEGIN
// リアルタイムのスレッドから呼ばれる。待つ処理、メモリの確保、ログの出力をしないこと。
STDMETHODIMP_(void) EqApo::APOProcess(UINT32 /*u32NumInputConnections*/,
                                      APO_CONNECTION_PROPERTY** ppInputConnections,
                                      UINT32 /*u32NumOutputConnections*/,
                                      APO_CONNECTION_PROPERTY** ppOutputConnections)
{
    APO_CONNECTION_PROPERTY* input = ppInputConnections[0];
    APO_CONNECTION_PROPERTY* output = ppOutputConnections[0];

    // 設定が変わっていたら取り込む。取り込めなければ（書き換えの最中）次の回に回す。
    if (eq::StoreVersion() != seenVersion_)
    {
        eq::Settings settings;
        unsigned version = 0;
        if (eq::StoreTryRead(&settings, &version))
        {
            processor_.Configure(settings, sampleRate_, channels_);
            seenVersion_ = version;
        }
    }

    switch (input->u32BufferFlags)
    {
    case BUFFER_VALID:
        // 形式は 32 bit の浮動小数点（基底クラスの既定）だけを受け付けている。
        processor_.Process(reinterpret_cast<const float*>(input->pBuffer),
                           reinterpret_cast<float*>(output->pBuffer), input->u32ValidFrameCount);
        output->u32ValidFrameCount = input->u32ValidFrameCount;
        output->u32BufferFlags = BUFFER_VALID;
        break;
    case BUFFER_SILENT:
        output->u32ValidFrameCount = input->u32ValidFrameCount;
        output->u32BufferFlags = BUFFER_SILENT;
        break;
    default:
        break;
    }
}
#pragma AVRT_CODE_END
