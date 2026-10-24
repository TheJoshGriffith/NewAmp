#include "core/LocalFileSource.h"

#include "core/LocalTagEditor.h"

#include <QDirIterator>
#include <QFileInfo>

namespace newamp {

namespace {
const QStringList &extensions()
{
    static const QStringList exts = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"),
        QStringLiteral("opus"), QStringLiteral("wav"), QStringLiteral("aiff"),
        QStringLiteral("aif"), QStringLiteral("m4a"), QStringLiteral("aac"),
        QStringLiteral("wma"),
    };
    return exts;
}
} // namespace

bool LocalFileSource::isSupportedFile(const QString &path)
{
    return extensions().contains(QFileInfo(path).suffix().toLower());
}

QStringList LocalFileSource::fileFilters()
{
    QStringList patterns;
    for (const QString &ext : extensions())
        patterns << QStringLiteral("*.") + ext;
    return patterns;
}

MetadataEditor *LocalFileSource::metadataEditor() const
{
    static LocalTagEditor editor; // stateless
    return &editor;
}

Track LocalFileSource::trackForFile(const QString &path)
{
    const QFileInfo info(path);
    Track track;
    track.sourceId = QString::fromLatin1(Id);
    track.location = QUrl::fromLocalFile(info.absoluteFilePath());
    track.title = info.completeBaseName();
    return track;
}

QList<Track> LocalFileSource::tracksForPaths(const QStringList &paths)
{
    QList<Track> tracks;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QStringList files;
            QDirIterator it(info.absoluteFilePath(), fileFilters(), QDir::Files,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
                files << it.next();
            files.sort(Qt::CaseInsensitive);
            for (const QString &file : files)
                tracks << trackForFile(file);
        } else if (isSupportedFile(path)) {
            tracks << trackForFile(path);
        }
    }
    return tracks;
}

} // namespace newamp
