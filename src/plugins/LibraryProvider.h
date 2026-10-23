#pragma once

#include "core/Track.h"

#include <QList>
#include <QString>

#include <functional>

class QAction;

namespace newamp {

// One entry in a library: a folder to open (a section, artist, album or
// playlist) or a track.
struct LibraryItem
{
    // What the entry is. Everything but Track opens as a folder; the kind
    // only groups search results.
    enum class Kind { Folder, Artist, Album, Playlist, Track };

    QString id; // the provider's key; the host never looks inside it
    QString title;
    QString detail; // shown right-aligned: a year, a count or a duration
    Kind kind = Kind::Folder;
    Track track; // set for tracks

    bool isFolder() const { return kind != Kind::Track; }
};

// Music a plugin offers for browsing in the Media Library window. The host
// draws it in the current skin; the plugin only answers questions about it.
//
// Header-only and free of QObject, like PluginHost, so plugins need no
// symbols from the executable.
class LibraryProvider
{
public:
    using ItemsCallback = std::function<void(const QList<LibraryItem> &items)>;
    using TracksCallback = std::function<void(const QList<Track> &tracks)>;

    virtual ~LibraryProvider() = default;

    virtual QString libraryName() const = 0;

    // The items in a folder; an empty id is the top level. done runs on the
    // main thread, possibly later, or never if the request fails; status()
    // then says why.
    virtual void listItems(const QString &folderId, const ItemsCallback &done) = 0;
    // Every track inside a folder, in play order. Same rules as listItems.
    virtual void folderTracks(const QString &folderId, const TracksCallback &done) = 0;

    // Whether search() can answer right now. It may change, e.g. after a
    // sign-in; call PluginHost::libraryStatusChanged() when it does.
    virtual bool canSearch() const { return false; }
    // Artists, albums, playlists and tracks matching query, which is what
    // the user typed. scopeId is the folder they searched from (empty for
    // the top level); results may come from wider than it. Result folders
    // open with listItems() and queue with folderTracks(). Same callback
    // rules as listItems.
    virtual void search(const QString &scopeId, const QString &query, const ItemsCallback &done)
    {
        Q_UNUSED(scopeId)
        Q_UNUSED(query)
        done({});
    }

    // One line about the connection or the last error; empty when idle.
    virtual QString status() const = 0;
    // Account, server choice, refresh: shown in the library window's menu.
    virtual QList<QAction *> libraryActions() { return {}; }
};

} // namespace newamp
