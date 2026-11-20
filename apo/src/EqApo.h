// 音のエンジン（audiodg.exe）に読み込まれる APO の本体。
// 設定ファイルに従ってイコライザーをかける。設定が無い、または切ってあるときは素通し。
#pragma once

#include <windows.h>
#include <audioenginebaseapo.h>
#include <baseaudioprocessingobject.h>

#include "Dsp.h"

// {40874C30-A23F-4E8D-B383-B40845BA5584}
extern const CLSID CLSID_EqApo;

extern long g_objectCount;
extern long g_lockCount;

void EqLog(const wchar_t* format, ...);

// 集約（aggregation）されたときに、外側の IUnknown へ回さない側の IUnknown。
// メソッドの並びは IUnknown と同じ。
struct INonDelegatingUnknown
{
    virtual HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void** ppv) = 0;
    virtual ULONG STDMETHODCALLTYPE NonDelegatingAddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE NonDelegatingRelease() = 0;
};

class EqApo : public CBaseAudioProcessingObject,
              public IAudioSystemEffects2,
              public INonDelegatingUnknown
{
public:
    explicit EqApo(IUnknown* outer);
    virtual ~EqApo();

    // IUnknown（集約されていれば外側へ回す）
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // INonDelegatingUnknown
    HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE NonDelegatingAddRef() override;
    ULONG STDMETHODCALLTYPE NonDelegatingRelease() override;

    // IAudioProcessingObject
    STDMETHOD(Initialize)(UINT32 cbDataSize, BYTE* pbyData) override;
    STDMETHOD(IsInputFormatSupported)(IAudioMediaType* pOutputFormat,
                                      IAudioMediaType* pRequestedInputFormat,
                                      IAudioMediaType** ppSupportedInputFormat) override;
    STDMETHOD(IsOutputFormatSupported)(IAudioMediaType* pInputFormat,
                                       IAudioMediaType* pRequestedOutputFormat,
                                       IAudioMediaType** ppSupportedOutputFormat) override;

    // IAudioProcessingObjectConfiguration
    STDMETHOD(LockForProcess)(UINT32 u32NumInputConnections,
                              APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                              UINT32 u32NumOutputConnections,
                              APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override;

    // IAudioSystemEffects2
    STDMETHOD(GetEffectsList)(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event) override;

    // IAudioProcessingObjectRT（リアルタイムのスレッドから呼ばれる）
    STDMETHOD_(void, APOProcess)(UINT32 u32NumInputConnections,
                                 APO_CONNECTION_PROPERTY** ppInputConnections,
                                 UINT32 u32NumOutputConnections,
                                 APO_CONNECTION_PROPERTY** ppOutputConnections) override;

    static const CRegAPOProperties<1> regProperties;

private:
    long refCount_;
    IUnknown* outer_;
    // ここから下は、LockForProcess で用意し、そのあとは APOProcess だけが触る。
    eq::Processor processor_;
    unsigned seenVersion_; // processor_ に取り込んである設定の版
    double sampleRate_;
    unsigned channels_;
};
