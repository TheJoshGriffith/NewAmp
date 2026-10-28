#include "ui/MainWindow.h"

#include "core/LocalFileSource.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "plugins/PluginManager.h"
#include "plugins/SourcePlugin.h"
#include "skin/DefaultSkin.h"
#include "skin/Skin.h"
#include "ui/SkinElements.h"
#include "ui/SkinVisualizer.h"
#include "ui/WindowManager.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QSettings>
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

    // Classic Ctrl+L, from any NewAmp window.
    m_openUrl = new QAction(tr("Open URL..."), this);
    m_openUrl->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    m_openUrl->setShortcutContext(Qt::ApplicationShortcut);
    addAction(m_openUrl);
    connect(m_openUrl, &QAction::triggered, this, &MainWindow::openUrl);

    PlayerController *c = &m_controller;
    addElement<SkinButton>(OptionsButtonPos, Sheet::TitleBar, OptionsButton, [this] { showMenuAtOptionsButton(); });
    addElement<SkinButton>(MinimizeButtonPos, Sheet::TitleBar, MinimizeButton, [this] { showMinimized(); });
    addElement<SkinButton>(ShadeButtonPos, Sheet::TitleBar, ShadeButton, [this] { setShaded(true); });
    addElement<SkinButton>(CloseButtonPos, Sheet::TitleBar, CloseButton, [this] { close(); });

    addElement<SkinButton>(PreviousButtonPos, Sheet::CButtons, PreviousButton, [c] { c->previous(); });
    addElement<SkinButton>(PlayButtonPos, Sheet::CButtons, PlayButton, [c] { c->play(); });
    addElement<SkinButton>(PauseButtonPos, Sheet::CButtons, PauseButton, [c] { c->pause(); });
    addElement<SkinButton>(StopButtonPos, Sheet::CButtons, StopButton, [c] { c->stop(); });
    addElement<SkinButton>(NextButtonPos, Sheet::CButtons, NextButton, [c] { c->next(); });
    addElement<SkinButton>(EjectButtonPos, Sheet::CButtons, EjectButton, [this] { openFiles(); });

    m_stateIcon = addElement<SkinImage>(PlayStatePos, Sheet::PlayPause, StoppedIndicator);
    m_timeRemaining = QSettings().value(QStringLiteral("player/timeRemaining"), false).toBool();
    m_time = addElement<SkinTimeDisplay>(TimeDisplay);
    m_time->onClicked = [this] { setShowsTimeRemaining(!m_timeRemaining); };
    m_marquee = addElement<SkinMarquee>(Marquee);

    const auto seekTo = [this](double v) {
        PlaybackEngine &engine = m_controller.engine();
        if (engine.duration() > 0)
            engine.seek(qint64(v * engine.duration()));
    };
    const auto seekBy = [this](double notches) { m_controller.seekBy(qRound64(notches * SeekStepMs)); };
    m_seek = addElement<SkinSlider>(PosBar, Sheet::PosBar,
                                    [](double) { return PosBarBackground; }, PosBarThumb);
    m_seek->onReleased = seekTo;
    m_seek->onWheel = seekBy;
    m_seek->setVisible(false);

    // Windowshade bar: title bar buttons, transport, time and a tiny seek bar.
    addShadeElement<SkinButton>(OptionsButtonPos, Sheet::TitleBar, OptionsButton,
                                [this] { showMenuAtOptionsButton(); });
    addShadeElement<SkinButton>(MinimizeButtonPos, Sheet::TitleBar, MinimizeButton, [this] { showMinimized(); });
    addShadeElement<SkinButton>(ShadeButtonPos, Sheet::TitleBar, UnshadeButton, [this] { setShaded(false); });
    addShadeElement<SkinButton>(CloseButtonPos, Sheet::TitleBar, CloseButton, [this] { close(); });
    addShadeElement<SkinHotspot>(MainShadePrevious, [c] { c->previous(); });
    addShadeElement<SkinHotspot>(MainShadePlay, [c] { c->play(); });
    addShadeElement<SkinHotspot>(MainShadePause, [c] { c->pause(); });
    addShadeElement<SkinHotspot>(MainShadeStop, [c] { c->stop(); });
    addShadeElement<SkinHotspot>(MainShadeNext, [c] { c->next(); });
    addShadeElement<SkinHotspot>(MainShadeEject, [this] { openFiles(); });
    m_shadeTime = addShadeElement<SkinTimeDisplay>(MainShadeTime, SkinTimeDisplay::Font::Text);
    m_shadeTime->onClicked = m_time->onClicked;
    m_shadeSeek = addShadeElement<SkinSlider>(MainShadePosBar, Sheet::TitleBar,
                                              [](double) { return MainShadePosBarBackground; },
                                              MainShadePosBarThumb);
    m_shadeSeek->onReleased = seekTo;
    m_shadeSeek->onWheel = seekBy;
    m_shadeSeek->setVisible(false);

    m_volume = addElement<SkinSlider>(
        VolumeBar, Sheet::Volume,
        [](double v) { return volumeBackground(qRound(v * (VolumeSteps - 1))); }, VolumeThumb,
        Qt::Horizontal, QPoint(0, 1));
    m_volume->setValue(m_controller.engine().volume());
    m_volume->onMoved = [this](double v) { m_controller.engine().setVolume(float(v)); };
    m_volume->onReleased = m_volume->onMoved;
    connect(&m_controller.engine(), &PlaybackEngine::volumeChanged, this,
            [this](float volume) { m_volume->setValue(volume); });

    // Balance art is symmetric; the step is the distance from centre.
    m_balance = addElement<SkinSlider>(
        BalanceBar, Sheet::Balance,
        [](double v) { return balanceBackground(qRound(qAbs(v - 0.5) * 2 * (VolumeSteps - 1))); }, BalanceThumb,
        Qt::Horizontal, QPoint(0, 1));
    m_balance->setValue(0.5 + m_controller.engine().balance() / 2);
    m_balance->onMoved = [this](double v) { m_controller.engine().setBalance((v - 0.5) * 2); };
    m_balance->onReleased = [this](double v) {
        // Snap back to centre when close, as the classic slider does.
        if (qAbs(v - 0.5) < 0.08) {
            m_balance->setValue(0.5);
            v = 0.5;
        }
        m_controller.engine().setBalance((v - 0.5) * 2);
    };
    connect(&m_controller.engine(), &PlaybackEngine::balanceChanged, this,
            [this](double balance) { m_balance->setValue(0.5 + balance / 2); });

    m_kbps = addElement<SkinText>(KbpsText, Qt::AlignRight);
    m_khz = addElement<SkinText>(KhzText, Qt::AlignRight);
    m_mono = addElement<SkinImage>(MonoPos, Sheet::MonoSter, MonoUnlit);
    m_stereo = addElement<SkinImage>(StereoPos, Sheet::MonoSter, StereoUnlit);

    m_visualizer = addElement<SkinVisualizer>(Visualizer, [this](float *out, int count) {
        return m_controller.engine().visualizerSamples(out, count);
    });
    m_visTimer = new QTimer(this);
    m_visTimer->setInterval(33);
    connect(m_visTimer, &QTimer::timeout, this, [this] { m_visualizer->tick(); });

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
    connect(&engine, &PlaybackEngine::formatChanged, this, [this](int sampleRate, int channels) {
        m_khz->setText(sampleRate > 0 ? QString::number(qRound(sampleRate / 1000.0)) : QString());
        m_mono->setSource(channels == 1 ? MonoLit : MonoUnlit);
        m_stereo->setSource(channels >= 2 ? StereoLit : StereoUnlit);
    });
    connect(&engine, &PlaybackEngine::stateChanged, this, &MainWindow::updateState);
    connect(&engine, &PlaybackEngine::positionChanged, this, &MainWindow::updatePosition);
    connect(&engine, &PlaybackEngine::durationChanged, this, &MainWindow::updateTime);
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

