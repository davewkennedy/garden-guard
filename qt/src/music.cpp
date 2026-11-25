// Garden Guard: background music (owner: Bill). See music.h.
#include "music.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QLoggingCategory>
#include <QSettings>
#include <QTimer>
#include <cmath>

using namespace music;

// QT_LOGGING_RULES="gg.music.debug=true": sink start, phase changes, and the output peak every 5 s.
Q_LOGGING_CATEGORY(lcMusic, "gg.music", QtWarningMsg)

namespace {
constexpr float HALF_PI = 1.5707963f;

// Track names in music.json -> files in web/audio (embedded as :/audio/...).
struct Def { const char *name, *file; bool loop; };
constexpr Def DEFS[] = {
    {"build", "music-build.mp3", true},
    {"wave", "music-wave.mp3", true},
    {"win", "stinger-win.mp3", false},
    {"lose", "stinger-lose.mp3", false},
};

// Loop window in samples: loop_start/loop_end, else loopStart/loopEnd seconds, else
// "loop" seconds starting at 0.5 s (Dale's files: 0.5 s lead-in, 1.5 s wrapped tail).
bool loopWindow(const QJsonObject &t, double rate, size_t *start, size_t *end) {
    if (t.contains("loop_start") && t.contains("loop_end")) {
        *start = size_t(t.value("loop_start").toDouble());
        *end = size_t(t.value("loop_end").toDouble());
    } else if (t.contains("loopStart") && t.contains("loopEnd")) {
        *start = size_t(std::llround(t.value("loopStart").toDouble() * rate));
        *end = size_t(std::llround(t.value("loopEnd").toDouble() * rate));
    } else if (t.contains("loop")) {
        *start = size_t(std::llround(0.5 * rate));
        *end = *start + size_t(std::llround(t.value("loop").toDouble() * rate));
    } else {
        return false;
    }
    return *end > *start;
}
}  // namespace

// ---------- Source ----------

qint64 Source::readData(char *out, qint64 maxlen) {
    const qint64 frames = maxlen / qint64(sizeof(qint16));
    auto *dst = reinterpret_cast<qint16 *>(out);
    for (qint64 i = 0; i < frames; i++) {
        const float m = master.next(), d = duck.next();
        float mix = 0;
        for (Slot &s : lanes) {
            const float lv = s.fade.next();
            if (!s.track || lv <= 0) continue;
            const Track &t = *s.track;
            if (s.pos >= t.loopEnd) s.pos = t.loopStart;   // sample-accurate wrap
            mix += t.pcm[s.pos++] * std::sin(lv * HALF_PI) * d;
            if (s.pos >= t.loopEnd) s.pos = t.loopStart;
        }
        if (stinger) {
            if (stingerPos < stinger->pcm.size()) {
                mix += stinger->pcm[stingerPos++];
            } else {
                stinger = nullptr;
                stingerDone = true;
                if (restoreAfterStinger) duck.to(1, MUSIC_FADE_MS);
            }
        }
        const float v = std::clamp(mix * gain * m, -1.f, 1.f);
        peak = std::max(peak, std::abs(v));
        dst[i] = qint16(v * 32767);
    }
    // Drop a slot that has faded out completely.
    for (Slot &s : lanes)
        if (s.fade.level <= 0 && s.fade.step == 0) s.track = nullptr;
    return frames * qint64(sizeof(qint16));
}

// ---------- Music ----------

Music::Music(QObject *parent, bool withDevice) : QObject(parent), withDevice_(withDevice) {
    QSettings settings;
    enabled_ = settings.value("music", true).toBool();
    volume_ = std::clamp(settings.value("musicVolume", double(MUSIC_DEFAULT_VOLUME)).toFloat(), 0.f, 1.f);

    QFile jf(QStringLiteral(":/audio/music.json"));
    QJsonObject meta;
    if (jf.open(QIODevice::ReadOnly)) meta = QJsonDocument::fromJson(jf.readAll()).object();
    else qWarning("Garden Guard: music.json missing, music disabled");
    const double rate = meta.value("rate").toDouble(RATE);
    for (const Def &d : DEFS) {
        size_t s = 0, e = 0;
        const QJsonObject t = meta.value(QLatin1String(d.name)).toObject();
        if (d.loop && !loopWindow(t, rate, &s, &e)) {
            qWarning("Garden Guard: no loop window for %s, that loop is silent", d.name);
            continue;
        }
        load(QLatin1String(d.name), QLatin1String(d.file), d.loop, s, e);
    }
}

