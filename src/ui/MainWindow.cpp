#include "ui/MainWindow.h"

#include "audio/DspRack.h"
#include "core/LocalFileSource.h"
#include "core/PlaybackEngine.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "plugins/AppPluginHost.h"
#include "plugins/DspEffect.h"
#include "plugins/PluginManager.h"
#include "plugins/SourcePlugin.h"
#include "skin/DefaultSkin.h"
#include "skin/Skin.h"
#include "ui/PlaybackMenu.h"
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

#include <algorithm>
#include <functional>
#include <iterator>
#include <utility>

namespace newamp {

using namespace sprites;

namespace {
constexpr qreal ScaleSteps[] = {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0};

QString skinsDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Skins");
}

QString formatTime(qint64 ms)
{
    const qint64 seconds = qMax<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

const QString TrackNumberKey = QStringLiteral("display/titleTrackNumber");
const QString LengthKey = QStringLiteral("display/titleLength");
const QString AutoScrollKey = QStringLiteral("display/titleAutoScroll");
} // namespace

// The O A I D V strip: options menu, always on top, file info, double size
// and the visualization menu. A and D light while they're on.
class SkinClutterBar : public SkinElement
{
public:
    SkinClutterBar(SkinnedWindow *window, std::function<void(int)> onClicked, std::function<bool(int)> isLit)
        : SkinElement(window, sprites::ClutterBar)
        , m_onClicked(std::move(onClicked))
        , m_isLit(std::move(isLit))
    {
    }

    void paint(QPainter &painter, const Skin &skin) const override
    {
        skin.drawSprite(painter, rect().topLeft(), Sheet::TitleBar, ClutterBarBackground);
        for (int button = 0; button < ClutterButtonCount; ++button)
            if ((button == m_pressed && m_inside) || m_isLit(button))
                skin.drawSprite(painter, clutterButtonArea(button).topLeft(), Sheet::TitleBar, clutterButtonLit(button));
    }

    using SkinElement::update; // when A or D change from elsewhere

    bool acceptsMouse() const override { return true; }
    void mousePress(const QPoint &pos) override
    {
        m_pressed = buttonAt(pos);
        m_inside = true;
        update();
    }
    void mouseMove(const QPoint &pos) override
    {
        const bool inside = m_pressed >= 0 && buttonAt(pos) == m_pressed;
        if (inside != m_inside) {
            m_inside = inside;
            update();
        }
    }
    void mouseRelease(const QPoint &pos) override
    {
        const int button = std::exchange(m_pressed, -1);
        update();
        if (button >= 0 && buttonAt(pos) == button)
            m_onClicked(button);
    }

private:
    static int buttonAt(const QPoint &pos)
    {
        for (int button = 0; button < ClutterButtonCount; ++button)
            if (clutterButtonArea(button).contains(pos))
                return button;
        return -1;
    }

    std::function<void(int)> m_onClicked;
    std::function<bool(int)> m_isLit;
    int m_pressed = -1;
    bool m_inside = false;
};

MainWindow::MainWindow(const Skin *skin, PlayerController &controller, QWidget *parent)
    : SkinnedWindow(skin, MainWindowSize, parent)
    , m_controller(controller)
    , m_marqueeTimer(new QTimer(this))
    , m_messageTimer(new QTimer(this))
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

    // Ctrl+A as in the classic player, but only here: the lists use it to
    // select everything.
    m_alwaysOnTop = new QAction(tr("Always on top"), this);
    m_alwaysOnTop->setCheckable(true);
    m_alwaysOnTop->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
    m_alwaysOnTop->setShortcutContext(Qt::WindowShortcut);
    addAction(m_alwaysOnTop);
    connect(m_alwaysOnTop, &QAction::triggered, this, [this] {
        if (WindowManager *manager = windowManager())
            manager->setAlwaysOnTop(!manager->alwaysOnTop());
        m_clutter->update();
    });
    m_options.append(m_alwaysOnTop);

    QSettings settings;
    m_showTrackNumber = settings.value(TrackNumberKey, true).toBool();
    m_showLength = settings.value(LengthKey, true).toBool();

