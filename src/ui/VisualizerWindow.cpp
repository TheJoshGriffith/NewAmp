#include "ui/VisualizerWindow.h"

#include "skin/Skin.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>

namespace newamp {

namespace {
const QString EffectKey = QStringLiteral("visualizer/effect");
const QString RandomKey = QStringLiteral("visualizer/randomOnTrackChange");
const QString CycleKey = QStringLiteral("visualizer/autoCycleSeconds");
constexpr int CycleOptions[] = {10, 20, 30, 60};
constexpr int NameShownMs = 2000; // the effect's name shows briefly after a change
} // namespace

VisualizerWindow::VisualizerWindow(const Skin *skin, SampleSource source, QWidget *parent)
    : GenericWindow(skin, QStringLiteral("Visualization"), parent, DefaultSize)
    , m_source(std::move(source))
    , m_effects(createVisEffects())
    , m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("visualizer"));
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, &VisualizerWindow::tick);
    connect(this, &SkinnedWindow::visibilityChanged, this, &VisualizerWindow::updateTimer);

    QSettings settings;
    const QString saved = settings.value(EffectKey).toString();
    for (int i = 0; i < effectCount(); ++i)
        if (effectName(i) == saved)
            m_current = i;
    m_randomOnTrack = settings.value(RandomKey, false).toBool();
    m_cycleSeconds = qMax(0, settings.value(CycleKey, 0).toInt());
}

VisualizerWindow::~VisualizerWindow() = default;

QString VisualizerWindow::effectName(int index) const
{
    return index >= 0 && index < effectCount() ? m_effects[size_t(index)]->name() : QString();
}

void VisualizerWindow::setEffect(int index)
{
    if (index < 0 || index >= effectCount())
        return;
    m_current = index;
    m_sinceChange = 0;
    // The new effect starts from black; draw it at once so it shows even
    // while paused.
    const bool shown = !m_image.isNull();
    m_image = QImage();
    if (shown)
        render(m_analyzer.frame());
    QSettings().setValue(EffectKey, effectName(index));
    // Show the name for a moment, even while paused.
    m_nameClock.start();
    QTimer::singleShot(NameShownMs + 50, this, [this] { update(); });
    update();
}

void VisualizerWindow::nextEffect()
{
    setEffect((m_current + 1) % effectCount());
}

void VisualizerWindow::setRandomOnTrackChange(bool on)
{
    m_randomOnTrack = on;
    QSettings().setValue(RandomKey, on);
}

void VisualizerWindow::setAutoCycleSeconds(int seconds)
{
    m_cycleSeconds = qMax(0, seconds);
    m_sinceChange = 0;
    QSettings().setValue(CycleKey, m_cycleSeconds);
}

void VisualizerWindow::setPlaying(bool playing)
{
    m_playing = playing;
    updateTimer();
}

void VisualizerWindow::trackChanged()
{
    if (!m_randomOnTrack || effectCount() < 2)
        return;
    // Any effect but the current one.
    const int pick = int(QRandomGenerator::global()->bounded(effectCount() - 1));
    setEffect(pick >= m_current ? pick + 1 : pick);
}

void VisualizerWindow::updateTimer()
{
    if (m_playing && isVisible()) {
        if (!m_timer->isActive()) {
            m_frameClock.start();
            m_timer->start();
        }
    } else {
        m_timer->stop();
    }
}

void VisualizerWindow::tick()
{
    if (isMinimized())
        return;
    const float dt = m_frameClock.isValid() ? float(m_frameClock.restart()) / 1000.0f : 1.0f / 30.0f;
    float samples[VisFrame::WaveSize];
    const int got = m_source ? m_source(samples, VisFrame::WaveSize) : 0;
    const VisFrame &frame = m_analyzer.process(got == VisFrame::WaveSize ? samples : nullptr, dt);

    m_sinceChange += frame.dt;
    if (m_cycleSeconds > 0 && m_sinceChange >= float(m_cycleSeconds))
        nextEffect();
    render(frame);
}

void VisualizerWindow::render(const VisFrame &frame)
{
    VisEffect &effect = *m_effects[size_t(m_current)];
    const QSize size = effect.frameSize(contentRect().size());
    if (m_image.size() != size) {
        m_image = QImage(size, QImage::Format_RGB32);
        m_image.fill(Qt::black);
    }
    VisFrame withPalette = frame;
    withPalette.palette = &skin().visColors();
    effect.render(m_image, withPalette);
    updateSkinRect(contentRect());
}

void VisualizerWindow::paintContent(QPainter &painter, const Skin &skin, const QRect &content)
{
    if (m_image.isNull()) {
        painter.fillRect(content, Qt::black);
    } else {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, m_effects[size_t(m_current)]->smoothScaling());
        painter.drawImage(content, m_image);
    }
    if (m_nameClock.isValid() && m_nameClock.elapsed() < NameShownMs)
        skin.drawText(painter, content.topLeft() + QPoint(3, 3), effectName(m_current).toUpper(), content.width() - 6);
}

bool VisualizerWindow::inContent(const QPointF &pos) const
{
    return contentRect().contains(QPoint(int(pos.x() / scale()), int(pos.y() / scale())));
}

void VisualizerWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && inContent(event->position())) {
        nextEffect();
        event->accept();
        return;
    }
    GenericWindow::mousePressEvent(event);
}

void VisualizerWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    // A quick second click is a double click; it still moves on.
    if (event->button() == Qt::LeftButton && inContent(event->position())) {
        nextEffect();
        event->accept();
        return;
    }
    GenericWindow::mouseDoubleClickEvent(event);
}

void VisualizerWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    for (int i = 0; i < effectCount(); ++i) {
        QAction *action = menu.addAction(effectName(i), this, [this, i] { setEffect(i); });
        action->setCheckable(true);
        action->setChecked(i == m_current);
    }
    menu.addSeparator();
    menu.addAction(tr("Next effect"), this, &VisualizerWindow::nextEffect);
    QAction *random = menu.addAction(tr("Random on track change"), this,
                                     [this](bool on) { setRandomOnTrackChange(on); });
    random->setCheckable(true);
    random->setChecked(m_randomOnTrack);
    QMenu *cycle = menu.addMenu(tr("Auto-cycle"));
    QAction *off = cycle->addAction(tr("Off"), this, [this] { setAutoCycleSeconds(0); });
    off->setCheckable(true);
    off->setChecked(m_cycleSeconds == 0);
    for (const int seconds : CycleOptions) {
        QAction *action = cycle->addAction(tr("Every %1 seconds").arg(seconds), this,
                                           [this, seconds] { setAutoCycleSeconds(seconds); });
        action->setCheckable(true);
        action->setChecked(m_cycleSeconds == seconds);
    }
    menu.exec(event->globalPos());
}

} // namespace newamp
