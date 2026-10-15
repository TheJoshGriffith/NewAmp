#pragma once

#include "audio/AudioBackend.h"

class QAudioOutput;
class QMediaPlayer;

namespace newamp {

// Plays anything Qt Multimedia can (AAC, M4A, Opus, network streams, ...)
// through QMediaPlayer. It has no effects hook, so only volume applies.
class MediaPlayerBackend final : public AudioBackend
{
    Q_OBJECT

public:
    explicit MediaPlayerBackend(QObject *parent = nullptr);

    bool load(const QUrl &url) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;
    qint64 position() const override;
    qint64 duration() const override;
    void setDspParams(const DspParams &params) override;

private:
    QMediaPlayer *m_player;
    QAudioOutput *m_output;
};

} // namespace newamp