    setCursorNames(QStringLiteral("normal"), QStringLiteral("titlebar"));
    PlayerController *c = &m_controller;
    addElement<SkinButton>(OptionsButtonPos, Sheet::TitleBar, OptionsButton, [this] { showMenuAtOptionsButton(); })
        ->setCursorName(QStringLiteral("mainmenu"));
    addElement<SkinButton>(MinimizeButtonPos, Sheet::TitleBar, MinimizeButton, [this] { showMinimized(); })
        ->setCursorName(QStringLiteral("min"));
    addElement<SkinButton>(ShadeButtonPos, Sheet::TitleBar, ShadeButton, [this] { setShaded(true); })
        ->setCursorName(QStringLiteral("winbut"));
    addElement<SkinButton>(CloseButtonPos, Sheet::TitleBar, CloseButton, [this] { close(); })
        ->setCursorName(QStringLiteral("close"));
    m_clutter = addElement<SkinClutterBar>([this](int button) { clutterClicked(button); }, [this](int button) {
        const WindowManager *manager = windowManager();
        return (button == ClutterAlwaysOnTop && manager && manager->alwaysOnTop())
            || (button == ClutterDoubleSize && m_scaleBeforeDouble > 0);
    });

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
    m_marquee->setCursorName(QStringLiteral("songname"));
    m_marquee->setAutoScroll(settings.value(AutoScrollKey, true).toBool());
    m_messageTimer->setSingleShot(true);
    m_messageTimer->setInterval(1000);

    const auto seekTo = [this](double v) {
        PlaybackEngine &engine = m_controller.engine();
        if (engine.duration() > 0)
            engine.seek(qint64(v * engine.duration()));
        m_marquee->clearMessage();
    };
    const auto seekBy = [this](double notches) { m_controller.seekBy(qRound64(notches * SeekStepMs)); };
    m_seek = addElement<SkinSlider>(PosBar, Sheet::PosBar,
                                    [](double) { return PosBarBackground; }, PosBarThumb);
    m_seek->setCursorName(QStringLiteral("posbar"));
    m_seek->onMoved = [this](double v) {
        const qint64 duration = m_controller.engine().duration();
        if (duration > 0)
            flashMessage(tr("SEEK TO: %1/%2 (%3%)")
                             .arg(formatTime(qint64(v * duration)), formatTime(duration))
                             .arg(qRound(v * 100)));
    };
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
    m_volume->setCursorName(QStringLiteral("volbal"));
    m_volume->setValue(m_controller.engine().volume());
    m_volume->onMoved = [this](double v) {
        m_controller.engine().setVolume(float(v));
        flashMessage(tr("VOLUME: %1%").arg(qRound(v * 100)));
    };
    m_volume->onReleased = m_volume->onMoved;
    connect(&m_controller.engine(), &PlaybackEngine::volumeChanged, this,
            [this](float volume) { m_volume->setValue(volume); });

    // Balance art is symmetric; the step is the distance from centre.
    m_balance = addElement<SkinSlider>(
        BalanceBar, Sheet::Balance,
        [](double v) { return balanceBackground(qRound(qAbs(v - 0.5) * 2 * (VolumeSteps - 1))); }, BalanceThumb,
        Qt::Horizontal, QPoint(0, 1));
    m_balance->setCursorName(QStringLiteral("volbal"));
    m_balance->setValue(0.5 + m_controller.engine().balance() / 2);
    auto balanceMessage = [this](double v) {
        const int percent = qRound(qAbs(v - 0.5) * 200);
        flashMessage(percent == 0 ? tr("BALANCE: CENTER")
                                  : tr("BALANCE: %1% %2").arg(percent).arg(v < 0.5 ? tr("LEFT") : tr("RIGHT")));
    };
    m_balance->onMoved = [this, balanceMessage](double v) {
        m_controller.engine().setBalance((v - 0.5) * 2);
        balanceMessage(v);
    };
    m_balance->onReleased = [this, balanceMessage](double v) {
        // Snap back to centre when close, as the classic slider does.
        if (qAbs(v - 0.5) < 0.08) {
            m_balance->setValue(0.5);
            v = 0.5;
        }
        m_controller.engine().setBalance((v - 0.5) * 2);
        balanceMessage(v);
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
            [this](int kbps) { m_kbps->setText(kbpsText(kbps)); });
    connect(&engine, &PlaybackEngine::formatChanged, this, [this](int sampleRate, int channels) {
        m_khz->setText(sampleRate > 0 ? QString::number(qRound(sampleRate / 1000.0)) : QString());
        m_mono->setSource(channels == 1 ? MonoLit : MonoUnlit);
        m_stereo->setSource(channels >= 2 ? StereoLit : StereoUnlit);
    });
    connect(&engine, &PlaybackEngine::stateChanged, this, &MainWindow::updateState);
    connect(&engine, &PlaybackEngine::positionChanged, this, &MainWindow::updatePosition);
    connect(&engine, &PlaybackEngine::durationChanged, this, &MainWindow::updateTime);
    // Streams learn their length only once they start.
    connect(&engine, &PlaybackEngine::durationChanged, this, &MainWindow::updateTitle);
    connect(&engine, &PlaybackEngine::errorOccurred, this, [this](const QString &message) {
        m_marquee->setText(tr("Error: %1").arg(message));
    });
    connect(&m_controller.playlist(), &Playlist::currentIndexChanged, this, &MainWindow::updateTitle);
    connect(&m_controller.playlist(), &Playlist::tracksChanged, this, &MainWindow::updateTitle);

