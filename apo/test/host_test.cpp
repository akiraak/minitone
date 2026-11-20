// APO の DLL を、Windows の音のエンジンを使わずに読み込んで確かめるテスト。
// 音のエンジンが行う呼び出し（生成、初期化、形式の確認、ロック、処理）をまねる。
// 使い方: host_test.exe <equalizer_apo.dll のパス>
#include <windows.h>
#include <audioenginebaseapo.h>
#include <baseaudioprocessingobject.h>
#include <audiomediatype.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

// APO が読む設定ファイル。テストでは環境変数 EQAPO_SETTINGS_PATH で一時のファイルに向ける。
static wchar_t g_settingsPath[MAX_PATH] = L"";

static void WriteSettings(const char* text)
{
    HANDLE file = CreateFileW(g_settingsPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        printf("FAIL cannot write the settings file (error=%lu)\n", GetLastError());
        ++g_failures;
        return;
    }
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
    CloseHandle(file);
}

#define CHECK(condition, ...)                \
    do                                       \
    {                                        \
        if (condition)                       \
        {                                    \
            printf("ok   ");                 \
        }                                    \
        else                                 \
        {                                    \
            printf("FAIL ");                 \
            ++g_failures;                    \
        }                                    \
        printf(__VA_ARGS__);                 \
        printf("\n");                        \
    } while (0)

static const CLSID kClsid =
    {0x40874c30, 0xa23f, 0x4e8d, {0xb3, 0x83, 0xb4, 0x08, 0x45, 0xba, 0x55, 0x84}};

typedef HRESULT(STDAPICALLTYPE* GetClassObjectFn)(REFCLSID, REFIID, void**);

static HRESULT MakeFormat(UINT32 channels, float rate, IAudioMediaType** format)
{
    UNCOMPRESSEDAUDIOFORMAT f = {};
    f.guidFormatType = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    f.dwSamplesPerFrame = channels;
    f.dwBytesPerSampleContainer = 4;
    f.dwValidBitsPerSample = 32;
    f.fFramesPerSecond = rate;
    f.dwChannelMask = (channels == 2) ? 3 : 0;
    return CreateAudioMediaTypeFromUncompressedAudioFormat(&f, format);
}

