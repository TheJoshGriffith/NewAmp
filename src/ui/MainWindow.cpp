#include "ui/MainWindow.h"

#include "core/LocalFileSource.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "skin/Skin.h"
#include "ui/SkinElements.h"
#include "ui/WindowManager.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QStandardPaths>
#include <QTimer>

#include <iterator>

namespace newamp {

using namespace sprites;

namespace {
constexpr qreal ScaleSteps[] = {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0};

QString skinsDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Skins");
}
} // namespace

MainWindow::MainWindow(const Skin *skin, PlayerController &controller, QWidget *parent)
    : SkinnedWindow(skin, MainWindowSize, parent)
    , m_controller(controller)
    , m_marqueeTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("main"));
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
        [](double v) { return volumeBackground(qRound(v * (VolumeSteps - 1))); }, VolumeThumb,
        Qt::Horizontal, QPoint(0, 1));
    m_volume->setValue(m_controller.engine().volume());
    m_volume->onMoved = [this](double v) { m_controller.engine().setVolume(float(v)); };
    m_volume->onReleased = m_volume->onMoved;

    // Balance art is symmetric; the step is the distance from centre.
    m_balance = addElement<SkinSlider>(
        BalanceBar, Sheet::Balance,
        [](double v) { return balanceBackground(qRound(qAbs(v - 0.5) * 2 * (VolumeSteps - 1))); }, BalanceThumb,
        Qt::Horizontal, QPoint(0, 1));
    m_balance->setValue(0.5);
    m_balance->onReleased = [this](double v) {
        // Snap back to centre when close, as the classic slider does.
        if (qAbs(v - 0.5) < 0.08)
            m_balance->setValue(0.5);
    };

    m_kbps = addElement<SkinText>(KbpsText, Qt::AlignRight);
    // Channel count isn't reported by the playback backend yet, so the
    // indicators stay unlit for now.
    addElement<SkinImage>(MonoPos, Sheet::MonoSter, MonoUnlit);
    addElement<SkinImage>(StereoPos, Sheet::MonoSter, StereoUnlit);

    Playlist &playlist = m_controller.playlist();
    m_shuffle = addElement<SkinToggleButton>(ShuffleButtonPos, Sheet::ShufRep, ShuffleButton,
                                             [&playlist](bool on) { playlist.setShuffle(on); });
    m_repeat = addElement<SkinToggleButton>(RepeatButtonPos, Sheet::ShufRep, RepeatButton,
                                            [&playlist](bool on) { playlist.setRepeat(on); });
    m_eqToggle = addElement<SkinToggleButton>(EqToggleButtonPos, Sheet::ShufRep, EqToggleButton, nullptr);
    m_plToggle = addElement<SkinToggleButton>(PlaylistToggleButtonPos, Sheet::ShufRep, PlaylistToggleButton, nullptr);
    auto syncModes = [this, &playlist] {
        m_shuffle->setChecked(playlist.shuffle());
        m_repeat->setChecked(playlist.repeat());
    };
    connect(&playlist, &Playlist::modesChanged, this, syncModes);
    syncModes();

    PlaybackEngine &engine = m_controller.engine();
    connect(&engine, &PlaybackEngine::bitrateChanged, this,
            [this](int kbps) { m_kbps->setText(kbps > 0 ? QString::number(kbps) : QString()); });
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

void MainWindow::setCompanionWindows(SkinnedWindow *equalizer, SkinnedWindow *playlist)
{
    for (const auto &[toggle, window] : {std::pair{m_eqToggle, equalizer}, std::pair{m_plToggle, playlist}}) {
        SkinToggleButton *button = toggle;
        SkinnedWindow *target = window;
        button->setOnToggled([target](bool on) { target->setVisible(on); });
        button->setChecked(target->isVisible());
        connect(target, &SkinnedWindow::visibilityChanged, this, [button](bool visible) { button->setChecked(visible); });
    }
}

void MainWindow::setGlobalScale(qreal scale)
{
    if (WindowManager *manager = windowManager())
        manager->setScale(scale);
    else
        setScale(scale);
}

