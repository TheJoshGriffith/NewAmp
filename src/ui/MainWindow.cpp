#include "ui/MainWindow.h"

#include "core/LocalFileSource.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "skin/Skin.h"
#include "ui/SkinElements.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QStandardPaths>
#include <QTimer>

namespace newamp {

using namespace sprites;

MainWindow::MainWindow(const Skin *skin, PlayerController &controller, QWidget *parent)
    : SkinnedWindow(skin, MainWindowSize, parent)
    , m_controller(controller)
    , m_marqueeTimer(new QTimer(this))
{
    setWindowTitle(QStringLiteral("NewAmp"));
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);

    PlayerController *c = &m_controller;
    addElement<SkinButton>(OptionsButtonPos, Sheet::TitleBar, OptionsButton, [this] {
        const QPoint at = mapToGlobal(QPoint(OptionsButtonPos.x(), TitleBarHeight) * scale());
        QContextMenuEvent event(QContextMenuEvent::Mouse, mapFromGlobal(at), at);
        contextMenuEvent(&event);
    });
    addElement<SkinButton>(MinimizeButtonPos, Sheet::TitleBar, MinimizeButton, [this] { showMinimized(); });
    addElement<SkinButton>(CloseButtonPos, Sheet::TitleBar, CloseButton, [this] { close(); });

    addElement<SkinButton>(PreviousButtonPos, Sheet::CButtons, PreviousButton, [c] { c->previous(); });
    addElement<SkinButton>(PlayButtonPos, Sheet::CButtons, PlayButton, [c] { c->play(); });
    addElement<SkinButton>(PauseButtonPos, Sheet::CButtons, PauseButton, [c] { c->pause(); });
    addElement<SkinButton>(StopButtonPos, Sheet::CButtons, StopButton, [c] { c->stop(); });
    addElement<SkinButton>(NextButtonPos, Sheet::CButtons, NextButton, [c] { c->next(); });
    addElement<SkinButton>(EjectButtonPos, Sheet::CButtons, EjectButton, [this] { openFiles(); });

    m_stateIcon = addElement<SkinImage>(PlayStatePos, Sheet::PlayPause, StoppedIndicator);
    m_time = addElement<SkinTimeDisplay>(TimeDisplay);
    m_marquee = addElement<SkinMarquee>(Marquee);

    m_seek = addElement<SkinSlider>(PosBar, Sheet::PosBar,
                                    [](double) { return PosBarBackground; }, PosBarThumb);
    m_seek->onReleased = [this](double v) {
        PlaybackEngine &engine = m_controller.engine();
        if (engine.duration() > 0)
            engine.seek(qint64(v * engine.duration()));
    };
    m_seek->setVisible(false);

    m_volume = addElement<SkinSlider>(
        VolumeBar, Sheet::Volume,
        [](double v) { return volumeBackground(qRound(v * (VolumeSteps - 1))); }, VolumeThumb, 1);
    m_volume->setValue(m_controller.engine().volume());
    m_volume->onMoved = [this](double v) { m_controller.engine().setVolume(float(v)); };
    m_volume->onReleased = m_volume->onMoved;

    PlaybackEngine &engine = m_controller.engine();
    connect(&engine, &PlaybackEngine::stateChanged, this, &MainWindow::updateState);
    connect(&engine, &PlaybackEngine::positionChanged, this, &MainWindow::updatePosition);
    connect(&engine, &PlaybackEngine::errorOccurred, this, [this](const QString &message) {
        m_marquee->setText(tr("Error: %1").arg(message));
    });
    connect(&m_controller.playlist(), &Playlist::currentIndexChanged, this, &MainWindow::updateTitle);
    connect(&m_controller.playlist(), &Playlist::tracksChanged, this, &MainWindow::updateTitle);

    connect(m_marqueeTimer, &QTimer::timeout, this, [this] { m_marquee->tick(); });
    m_marqueeTimer->start(200);