// in と out を分けるか（inPlace = false）、同じバッファにするか（inPlace = true）で 1 回処理する。
// settingsAfterLock を渡すと、ロックのあとで設定ファイルを書き換え、APO が取り込むのを待ってから処理する
// （音を鳴らしている最中に設定を変えた場合に当たる）。expectedGain は、出力が入力の何倍になるはずか。
static void RunProcess(IAudioProcessingObjectRT* rt, IAudioProcessingObjectConfiguration* config,
                       IAudioMediaType* format, bool inPlace, UINT32 flags, const char* label,
                       float expectedGain = 1.0f, const char* settingsAfterLock = nullptr)
{
    const UINT32 channels = 2;
    const UINT32 frames = 480;
    const size_t bytes = frames * channels * sizeof(float);

    float* input = nullptr;
    float* output = nullptr;
    AERT_Allocate(bytes, reinterpret_cast<void**>(&input));
    if (inPlace)
        output = input;
    else
        AERT_Allocate(bytes, reinterpret_cast<void**>(&output));

    float expected[480 * 2];
    for (UINT32 i = 0; i < frames * channels; ++i)
    {
        input[i] = static_cast<float>(i % 97) / 97.0f - 0.5f;
        expected[i] = input[i] * expectedGain;
        if (!inPlace)
            output[i] = 123.0f;
    }

    APO_CONNECTION_DESCRIPTOR inDesc = {};
    inDesc.Type = APO_CONNECTION_BUFFER_TYPE_EXTERNAL;
    inDesc.pBuffer = reinterpret_cast<UINT_PTR>(input);
    inDesc.u32MaxFrameCount = frames;
    inDesc.pFormat = format;
    inDesc.u32Signature = APO_CONNECTION_DESCRIPTOR_SIGNATURE;
    APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
    outDesc.pBuffer = reinterpret_cast<UINT_PTR>(output);
    APO_CONNECTION_DESCRIPTOR* inDescs[] = {&inDesc};
    APO_CONNECTION_DESCRIPTOR* outDescs[] = {&outDesc};

    HRESULT hr = config->LockForProcess(1, inDescs, 1, outDescs);
    if (inPlace)
    {
        // この APO は APO_FLAG_INPLACE を付けずに登録している。同じバッファは断られるのが正しい。
        CHECK(hr == APOERR_BUFFERS_OVERLAP, "%s: LockForProcess is rejected hr=0x%08X", label,
              static_cast<unsigned>(hr));
    }
    else
    {
        CHECK(SUCCEEDED(hr), "%s: LockForProcess hr=0x%08X", label, static_cast<unsigned>(hr));
    }
    if (SUCCEEDED(hr))
    {
        APO_CONNECTION_PROPERTY inProp = {};
        inProp.pBuffer = reinterpret_cast<UINT_PTR>(input);
        inProp.u32ValidFrameCount = frames;
        inProp.u32BufferFlags = static_cast<APO_BUFFER_FLAGS>(flags);
        inProp.u32Signature = APO_CONNECTION_PROPERTY_SIGNATURE;
        APO_CONNECTION_PROPERTY outProp = {};
        outProp.pBuffer = reinterpret_cast<UINT_PTR>(output);
        outProp.u32ValidFrameCount = 0;
        outProp.u32BufferFlags = BUFFER_INVALID;
        outProp.u32Signature = APO_CONNECTION_PROPERTY_SIGNATURE;
        APO_CONNECTION_PROPERTY* inProps[] = {&inProp};
        APO_CONNECTION_PROPERTY* outProps[] = {&outProp};

        if (settingsAfterLock != nullptr)
        {
            WriteSettings(settingsAfterLock);
            Sleep(800); // 見張りのスレッドは 250 ms ごとにファイルを見る
        }

        rt->APOProcess(1, inProps, 1, outProps);

        CHECK(outProp.u32ValidFrameCount == frames, "%s: valid frame count = %u", label,
              outProp.u32ValidFrameCount);
        CHECK(static_cast<UINT32>(outProp.u32BufferFlags) == flags, "%s: buffer flags = %u", label,
              static_cast<unsigned>(outProp.u32BufferFlags));
        if (flags == BUFFER_VALID)
        {
            if (expectedGain == 1.0f)
            {
                CHECK(memcmp(output, expected, bytes) == 0, "%s: output equals input exactly", label);
            }
            else
            {
                float worst = 0.0f;
                for (UINT32 i = 0; i < frames * channels; ++i)
                {
                    float difference = output[i] - expected[i];
                    if (difference < 0.0f)
                        difference = -difference;
                    if (difference > worst)
                        worst = difference;
                }
                CHECK(worst < 1e-5f, "%s: output equals input x %.4f (largest error = %g)", label,
                      expectedGain, worst);
            }
        }

        hr = config->UnlockForProcess();
        CHECK(SUCCEEDED(hr), "%s: UnlockForProcess hr=0x%08X", label, static_cast<unsigned>(hr));
    }

    if (!inPlace)
        AERT_Free(output);
    AERT_Free(input);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        printf("usage: host_test.exe <path to equalizer_apo.dll>\n");
        return 2;
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // この PC に置いてある本物の設定に左右されないよう、一時の設定ファイルを使わせる。
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    swprintf_s(g_settingsPath, L"%sequalizer_host_test_%lu.txt", temp, GetCurrentProcessId());
    SetEnvironmentVariableW(L"EQAPO_SETTINGS_PATH", g_settingsPath);
    DeleteFileW(g_settingsPath);

    HMODULE dll = LoadLibraryW(argv[1]);
    CHECK(dll != nullptr, "LoadLibrary (error=%lu)", dll ? 0 : GetLastError());
    if (dll == nullptr)
        return 1;

    GetClassObjectFn getClassObject =
        reinterpret_cast<GetClassObjectFn>(GetProcAddress(dll, "DllGetClassObject"));
    CHECK(getClassObject != nullptr, "DllGetClassObject is exported");
    if (getClassObject == nullptr)
        return 1;

    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(kClsid, __uuidof(IClassFactory), reinterpret_cast<void**>(&factory));
    CHECK(SUCCEEDED(hr) && factory != nullptr, "class factory hr=0x%08X", static_cast<unsigned>(hr));
    if (factory == nullptr)
        return 1;

    IAudioProcessingObject* apo = nullptr;
    hr = factory->CreateInstance(nullptr, __uuidof(IAudioProcessingObject), reinterpret_cast<void**>(&apo));
    CHECK(SUCCEEDED(hr) && apo != nullptr, "CreateInstance hr=0x%08X", static_cast<unsigned>(hr));
    if (apo == nullptr)
        return 1;

    IAudioProcessingObjectRT* rt = nullptr;
    IAudioProcessingObjectConfiguration* config = nullptr;
    IAudioSystemEffects* effects = nullptr;
    IAudioSystemEffects2* effects2 = nullptr;
    IUnknown* unknown = nullptr;
    CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioProcessingObjectRT), reinterpret_cast<void**>(&rt))),
          "QueryInterface IAudioProcessingObjectRT");
    CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration),
                                        reinterpret_cast<void**>(&config))),
          "QueryInterface IAudioProcessingObjectConfiguration");
    CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioSystemEffects), reinterpret_cast<void**>(&effects))),
          "QueryInterface IAudioSystemEffects");
    CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioSystemEffects2), reinterpret_cast<void**>(&effects2))),
          "QueryInterface IAudioSystemEffects2");
    CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&unknown))),
          "QueryInterface IUnknown");
    void* nothing = nullptr;
    CHECK(apo->QueryInterface(__uuidof(IPersist), &nothing) == E_NOINTERFACE,
          "QueryInterface for an unsupported interface returns E_NOINTERFACE");
    if (rt == nullptr || config == nullptr)
        return 1;

    APO_REG_PROPERTIES* reg = nullptr;
    hr = apo->GetRegistrationProperties(&reg);
    CHECK(SUCCEEDED(hr) && reg != nullptr && reg->clsid == kClsid, "GetRegistrationProperties hr=0x%08X",
          static_cast<unsigned>(hr));
    if (reg != nullptr)
    {
        printf("     name=%ls flags=0x%X\n", reg->szFriendlyName, static_cast<unsigned>(reg->Flags));
        CoTaskMemFree(reg);
    }

    // 音のエンジンは、初期化のときに APOInitSystemEffects2 などの構造体を渡す。
    APOInitSystemEffects2 init = {};
    init.APOInit.cbSize = sizeof(init);
    init.APOInit.clsid = kClsid;
    init.AudioProcessingMode = AUDIO_SIGNALPROCESSINGMODE_DEFAULT;
    hr = apo->Initialize(sizeof(init), reinterpret_cast<BYTE*>(&init));
    CHECK(SUCCEEDED(hr), "Initialize(APOInitSystemEffects2) hr=0x%08X", static_cast<unsigned>(hr));

    if (effects2 != nullptr)
    {
        LPGUID ids = reinterpret_cast<LPGUID>(1);
        UINT count = 99;
        hr = effects2->GetEffectsList(&ids, &count, nullptr);
        CHECK(SUCCEEDED(hr) && ids == nullptr && count == 0, "GetEffectsList returns an empty list");
    }

    IAudioMediaType* format = nullptr;
    hr = MakeFormat(2, 48000.0f, &format);
    CHECK(SUCCEEDED(hr) && format != nullptr, "create float32 / 2ch / 48 kHz format hr=0x%08X",
          static_cast<unsigned>(hr));
    if (format == nullptr)
        return 1;

    IAudioMediaType* supported = nullptr;
    hr = apo->IsInputFormatSupported(nullptr, format, &supported);
    CHECK(hr == S_OK, "IsInputFormatSupported(float32) hr=0x%08X", static_cast<unsigned>(hr));
    if (supported != nullptr)
    {
        supported->Release();
        supported = nullptr;
    }
    hr = apo->IsOutputFormatSupported(format, format, &supported);
    CHECK(hr == S_OK, "IsOutputFormatSupported(float32, same as input) hr=0x%08X", static_cast<unsigned>(hr));
    if (supported != nullptr)
    {
        supported->Release();
        supported = nullptr;
    }

    // 設定ファイルが無い: 素通し
    RunProcess(rt, config, format, false, BUFFER_VALID, "no settings file");
    RunProcess(rt, config, format, true, BUFFER_VALID, "same buffer for input and output");
    RunProcess(rt, config, format, false, BUFFER_SILENT, "silent");

    // 音を鳴らしている最中に設定ファイルができる: 再起動なしで反映される（-6 dB = 0.501187 倍）
    RunProcess(rt, config, format, false, BUFFER_VALID, "settings written while locked", 0.501187f,
               "enabled=1\npreamp=-6\n");
    // 設定が既にある状態でロックする: 最初から反映されている
    RunProcess(rt, config, format, false, BUFFER_VALID, "settings present before lock", 0.501187f);
    // 切る: オリジナルの音に戻る
    RunProcess(rt, config, format, false, BUFFER_VALID, "disabled while locked", 1.0f,
               "enabled=0\npreamp=-6\nband=1000,6,1.41\n");
    // ファイルが消える: 素通し
    DeleteFileW(g_settingsPath);
    Sleep(800);
    RunProcess(rt, config, format, false, BUFFER_VALID, "settings file deleted");

    format->Release();
    if (unknown) unknown->Release();
    if (effects2) effects2->Release();
    if (effects) effects->Release();
    config->Release();
    rt->Release();
    ULONG remaining = apo->Release();
    CHECK(remaining == 0, "all references released (remaining=%lu)", remaining);
    factory->Release();

    typedef HRESULT(STDAPICALLTYPE* CanUnloadFn)();
    CanUnloadFn canUnload = reinterpret_cast<CanUnloadFn>(GetProcAddress(dll, "DllCanUnloadNow"));
    CHECK(canUnload != nullptr && canUnload() == S_OK, "DllCanUnloadNow returns S_OK after release");

    FreeLibrary(dll);
    DeleteFileW(g_settingsPath);
    CoUninitialize();

    printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
