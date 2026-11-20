// イコライザーの設定と、フィルタの計算。
// Windows の音のエンジンに依存しない。単体テスト（apo/test/dsp_test.cpp）から直接使う。
#pragma once

#include <stddef.h>

namespace eq
{

const int kMaxBands = 32;
const int kMaxChannels = 8;

struct BandSetting
{
    double frequencyHz;
    double gainDb;
    double q;
};

// 設定ファイルの中身。メモリの確保をしない、そのまま写せる構造体にしておく。
struct Settings
{
    bool enabled;
    double preampDb;
    int bandCount;
    BandSetting bands[kMaxBands];
};

// イコライザーを切った状態（オリジナルの音）。
Settings BypassSettings();

// 設定ファイルの中身を読む。読めない行、範囲外の値の行は捨てる。
// 1 行は "enabled=1" / "preamp=-3.0" / "band=周波数,増減,Q" のどれか。"#" で始まる行は注釈。
// 何が書かれていても Settings を返す（空なら素通しの設定）。
Settings ParseSettings(const char* text, size_t length);

// 双二次フィルタの係数（a0 で割ってある）。
struct Biquad
{
    double b0, b1, b2, a1, a2;
};

// ピーキングのフィルタの係数を計算する（RBJ の Audio EQ Cookbook の式）。
Biquad MakePeaking(double sampleRate, double frequencyHz, double gainDb, double q);

// 設定に従って音を処理する。インスタンスごとにフィルタの状態を持つ。
// Configure と Process はメモリの確保をしないので、リアルタイムのスレッドから呼べる。
class Processor
{
public:
    Processor();

    // 設定を取り込み、係数を計算し直す。バンドの数とチャンネルの数が前と同じなら、
    // フィルタの状態は引き継ぐ（設定を動かしたときに音が途切れないように）。
    void Configure(const Settings& settings, double sampleRate, unsigned channels);

    // 音を変えない設定（切ってある、またはチャンネルが多すぎる）なら true。
    bool IsBypassed() const { return bypassed_; }

    // input から output へ処理する。interleaved の 32 bit 浮動小数点。同じバッファでもよい。
    void Process(const float* input, float* output, unsigned frames);

private:
    bool bypassed_;
    unsigned channels_;
    int bandCount_;
    double preamp_;
    Biquad coefficients_[kMaxBands];
    // 転置直接形 II の状態。[バンド][チャンネル]
    double z1_[kMaxBands][kMaxChannels];
    double z2_[kMaxBands][kMaxChannels];
};

} // namespace eq
