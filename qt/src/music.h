// Garden Guard: background music (owner: Bill). Same behaviour and names as
// the web's music module (Bobby); shared spec in WHATS-NEW / the music notes.
//
// Tracks are Dale's MP3s in web/audio (embedded here as :/audio/...), decoded
// once to PCM. Loops play sample-accurately over [loop_start, loop_end) from
// music.json, starting at loop_start (bar 1), so there's no gap at the seam.
//  - Build <-> Wave: equal-power crossfade over MUSIC_FADE_MS.
//  - MapCleared / Won -> "win" stinger, Lost -> "lose" stinger: the loop ducks
//    to MUSIC_DUCK over MUSIC_DUCK_MS. After MapCleared it fades back up over
//    MUSIC_FADE_MS when the stinger ends; after Won / Lost it stays ducked
//    until the next run starts the build loop fresh.
//  - Pause and game speed don't touch the music.
//  - Settings (separate from SFX): "music" on/off (default on) and
//    "musicVolume" 0..1 (default 0.6); gain = MUSIC_GAIN * volume (linear,
//    as on the web). Off fades out over
//    MUSIC_OFF_MS; on resumes the current phase's loop.
// Any failure (no device, no decoder, missing file) means silence, never a crash.
#pragma once

#include "game.h"

#include <map>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <algorithm>
#include <vector>

class QAudioSink;

namespace music {
constexpr int RATE = 44100;
constexpr int MUSIC_FADE_MS = 1500;
constexpr int MUSIC_DUCK_MS = 200;
constexpr int MUSIC_OFF_MS = 300;
constexpr float MUSIC_DUCK = 0.25f;
constexpr float MUSIC_DEFAULT_VOLUME = 0.6f;
constexpr float MUSIC_GAIN = 0.1f;    // -20 dB: the files are ~-21 LUFS, the SFX much quieter (Hank, Dale)

struct Track {
    std::vector<float> pcm;   // mono, RATE
    size_t loopStart = 0, loopEnd = 0;   // loops only; loopEnd == 0: a one-shot
    bool ready = false;
};

// A ramp toward a target over a number of samples (linear in "level").
struct Ramp {
    float level = 0, target = 0, step = 0;
    void to(float t, int ms) {
        target = t;
        const float n = std::max(1.f, ms * RATE / 1000.f);
        step = (target - level) / n;
        if (ms <= 0) level = target, step = 0;
    }
    float next() {
        if (step != 0) {
            level += step;
            if ((step > 0 && level >= target) || (step < 0 && level <= target)) level = target, step = 0;
        }
        return level;
    }
};

// Pull-mode source: mixes the loop lanes (crossfade), the stinger and the duck.
class Source : public QIODevice {
    Q_OBJECT
public:
    explicit Source(QObject *parent = nullptr) : QIODevice(parent) {}
    struct Slot {
        const Track *track = nullptr;
        size_t pos = 0;
        Ramp fade;   // 0..1, output gain sin(level * pi/2): equal power against the other slot
    };
    Slot lanes[2];          // lanes[cur] is the playing loop; the other one fades out
    int cur = 0;
    const Track *stinger = nullptr;
    size_t stingerPos = 0;
    bool restoreAfterStinger = false;
    Ramp duck{1, 1, 0};
    Ramp master{0, 0, 0};
    float gain = 0;         // MUSIC_GAIN * volume (linear)
    bool stingerDone = false;
    float peak = 0;         // output peak since the last read of it (debug meter)

protected:
    qint64 readData(char *out, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return -1; }
    qint64 bytesAvailable() const override { return 4096 + QIODevice::bytesAvailable(); }
};
}  // namespace music

class Music : public QObject {
    Q_OBJECT
public:
    // withDevice false (tests): no audio sink; the mixer state still follows the game.
    explicit Music(QObject *parent = nullptr, bool withDevice = true);
    ~Music() override;

    // Call whenever the game may have changed phase (cheap and idempotent).
    void follow(Phase phase);
    // A fresh run or map: the next follow() restarts the build loop from bar 1.
    void restart() { lastPhase_ = -1; }

    bool enabled() const { return enabled_; }
    void setEnabled(bool on);       // saved ("music")
    float volume() const { return volume_; }
    void setVolume(float v);        // 0..1, saved ("musicVolume")
    bool ready() const;             // every track decoded
    const music::Source *source() const { return source_; }   // tests
    const music::Track *track(const QString &name) const {
        auto it = tracks_.find(name);
        return it == tracks_.end() ? nullptr : &it->second;
    }

private:
    void load(const QString &name, const QString &file, bool loop, size_t loopStart, size_t loopEnd);
    void startSink();
    void playLoop(const QString &name, bool fresh);
    void playStinger(const QString &name, bool restore);
    void applyGain();

    bool enabled_ = true;
    float volume_ = music::MUSIC_DEFAULT_VOLUME;
    bool failed_ = false;
    bool withDevice_ = true;
    int lastPhase_ = -1;
    QString loopName_;
    std::map<QString, music::Track> tracks_;   // node-based: Track addresses stay valid
    QAudioSink *sink_ = nullptr;
    music::Source *source_ = nullptr;
};
