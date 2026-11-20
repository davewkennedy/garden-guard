#include "audio.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace {

constexpr int RATE = 48000;
constexpr double MASTER_GAIN = 0.12;   // spec: <= 0.15
constexpr qint64 THROTTLE_MS = 50;     // same sound at most once per 50ms
constexpr int MAX_VOICES = 12;

enum class Osc { Sine, Triangle, Square, Saw, Noise };
enum class Filter { None, Lowpass, Highpass, Bandpass };

// One tone or noise burst inside a sound, like a Web Audio oscillator + filter + gain.
struct Part {
    double at = 0;          // start offset (s)
    Osc osc = Osc::Sine;
    double freq = 440;      // Hz (filter centre for noise)
    double freqEnd = 0;     // exponential glide target, 0 = none
    double dur = 0.1;       // s
    double gain = 0.3;
    double attack = 0.005;  // s
    Filter filter = Filter::None;
    double filterFreq = 1000;
    double q = 1;
};

Part tone(double at, Osc osc, double freq, double freqEnd, double dur, double gain,
          double attack = 0.005, Filter filter = Filter::None, double filterFreq = 1000) {
    Part p;
    p.at = at; p.osc = osc; p.freq = freq; p.freqEnd = freqEnd; p.dur = dur;
    p.gain = gain; p.attack = attack; p.filter = filter; p.filterFreq = filterFreq;
    return p;
}

Part noise(double at, Filter filter, double freq, double dur, double gain,
           double attack = 0.003, double q = 1) {
    Part p;
    p.at = at; p.osc = Osc::Noise; p.dur = dur; p.gain = gain; p.attack = attack;
    p.filter = filter; p.filterFreq = freq; p.q = q;
    return p;
}

