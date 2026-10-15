#pragma once

#include <QString>
#include <QUrl>

namespace newamp {

// A playable item. Sources (local files, Plex, ...) produce tracks; the
// engine asks the owning source to turn one into a stream URL at play time.
struct Track
{
    QString sourceId;
    QUrl location;
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = -1;

    QString displayTitle() const
    {
        const QString name = title.isEmpty() ? location.fileName() : title;
        return artist.isEmpty() ? name : artist + QStringLiteral(" - ") + name;
    }
};

} // namespace newamp