    connect(m_marqueeTimer, &QTimer::timeout, this, [this] { m_marquee->tick(); });
    connect(m_messageTimer, &QTimer::timeout, this, [this] {
        // Stays up while a slider is held still.
        if (m_seek->isDragging() || m_volume->isDragging() || m_balance->isDragging())
            m_messageTimer->start();
        else
            m_marquee->clearMessage();
    });
    m_marqueeTimer->start(200);

    updateTitle();
}

QString MainWindow::regionSection() const
{
    return isShaded() ? QStringLiteral("windowshade") : QStringLiteral("normal");
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
    showMainMenu(QPoint(OptionsButtonPos.x(), TitleBarHeight));
}

QString MainWindow::kbpsText(int kbps)
{
    if (kbps <= 0)
        return {};
    if (kbps < 1000)
        return QString::number(kbps);
    // Thousands and the first decimal: 1411 -> "1K4", 12000 -> "12K".
    if (kbps < 10000)
        return QStringLiteral("%1K%2").arg(kbps / 1000).arg(kbps % 1000 / 100);
    return QStringLiteral("%1K").arg(qMin(kbps / 1000, 99));
}

void MainWindow::flashMessage(const QString &text)
{
    m_marquee->showMessage(text);
    m_messageTimer->start();
}

void MainWindow::showMainMenu(const QPoint &skinPos)
{
    const QPoint at = mapToGlobal((QPointF(skinPos) * scale()).toPoint());
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

void MainWindow::showVisMenu(const QPoint &skinPos)
{
    QMenu menu(this);
    const std::pair<SkinVisualizer::Mode, QString> modes[] = {
        {SkinVisualizer::Mode::Spectrum, tr("Spectrum analyzer")},
        {SkinVisualizer::Mode::Oscilloscope, tr("Oscilloscope")},
        {SkinVisualizer::Mode::Off, tr("Off")},
    };
    for (const auto &[mode, name] : modes) {
        QAction *action = menu.addAction(name, this, [this, mode] { m_visualizer->setMode(mode); });
        action->setCheckable(true);
        action->setChecked(m_visualizer->mode() == mode);
    }
    for (QAction *toggle : std::as_const(m_windowToggles))
        if (toggle->data().toString() == QLatin1String("visualizer")) {
            menu.addSeparator();
            menu.addAction(toggle);
        }
    menu.exec(mapToGlobal((QPointF(skinPos) * scale()).toPoint()));
}

void MainWindow::clutterClicked(int button)
{
    const QPoint below = clutterButtonArea(button).topRight() + QPoint(1, 0);
    switch (button) {
    case ClutterOptions: showMainMenu(below); break;
    case ClutterAlwaysOnTop: m_alwaysOnTop->trigger(); break;
    case ClutterFileInfo: Q_EMIT fileInfoRequested(); break;
    case ClutterDoubleSize:
        toggleDoubleSize();
        m_clutter->update();
        break;
    case ClutterVisMenu: showVisMenu(below); break;
    }
}

void MainWindow::addTitleMenu(QMenu *menu)
{
    auto option = [this, menu](const QString &name, bool on, std::function<void(bool)> apply) {
        QAction *action = menu->addAction(name, this, std::move(apply));
        action->setCheckable(true);
        action->setChecked(on);
    };
    option(tr("Scroll song title"), QSettings().value(AutoScrollKey, true).toBool(), [this](bool on) {
        QSettings().setValue(AutoScrollKey, on);
        m_marquee->setAutoScroll(on);
    });
    option(tr("Show track number"), m_showTrackNumber, [this](bool on) {
        QSettings().setValue(TrackNumberKey, on);
        m_showTrackNumber = on;
        updateTitle();
    });
    option(tr("Show song length"), m_showLength, [this](bool on) {
        QSettings().setValue(LengthKey, on);
        m_showLength = on;
        updateTitle();
    });
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
    action->setData(window->objectName());
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
    addDspMenu(menu);
    if (!m_plugins)
        return;
    // One submenu per loaded plugin that offers actions.
    for (const LoadedPlugin &loaded : m_plugins->loaded()) {
        const QList<QAction *> actions = loaded.plugin->actions();
        if (!actions.isEmpty())
            menu->addMenu(loaded.info.name)->addActions(actions);
    }

    // Every installed plugin by kind, toggled in settings and applied on restart.
    QMenu *list = menu->addMenu(tr("Plugins"));
    if (m_plugins->available().isEmpty())
        list->addAction(tr("No plugins installed"))->setEnabled(false);
    const QString key = QStringLiteral("plugins/enabled");
    const QStringList enabled = QSettings().value(key).toStringList();
    static const std::pair<const char *, const char *> Categories[] = {
        {"source", QT_TR_NOOP("Music sources")},
        {"dsp", QT_TR_NOOP("DSP effects")},
        {"visualization", QT_TR_NOOP("Visualizations")},
        {"general", QT_TR_NOOP("General")},
    };
    for (const auto &[category, title] : Categories) {
        bool first = true;
        for (const PluginInfo &info : m_plugins->available()) {
            // Unknown categories go with general purpose plugins.
            const bool known = std::any_of(std::begin(Categories), std::end(Categories),
                                           [&](const auto &c) { return info.category == QLatin1String(c.first); });
            if (info.category != QLatin1String(category) && (known || qstrcmp(category, "general") != 0))
                continue;
            if (std::exchange(first, false))
                list->addSection(tr(title));
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
    }
    list->setToolTipsVisible(true);
    if (!m_plugins->available().isEmpty()) {
        list->addSeparator();
        list->addAction(tr("Changes apply after a restart"))->setEnabled(false);
    }
}

void MainWindow::addDspMenu(QMenu *menu)
{
    DspRack &rack = m_controller.engine().dspRack();
    const QList<DspEffect *> effects = rack.effects();
    if (effects.isEmpty())
        return;
    QMenu *dsp = menu->addMenu(tr("DSP effects"));
    for (DspEffect *effect : effects) {
        QAction *toggle = dsp->addAction(effect->name(), this, [this, &rack, effect](bool on) {
            rack.setEnabled(effect, on);
            QSettings().setValue(AppPluginHost::enabledDspKey(), rack.enabledIds());
            if (on && !m_controller.engine().effectsAvailable()
                && m_controller.engine().state() != PlaybackEngine::State::Stopped)
                m_marquee->setText(tr("Effects don't apply to this track"));
        });
        toggle->setCheckable(true);
        toggle->setChecked(rack.isEnabled(effect));
        const QList<QAction *> settings = effect->actions();
        if (!settings.isEmpty())
            dsp->addMenu(tr("%1 settings").arg(effect->name()))->addActions(settings);
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
    if (PlaybackSettings *playback = m_controller.engine().playbackSettings()) {
        menu.addSeparator();
        populatePlaybackMenu(menu.addMenu(tr("Playback")), *playback);
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
    addTitleMenu(menu.addMenu(tr("Song title")));
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
    QAction *doubleSize = scaleMenu->addAction(tr("Double size"), this, [this] {
        toggleDoubleSize();
        m_clutter->update();
    });
    doubleSize->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    doubleSize->setCheckable(true);
    doubleSize->setChecked(m_scaleBeforeDouble > 0);
    if (WindowManager *manager = windowManager())
        m_alwaysOnTop->setChecked(manager->alwaysOnTop());
    menu.addMenu(tr("Options"))->addActions(m_options);
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
            m_clutter->update();
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
        // "3. Artist - Title (4:05)", as the classic player shows it.
        QString text = track->displayTitle();
        if (m_showTrackNumber)
            text = QStringLiteral("%1. %2").arg(playlist.currentIndex() + 1).arg(text);
        qint64 length = track->durationMs;
        if (length <= 0 && m_controller.engine().state() != PlaybackEngine::State::Stopped)
            length = m_controller.engine().duration();
        if (m_showLength && length > 0)
            text += QStringLiteral(" (%1)").arg(formatTime(length));
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