void MainWindow::paintShadeBackground(QPainter &painter, const Skin &skin)
{
    skin.drawSprite(painter, {0, 0}, Sheet::TitleBar, isActiveWindow() ? MainShadeActive : MainShadeInactive);
}

void MainWindow::showMenuAtOptionsButton()
{
    const QPoint at = mapToGlobal(QPoint(OptionsButtonPos.x(), TitleBarHeight) * scale());
    QContextMenuEvent event(QContextMenuEvent::Mouse, mapFromGlobal(at), at);
    contextMenuEvent(&event);
}

void MainWindow::setShowsTimeRemaining(bool remaining)
{
    if (remaining == m_timeRemaining)
        return;
    m_timeRemaining = remaining;
    QSettings().setValue(QStringLiteral("player/timeRemaining"), remaining);
    updateTime();
}

void MainWindow::openFiles()
{
    static QString lastDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    const QString filter = tr("Audio files and playlists (%1 *.nap *.m3u8 *.m3u *.pls)")
                               .arg(LocalFileSource::fileFilters().join(QLatin1Char(' ')));
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open files"), lastDir, filter);
    if (files.isEmpty())
        return;
    lastDir = QFileInfo(files.first()).absolutePath();
    m_controller.openTracks(LocalFileSource::tracksForPaths(files));
}

void MainWindow::openUrl()
{
    const QString text = QInputDialog::getText(this, tr("Open URL"), tr("Stream or playlist address:"));
    if (!text.trimmed().isEmpty())
        m_controller.openUrl(QUrl::fromUserInput(text.trimmed()));
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

void MainWindow::addWindowToggle(const QString &name, SkinnedWindow *window, const QKeySequence &shortcut)
{
    // Added to this widget too, so the shortcut works without the menu.
    auto *action = new QAction(name, this);
    action->setCheckable(true);
    action->setChecked(window->isVisible());
    action->setShortcut(shortcut);
    action->setShortcutContext(Qt::ApplicationShortcut); // from any NewAmp window
    addAction(action);
    connect(action, &QAction::triggered, window, [window](bool on) {
        window->setVisible(on);
        if (on)
            window->raise();
    });
    connect(window, &SkinnedWindow::visibilityChanged, action, &QAction::setChecked);
    m_windowToggles.append(action);
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
    // Built-in themes first; the first theme is the default skin.
    const QList<DefaultSkin::ThemeInfo> themes = DefaultSkin::themes();
    for (int i = 0; i < themes.size(); ++i) {
        const QString path = i == 0 ? QString() : WindowManager::themePath(themes.at(i).id);
        QAction *action = menu->addAction(themes.at(i).name, this, [this, manager, path] {
            if (path.isEmpty())
                manager->useDefaultSkin();
            else
                loadSkin(path);
        });
        action->setCheckable(true);
        action->setChecked(manager->skinPath() == path);
    }

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
    // Classic skins belong to their artists, so NewAmp doesn't bundle them;
    // the Winamp Skin Museum is the archive people download them from.
    menu->addAction(tr("Get more skins..."), this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://skins.webamp.org/")));
    });
}

