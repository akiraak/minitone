// フィルタの計算と設定の読み取りの単体テスト。Windows の音のエンジンは使わない。
// WSL の g++ でもビルドできるよう、標準の関数（strncat など）を使う。MSVC の警告は止める。
#define _CRT_SECURE_NO_WARNINGS
#include "../src/Dsp.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(condition, ...)    \
    do                           \
    {                            \
        if (condition)           \
        {                        \
            printf("ok   ");     \
        }                        \
        else                     \
        {                        \
            printf("FAIL ");     \
            ++g_failures;        \
        }                        \
        printf(__VA_ARGS__);     \
        printf("\n");            \
    } while (0)

static const double kPi = 3.14159265358979323846;

static eq::Settings Parse(const char* text)
{
    return eq::ParseSettings(text, strlen(text));
}

// 正弦波を通し、落ち着いたあとの出力の大きさを入力との比（dB）で返す。
static double MeasureGainDb(const eq::Settings& settings, double sampleRate, double frequencyHz, unsigned channels)
{
    eq::Processor processor;
    processor.Configure(settings, sampleRate, channels);

    const unsigned total = static_cast<unsigned>(sampleRate * 2.0);
    const unsigned block = 480;
    static float input[480 * eq::kMaxChannels];
    static float output[480 * eq::kMaxChannels];
    double sumIn = 0.0;
    double sumOut = 0.0;

    for (unsigned start = 0; start < total; start += block)
    {
        for (unsigned i = 0; i < block; ++i)
        {
            float value = static_cast<float>(0.1 * sin(2.0 * kPi * frequencyHz * (start + i) / sampleRate));
            for (unsigned c = 0; c < channels; ++c)
                input[i * channels + c] = value;
        }
        processor.Process(input, output, block);
        if (start >= total / 2) // 後ろ半分だけを測る
        {
            for (unsigned i = 0; i < block; ++i)
            {
                // 最後のチャンネルで測る（チャンネルごとの状態が混ざっていないことも確かめる）。
                sumIn += static_cast<double>(input[i * channels + channels - 1]) * input[i * channels + channels - 1];
                sumOut += static_cast<double>(output[i * channels + channels - 1]) * output[i * channels + channels - 1];
            }
        }
    }
    return 10.0 * log10(sumOut / sumIn);
}

static void TestParse()
{
    eq::Settings s = Parse("enabled=1\npreamp=-3.5\nband=1000,4.5,1.41\nband=60, -6 , 0.7\n");
    CHECK(s.enabled && s.preampDb == -3.5 && s.bandCount == 2, "parse: basic file (bands=%d)", s.bandCount);
    CHECK(s.bands[0].frequencyHz == 1000.0 && s.bands[0].gainDb == 4.5 && s.bands[0].q == 1.41, "parse: first band");
    CHECK(s.bands[1].frequencyHz == 60.0 && s.bands[1].gainDb == -6.0 && s.bands[1].q == 0.7,
          "parse: spaces around numbers");

    s = Parse("\xEF\xBB\xBF" "enabled=1\r\n# comment\r\n\r\nband=100,3,1\r\n");
    CHECK(s.enabled && s.bandCount == 1, "parse: BOM, CRLF, comment and empty lines");

    s = Parse("");
    CHECK(!s.enabled && s.bandCount == 0 && s.preampDb == 0.0, "parse: empty text is bypass");

    s = eq::ParseSettings(nullptr, 0);
    CHECK(!s.enabled && s.bandCount == 0, "parse: null text is bypass");

    s = Parse("enabled=1\nband=abc,1,1\nband=1000,1\nband=1000,99,1\nband=5,1,1\nband=1000,1,0\nband=1000,nan,1\n"
              "band=1000,1,1x\npreamp=1000\ngarbage\n=\nband=2000,2,2\n");
    CHECK(s.bandCount == 1 && s.bands[0].frequencyHz == 2000.0 && s.preampDb == 0.0,
          "parse: broken and out-of-range lines are dropped (bands=%d)", s.bandCount);

    s = Parse("enabled=1\nvolume=1.25\nlowshelf=150,6,0.707\nhighshelf=5000,-4,0.707\nband=1000,2,1\n");
    CHECK(s.volume == 1.25 && s.bandCount == 3 && s.bands[0].type == eq::BandType::LowShelf &&
              s.bands[1].type == eq::BandType::HighShelf && s.bands[2].type == eq::BandType::Peaking,
          "parse: volume, lowshelf, highshelf");
    s = Parse("enabled=1\nvolume=-1\n");
    CHECK(s.volume == 1.0, "parse: negative volume is dropped");
    s = Parse("enabled=1\nvolume=99\n");
    CHECK(s.volume == 1.0, "parse: volume above the limit is dropped");
    CHECK(Parse("").volume == 1.0, "parse: default volume is 1.0");

    char many[4096] = "enabled=1\n";
    for (int i = 0; i < 40; ++i)
        strncat(many, "band=1000,1,1\n", sizeof(many) - strlen(many) - 1);
    s = Parse(many);
    CHECK(s.bandCount == eq::kMaxBands, "parse: bands are capped at %d", eq::kMaxBands);

    s = Parse("enabled=1\nband=1000,3,1"); // 最後に改行が無い
    CHECK(s.bandCount == 1, "parse: last line without newline");
}

