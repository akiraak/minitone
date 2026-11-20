// イコライザーの設定と、フィルタの計算。
// Windows の音のエンジンに依存しない。単体テスト（apo/test/dsp_test.cpp）から直接使う。
#pragma once

#include <stddef.h>

namespace eq
{

const int kMaxBands = 32;
const int kMaxChannels = 8;
// 上げ幅を抑えるときの、出力の上限（1.0 がデジタルの上限）。
const double kCeiling = 0.98;

// フィルタの種類。
enum class BandType
{
    Peaking,   // 中心の周波数の周りだけを上げ下げする
    LowShelf,  // 指定の周波数から下をまとめて上げ下げする（Bass のツマミ）
    HighShelf, // 指定の周波数から上をまとめて上げ下げする（Treble のツマミ）
};

struct BandSetting
{
    BandType type;
    double frequencyHz;
    double gainDb;
    double q;
};

// 設定ファイルの中身。メモリの確保をしない、そのまま写せる構造体にしておく。
struct Settings
{
    bool enabled;
    double preampDb;
    // 音量の倍率。1.0 が元の音量、1.5 が 150%、0 が無音。preampDb と掛け合わせる。
    double volume;
    // true なら、上げているバンドの上げ幅を、出力が上限を超えない範囲に自動で抑える。
    // 上げた帯域の音が大きい瞬間だけ上げ幅が減り、ほかの帯域の音量は変わらない。
    bool limitBoost;
    int bandCount;
    BandSetting bands[kMaxBands];
};

// イコライザーを切った状態（オリジナルの音）。
Settings BypassSettings();

// 設定ファイルの中身を読む。読めない行、範囲外の値の行は捨てる。
// 1 行は "enabled=1" / "preamp=-3.0" / "volume=1.25" / "limit=1" / "band=周波数,増減,Q" /
// "lowshelf=周波数,増減,Q" / "highshelf=周波数,増減,Q" のどれか。"#" で始まる行は注釈。
// 何が書かれていても Settings を返す（空なら素通しの設定）。
Settings ParseSettings(const char* text, size_t length);

// 双二次フィルタの係数（a0 で割ってある）。
struct Biquad
{
    double b0, b1, b2, a1, a2;
};

// ピーキングのフィルタの係数を計算する（RBJ の Audio EQ Cookbook の式）。
Biquad MakePeaking(double sampleRate, double frequencyHz, double gainDb, double q);
// シェルビングのフィルタの係数（同じく RBJ の式）。
Biquad MakeLowShelf(double sampleRate, double frequencyHz, double gainDb, double q);
Biquad MakeHighShelf(double sampleRate, double frequencyHz, double gainDb, double q);

// 設定に従って音を処理する。インスタンスごとにフィルタの状態を持つ。
// Configure と Process はメモリの確保をしないので、リアルタイムのスレッドから呼べる。
class Processor
{
public:
    Processor();

    // 設定を取り込み、係数を計算し直す。バンドの数とチャンネルの数が前と同じなら、
    // フィルタの状態は引き継ぐ（設定を動かしたときに音が途切れないように）。
    // 音量は、前の値から新しい値へ約 10 ms かけてなめらかに動かす（ツマミを回したときの雑音を防ぐ）。
    // 素通しの状態から始まるときは、最初から新しい音量にする。
    void Configure(const Settings& settings, double sampleRate, unsigned channels);

    // 音を変えない設定（切ってある、またはチャンネルが多すぎる）なら true。
    bool IsBypassed() const { return bypassed_; }

    // input から output へ処理する。interleaved の 32 bit 浮動小数点。同じバッファでもよい。
    //
    // 上げ幅を抑えるバンドは、同じ入力に並べてかけ、出力を
    // 「入力 + 割合 x (各フィルタの出力 - 入力 の合計)」で作る。
    // 割合が 1 なら設定どおりの上げ幅。出力が上限（kCeiling）を超えそうな瞬間に割合をすぐ下げ、
    // そのあとゆっくり 1 へ戻す。入力が上限以下なら、出力も上限以下になる。
    // 割合は全バンドで共通なので、Bass と Treble は同じ比率で抑えられる。
    void Process(const float* input, float* output, unsigned frames);

private:
    bool bypassed_;
    unsigned channels_;
    int bandCount_;
    double targetGain_;  // 目標の音量（preamp と volume を掛けたもの）
    double currentGain_; // いまの音量。targetGain_ へ近づいていく
    double gainStep_;    // 1 サンプルごとに近づく割合
    Biquad coefficients_[kMaxBands];
    // 上げ幅を自動で抑えるバンドか（上げているバンドで、limitBoost のとき）。
    bool limited_[kMaxBands];
    // 抑えるバンドの、いま効かせている上げ幅の割合（0〜1）。1 なら設定どおり。全バンドで共通。
    double boostAmount_;
    double boostRelease_; // 抑えたあと、1 サンプルごとに 1 へ戻る割合
    // 転置直接形 II の状態。[バンド][チャンネル]
    double z1_[kMaxBands][kMaxChannels];
    double z2_[kMaxBands][kMaxChannels];
};

} // namespace eq