void MainWindow::addPluginMenus(QMenu *menu)
{
    if (!m_plugins)
        return;
    // One submenu per loaded plugin that offers actions.
    for (const LoadedPlugin &loaded : m_plugins->loaded()) {
        const QList<QAction *> actions = loaded.plugin->actions();
        if (!actions.isEmpty())
            menu->addMenu(loaded.info.name)->addActions(actions);
    }

    // Every installed plugin, toggled in settings and applied on restart.
    QMenu *list = menu->addMenu(tr("Plugins"));
    if (m_plugins->available().isEmpty())
        list->addAction(tr("No plugins installed"))->setEnabled(false);
    const QString key = QStringLiteral("plugins/enabled");
    const QStringList enabled = QSettings().value(key).toStringList();
    for (const PluginInfo &info : m_plugins->available()) {
        QAction *action = list->addAction(info.name, this, [this, key, id = info.id](bool on) {
            QSettings settings;
            QStringList ids = settings.value(key).toStringList();
            ids.removeAll(id);
            if (on)
                ids << id;
            settings.setValue(key, ids);
            m_marquee->setText(tr("Restart NewAmp to apply plugin changes"));
        });
        action->setCheckable(true);
        action->setChecked(enabled.contains(info.id));
        action->setToolTip(info.description);
    }
    list->setToolTipsVisible(true);
    if (!m_plugins->available().isEmpty()) {
        list->addSeparator();
        list->addAction(tr("Changes apply after a restart"))->setEnabled(false);
    }
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(tr("Open files..."), this, &MainWindow::openFiles);
    menu.addAction(m_openUrl);
    menu.addAction(tr("Open folder..."), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Open folder"));
        if (!dir.isEmpty())
            m_controller.openTracks(LocalFileSource::tracksForPaths({dir}));
    });
    if (!m_windowToggles.isEmpty()) {
        menu.addSeparator();
        menu.addActions(m_windowToggles);
    }
    menu.addSeparator();
    QAction *remaining = menu.addAction(tr("Time remaining"), this,
                                        [this](bool on) { setShowsTimeRemaining(on); });
    remaining->setCheckable(true);
    remaining->setChecked(m_timeRemaining);
    QAction *shade = menu.addAction(tr("Windowshade mode"), this, [this] { toggleShaded(); });
    shade->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    shade->setCheckable(true);
    shade->setChecked(isShaded());
    menu.addSeparator();
    addPluginMenus(&menu);
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
    // L opens files; the transport keys live in SkinnedWindow.
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    switch (event->key()) {
    case Qt::Key_L:
        if (!ctrl) {
            openFiles();
            break;
        }
        SkinnedWindow::keyPressEvent(event);
        return;
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
    // The visualizer only animates while there's something to show.
    if (state == PlaybackEngine::State::Playing)
        m_visTimer->start();
    else
        m_visTimer->stop();
    if (state == PlaybackEngine::State::Stopped)
        m_visualizer->setMode(m_visualizer->mode()); // clears it
    m_seek->setVisible(active);
    m_shadeSeek->setVisible(active);
    if (!active) {
        m_seek->setValue(0);
        m_shadeSeek->setValue(0);
    }
    updateTime();
}

void MainWindow::updatePosition(qint64 positionMs)
{
    if (m_controller.engine().state() == PlaybackEngine::State::Stopped)
        return;
    updateTime();
    const qint64 duration = m_controller.engine().duration();
    if (duration > 0) {
        m_seek->setValue(double(positionMs) / duration);
        m_shadeSeek->setValue(double(positionMs) / duration);
    }
}

void MainWindow::updateTime()
{
    const PlaybackEngine &engine = m_controller.engine();
    qint64 seconds = -1;
    bool countingDown = false;
    if (engine.state() != PlaybackEngine::State::Stopped) {
        // Streams without a length can only count up.
        const qint64 duration = engine.duration();
        countingDown = m_timeRemaining && duration > 0;
        // Rounded up when counting down, so it reaches 0:00 as the track ends.
        seconds = countingDown ? (qMax<qint64>(0, duration - engine.position()) + 999) / 1000
                               : engine.position() / 1000;
    }
    m_time->setSeconds(seconds, countingDown);
    m_shadeTime->setSeconds(seconds, countingDown);
}

} // namespace newamp
