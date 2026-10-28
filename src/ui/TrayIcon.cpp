#include "ui/TrayIcon.h"

#include "core/PlaybackEngine.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"

#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWidget>

namespace newamp {

namespace {
const QString TraySetting = QStringLiteral("ui/trayIcon");
constexpr int MaxTitleLength = 60;
} // namespace

TrayIcon::TrayIcon(PlayerController &controller, QWidget *mainWindow, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_mainWindow(mainWindow)
{
    m_nowPlaying = m_menu.addAction(QString());
    m_nowPlaying->setEnabled(false);
    m_menu.addSeparator();
    PlayerController *c = &m_controller;
    m_menu.addAction(tr("Previous"), this, [c] { c->previous(); });
    m_menu.addAction(tr("Play"), this, [c] { c->play(); });
    m_menu.addAction(tr("Pause"), this, [c] { c->pause(); });
    m_menu.addAction(tr("Stop"), this, [c] { c->stop(); });
    m_menu.addAction(tr("Next"), this, [c] { c->next(); });

    connect(&m_controller.playlist(), &Playlist::currentIndexChanged, this, &TrayIcon::updateMenu);
    connect(&m_controller.playlist(), &Playlist::tracksChanged, this, &TrayIcon::updateMenu);
    connect(&m_controller.engine(), &PlaybackEngine::stateChanged, this, &TrayIcon::updateMenu);

#ifdef Q_OS_MACOS
    // The Dock has its own Quit, and clicking the icon shows the player.
    m_menu.setAsDockMenu();
#else
    m_menu.addSeparator();
    m_menu.addAction(tr("Show NewAmp"), this, &TrayIcon::showMainWindow);
    m_menu.addAction(tr("Exit"), qApp, &QApplication::quit);
    if (hasTray()) {
        m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/newamp/newamp.png")), this);
        m_tray->setContextMenu(&m_menu);
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                showMainWindow();
            else if (reason == QSystemTrayIcon::MiddleClick)
                m_controller.playPause();
        });

        m_toggle = new QAction(tr("Show tray icon"), this);
        m_toggle->setCheckable(true);
        connect(m_toggle, &QAction::toggled, this, [this](bool on) {
            setVisible(on);
            QSettings().setValue(TraySetting, on);
        });
        m_toggle->setChecked(QSettings().value(TraySetting, true).toBool());
    }
#endif
    updateMenu();
}

TrayIcon::~TrayIcon() = default;

bool TrayIcon::hasTray()
{
#ifdef Q_OS_MACOS
    return false;
#else
    return QSystemTrayIcon::isSystemTrayAvailable();
#endif
}

void TrayIcon::setVisible(bool visible)
{
    if (m_tray)
        m_tray->setVisible(visible);
}

bool TrayIcon::isVisible() const
{
    return m_tray && m_tray->isVisible();
}

void TrayIcon::updateMenu()
{
    const Track *track = m_controller.playlist().currentTrack();
    QString title = track ? track->displayTitle() : QStringLiteral("NewAmp");
    if (title.size() > MaxTitleLength)
        title = title.left(MaxTitleLength - 1) + QChar(0x2026);
    m_nowPlaying->setText(title);
    if (m_tray) {
        const bool playing = m_controller.engine().state() == PlaybackEngine::State::Playing;
        m_tray->setToolTip(track && playing ? QStringLiteral("NewAmp - %1").arg(title) : QStringLiteral("NewAmp"));
    }
}

void TrayIcon::showMainWindow()
{
    if (m_mainWindow->isMinimized())
        m_mainWindow->showNormal();
    else
        m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

} // namespace newamp