// RBJ "audio EQ cookbook" biquad, the same filter shapes Web Audio uses.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    Biquad(Filter type, double freq, double q) {
        if (type == Filter::None) return;
        double w = 2 * M_PI * std::min(freq, RATE * 0.49) / RATE;
        double cw = std::cos(w), alpha = std::sin(w) / (2 * q);
        double a0 = 1 + alpha;
        switch (type) {
        case Filter::Lowpass:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; break;
        case Filter::Highpass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; break;
        case Filter::Bandpass:
            b0 = alpha; b1 = 0; b2 = -alpha; break;
        case Filter::None: break;
        }
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 = -2 * cw / a0;
        a2 = (1 - alpha) / a0;
    }

    double operator()(double x) {
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

void render(std::vector<float> &buf, const Part &p, std::mt19937 &rng) {
    std::uniform_real_distribution<double> white(-1, 1);
    // Lowpass/highpass use a gentle Q like Web Audio's default; bandpass uses the given Q.
    Biquad filter(p.filter, p.filterFreq, p.filter == Filter::Bandpass ? p.q : 0.707);
    const size_t start = size_t(p.at * RATE);
    const size_t len = size_t(p.dur * RATE);
    if (buf.size() < start + len) buf.resize(start + len, 0.f);

    double phase = 0;
    for (size_t i = 0; i < len; i++) {
        double t = double(i) / RATE;

        // Envelope: quick linear attack, then exponential decay to silence at `dur`.
        double env;
        if (t < p.attack) env = 0.0001 + (p.gain - 0.0001) * t / p.attack;
        else env = p.gain * std::pow(0.0001 / p.gain, (t - p.attack) / std::max(1e-6, p.dur - p.attack));

        double s;
        if (p.osc == Osc::Noise) {
            s = white(rng);
        } else {
            double f = p.freqEnd > 0 ? p.freq * std::pow(p.freqEnd / p.freq, t / p.dur) : p.freq;
            phase += f / RATE;
            phase -= std::floor(phase);
            switch (p.osc) {
            case Osc::Sine: s = std::sin(2 * M_PI * phase); break;
            case Osc::Triangle: s = 4 * std::abs(phase - 0.5) - 1; break;
            case Osc::Square: s = phase < 0.5 ? 1 : -1; break;
            default: s = 2 * phase - 1; break;   // sawtooth
            }
        }
        if (p.filter != Filter::None) s = filter(s);
        buf[start + i] += float(s * env * MASTER_GAIN);
    }
}

QHash<QString, std::vector<Part>> recipes() {
    using O = Osc;
    using F = Filter;
    QHash<QString, std::vector<Part>> r;
    r["shoot_sprout"] = {tone(0, O::Sine, 620, 300, 0.07, 0.35)};
    r["shoot_frost"] = {noise(0, F::Highpass, 5000, 0.18, 0.18, 0.02),
                        tone(0, O::Triangle, 1800, 2500, 0.16, 0.07, 0.02)};
    r["shoot_mortar"] = {tone(0, O::Sine, 150, 48, 0.26, 0.8),
                         noise(0, F::Lowpass, 500, 0.12, 0.3)};
    r["shoot_cactus"] = {noise(0, F::Bandpass, 3200, 0.045, 0.45, 0.003, 2),
                         tone(0, O::Square, 1300, 800, 0.035, 0.06)};
    r["shoot_mushroom"] = {noise(0, F::Lowpass, 1400, 0.12, 0.3, 0.01),   // soft "pfff"
                           tone(0, O::Sine, 340, 210, 0.1, 0.18, 0.01)};
    r["shoot_chili"] = {noise(0, F::Bandpass, 900, 0.22, 0.5, 0.02, 0.8),  // fiery whoosh
                        noise(0.02, F::Highpass, 3000, 0.12, 0.12, 0.01),
                        tone(0, O::Saw, 90, 60, 0.2, 0.12, 0.02, F::Lowpass, 400)};
    r["shoot_rose"] = {noise(0, F::Highpass, 4200, 0.09, 0.3, 0.004),        // thorny "fwip"
                       tone(0, O::Triangle, 900, 1500, 0.08, 0.12, 0.005)};
    r["shoot_corn"] = {noise(0, F::Bandpass, 2200, 0.05, 0.4, 0.002, 3),      // "pop!"
                       tone(0, O::Sine, 500, 900, 0.05, 0.2, 0.002)};
    r["harvest"] = {tone(0, O::Sine, 1047, 0, 0.12, 0.18, 0.005),          // sunflower coins
                    tone(0.08, O::Sine, 1319, 0, 0.12, 0.18, 0.005),
                    tone(0.16, O::Sine, 1568, 0, 0.2, 0.18, 0.005)};
    r["kill"] = {tone(0, O::Triangle, 1150, 1800, 0.09, 0.28)};
    r["leak"] = {tone(0, O::Saw, 110, 0, 0.38, 0.25, 0.02, F::Lowpass, 700),
                 tone(0, O::Saw, 116, 0, 0.38, 0.2, 0.02, F::Lowpass, 700)};
    r["place"] = {tone(0, O::Sine, 240, 130, 0.16, 0.6),
                  noise(0, F::Lowpass, 900, 0.05, 0.2)};
    {
        std::vector<Part> up;
        const double notes[] = {660, 880, 1100, 1320};
        for (int i = 0; i < 4; i++) up.push_back(tone(i * 0.06, O::Triangle, notes[i], 0, 0.12, 0.22));
        up.push_back(noise(0.18, F::Highpass, 7000, 0.15, 0.08));
        r["upgrade"] = up;
    }
    r["sell"] = {tone(0, O::Triangle, 1320, 0, 0.06, 0.25),
                 tone(0.07, O::Triangle, 1760, 0, 0.24, 0.25),
                 noise(0.07, F::Highpass, 6000, 0.1, 0.1)};
    r["waveStart"] = {tone(0, O::Saw, 392, 0, 0.18, 0.25, 0.02, F::Lowpass, 1400),
                      tone(0.2, O::Saw, 523, 0, 0.32, 0.25, 0.02, F::Lowpass, 1600)};
    r["waveEnd"] = {tone(0, O::Triangle, 523, 0, 0.6, 0.16, 0.02),
                    tone(0.03, O::Triangle, 659, 0, 0.6, 0.16, 0.02),
                    tone(0.06, O::Triangle, 784, 0, 0.6, 0.16, 0.02)};
    r["win"] = {tone(0, O::Triangle, 523, 0, 0.13, 0.25, 0.01),
                tone(0.13, O::Triangle, 659, 0, 0.13, 0.25, 0.01),
                tone(0.26, O::Triangle, 784, 0, 0.13, 0.25, 0.01),
                tone(0.39, O::Triangle, 1047, 0, 0.6, 0.25, 0.01),
                tone(0.39, O::Sine, 523 / 2.0, 0, 0.6, 0.12, 0.03),
                tone(0.39, O::Sine, 659 / 2.0, 0, 0.6, 0.12, 0.03),
                tone(0.39, O::Sine, 784 / 2.0, 0, 0.6, 0.12, 0.03)};
    r["lose"] = {tone(0, O::Triangle, 392, 0, 0.22, 0.25, 0.01),
                 tone(0.22, O::Triangle, 349, 0, 0.22, 0.25, 0.01),
                 tone(0.44, O::Triangle, 311, 0, 0.22, 0.25, 0.01),
                 tone(0.66, O::Triangle, 262, 220, 0.45, 0.25, 0.01)};
    r["click"] = {tone(0, O::Sine, 1500, 0, 0.025, 0.1, 0.002)};
    r["error"] = {tone(0, O::Square, 180, 0, 0.08, 0.2, 0.005, F::Lowpass, 800),
                  tone(0.12, O::Square, 150, 0, 0.1, 0.2, 0.005, F::Lowpass, 800)};
    // ---- Bug abilities (claude2-63) ----
    // Cricket: two quick high "chirp-chirp" trills.
    r["bug_hop"] = {tone(0, O::Square, 4200, 4600, 0.03, 0.05, 0.002, F::Bandpass, 4400),
                    tone(0.045, O::Square, 4300, 4700, 0.03, 0.05, 0.002, F::Bandpass, 4400),
                    noise(0, F::Lowpass, 600, 0.05, 0.08)};
    // Mosquito heal: soft rising shimmer.
    r["bug_heal"] = {tone(0, O::Sine, 660, 1320, 0.32, 0.1, 0.06),
                     tone(0.04, O::Sine, 990, 1980, 0.28, 0.07, 0.06),
                     noise(0, F::Highpass, 8000, 0.3, 0.03, 0.08)};
    // Caterpillar split: wet splat.
    r["bug_split"] = {noise(0, F::Lowpass, 700, 0.16, 0.45, 0.002, 2),
                      tone(0, O::Sine, 220, 70, 0.14, 0.35, 0.002),
                      noise(0.05, F::Bandpass, 1600, 0.06, 0.15, 0.002, 4)};
    // Golden beetle: bright coin fanfare (~0.6 s).
    {
        std::vector<Part> fan;
        const double notes[] = {1047, 1319, 1568, 2093};
        for (int i = 0; i < 4; i++) {
            fan.push_back(tone(i * 0.07, O::Square, notes[i], 0, 0.1, 0.09, 0.003, F::Lowpass, 5000));
            fan.push_back(tone(i * 0.07, O::Sine, notes[i] * 2, 0, 0.08, 0.06, 0.003));
        }
        fan.push_back(tone(0.3, O::Triangle, 2093, 0, 0.3, 0.2, 0.005));
        fan.push_back(tone(0.3, O::Triangle, 2637, 0, 0.3, 0.14, 0.005));
        fan.push_back(noise(0.3, F::Highpass, 7000, 0.3, 0.08));
        r["jackpot"] = fan;
    }
    // Boss entrance: a low, soft rumble (kept gentle for a cozy game).
    r["boss_rumble"] = {noise(0, F::Lowpass, 160, 1.0, 0.55, 0.15, 1),
                        tone(0, O::Sine, 55, 40, 1.0, 0.35, 0.12),
                        tone(0.05, O::Sine, 82, 60, 0.8, 0.18, 0.1)};
    // Scorpion spit: a short wet "ptoo" (the splat on landing reuses bug_split).
    r["bug_spit"] = {tone(0, O::Sine, 420, 900, 0.09, 0.22, 0.003),
                     noise(0, F::Bandpass, 1400, 0.08, 0.25, 0.002, 3)};
    return r;
}

}  // namespace