    updateTitle();
}

void MainWindow::paintBackground(QPainter &painter, const Skin &skin)
{
    skin.drawSprite(painter, {0, 0}, Sheet::Main, QRect(QPoint(0, 0), MainWindowSize));
    skin.drawSprite(painter, {0, 0}, Sheet::TitleBar, isActiveWindow() ? TitleBarActive : TitleBarInactive);
}

void MainWindow::openFiles()
{
    static QString lastDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    const QString filter = tr("Audio files (%1)").arg(LocalFileSource::fileFilters().join(QLatin1Char(' ')));
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open files"), lastDir, filter);
    if (files.isEmpty())
        return;
    lastDir = QFileInfo(files.first()).absolutePath();
    m_controller.openTracks(LocalFileSource::tracksForPaths(files));
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(tr("Open files..."), this, &MainWindow::openFiles);
    menu.addAction(tr("Open folder..."), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Open folder"));
        if (!dir.isEmpty())
            m_controller.openTracks(LocalFileSource::tracksForPaths({dir}));
    });
    menu.addSeparator();
    QAction *doubleSize = menu.addAction(tr("Double size"), this, [this] { setScale(scale() == 1 ? 2 : 1); });
    doubleSize->setCheckable(true);
    doubleSize->setChecked(scale() == 2);
    menu.addSeparator();
    menu.addAction(tr("About NewAmp"), this, [this] {
        m_marquee->setText(QStringLiteral("NEWAMP " NEWAMP_VERSION " - FREE SOFTWARE UNDER GPL-3.0"));
    });
    menu.addAction(tr("Exit"), qApp, &QApplication::quit);
    menu.exec(event->globalPos());
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // The classic Z X C V B transport row, plus L to open.
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    switch (event->key()) {
    case Qt::Key_Z: m_controller.previous(); break;
    case Qt::Key_X: m_controller.play(); break;
    case Qt::Key_C: m_controller.pause(); break;
    case Qt::Key_V: m_controller.stop(); break;
    case Qt::Key_B: m_controller.next(); break;
    case Qt::Key_L: openFiles(); break;
    case Qt::Key_D:
        if (ctrl) {
            setScale(scale() == 1 ? 2 : 1);
            break;
        }
        Q_FALLTHROUGH();
    default: SkinnedWindow::keyPressEvent(event); return;
    }
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    m_controller.openTracks(LocalFileSource::tracksForPaths(paths));
    event->acceptProposedAction();
}

void MainWindow::updateTitle()
{
    const Playlist &playlist = m_controller.playlist();
    if (const Track *track = playlist.currentTrack()) {
        QString text = QStringLiteral("%1. %2").arg(playlist.currentIndex() + 1).arg(track->displayTitle());
        m_marquee->setText(text);
    } else {
        m_marquee->setText(QStringLiteral("NEWAMP " NEWAMP_VERSION));
    }
}

void MainWindow::updateState(PlaybackEngine::State state)
{
    switch (state) {
    case PlaybackEngine::State::Playing: m_stateIcon->setSource(PlayingIndicator); break;
    case PlaybackEngine::State::Paused: m_stateIcon->setSource(PausedIndicator); break;
    case PlaybackEngine::State::Stopped: m_stateIcon->setSource(StoppedIndicator); break;
    }
    const bool active = state != PlaybackEngine::State::Stopped;
    m_seek->setVisible(active);
    m_time->setSeconds(active ? m_controller.engine().position() / 1000 : -1);
    if (!active)
        m_seek->setValue(0);
}

void MainWindow::updatePosition(qint64 positionMs)
{
    if (m_controller.engine().state() == PlaybackEngine::State::Stopped)
        return;
    m_time->setSeconds(positionMs / 1000);
    const qint64 duration = m_controller.engine().duration();
    if (duration > 0)
        m_seek->setValue(double(positionMs) / duration);
}

} // namespace newamp
