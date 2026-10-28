#include "core/LocalLyrics.h"

#include "audio/TagReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>

namespace newamp {

namespace {
constexpr qint64 MaxLyricsFileBytes = 512 * 1024;
} // namespace

QString SidecarLyrics::name() const
{
    return QCoreApplication::translate("Lyrics", "File");
}

bool SidecarLyrics::canHandle(const Track &track) const
{
    return track.location.isLocalFile();
}

void SidecarLyrics::fetch(const Track &track, const Callback &done)
{
    const QString path = sidecarPath(track.location.toLocalFile());
    done(path.isEmpty() ? QString() : readTextFile(path));
}

QString SidecarLyrics::sidecarPath(const QString &trackPath)
{
    const QFileInfo info(trackPath);
    const QString base = info.dir().filePath(info.completeBaseName());
    for (const char *suffix : {".lrc", ".LRC", ".Lrc", ".txt", ".TXT"}) {
        const QString candidate = base + QLatin1String(suffix);
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return {};
}

QString SidecarLyrics::readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxLyricsFileBytes)
        return {};
    const QByteArray data = file.readAll();
    if (auto encoding = QStringConverter::encodingForData(data)) {
        QStringDecoder decoder(*encoding);
        return decoder.decode(data);
    }
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = utf8.decode(data);
    return utf8.hasError() ? QString::fromLatin1(data) : text;
}

QString TagLyrics::name() const
{
    return QCoreApplication::translate("Lyrics", "Tags");
}

bool TagLyrics::canHandle(const Track &track) const
{
    return track.location.isLocalFile();
}

void TagLyrics::fetch(const Track &track, const Callback &done)
{
    done(TagReader::read(track.location.toLocalFile()).lyrics);
}

} // namespace newamp
