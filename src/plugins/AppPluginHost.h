#pragma once

#include "plugins/PluginHost.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <optional>

namespace newamp {

class PlaybackEngine;
class PlayerController;
class Playlist;

// The app's side of PluginHost: turns playlist and engine signals into the
// observer calls plugins see, and forwards their tracks to the controller.
//
// Create it before the PlayerController so it sees endOfTrack before the
// controller moves on to the next track.
class AppPluginHost final : public QObject, public PluginHost
{
public:
    AppPluginHost(Playlist &playlist, PlaybackEngine &engine, QObject *parent = nullptr);

    void setController(PlayerController *controller) { m_controller = controller; }
    void setDialogParent(QWidget *parent) { m_dialogParent = parent; }

    void addTracks(const QList<Track> &tracks) override;
    void playTracks(const QList<Track> &tracks) override;
    QWidget *dialogParent() const override { return m_dialogParent; }
    std::unique_ptr<QSettings> settings(const QString &pluginId) const override;
    void addPlaybackObserver(PlaybackObserver *observer) override;
    void addLibrary(LibraryProvider *library) override;
    void libraryChanged(LibraryProvider *library) override;
    void libraryStatusChanged(LibraryProvider *library) override;
    void showLibrary(LibraryProvider *library) override;

    QList<LibraryProvider *> libraries() const { return m_libraries; }
    // The library window connects here; the plugin layer doesn't know it.
    std::function<void(LibraryProvider *)> onLibraryAdded;
    std::function<void(LibraryProvider *)> onLibraryChanged;
    std::function<void(LibraryProvider *)> onLibraryStatusChanged;
    std::function<void(LibraryProvider *)> onShowLibrary;

private:
    void startCurrentTrack(bool force);
    template<typename Fn>
    void notify(Fn fn);

    Playlist &m_playlist;
    PlaybackEngine &m_engine;
    PlayerController *m_controller = nullptr;
    QPointer<QWidget> m_dialogParent;
    QList<PlaybackObserver *> m_observers;
    QList<LibraryProvider *> m_libraries;
    std::optional<Track> m_started; // the track observers were told about
    bool m_restart = true;          // next Playing state starts a track afresh
    bool m_indexChanged = false;    // cursor moved; check on next position
};

} // namespace newamp