Music::~Music() {
    if (sink_) sink_->stop();
}

void Music::load(const QString &name, const QString &file, bool loop, size_t loopStart, size_t loopEnd) {
    auto *f = new QFile(QStringLiteral(":/audio/") + file, this);
    if (!f->open(QIODevice::ReadOnly)) {
        qWarning("Garden Guard: %s missing", qPrintable(file));
        return;
    }
    Track &t = tracks_[name];
    auto *dec = new QAudioDecoder(this);
    QAudioFormat fmt;
    fmt.setSampleRate(RATE);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Float);
    dec->setAudioFormat(fmt);
    dec->setSourceDevice(f);
    connect(dec, &QAudioDecoder::bufferReady, this, [dec, &t] {
        const QAudioBuffer b = dec->read();
        const QAudioFormat bf = b.format();
        const int ch = std::max(1, bf.channelCount());
        const qsizetype frames = b.frameCount();
        if (bf.sampleFormat() == QAudioFormat::Float) {
            const float *p = b.constData<float>();
            for (qsizetype i = 0; i < frames; i++) {
                float s = 0;
                for (int c = 0; c < ch; c++) s += p[i * ch + c];
                t.pcm.push_back(s / ch);
            }
        } else if (bf.sampleFormat() == QAudioFormat::Int16) {
            const qint16 *p = b.constData<qint16>();
            for (qsizetype i = 0; i < frames; i++) {
                float s = 0;
                for (int c = 0; c < ch; c++) s += p[i * ch + c] / 32768.f;
                t.pcm.push_back(s / ch);
            }
        }
    });
    connect(dec, &QAudioDecoder::finished, this, [this, dec, f, &t, name, loop, loopStart, loopEnd] {
        if (loop) {
            t.loopStart = std::min(loopStart, t.pcm.size());
            t.loopEnd = std::min(loopEnd, t.pcm.size());
            if (loopEnd > t.pcm.size())
                qWarning("Garden Guard: %s decoded to %zu samples, loop_end %zu clamped", qPrintable(name),
                         t.pcm.size(), loopEnd);
        }
        t.ready = !t.pcm.empty() && (!loop || t.loopEnd > t.loopStart);
        dec->deleteLater();
        f->deleteLater();
        // A loop that finished decoding after its phase began: start it now.
        if (t.ready && loop && loopName_ == name && source_ && !source_->lanes[source_->cur].track) playLoop(name, true);
    });
    connect(dec, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [dec, name](QAudioDecoder::Error) {
        qWarning("Garden Guard: cannot decode %s (%s), it stays silent", qPrintable(name), qPrintable(dec->errorString()));
    });
    dec->start();
}

bool Music::ready() const {
    if (tracks_.size() != std::size(DEFS)) return false;
    for (const auto &[n, t] : tracks_)
        if (!t.ready) return false;
    return true;
}