void MainWindow::stepScale(int direction)
{
    m_scaleBeforeDouble = 0;
    const qreal current = scale();
    if (direction > 0) {
        for (const qreal step : ScaleSteps)
            if (step > current + 0.01) {
                setGlobalScale(step);
                return;
            }
    } else {
        for (auto it = std::rbegin(ScaleSteps); it != std::rend(ScaleSteps); ++it)
            if (*it < current - 0.01) {
                setGlobalScale(*it);
                return;
            }
    }
}

void MainWindow::toggleDoubleSize()
{
    // Doubles whatever scale is current, so it stays useful on big screens.
    if (m_scaleBeforeDouble > 0) {
        setGlobalScale(m_scaleBeforeDouble);
        m_scaleBeforeDouble = 0;
    } else {
        m_scaleBeforeDouble = scale();
        setGlobalScale(qMin(scale() * 2, MaxScale));
    }
}

void MainWindow::loadSkin(const QString &path)
{
    WindowManager *manager = windowManager();
    if (!manager)
        return;
    QString error;
    if (!manager->loadSkin(path, &error))
        m_marquee->setText(tr("Could not load skin: %1").arg(error));
}

void MainWindow::populateSkinsMenu(QMenu *menu)
{
    WindowManager *manager = windowManager();
    if (!manager)
        return;
    QAction *defaultSkin = menu->addAction(tr("NewAmp default"), manager, &WindowManager::useDefaultSkin);
    defaultSkin->setCheckable(true);
    defaultSkin->setChecked(manager->skinPath().isEmpty());

    // Skins the user dropped into the skins folder.
    const QString dir = skinsDirectory();
    const QFileInfoList skins = QDir(dir).entryInfoList({QStringLiteral("*.wsz"), QStringLiteral("*.zip")},
                                                       QDir::Files, QDir::Name | QDir::IgnoreCase);
    if (!skins.isEmpty())
        menu->addSeparator();
    for (const QFileInfo &info : skins) {
        QAction *action = menu->addAction(info.completeBaseName(), this,
                                          [this, path = info.absoluteFilePath()] { loadSkin(path); });
        action->setCheckable(true);
        action->setChecked(info.absoluteFilePath() == manager->skinPath());
    }
    menu->addSeparator();
    menu->addAction(tr("Load skin..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Load skin"), skinsDirectory(),
                                                          tr("Classic skins (*.wsz *.zip)"));
        if (!path.isEmpty())
            loadSkin(path);
    });
    menu->addAction(tr("Open skins folder"), this, [dir] {
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
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
    populateSkinsMenu(menu.addMenu(tr("Skins")));
    QMenu *scaleMenu = menu.addMenu(tr("Scale"));
    for (const qreal option : ScaleSteps) {
        QAction *action = scaleMenu->addAction(QStringLiteral("%1\u00d7").arg(option), this, [this, option] {
            m_scaleBeforeDouble = 0;
            setGlobalScale(option);
        });
        action->setCheckable(true);
        action->setChecked(qFuzzyCompare(scale(), option));
    }
    scaleMenu->addSeparator();
    QAction *bigger = scaleMenu->addAction(tr("Larger"), this, [this] { stepScale(1); });
    bigger->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
    QAction *smaller = scaleMenu->addAction(tr("Smaller"), this, [this] { stepScale(-1); });
    smaller->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    QAction *doubleSize = scaleMenu->addAction(tr("Double size"), this, &MainWindow::toggleDoubleSize);
    doubleSize->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    doubleSize->setCheckable(true);
    doubleSize->setChecked(m_scaleBeforeDouble > 0);
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
            toggleDoubleSize();
            break;
        }
        SkinnedWindow::keyPressEvent(event);
        return;
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        if (ctrl) {
            stepScale(1);
            break;
        }
        SkinnedWindow::keyPressEvent(event);
        return;
    case Qt::Key_Minus:
        if (ctrl) {
            stepScale(-1);
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
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        // Dropping a skin applies it, as in the classic player.
        if (path.endsWith(QLatin1String(".wsz"), Qt::CaseInsensitive)) {
            loadSkin(path);
            event->acceptProposedAction();
            return;
        }
        paths << path;
    }
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