static void TestResponse()
{
    eq::Settings s = Parse("enabled=1\nband=1000,6,1.41\n");
    double center = MeasureGainDb(s, 48000.0, 1000.0, 2);
    double low = MeasureGainDb(s, 48000.0, 50.0, 2);
    double high = MeasureGainDb(s, 48000.0, 15000.0, 2);
    CHECK(fabs(center - 6.0) < 0.05, "peaking +6 dB at 1 kHz: center = %.3f dB", center);
    CHECK(fabs(low) < 0.1 && fabs(high) < 0.1, "peaking: far frequencies unchanged (%.3f dB, %.3f dB)", low, high);

    s = Parse("enabled=1\nband=100,-9,2\n");
    center = MeasureGainDb(s, 44100.0, 100.0, 2);
    CHECK(fabs(center + 9.0) < 0.05, "peaking -9 dB at 100 Hz, 44.1 kHz: center = %.3f dB", center);

    s = Parse("enabled=1\npreamp=-6\n");
    double gain = MeasureGainDb(s, 48000.0, 440.0, 2);
    CHECK(fabs(gain + 6.0) < 0.01, "preamp -6 dB only: %.3f dB", gain);

    s = Parse("enabled=1\npreamp=-3\nband=1000,6,1.41\nband=1000,-6,1.41\n");
    gain = MeasureGainDb(s, 48000.0, 1000.0, 6);
    CHECK(fabs(gain + 3.0) < 0.05, "opposite bands cancel, 6 channels: %.3f dB", gain);

    s = Parse("enabled=1\nband=30000,6,1\n");
    gain = MeasureGainDb(s, 48000.0, 1000.0, 2);
    CHECK(fabs(gain) < 0.001, "band above Nyquist is ignored: %.3f dB", gain);

    // Bass のツマミ: 低い側だけが上がり、高い側は変わらない。
    s = Parse("enabled=1\nlowshelf=150,8,0.707\n");
    low = MeasureGainDb(s, 48000.0, 20.0, 2);
    double middle = MeasureGainDb(s, 48000.0, 150.0, 2);
    high = MeasureGainDb(s, 48000.0, 5000.0, 2);
    CHECK(fabs(low - 8.0) < 0.2, "low shelf +8 dB at 150 Hz: 20 Hz = %.3f dB", low);
    CHECK(fabs(middle - 4.0) < 0.2, "low shelf: half the gain at the corner (%.3f dB)", middle);
    CHECK(fabs(high) < 0.1, "low shelf: 5 kHz unchanged (%.3f dB)", high);

    // Treble のツマミ: 高い側だけが下がり、低い側は変わらない。
    s = Parse("enabled=1\nhighshelf=5000,-10,0.707\n");
    low = MeasureGainDb(s, 44100.0, 100.0, 2);
    middle = MeasureGainDb(s, 44100.0, 5000.0, 2);
    high = MeasureGainDb(s, 44100.0, 20000.0, 2);
    CHECK(fabs(low) < 0.1, "high shelf -10 dB at 5 kHz: 100 Hz unchanged (%.3f dB)", low);
    CHECK(fabs(middle + 5.0) < 0.2, "high shelf: half the gain at the corner (%.3f dB)", middle);
    CHECK(fabs(high + 10.0) < 0.2, "high shelf: 20 kHz = %.3f dB", high);

    // Volume のツマミ: 150% は +3.52 dB、50% は -6.02 dB、0% は無音。
    gain = MeasureGainDb(Parse("enabled=1\nvolume=1.5\n"), 48000.0, 440.0, 2);
    CHECK(fabs(gain - 3.5218) < 0.01, "volume 150%%: %.3f dB", gain);
    gain = MeasureGainDb(Parse("enabled=1\nvolume=0.5\npreamp=-6\n"), 48000.0, 440.0, 2);
    CHECK(fabs(gain + 12.0206) < 0.01, "volume 50%% with preamp -6 dB: %.3f dB", gain);
    {
        eq::Processor muted;
        muted.Configure(Parse("enabled=1\nvolume=0\n"), 48000.0, 2);
        float in[64];
        float out[64];
        for (int i = 0; i < 64; ++i)
        {
            in[i] = 0.5f;
            out[i] = 9.0f;
        }
        muted.Process(in, out, 32);
        bool silent = true;
        for (int i = 0; i < 64; ++i)
            silent = silent && out[i] == 0.0f;
        CHECK(!muted.IsBypassed() && silent, "volume 0%% is silence");
    }
}