// ---------- Mixer ----------

Mixer::Mixer(QObject *parent) : QIODevice(parent) {
    open(QIODevice::ReadOnly);
}

void Mixer::add(const std::vector<float> *samples) {
    voices_.push_back({samples, 0});
}

qint64 Mixer::readData(char *out, qint64 maxlen) {
    // Always return a full buffer (silence when idle) so the stream never stalls.
    const qint64 frames = maxlen / qint64(sizeof(qint16));
    auto *dst = reinterpret_cast<qint16 *>(out);
    for (qint64 i = 0; i < frames; i++) {
        float mix = 0;
        for (auto &v : voices_)
            if (v.pos < v.samples->size()) mix += (*v.samples)[v.pos++];
        dst[i] = qint16(std::clamp(mix, -1.f, 1.f) * 32767);
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice &v) { return v.pos >= v.samples->size(); }),
                  voices_.end());
    return frames * qint64(sizeof(qint16));
}

// ---------- Audio ----------

Audio::Audio(QObject *parent) : QObject(parent) {
    QSettings settings;
    enabled_ = settings.value("sound", true).toBool();
    clock_.start();

    std::mt19937 rng(12345);
    const auto all = recipes();
    for (auto it = all.begin(); it != all.end(); ++it) {
        std::vector<float> buf;
        for (const Part &p : it.value()) render(buf, p, rng);
        sounds_.insert(it.key(), std::move(buf));
    }
    if (enabled_) start();
}

Audio::~Audio() {
    if (sink_) sink_->stop();
}

void Audio::start() {
    if (sink_ || failed_) return;
    QAudioFormat fmt;
    fmt.setSampleRate(RATE);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() || !device.isFormatSupported(fmt)) {
        failed_ = true;
        qWarning("Garden Guard: no usable audio output, sound disabled");
        return;
    }
    mixer_ = new Mixer(this);
    sink_ = new QAudioSink(device, fmt, this);
    sink_->setBufferSize(RATE * 2 / 20);   // ~50ms keeps sounds in sync with the action
    sink_->start(mixer_);
}

void Audio::play(const QString &name) {
    if (!enabled_ || failed_) return;
    start();
    if (!sink_) return;
    auto it = sounds_.constFind(name);
    if (it == sounds_.constEnd()) return;
    const qint64 now = clock_.elapsed();
    auto last = lastPlayed_.constFind(name);
    if (last != lastPlayed_.constEnd() && now - *last < THROTTLE_MS) return;
    if (mixer_->voices() >= MAX_VOICES) return;
    lastPlayed_[name] = now;
    mixer_->add(&*it);
}

void Audio::setEnabled(bool on) {
    enabled_ = on;
    QSettings().setValue("sound", on);
    if (on) start();
}
