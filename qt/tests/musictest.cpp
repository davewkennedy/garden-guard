// Garden Guard: headless music test (owner: Bill). No window and no audio
// device: it drives music::Source directly and decodes the embedded files.
//   ./build/debug/garden_musictest        exit 0 = all PASS
#include "../src/music.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>
#include <cmath>
#include <map>
#include <set>
#include <cstdio>

using namespace music;

static int fails = 0;
static void check(bool ok, const char *what, const QString &detail = {}) {
    std::printf("%s  %s%s\n", ok ? "PASS" : "FAIL", what, ok || detail.isEmpty() ? "" : qPrintable("  " + detail));
    if (!ok) fails++;
}

static std::vector<qint16> render(Source &s, int frames) {
    std::vector<qint16> out(static_cast<size_t>(frames));
    s.read(reinterpret_cast<char *>(out.data()), qint64(frames) * 2);
    return out;
}

static qint16 q(float v) { return qint16(std::clamp(v, -1.f, 1.f) * 32767); }

// 1. Seam: the loop wraps from loopEnd-1 to loopStart, sample for sample, forever.
static void seam() {
    Track t;
    for (int i = 0; i < 1600; i++) t.pcm.push_back(((i * 37) % 1000 - 500) / 1000.f);
    t.loopStart = 300;
    t.loopEnd = 1300;
    t.ready = true;
    Source s;
    s.open(QIODevice::ReadOnly);
    s.gain = 1;
    s.master.to(1, 0);
    s.lanes = {Source::Slot{&t, t.loopStart, {}}};
    s.cur = 0;
    s.lanes[0].fade.to(1, 0);
    const auto out = render(s, 3500);
    int bad = -1;
    for (int k = 0; k < 3500 && bad < 0; k++)
        if (out[size_t(k)] != q(t.pcm[t.loopStart + size_t(k) % 1000])) bad = k;
    check(bad < 0, "loop seam is sample-exact over 3.5 loops (start at loop_start, wrap at loop_end)",
          QString("first mismatch at output sample %1").arg(bad));
}

// 2. Crossfade: equal power (gain_in^2 + gain_out^2 == 1) for the whole fade, then the old slot is gone.
static void crossfade() {
    Track a, b;
    a.pcm.assign(5000, 0.5f), b.pcm.assign(5000, 0.5f);
    a.loopEnd = b.loopEnd = 5000;
    a.ready = b.ready = true;
    Source s;
    s.open(QIODevice::ReadOnly);
    s.gain = 1;
    s.master.to(1, 0);
    s.lanes = {Source::Slot{&a, 0, {}}, Source::Slot{&b, 0, {}}};
    s.lanes[0].fade.to(1, 0);
    s.cur = 1;
    s.lanes[1].fade.to(1, MUSIC_FADE_MS);
    s.lanes[0].fade.to(0, MUSIC_FADE_MS);
    const int n = MUSIC_FADE_MS * RATE / 1000;
    double worst = 0;
    for (int i = 0; i < n; i += 441) {
        render(s, 441);
        const float gi = std::sin(s.playing()->fade.level * 1.5707963f);
        const float go = s.lanes.size() > 1 ? std::sin(s.lanes[0].fade.level * 1.5707963f) : 0.f;
        worst = std::max(worst, std::abs(double(gi * gi + go * go) - 1.0));
    }
    check(worst < 0.01, "crossfade is equal-power throughout MUSIC_FADE_MS", QString("worst |g1^2+g2^2-1| = %1").arg(worst));
    render(s, 4410);
    check(s.lanes.size() == 1 && s.playing()->track == &b && s.playing()->fade.level == 1.f,
          "after the crossfade only the new loop plays");
}

