#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace newamp {

// Pulls the in-band titles out of a SHOUTcast/Icecast stream: every
// `metaInt` bytes of audio are followed by a length byte (x16) and that
// many bytes of "StreamTitle='...';" text.
class IcyParser
{
public:
    explicit IcyParser(int metaInt = 0) : m_metaInt(metaInt), m_untilMeta(metaInt) {}

    // Returns the StreamTitle of each metadata block completed by data.
    QStringList feed(const QByteArray &data);

    static QString streamTitle(const QByteArray &block);

private:
    int m_metaInt;
    int m_untilMeta;
    int m_metaLength = -1; // bytes of the current block still expected; -1 when reading audio
    QByteArray m_block;
};

// Reads a radio stream's titles alongside the player, over a second
// connection that asks for metadata (Qt Multimedia doesn't surface it). It
// gives up as soon as the headers show the stream has none.
class IcyReader : public QObject
{
    Q_OBJECT

public:
    explicit IcyReader(QObject *parent = nullptr);
    ~IcyReader() override;

    void start(const QUrl &url);
    void stop();

Q_SIGNALS:
    // The song now playing ("Artist - Title" as the station sends it), and
    // the station's name; either may be empty.
    void metaData(const QString &title, const QString &station);

private:
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    IcyParser m_parser;
    QString m_station;
};

} // namespace newamp
