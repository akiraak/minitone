#include "Dsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace eq
{

namespace
{

const double kPi = 3.14159265358979323846;

// 受け付ける値の範囲。外れた値の行は捨てる。
const double kMinFrequencyHz = 10.0;
const double kMaxFrequencyHz = 22000.0;
const double kMaxGainDb = 24.0;
const double kMinQ = 0.1;
const double kMaxQ = 20.0;
const double kMaxVolume = 4.0;
const double kGainSmoothingSeconds = 0.010;

double DbToLinear(double db)
{
    return pow(10.0, db / 20.0);
}

// [begin, end) を数値として読む。全部を読めて、有限の値なら true。
bool ParseNumber(const char* begin, const char* end, double* value)
{
    char buffer[64];
    while (begin < end && (*begin == ' ' || *begin == '\t'))
        ++begin;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t'))
        --end;
    size_t length = static_cast<size_t>(end - begin);
    if (length == 0 || length >= sizeof(buffer))
        return false;
    memcpy(buffer, begin, length);
    buffer[length] = '\0';

    char* stop = nullptr;
    double parsed = strtod(buffer, &stop);
    if (stop != buffer + length)
        return false;
    if (!(parsed == parsed) || parsed > 1e300 || parsed < -1e300) // NaN と無限大を弾く
        return false;
    *value = parsed;
    return true;
}

bool KeyIs(const char* begin, const char* end, const char* key)
{
    size_t length = strlen(key);
    return static_cast<size_t>(end - begin) == length && memcmp(begin, key, length) == 0;
}

void ParseLine(const char* begin, const char* end, Settings* settings)
{
    while (begin < end && (*begin == ' ' || *begin == '\t'))
        ++begin;
    if (begin == end || *begin == '#')
        return;

    const char* equals = begin;
    while (equals < end && *equals != '=')
        ++equals;
    if (equals == end)
        return;

    const char* keyEnd = equals;
    while (keyEnd > begin && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t'))
        --keyEnd;
    const char* value = equals + 1;

    if (KeyIs(begin, keyEnd, "enabled"))
    {
        double number = 0.0;
        if (ParseNumber(value, end, &number))
            settings->enabled = (number != 0.0);
    }
    else if (KeyIs(begin, keyEnd, "preamp"))
    {
        double number = 0.0;
        if (ParseNumber(value, end, &number) && number >= -kMaxGainDb && number <= kMaxGainDb)
            settings->preampDb = number;
    }
    else if (KeyIs(begin, keyEnd, "volume"))
    {
        double number = 0.0;
        if (ParseNumber(value, end, &number) && number >= 0.0 && number <= kMaxVolume)
            settings->volume = number;
    }
    else if (KeyIs(begin, keyEnd, "band") || KeyIs(begin, keyEnd, "lowshelf") ||
             KeyIs(begin, keyEnd, "highshelf"))
    {
        BandType type = BandType::Peaking;
        if (KeyIs(begin, keyEnd, "lowshelf"))
            type = BandType::LowShelf;
        else if (KeyIs(begin, keyEnd, "highshelf"))
            type = BandType::HighShelf;

        const char* comma1 = value;
        while (comma1 < end && *comma1 != ',')
            ++comma1;
        if (comma1 == end)
            return;
        const char* comma2 = comma1 + 1;
        while (comma2 < end && *comma2 != ',')
            ++comma2;
        if (comma2 == end)
            return;

        BandSetting band = {};
        band.type = type;
        if (!ParseNumber(value, comma1, &band.frequencyHz) ||
            !ParseNumber(comma1 + 1, comma2, &band.gainDb) || !ParseNumber(comma2 + 1, end, &band.q))
            return;
        if (band.frequencyHz < kMinFrequencyHz || band.frequencyHz > kMaxFrequencyHz)
            return;
        if (band.gainDb < -kMaxGainDb || band.gainDb > kMaxGainDb)
            return;
        if (band.q < kMinQ || band.q > kMaxQ)
            return;
        if (settings->bandCount < kMaxBands)
            settings->bands[settings->bandCount++] = band;
    }
}

} // namespace

Settings BypassSettings()
{
    Settings settings = {};
    settings.enabled = false;
    settings.preampDb = 0.0;
    settings.volume = 1.0;
    settings.bandCount = 0;
    return settings;
}

Settings ParseSettings(const char* text, size_t length)
{
    Settings settings = BypassSettings();
    if (text == nullptr)
        return settings;

    const char* end = text + length;
    // UTF-8 の BOM は読み飛ばす。
    if (length >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text += 3;

    const char* line = text;
    while (line < end)
    {
        const char* lineEnd = line;
        while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
            ++lineEnd;
        ParseLine(line, lineEnd, &settings);
        line = lineEnd;
        while (line < end && (*line == '\n' || *line == '\r'))
            ++line;
    }
    return settings;
}

Biquad MakePeaking(double sampleRate, double frequencyHz, double gainDb, double q)
{
    const double a = pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * frequencyHz / sampleRate;
    const double cosW0 = cos(w0);
    const double alpha = sin(w0) / (2.0 * q);

    const double a0 = 1.0 + alpha / a;
    Biquad biquad;
    biquad.b0 = (1.0 + alpha * a) / a0;
    biquad.b1 = (-2.0 * cosW0) / a0;
    biquad.b2 = (1.0 - alpha * a) / a0;
    biquad.a1 = (-2.0 * cosW0) / a0;
    biquad.a2 = (1.0 - alpha / a) / a0;
    return biquad;
}

Biquad MakeLowShelf(double sampleRate, double frequencyHz, double gainDb, double q)
{
    const double a = pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * frequencyHz / sampleRate;
    const double cosW0 = cos(w0);
    const double alpha = sin(w0) / (2.0 * q);
    const double k = 2.0 * sqrt(a) * alpha;

    const double a0 = (a + 1.0) + (a - 1.0) * cosW0 + k;
    Biquad biquad;
    biquad.b0 = a * ((a + 1.0) - (a - 1.0) * cosW0 + k) / a0;
    biquad.b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosW0) / a0;
    biquad.b2 = a * ((a + 1.0) - (a - 1.0) * cosW0 - k) / a0;
    biquad.a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosW0) / a0;
    biquad.a2 = ((a + 1.0) + (a - 1.0) * cosW0 - k) / a0;
    return biquad;
}

Biquad MakeHighShelf(double sampleRate, double frequencyHz, double gainDb, double q)
{
    const double a = pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * frequencyHz / sampleRate;
    const double cosW0 = cos(w0);
    const double alpha = sin(w0) / (2.0 * q);
    const double k = 2.0 * sqrt(a) * alpha;

    const double a0 = (a + 1.0) - (a - 1.0) * cosW0 + k;
    Biquad biquad;
    biquad.b0 = a * ((a + 1.0) + (a - 1.0) * cosW0 + k) / a0;
    biquad.b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW0) / a0;
    biquad.b2 = a * ((a + 1.0) + (a - 1.0) * cosW0 - k) / a0;
    biquad.a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosW0) / a0;
    biquad.a2 = ((a + 1.0) - (a - 1.0) * cosW0 - k) / a0;
    return biquad;
}