// 3. Duck + stinger: the loop ducks to MUSIC_DUCK in MUSIC_DUCK_MS; after the stinger it comes back
//    only if restoreAfterStinger (MapCleared), else stays ducked (Won / Lost).
static void duck() {
    for (bool restore : {true, false}) {
        Track loop, sting;
        loop.pcm.assign(44100, 0.f);
        loop.loopEnd = 44100;
        sting.pcm.assign(22050, 0.f);   // 0.5 s stinger (longer than the 200 ms duck)
        loop.ready = sting.ready = true;
        Source s;
        s.open(QIODevice::ReadOnly);
        s.gain = 1;
        s.master.to(1, 0);
        s.lanes = {Source::Slot{&loop, 0, {}}};
        s.cur = 0;
        s.lanes[0].fade.to(1, 0);
        s.stinger = &sting;
        s.restoreAfterStinger = restore;
        s.duck.to(MUSIC_DUCK, MUSIC_DUCK_MS);
        render(s, MUSIC_DUCK_MS * RATE / 1000 + 10);
        const bool ducked = std::abs(s.duck.level - MUSIC_DUCK) < 1e-4f;
        render(s, (MUSIC_FADE_MS + 600) * RATE / 1000);
        const bool after = restore ? s.duck.level == 1.f : std::abs(s.duck.level - MUSIC_DUCK) < 1e-4f;
        check(ducked && after && s.stingerDone,
              restore ? "MapCleared: duck to 0.25 in 200 ms, back up after the stinger"
                      : "Won/Lost: duck to 0.25 and stay ducked after the stinger",
              QString("duck level %1").arg(s.duck.level));
    }
}

// 4. Off/on: off fades the master to 0 over MUSIC_OFF_MS; the output is silent after it.
static void offFade() {
    Track t;
    t.pcm.assign(44100, 0.5f);
    t.loopEnd = 44100;
    t.ready = true;
    Source s;
    s.open(QIODevice::ReadOnly);
    s.gain = 1;
    s.master.to(1, 0);
    s.lanes = {Source::Slot{&t, 0, {}}};
    s.cur = 0;
    s.lanes[0].fade.to(1, 0);
    s.master.to(0, MUSIC_OFF_MS);
    const auto mid = render(s, MUSIC_OFF_MS * RATE / 2000);
    const auto end = render(s, MUSIC_OFF_MS * RATE / 2000 + 50);
    const auto after = render(s, 100);
    check(mid.back() > 0 && after.front() == 0 && after.back() == 0, "music off fades out over 300 ms, then silence");
}

// 5. The real files: decode each, check the loop window lies inside, and that the tail after loop_end
//    repeats the start (Dale's loops are periodic by construction), i.e. the window is right.
static std::vector<float> decode(const QString &file) {
    std::vector<float> pcm;
    QFile f(":/audio/" + file);
    if (!f.open(QIODevice::ReadOnly)) return pcm;
    QAudioDecoder dec;
    QAudioFormat fmt;
    fmt.setSampleRate(RATE);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Float);
    dec.setAudioFormat(fmt);
    dec.setSourceDevice(&f);
    QEventLoop loop;
    QObject::connect(&dec, &QAudioDecoder::bufferReady, [&] {
        const QAudioBuffer b = dec.read();
        const float *p = b.constData<float>();
        const int ch = std::max(1, b.format().channelCount());
        for (qsizetype i = 0; i < b.frameCount(); i++) {
            float s = 0;
            for (int c = 0; c < ch; c++) s += p[i * ch + c];
            pcm.push_back(s / ch);
        }
    });
    QObject::connect(&dec, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&dec, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    dec.start();
    loop.exec();
    return pcm;
}

