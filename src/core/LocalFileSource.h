#pragma once

#include "core/MediaSource.h"

#include <QList>
#include <QStringList>

namespace newamp {

class LocalFileSource final : public MediaSource
{
public:
    static constexpr const char *Id = "local";

    QString id() const override { return QString::fromLatin1(Id); }
    QString displayName() const override { return QStringLiteral("Local files"); }
    QUrl streamUrl(const Track &track) const override { return track.location; }

    static bool isSupportedFile(const QString &path);
    static QStringList fileFilters();
    static Track trackForFile(const QString &path);

    // Expands directories recursively and playlist files into their tracks,
    // and drops unsupported files.
    static QList<Track> tracksForPaths(const QStringList &paths);
};

} // namespace newamp
