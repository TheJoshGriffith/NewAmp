#include "audio/IcyReader.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringDecoder>

#include <utility>

namespace newamp {

namespace {

// Stations send UTF-8 or their own code page; Latin-1 covers the latter best.
QString decode(const QByteArray &data)
{
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(data);
    return utf8.hasError() ? QString::fromLatin1(data) : text;
}

} // namespace

QStringList IcyParser::feed(const QByteArray &data)
{
    QStringList titles;
    if (m_metaInt <= 0)
        return titles;
    qsizetype pos = 0;
    while (pos < data.size()) {
        if (m_metaLength < 0) {
            const qsizetype skip = qMin<qsizetype>(m_untilMeta, data.size() - pos);
            pos += skip;
            m_untilMeta -= int(skip);
            if (m_untilMeta == 0 && pos < data.size()) {
                m_metaLength = quint8(data.at(pos++)) * 16;
                m_block.clear();
            }
        } else {
            const qsizetype take = qMin<qsizetype>(m_metaLength, data.size() - pos);
            m_block += data.mid(pos, take);
            pos += take;
            m_metaLength -= int(take);
        }
        if (m_metaLength == 0) {
            const QString title = streamTitle(m_block);
            if (!title.isNull())
                titles << title;
            m_metaLength = -1;
            m_untilMeta = m_metaInt;
        }
    }
    return titles;
}

QString IcyParser::streamTitle(const QByteArray &block)
{
    // StreamTitle='Artist - Title';StreamUrl='...'; padded with NULs. A
    // title can itself contain "'" so it ends at the first "';".
    const QByteArray key = "StreamTitle='";
    const qsizetype start = block.indexOf(key);
    if (start < 0)
        return {};
    const qsizetype from = start + key.size();
    qsizetype end = block.indexOf("';", from);
    if (end < 0) {
        end = block.indexOf('\0', from);
        if (end < 0)
            end = block.size();
        if (end > from && block.at(end - 1) == '\'')
            --end;
    }
    return decode(block.mid(from, end - from)).trimmed();
}

IcyReader::IcyReader(QObject *parent)
    : QObject(parent)
{
}

IcyReader::~IcyReader()
{
    stop();
}

void IcyReader::start(const QUrl &url)
{
    stop();
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("Icy-MetaData", "1");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NewAmp/" NEWAMP_VERSION));
    m_reply = m_network->get(request);
    m_station.clear();
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
        if (reply != m_reply)
            return;
        m_station = decode(reply->rawHeader("icy-name")).trimmed();
        m_parser = IcyParser(reply->rawHeader("icy-metaint").toInt());
        if (!m_station.isEmpty())
            Q_EMIT metaData(QString(), m_station);
        if (reply->rawHeader("icy-metaint").toInt() <= 0)
            stop(); // no titles coming; don't fetch the audio twice
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != m_reply)
            return;
        for (const QString &title : m_parser.feed(reply->readAll()))
            Q_EMIT metaData(title, m_station);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply == m_reply)
            m_reply = nullptr;
        reply->deleteLater();
    });
}

void IcyReader::stop()
{
    if (QNetworkReply *reply = std::exchange(m_reply, nullptr))
        reply->abort();
}

} // namespace newamp