static void realFiles() {
    QFile jf(":/audio/music.json");
    if (!jf.open(QIODevice::ReadOnly)) {
        std::printf("SKIP  real files: web/audio has no music.json yet (Dale's files not landed); re-run cmake when they do\n");
        return;
    }
    const QJsonObject meta = QJsonDocument::fromJson(jf.readAll()).object();
    struct F { QString name, file; bool loop; };
    std::vector<F> files = {F{"build", "music-build.mp3", true}, F{"wave", "music-wave.mp3", true},
                            F{"win", "stinger-win.mp3", false}, F{"lose", "stinger-lose.mp3", false}};
    // Every map's own loops (music.json "maps"), and they agree with music::loopName (the web's rule too).
    const QJsonArray maps = meta.value("maps").toArray();
    check(maps.size() == MAP_COUNT, qPrintable(QString("music.json lists %1 maps (want %2)").arg(maps.size()).arg(MAP_COUNT)));
    QStringList names;
    for (int i = 0; i < maps.size(); i++) {
        const QJsonObject mp = maps.at(i).toObject();
        const QString b = mp.value("build").toString(), w = mp.value("wave").toString();
        check(b == loopName(i, false) && w == loopName(i, true),
              qPrintable(QString("map %1 (%2): music.json loops %3/%4 = loopName()").arg(i + 1).arg(mp.value("title").toString(), b, w)));
        names << b << w;
        if (i > 0)
            for (const QString &n : {b, w}) files.push_back(F{n, meta.value(n).toObject().value("file").toString(), true});
    }
    names.removeDuplicates();
    check(names.size() == 2 * MAP_COUNT, "every map has its own two loops (20 different loops)");
    for (const F &f : files) {
        const auto pcm = decode(f.file);
        check(!pcm.empty(), qPrintable(QString("%1 decodes (%2 samples, %3 s)").arg(f.file).arg(pcm.size())
                                           .arg(pcm.size() / double(RATE), 0, 'f', 3)));
        if (!f.loop || pcm.empty()) continue;
        const QJsonObject t = meta.value(f.name).toObject();
        size_t s = size_t(t.value("loop_start").toDouble(std::llround(t.value("loopStart").toDouble(0.5) * RATE)));
        size_t e = size_t(t.value("loop_end").toDouble(std::llround(t.value("loopEnd").toDouble() * RATE)));
        check(e > s && e <= pcm.size(), qPrintable(QString("%1 loop window [%2, %3) inside the decoded audio").arg(f.name).arg(s).arg(e)));
        if (!(e > s && e <= pcm.size())) continue;
        // Periodicity: compare 0.5 s after loop_end with 0.5 s after loop_start (RMS of the difference
        // vs RMS of the signal). A wrong window (e.g. off by the decoder delay) shows up as a large ratio.
        // The residual at the right window is MP3 codec noise: tiny for the 80 kbps Sunny Meadow loops
        // (< 5%), up to ~20% for bright, noisy 64 kbps map loops (plucks, hats), while ANY misalignment
        // is far worse (one sample: ~1.6x; a decoder delay of 529/1105/1152 samples: 70-180%). So the
        // check is that the window is exact: lag 0 beats lags of +-1 sample and is at most a third of
        // the decoder-delay lags (and < 25%). Audible seams are build.js's splice check.
        const size_t tail = pcm.size() - e, n = tail > 1200 ? std::min<size_t>(RATE / 2, tail - 1200) : 0;
        auto ratioAt = [&](long lag) {
            double diff = 0, sig = 0;
            for (size_t k = 0; k < n; k++) {
                const double d = pcm[size_t(long(e + k) + lag)] - pcm[s + k];
                diff += d * d;
                sig += double(pcm[s + k]) * pcm[s + k];
            }
            return sig > 0 ? std::sqrt(diff / sig) : 1;
        };
        const double ratio = ratioAt(0), near = std::min(ratioAt(1), ratioAt(-1));
        const double far = std::min({ratioAt(529), ratioAt(1105), ratioAt(1152), ratioAt(-529), ratioAt(-1105)});
        check(n > RATE / 10 && ratio < 0.25 && ratio < near && ratio * 3 < far,
              qPrintable(QString("%1 seam: the audio after loop_end repeats loop_start, window exact (rms diff %2%; "
                                 "1 sample off %3%, a decoder delay off %4%)")
                             .arg(f.name).arg(ratio * 100, 0, 'f', 2).arg(near * 100, 0, 'f', 1).arg(far * 100, 0, 'f', 0)));
    }
}