Processor::Processor()
    : bypassed_(true), channels_(0), bandCount_(0), targetGain_(1.0), currentGain_(1.0), gainStep_(1.0)
{
    memset(coefficients_, 0, sizeof(coefficients_));
    memset(z1_, 0, sizeof(z1_));
    memset(z2_, 0, sizeof(z2_));
}

void Processor::Configure(const Settings& settings, double sampleRate, unsigned channels)
{
    // ナイキスト周波数より上のバンド、増減が 0 のバンドは使わない。
    Biquad coefficients[kMaxBands];
    int bandCount = 0;
    if (settings.enabled && sampleRate > 0.0)
    {
        for (int i = 0; i < settings.bandCount && i < kMaxBands; ++i)
        {
            const BandSetting& band = settings.bands[i];
            if (band.gainDb == 0.0 || band.frequencyHz >= sampleRate * 0.5)
                continue;
            switch (band.type)
            {
            case BandType::LowShelf:
                coefficients[bandCount++] = MakeLowShelf(sampleRate, band.frequencyHz, band.gainDb, band.q);
                break;
            case BandType::HighShelf:
                coefficients[bandCount++] = MakeHighShelf(sampleRate, band.frequencyHz, band.gainDb, band.q);
                break;
            default:
                coefficients[bandCount++] = MakePeaking(sampleRate, band.frequencyHz, band.gainDb, band.q);
                break;
            }
        }
    }

    const bool supported = channels >= 1 && channels <= static_cast<unsigned>(kMaxChannels);
    const double gain = DbToLinear(settings.preampDb) * settings.volume;
    const bool bypassed = !settings.enabled || !supported || (bandCount == 0 && gain == 1.0);

    // 形が変わったときだけ状態を消す。同じ形なら引き継ぐ。
    if (bypassed || bandCount != bandCount_ || channels != channels_ || bypassed_)
    {
        memset(z1_, 0, sizeof(z1_));
        memset(z2_, 0, sizeof(z2_));
    }

    // 音量は、素通しから入るときは新しい値から始め、それ以外は前の値からなめらかに動かす。
    // 素通しへ出るときは、次に入るときのために 1.0 に戻しておく。
    targetGain_ = bypassed ? 1.0 : gain;
    if (bypassed || bypassed_)
        currentGain_ = targetGain_;
    gainStep_ = (sampleRate > 0.0) ? 1.0 - exp(-1.0 / (kGainSmoothingSeconds * sampleRate)) : 1.0;

    bypassed_ = bypassed;
    channels_ = channels;
    bandCount_ = bandCount;
    for (int i = 0; i < bandCount; ++i)
        coefficients_[i] = coefficients[i];
}

void Processor::Process(const float* input, float* output, unsigned frames)
{
    if (bypassed_)
    {
        if (input != output)
            memmove(output, input, static_cast<size_t>(frames) * channels_ * sizeof(float));
        return;
    }

    const unsigned channels = channels_;
    const int bandCount = bandCount_;
    const double targetGain = targetGain_;
    const double gainStep = gainStep_;
    double gain = currentGain_;

    for (unsigned frame = 0; frame < frames; ++frame)
    {
        gain += (targetGain - gain) * gainStep;
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            double sample = static_cast<double>(input[frame * channels + channel]) * gain;
            for (int band = 0; band < bandCount; ++band)
            {
                const Biquad& c = coefficients_[band];
                const double out = c.b0 * sample + z1_[band][channel];
                z1_[band][channel] = c.b1 * sample - c.a1 * out + z2_[band][channel];
                z2_[band][channel] = c.b2 * sample - c.a2 * out;
                sample = out;
            }
            output[frame * channels + channel] = static_cast<float>(sample);
        }
    }

    // 目標にほぼ着いたら、ぴったり合わせる（いつまでも端数を追いかけない）。
    if (fabs(targetGain - gain) < 1e-9)
        gain = targetGain;
    currentGain_ = gain;

    // 無音が続くと状態がごく小さい値（非正規化数）になり、計算が極端に遅くなる。0 に落とす。
    for (int band = 0; band < bandCount; ++band)
    {
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            if (fabs(z1_[band][channel]) < 1e-30)
                z1_[band][channel] = 0.0;
            if (fabs(z2_[band][channel]) < 1e-30)
                z2_[band][channel] = 0.0;
        }
    }
}

} // namespace eq