void Music::startSink() {
    if (sink_ || failed_ || (source_ && !withDevice_)) return;
    if (!withDevice_) {   // tests: the mixer without a device
        source_ = new Source(this);
        source_->open(QIODevice::ReadOnly);
        applyGain();
        source_->master.to(enabled_ ? 1 : 0, 0);
        return;
    }
    QAudioFormat fmt;
    fmt.setSampleRate(RATE);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() || !device.isFormatSupported(fmt)) {
        failed_ = true;
        qWarning("Garden Guard: no usable audio output, music disabled");
        return;
    }
    source_ = new Source(this);
    source_->open(QIODevice::ReadOnly);
    applyGain();
    source_->master.to(enabled_ ? 1 : 0, 0);
    sink_ = new QAudioSink(device, fmt, this);
    sink_->setBufferSize(RATE * 2 / 10);   // ~100 ms; music needn't be as tight as the SFX
    sink_->start(source_);
    qCDebug(lcMusic) << "sink started on" << device.description() << "state" << sink_->state()
                     << "error" << sink_->error() << "gain" << source_->gain << "enabled" << enabled_;
    if (lcMusic().isDebugEnabled()) {
        auto *meter = new QTimer(this);
        connect(meter, &QTimer::timeout, this, [this] {
            const float p = source_->peak;
            source_->peak = 0;
            qCDebug(lcMusic, "loop=%s peak=%.1f dBFS duck=%.2f master=%.2f stinger=%s",
                    qPrintable(loopName_), p > 0 ? 20 * std::log10(p) : -999.0, source_->duck.level,
                    source_->master.level, source_->stinger ? "on" : "off");
        });
        meter->start(5000);
    }
}

void Music::applyGain() {
    if (source_) source_->gain = MUSIC_GAIN * volume_;   // linear, same as the web
}

void Music::playLoop(const QString &name, bool fresh) {
    loopName_ = name;
    startSink();
    if (!source_) return;
    auto it = tracks_.find(name);
    const Track *t = it != tracks_.end() && it->second.ready ? &it->second : nullptr;
    Source::Slot &now = source_->lanes[source_->cur];
    if (t && now.track == t && !fresh) return;   // already playing this loop
    if (fresh || !now.track) {   // a fresh run (or nothing playing): straight in at bar 1
        source_->lanes[source_->cur ^ 1] = Source::Slot{};
        now = Source::Slot{t, t ? t->loopStart : 0, {}};
        now.fade.to(1, 0);
        return;
    }
    // Crossfade: the current loop fades out in the other slot, the new one fades in.
    source_->cur ^= 1;
    Source::Slot &in = source_->lanes[source_->cur];
    in = Source::Slot{t, t ? t->loopStart : 0, {}};
    in.fade.to(1, MUSIC_FADE_MS);
    source_->lanes[source_->cur ^ 1].fade.to(0, MUSIC_FADE_MS);
}

void Music::playStinger(const QString &name, bool restore) {
    startSink();
    if (!source_) return;
    auto it = tracks_.find(name);
    if (it == tracks_.end() || !it->second.ready) return;
    source_->stinger = &it->second;
    source_->stingerPos = 0;
    source_->restoreAfterStinger = restore;
    source_->duck.to(MUSIC_DUCK, MUSIC_DUCK_MS);
}

void Music::follow(Phase phase) {
    const int p = int(phase);
    if (p == lastPhase_) return;
    const int prev = lastPhase_;
    lastPhase_ = p;
    qCDebug(lcMusic) << "phase" << prev << "->" << p;
    const bool fromOver = prev < 0 || prev == int(Phase::MapCleared) || prev == int(Phase::Won) || prev == int(Phase::Lost);
    switch (phase) {
    case Phase::Build:
    case Phase::Wave:
        if (fromOver && source_) {   // a fresh map or run: undo the duck, start from bar 1
            source_->stinger = nullptr;
            source_->duck.to(1, prev == int(Phase::MapCleared) ? MUSIC_FADE_MS : 0);
        }
        playLoop(phase == Phase::Build ? QStringLiteral("build") : QStringLiteral("wave"),
                 prev < 0 || prev == int(Phase::Won) || prev == int(Phase::Lost));
        break;
    case Phase::MapCleared: playStinger(QStringLiteral("win"), true); break;
    case Phase::Won: playStinger(QStringLiteral("win"), false); break;
    case Phase::Lost: playStinger(QStringLiteral("lose"), false); break;
    }
}

void Music::setEnabled(bool on) {
    enabled_ = on;
    QSettings().setValue("music", on);
    startSink();
    if (source_) source_->master.to(on ? 1 : 0, MUSIC_OFF_MS);
}

void Music::setVolume(float v) {
    volume_ = std::clamp(v, 0.f, 1.f);
    QSettings().setValue("musicVolume", double(volume_));
    applyGain();
}