// 6. The state machine on the real decoded tracks (no device): every phase change the game makes.
static void followGame() {
    QCoreApplication::setOrganizationName("GardenGuardTest");   // keep the player's settings out of it
    Music m(nullptr, false);
    QEventLoop wait;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&] { if (m.ready()) wait.quit(); });
    poll.start(50);
    QTimer::singleShot(30000, &wait, &QEventLoop::quit);
    wait.exec();
    if (!m.ready()) { check(false, "all four tracks decode (Music)"); return; }
    m.follow(Phase::Build);   // creates the mixer (lazily, as in the game)
    auto *s = m.source();
    if (!s) { check(false, "the mixer exists after the first follow()"); return; }
    auto playing = [&]() -> const Track * { return s->playing() ? s->playing()->track : nullptr; };
    auto fadingOut = [&](const Track *t) {
        for (int i = 0; i < int(s->lanes.size()); i++)
            if (i != s->cur && s->lanes[size_t(i)].track == t) return true;
        return false;
    };
    auto pump = [&](int ms) { render(const_cast<Source &>(*s), ms * RATE / 1000); };
    check(playing() == m.track("build") && s->playing()->pos == m.track("build")->loopStart,
          "first Build: the build loop starts from bar 1 (loop_start), fading in over 200 ms");
    pump(MUSIC_DUCK_MS + 20);
    m.follow(Phase::Build);   // idempotent
    m.follow(Phase::Wave);
    check(playing() == m.track("wave") && fadingOut(m.track("build")),
          "Build -> Wave: the wave loop fades in while the build loop fades out");
    pump(MUSIC_FADE_MS + 100);
    check(s->lanes.size() == 1, "  ...and after MUSIC_FADE_MS only the wave loop is left");
    // A flip back and forth inside the crossfade: no lane's level may jump (Bobby's web bug, 41044cc).
    m.follow(Phase::Build);
    pump(500);
    std::map<const Track *, float> before;
    for (const auto &l : s->lanes) before[l.track] = l.fade.level;
    m.follow(Phase::Wave);
    bool continuous = true;
    for (const auto &l : s->lanes)
        if (before.count(l.track) && std::abs(before[l.track] - l.fade.level) > 1e-6f) continuous = false;
    check(continuous && s->lanes.size() == 2 && playing() == m.track("wave"),
          "a flip inside the crossfade reverses the fades from where they are (no jump, no click)");
    pump(MUSIC_FADE_MS + 100);
    check(s->lanes.size() == 1 && playing() == m.track("wave"), "  ...and settles on the wave loop");
    m.follow(Phase::MapCleared);
    check(s->stinger == m.track("win") && s->restoreAfterStinger, "MapCleared: the win stinger, loop ducked");
    pump(MUSIC_DUCK_MS + 20);
    check(std::abs(s->duck.level - MUSIC_DUCK) < 1e-3f, "  ...duck at 0.25 after 200 ms");
    pump(4800 + MUSIC_FADE_MS + 200);
    check(!s->stinger && s->duck.level == 1.f, "  ...stinger over (4.8 s): the loop is back up");
    m.follow(Phase::Build);   // the next map
    pump(MUSIC_FADE_MS + 100);
    check(playing() == m.track("build") && s->lanes.size() == 1, "next map: back to the build loop");
    m.follow(Phase::Wave);
    m.follow(Phase::Lost);
    check(s->stinger == m.track("lose") && !s->restoreAfterStinger, "Lost: the lose stinger, loop ducked");
    pump(3700 + MUSIC_FADE_MS + 200);
    check(std::abs(s->duck.level - MUSIC_DUCK) < 1e-3f, "  ...and it stays ducked after the stinger");
    m.follow(Phase::Build);   // play again
    check(playing() == m.track("build") && s->playing()->pos == m.track("build")->loopStart,
          "a new run: the build loop starts fresh from bar 1");
    pump(MUSIC_DUCK_MS + 20);
    check(s->duck.level == 1.f && s->lanes.size() == 1, "  ...undocked, and the old loop faded out in 200 ms");
    // A new run while a stinger still plays: the stinger fades out (no dead stop).
    m.follow(Phase::Wave);
    m.follow(Phase::Lost);
    pump(300);
    m.follow(Phase::Build);
    check(s->stinger && s->stingerFade.step < 0, "a new run during the stinger: the stinger fades out");
    pump(MUSIC_DUCK_MS + 20);
    check(!s->stinger, "  ...and is gone after 200 ms");
    m.follow(Phase::Wave);
    m.follow(Phase::Won);
    check(s->stinger == m.track("win") && !s->restoreAfterStinger, "Won: the win stinger, stays ducked");
    m.setEnabled(false);
    pump(MUSIC_OFF_MS + 50);
    check(s->master.level == 0.f, "music off: faded out in 300 ms");
    m.setEnabled(true);
    pump(10);
    check(s->master.level > 0.f, "music on: it resumes");
    m.setVolume(0.6f);
    check(std::abs(s->gain - 0.06f) < 1e-6f, "volume 0.6 -> gain 0.06 (0.1 x volume, linear, as on the web)");
    QSettings().remove("music");
    QSettings().remove("musicVolume");
}

