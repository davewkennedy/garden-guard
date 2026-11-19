// Garden Guard: synthesized sound effects (ported from js/audio.js).
// Each sound is synthesized once at startup into a sample buffer; play() mixes
// buffers into a single Qt audio stream. Failure means silence, never a crash.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <vector>

class QAudioSink;

// Pull-mode audio source: Qt asks it for samples and it mixes the active voices.
class Mixer : public QIODevice {
    Q_OBJECT
public:
    explicit Mixer(QObject *parent = nullptr);
    void add(const std::vector<float> *samples);
    int voices() const { return int(voices_.size()); }

protected:
    qint64 readData(char *out, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return -1; }
    qint64 bytesAvailable() const override { return 4096 + QIODevice::bytesAvailable(); }

private:
    struct Voice { const std::vector<float> *samples; size_t pos; };
    std::vector<Voice> voices_;
};

class Audio : public QObject {
    Q_OBJECT
public:
    explicit Audio(QObject *parent = nullptr);
    ~Audio() override;

    void play(const QString &name);
    bool enabled() const { return enabled_; }
    void setEnabled(bool on);    // remembered between runs

private:
    void start();

    bool enabled_ = true;
    bool failed_ = false;
    QAudioSink *sink_ = nullptr;
    Mixer *mixer_ = nullptr;
    QHash<QString, std::vector<float>> sounds_;
    QHash<QString, qint64> lastPlayed_;
    QElapsedTimer clock_;
};
