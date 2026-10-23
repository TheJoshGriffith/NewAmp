#pragma once

#include "plugins/LibraryProvider.h"
#include "ui/GenericWindow.h"

#include <QSet>

namespace newamp {

class PluginHost;
class SkinHotspot;
class SkinListView;
class SkinSlider;

// The Media Library: browses the libraries plugins offer (Plex, Jellyfin) in
// the current skin. Folders open in place with a path bar to go back;
// tracks and whole folders can be played or added to the playlist. Typing
// filters the open folder; Find (Ctrl+F) asks the library itself, and the
// results open as a folder grouped by kind.
class LibraryWindow : public GenericWindow
{
    Q_OBJECT

public:
    // host receives the tracks to add or play.
    LibraryWindow(const Skin *skin, PluginHost &host, QWidget *parent = nullptr);

    void addLibrary(LibraryProvider *library);
    // Start the library over from its top level, e.g. after a sign-in.
    void reload(LibraryProvider *library);
    // Show the window open at this library.
    void showLibrary(LibraryProvider *library);

    // Play or add the selected items, as the buttons do.
    void playSelection() { useSelection(true); }
    void addSelection() { useSelection(false); }
    // Start typing a search of the open library, seeded with the filter.
    void beginSearch();
    // Search the open library from the current folder.
    void search(const QString &query);

    QString currentPath() const { return pathText(); }
    int itemCount() const { return int(m_visible.size()); }

protected:
    void paintContent(QPainter &painter, const Skin &skin, const QRect &content) override;
    void layoutContent(const QRect &content) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct Level
    {
        LibraryProvider *library = nullptr; // null for the list of libraries
        QString folderId; // for search results, the folder searched from
        QString title;
        QString query;    // set for search results
        int firstRow = 0; // scroll and cursor to restore on the way back
        int anchor = 0;
    };

    void goTo(const QList<Level> &path);
    void load();
    void open(int row);
    void back();
    void useSelection(bool play);
    void setFilter(const QString &filter);
    void applyFilter();
    void setResults(const QList<LibraryItem> &items);
    bool isHeader(int row) const { return m_headers.contains(m_visible.at(row)); }
    LibraryProvider *searchableLibrary() const;
    void updateScrollBar();
    const LibraryItem &itemAt(int row) const { return m_items.at(m_visible.at(row)); }
    QString statusText() const;
    QString pathText() const;

    PluginHost &m_host;
    QList<LibraryProvider *> m_libraries;
    QList<Level> m_path;         // the root list of libraries is an empty path
    QList<LibraryItem> m_items;  // the current folder
    QList<int> m_visible;        // m_items indexes that pass the filter
    QString m_filter;
    QSet<int> m_headers;         // m_items indexes of search group titles
    bool m_typingQuery = false;  // keys go to m_query, not the filter
    QString m_query;
    QString m_message;           // replaces the status line until the next load
    bool m_loading = false;
    int m_generation = 0;        // drops replies for folders already left

    SkinListView *m_list = nullptr;
    SkinSlider *m_scrollBar = nullptr;
    SkinHotspot *m_backButton = nullptr;
    SkinHotspot *m_playButton = nullptr;
    SkinHotspot *m_addButton = nullptr;
    SkinHotspot *m_findButton = nullptr;
    QRect m_pathBar;
    QRect m_bottomBar;
};

} // namespace newamp
