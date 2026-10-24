#pragma once

#include "core/Track.h"

#include <QList>
#include <QPair>
#include <QString>

#include <functional>

namespace newamp {

// One tag in the File Info window. Keys are TagLib's property names, so
// every source speaks the same language: TITLE, ARTIST, ALBUM, ALBUMARTIST,
// TRACKNUMBER, DISCNUMBER, DATE, GENRE, COMMENT.
struct TagField
{
    QString key;
    QString value;
    // The edit applies to more than this track, such as Plex's album title.
    bool wholeAlbum = false;
};

// What the File Info window shows for a track.
struct TrackInfo
{
    QList<TagField> tags; // in display order; editable when canEdit
    QList<QPair<QString, QString>> details; // read-only lines: format, bitrate, ...
    bool canEdit = false;
    // One line under the tags: why they're read-only, or where edits go
    // ("Saved to Plex, not to the file").
    QString note;
    QString error; // reading failed; nothing else is set
};

// Reads and writes a source's track metadata. A source offers one through
// MediaSource::metadataEditor().
//
// Header-only and free of QObject, like PluginHost, so plugins need no
// symbols from the executable. Callbacks run on the main thread, possibly
// later.
class MetadataEditor
{
public:
    using InfoCallback = std::function<void(const TrackInfo &info)>;
    // error is empty on success.
    using SaveCallback = std::function<void(const QString &error)>;

    virtual ~MetadataEditor() = default;

    virtual void readInfo(const Track &track, const InfoCallback &done) = 0;
    // changes holds only the tags the user changed; an empty value removes
    // the tag.
    virtual void writeTags(const Track &track, const QList<TagField> &changes, const SaveCallback &done) = 0;
};

// The standard keys in the order the window shows them.
inline const QList<QString> &standardTagKeys()
{
    static const QList<QString> keys = {
        QStringLiteral("TITLE"),       QStringLiteral("ARTIST"),      QStringLiteral("ALBUM"),
        QStringLiteral("ALBUMARTIST"), QStringLiteral("TRACKNUMBER"), QStringLiteral("DISCNUMBER"),
        QStringLiteral("DATE"),        QStringLiteral("GENRE"),       QStringLiteral("COMMENT"),
    };
    return keys;
}

} // namespace newamp