static void TestBypass()
{
    float input[256];
    float output[256];
    for (int i = 0; i < 256; ++i)
        input[i] = static_cast<float>(i % 31) / 31.0f - 0.5f;

    eq::Processor processor;
    processor.Configure(Parse("enabled=0\npreamp=-6\nband=1000,6,1\n"), 48000.0, 2);
    memset(output, 0, sizeof(output));
    processor.Process(input, output, 128);
    CHECK(processor.IsBypassed() && memcmp(input, output, sizeof(input)) == 0, "enabled=0 is bit-exact pass-through");

    processor.Configure(Parse("enabled=1\nband=1000,0,1\n"), 48000.0, 2);
    memset(output, 0, sizeof(output));
    processor.Process(input, output, 128);
    CHECK(processor.IsBypassed() && memcmp(input, output, sizeof(input)) == 0,
          "enabled=1 with only 0 dB bands is bit-exact pass-through");

    processor.Configure(Parse("enabled=1\npreamp=-6\n"), 48000.0, 16);
    CHECK(processor.IsBypassed(), "more than %d channels is bypassed", eq::kMaxChannels);

    eq::Processor fresh;
    CHECK(fresh.IsBypassed(), "a new processor is bypassed until configured");
}

static void TestStability()
{
    // 全部のバンドを最大まで上げ、大きな入力を長く通しても、有限の値に収まること。
    char text[2048] = "enabled=1\npreamp=24\n";
    const double frequencies[] = {31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    for (double f : frequencies)
    {
        char line[64];
        snprintf(line, sizeof(line), "band=%.2f,24,20\n", f);
        strncat(text, line, sizeof(text) - strlen(text) - 1);
    }
    eq::Processor processor;
    processor.Configure(Parse(text), 48000.0, 2);

    static float buffer[960];
    bool finite = true;
    unsigned seed = 12345;
    for (int block = 0; block < 500 && finite; ++block)
    {
        for (int i = 0; i < 960; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            buffer[i] = static_cast<float>(static_cast<int>(seed >> 8) % 20001 - 10000) / 10000.0f;
        }
        processor.Process(buffer, buffer, 480); // 同じバッファで処理する
        for (int i = 0; i < 960; ++i)
        {
            if (!(buffer[i] == buffer[i]) || fabs(buffer[i]) > 1e12f)
                finite = false;
        }
    }
    CHECK(finite, "extreme settings with full-scale noise stay finite");

    // 設定を動かしても状態を引き継ぎ、出力が跳ねないこと。
    eq::Processor moving;
    double worstJump = 0.0;
    float previous = 0.0f;
    static float in[960];
    static float out[960];
    unsigned position = 0;
    for (int step = 0; step < 60; ++step)
    {
        char moved[64];
        snprintf(moved, sizeof(moved), "enabled=1\nband=1000,%.1f,1.41\n", 1.0 + step * 0.1);
        moving.Configure(Parse(moved), 48000.0, 2);
        for (int i = 0; i < 480; ++i, ++position)
        {
            float value = static_cast<float>(0.1 * sin(2.0 * kPi * 1000.0 * position / 48000.0));
            in[i * 2] = value;
            in[i * 2 + 1] = value;
        }
        moving.Process(in, out, 480);
        for (int i = 0; i < 480; ++i)
        {
            if (step > 0 || i > 0)
            {
                double jump = fabs(static_cast<double>(out[i * 2]) - previous);
                if (jump > worstJump)
                    worstJump = jump;
            }
            previous = out[i * 2];
        }
    }
    // 1 kHz / 48 kHz の正弦波は、隣のサンプルとの差が振幅の約 13% まで。振幅は最大でも 0.1 x 2.3 倍。
    CHECK(worstJump < 0.05, "moving a band while playing does not click (largest step = %.4f)", worstJump);

    // 音量を一気に変えても、出力が跳ねずになめらかに動くこと。
    eq::Processor volume;
    volume.Configure(Parse("enabled=1\nvolume=0.2\n"), 48000.0, 2);
    worstJump = 0.0;
    position = 0;
    previous = 0.0f;
    float last = 0.0f;
    for (int step = 0; step < 20; ++step)
    {
        if (step == 5)
            volume.Configure(Parse("enabled=1\nvolume=1.5\n"), 48000.0, 2);
        for (int i = 0; i < 480; ++i, ++position)
        {
            float value = static_cast<float>(0.5 * sin(2.0 * kPi * 100.0 * position / 48000.0));
            in[i * 2] = value;
            in[i * 2 + 1] = value;
        }
        volume.Process(in, out, 480);
        for (int i = 0; i < 480; ++i)
        {
            double jump = fabs(static_cast<double>(out[i * 2]) - previous);
            if (jump > worstJump)
                worstJump = jump;
            previous = out[i * 2];
        }
        last = out[479 * 2] / in[479 * 2];
    }
    // 100 Hz / 48 kHz、振幅 0.75 の正弦波そのものの最大の差は約 0.01。
    CHECK(worstJump < 0.02, "a sudden volume change is smoothed (largest step = %.4f)", worstJump);
    CHECK(fabs(last - 1.5f) < 0.001f, "the volume settles at the new value (x %.4f)", last);
}

int main()
{
    TestParse();
    TestResponse();
    TestBypass();
    TestStability();
    printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
