#pragma once

#include "plugins/LibraryProvider.h"
#include "ui/GenericWindow.h"

namespace newamp {

class PluginHost;
class SkinHotspot;
class SkinListView;
class SkinSlider;

// The Media Library: browses the libraries plugins offer (Plex today) in
// the current skin. Folders open in place with a path bar to go back;
// tracks and whole folders can be played or added to the playlist.
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
        QString folderId;
        QString title;
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
    QString m_message;           // replaces the status line until the next load
    bool m_loading = false;
    int m_generation = 0;        // drops replies for folders already left

    SkinListView *m_list = nullptr;
    SkinSlider *m_scrollBar = nullptr;
    SkinHotspot *m_backButton = nullptr;
    SkinHotspot *m_playButton = nullptr;
    SkinHotspot *m_addButton = nullptr;
    QRect m_pathBar;
    QRect m_bottomBar;
};

} // namespace newamp