// 7. Maps (Dale): each map picks its own loops; a map change crossfades; menus (map 0) play Sunny Meadow's
//    build loop; tracks of maps no longer shown are freed.
static void mapsPickTheirOwnTracks() {
    QCoreApplication::setOrganizationName("GardenGuardTest");
    Music m(nullptr, false);
    auto waitDecoded = [&](const QString &name) {
        QEventLoop wait;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, [&] { if (m.track(name) && m.track(name)->ready) wait.quit(); });
        poll.start(20);
        QTimer::singleShot(30000, &wait, &QEventLoop::quit);
        wait.exec();
        return m.track(name) && m.track(name)->ready;
    };
    if (!waitDecoded("build")) { check(false, "maps: Sunny Meadow's build loop decodes"); return; }
    m.follow(Phase::Build);
    auto *s = m.source();
    auto pump = [&](int ms) { render(const_cast<Source &>(*s), ms * RATE / 1000); };
    auto playing = [&]() -> const Track * { return s->playing() ? s->playing()->track : nullptr; };
    pump(MUSIC_DUCK_MS + 20);
    std::set<QString> seen;   // by name: a freed track's memory can be reused by the next one
    bool allOwn = true;
    for (int i = 0; i < MAP_COUNT; i++) {
        for (bool wave : {false, true}) {
            m.setMap(i);
            m.follow(wave ? Phase::Wave : Phase::Build);
            const QString want = loopName(i, wave);
            const bool decoded = waitDecoded(want);
            pump(MUSIC_FADE_MS + 100);
            const bool ok = decoded && m.loopName() == want && playing() == m.track(want) && s->lanes.size() == 1;
            if (!ok) allOwn = false;
            check(ok, qPrintable(QString("map %1 %2: plays %3").arg(i + 1).arg(wave ? "wave" : "build", want)),
                  QString("loopName %1, decoded %2, lanes %3").arg(m.loopName()).arg(decoded).arg(s->lanes.size()));
            if (ok) seen.insert(want);
        }
        if (i == 4) {   // a map change in the middle of a wave crossfades to the new map's wave loop
            m.setMap(5);
            const bool fadesOut = s->lanes.size() >= 1 && playing() == m.track(loopName(4, true));   // old one plays until the new one is decoded
            waitDecoded(loopName(5, true));
            check(fadesOut && playing() == m.track(loopName(5, true)) && s->lanes.size() == 2,
                  "a map change during a wave crossfades from the old map's wave loop to the new one's");
            pump(MUSIC_FADE_MS + 100);
            m.follow(Phase::Build);
            pump(MUSIC_FADE_MS + 100);
        }
        // only the current map's loops, Sunny Meadow's build loop and the stingers stay decoded (the crossfades are
        // over, so nothing else may be kept)
        int other = 0;
        for (int j = 0; j < MAP_COUNT; j++)
            for (bool w : {false, true})
                if (j != m.map() && m.track(loopName(j, w)) && loopName(j, w) != "build") other++;
        if (other > 0) { check(false, qPrintable(QString("map %1: other maps' tracks are freed").arg(m.map() + 1)), QString("%1 kept").arg(other)); allOwn = false; }
    }
    check(allOwn && seen.size() == 2 * MAP_COUNT, qPrintable(QString("every map picks its own build and wave loop (%1 different tracks played)").arg(seen.size())));
    m.setMap(0);   // back to a menu
    m.follow(Phase::Build);
    pump(MUSIC_FADE_MS + 100);
    check(playing() == m.track("build") && m.loopName() == "build", "menus (map 0): Sunny Meadow's build loop");
    QSettings().remove("music");
    QSettings().remove("musicVolume");
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    seam();
    crossfade();
    duck();
    offFade();
    realFiles();
    followGame();
    mapsPickTheirOwnTracks();
    std::printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
