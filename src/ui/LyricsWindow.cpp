#include "ui/LyricsWindow.h"

#include "core/LocalLyrics.h"
#include "core/LyricsService.h"
#include "core/PlaybackEngine.h"
#include "core/PlayerController.h"
#include "skin/Skin.h"
#include "ui/WindowManager.h"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>

#include <iterator>

namespace newamp {

namespace {
const QString TextSizeKey = QStringLiteral("lyrics/textSize");
constexpr int Margin = 4;
constexpr int ManualScrollMs = 3000;  // the wheel stops following for this long
constexpr qint64 MaxExtrapolateMs = 1500; // between position updates

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QFont lyricsFont(const Skin &skin, int pixels)
{
    const QString family = skin.playlistStyle().fontFamily;
    QFont font(family.isEmpty() ? QStringLiteral("Helvetica") : family);
    font.setPixelSize(pixels);
    return font;
}

QString offsetText(qint64 ms)
{
    return QStringLiteral("%1%2 s").arg(ms > 0 ? QStringLiteral("+") : QString()).arg(double(ms) / 1000.0, 0, 'f', 1);
}
} // namespace

LyricsWindow::LyricsWindow(const Skin *skin, LyricsService &service, QWidget *parent)
    : GenericWindow(skin, QStringLiteral("Lyrics"), parent, DefaultSize)
    , m_service(service)
    , m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("lyrics"));
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, &LyricsWindow::tick);
    connect(this, &SkinnedWindow::visibilityChanged, this, &LyricsWindow::updateTimer);
    connect(&m_service, &LyricsService::lyricsChanged, this, &LyricsWindow::lyricsChanged);
    setTextSize(QSettings().value(TextSizeKey, TextSizes[1]).toInt());
}

void LyricsWindow::setTextSize(int pixels)
{
    m_textSize = qBound(6, pixels, 48);
    QSettings().setValue(TextSizeKey, m_textSize);
    m_layoutWidth = -1;
    m_scroll = targetScroll();
    update();
}

void LyricsWindow::setOffsetMs(qint64 offsetMs)
{
    m_offsetMs = offsetMs;
    const QString key = offsetKey();
    if (!key.isEmpty()) {
        QSettings settings;
        if (offsetMs == 0)
            settings.remove(key);
        else
            settings.setValue(key, offsetMs);
    }
    updateCurrentLine();
}

QString LyricsWindow::offsetKey() const
{
    if (!m_service.hasTrack())
        return {};
    const QByteArray id = m_service.track().location.toString().toUtf8();
    return QStringLiteral("lyrics/offsets/")
        + QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(16));
}

void LyricsWindow::setPosition(qint64 positionMs)
{
    m_positionMs = positionMs;
    m_positionClock.start();
    updateCurrentLine();
}

void LyricsWindow::setPlaying(bool playing)
{
    m_playing = playing;
    m_positionClock.start();
    updateTimer();
}

void LyricsWindow::lyricsChanged()
{
    m_offsetMs = offsetKey().isEmpty() ? 0 : QSettings().value(offsetKey(), 0).toLongLong();
    m_layoutWidth = -1;
    m_current = -1;
    m_manualScroll.invalidate();
    m_scroll = 0;
    updateCurrentLine();
    m_scroll = targetScroll();
    updateTimer();
    update();
}

void LyricsWindow::updateCurrentLine()
{
    qint64 position = m_positionMs;
    if (m_playing && m_positionClock.isValid())
        position += qMin(m_positionClock.elapsed(), MaxExtrapolateMs);
    const int line = m_service.lyrics().lineAt(position + m_offsetMs);
    if (line != m_current) {
        m_current = line;
        update();
    }
}

void LyricsWindow::updateTimer()
{
    // Runs while there is something to follow or a scroll to finish.
    const bool following = m_playing && m_service.lyrics().synced;
    const bool scrolling = qAbs(targetScroll() - m_scroll) > 0.5;
    if (isVisible() && (following || scrolling))
        m_timer->start();
    else
        m_timer->stop();
}

void LyricsWindow::tick()
{
    updateCurrentLine();
    const qreal target = targetScroll();
    if (qAbs(target - m_scroll) > 0.5) {
        m_scroll += (target - m_scroll) * 0.25;
        if (qAbs(target - m_scroll) <= 0.5)
            m_scroll = target;
        updateSkinRect(contentRect());
    }
    updateTimer();
}

void LyricsWindow::ensureLayout(int width) const
{
    const QFont font = lyricsFont(skin(), m_textSize);
    if (width == m_layoutWidth && font.family() == m_layoutFamily)
        return;
    m_layoutWidth = width;
    m_layoutFamily = font.family();
    m_boxes.clear();
    const QFontMetrics metrics(font);
    const int gap = m_service.lyrics().synced ? metrics.height() / 2 : 0;
    int y = 0;
    for (const LyricLine &line : m_service.lyrics().lines) {
        const QString text = line.text.isEmpty() ? QStringLiteral(" ") : line.text;
        const int height = metrics.boundingRect(QRect(0, 0, qMax(1, width), 100000),
                                                Qt::AlignHCenter | Qt::TextWordWrap, text).height();
        m_boxes.append({y, height});
        y += height + gap;
    }
    m_layoutHeight = y;
}

qreal LyricsWindow::targetScroll() const
{
    const Lyrics &lyrics = m_service.lyrics();
    if (lyrics.isEmpty())
        return 0;
    ensureLayout(contentRect().width() - 2 * Margin);
    if (!lyrics.synced)
        return qBound<qreal>(0, m_scroll, maxScroll());
    if (m_manualScroll.isValid() && m_manualScroll.elapsed() < ManualScrollMs)
        return m_scroll;
    // The line being sung sits in the middle; before the first, the first
    // line waits just below it.
    if (m_current < 0 || m_current >= m_boxes.size())
        return m_boxes.isEmpty() ? 0 : m_boxes.first().top - m_textSize;
    const LineBox &box = m_boxes.at(m_current);
    return box.top + box.height / 2.0;
}

qreal LyricsWindow::maxScroll() const
{
    return qMax<qreal>(0, m_layoutHeight - (contentRect().height() - 2 * Margin));
}

int LyricsWindow::lineTop(int index) const
{
    const QRect content = contentRect();
    const int top = m_service.lyrics().synced ? content.top() + content.height() / 2 : content.top() + Margin;
    return top + m_boxes.at(index).top - qRound(m_scroll);
}

int LyricsWindow::lineAtY(int y) const
{
    ensureLayout(contentRect().width() - 2 * Margin);
    for (int i = 0; i < m_boxes.size(); ++i) {
        const int top = lineTop(i);
        if (y >= top && y < top + m_boxes.at(i).height)
            return i;
    }
    return -1;
}

QString LyricsWindow::statusText() const
{
    switch (m_service.state()) {
    case LyricsService::State::Idle: return tr("Nothing playing");
    case LyricsService::State::Searching: return tr("Looking for lyrics...");
    case LyricsService::State::NotFound: return tr("No lyrics found.\nRight-click to open a .lrc file.");
    case LyricsService::State::Found: break;
    }
    return {};
}

void LyricsWindow::paintContent(QPainter &painter, const Skin &skin, const QRect &content)
{
    const PlaylistStyle &style = skin.playlistStyle();
    painter.fillRect(content, style.normalBackground);

    const Lyrics &lyrics = m_service.lyrics();
    if (lyrics.isEmpty()) {
        painter.setFont(lyricsFont(skin, 9));
        painter.setPen(style.normal);
        painter.drawText(content.adjusted(Margin, Margin, -Margin, -Margin), Qt::AlignCenter | Qt::TextWordWrap,
                         statusText());
        return;
    }

    ensureLayout(content.width() - 2 * Margin);
    painter.setFont(lyricsFont(skin, m_textSize));
    const QColor past = mix(style.normal, style.normalBackground, 0.45);
    for (int i = 0; i < lyrics.lines.size(); ++i) {
        const int top = lineTop(i);
        const LineBox &box = m_boxes.at(i);
        if (top + box.height < content.top() || top > content.bottom())
            continue;
        const LyricLine &line = lyrics.lines.at(i);
        QColor colour = style.normal;
        if (lyrics.synced && i == m_current)
            colour = style.current;
        else if (lyrics.synced && i < m_current)
            colour = past;
        painter.setPen(colour);
        // An empty synced line is an instrumental break.
        const QString text = line.text.isEmpty() && lyrics.synced ? QStringLiteral("♪") : line.text;
        painter.drawText(QRect(content.left() + Margin, top, content.width() - 2 * Margin, box.height),
                         Qt::AlignHCenter | Qt::TextWordWrap, text);
    }

    // Where the lyrics came from, small in the corner.
    if (!lyrics.source.isEmpty()) {
        const QFont small = lyricsFont(skin, 7);
        painter.setFont(small);
        const QFontMetrics metrics(small);
        QString label = lyrics.source;
        if (m_offsetMs != 0)
            label += QStringLiteral(" ") + offsetText(m_offsetMs);
        const QRect box(content.right() - metrics.horizontalAdvance(label) - 3, content.bottom() - metrics.height(),
                        metrics.horizontalAdvance(label) + 3, metrics.height() + 1);
        painter.fillRect(box, style.normalBackground);
        painter.setPen(past);
        painter.drawText(box, Qt::AlignCenter, label);
    }
}

void LyricsWindow::layoutContent(const QRect &content)
{
    Q_UNUSED(content)
    m_layoutWidth = -1;
    m_scroll = targetScroll();
}

void LyricsWindow::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = toSkin(event->position());
    if (event->button() == Qt::LeftButton && contentRect().contains(pos) && m_service.lyrics().synced) {
        // Jump to the clicked line.
        const int line = lineAtY(pos.y());
        PlayerController *controller = windowManager() ? windowManager()->controller() : nullptr;
        if (line >= 0 && controller) {
            m_manualScroll.invalidate();
            controller->engine().seek(qMax<qint64>(0, m_service.lyrics().lines.at(line).startMs - m_offsetMs));
        }
        event->accept();
        return;
    }
    GenericWindow::mousePressEvent(event);
}

void LyricsWindow::wheelEvent(QWheelEvent *event)
{
    if (m_service.lyrics().isEmpty()) {
        GenericWindow::wheelEvent(event);
        return;
    }
    m_scroll -= event->angleDelta().y() / 120.0 * m_textSize * 3;
    if (m_service.lyrics().synced) {
        m_manualScroll.start();
        ensureLayout(contentRect().width() - 2 * Margin);
        m_scroll = qBound<qreal>(0, m_scroll, m_layoutHeight);
        // Follow again once the pause is over.
        QTimer::singleShot(ManualScrollMs + 50, this, &LyricsWindow::updateTimer);
    } else {
        m_scroll = qBound<qreal>(0, m_scroll, maxScroll());
    }
    updateSkinRect(contentRect());
    event->accept();
}

void LyricsWindow::openLyricsFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Lyrics"), QString(),
                                                      tr("Lyrics (*.lrc *.txt);;All files (*)"));
    if (!path.isEmpty())
        m_service.setLyrics(SidecarLyrics::readTextFile(path), QFileInfo(path).fileName());
}

void LyricsWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    const Lyrics &lyrics = m_service.lyrics();

    QMenu *size = menu.addMenu(tr("Text size"));
    const QStringList names{tr("Small"), tr("Medium"), tr("Large")};
    for (int i = 0; i < int(std::size(TextSizes)); ++i) {
        const int pixels = TextSizes[i];
        QAction *action = size->addAction(names.at(i), this, [this, pixels] { setTextSize(pixels); });
        action->setCheckable(true);
        action->setChecked(m_textSize == pixels);
    }

    if (lyrics.synced) {
        menu.addSeparator();
        menu.addAction(tr("Show lyrics earlier (%1)").arg(offsetText(OffsetStepMs)), this,
                       [this] { setOffsetMs(m_offsetMs + OffsetStepMs); });
        menu.addAction(tr("Show lyrics later (%1)").arg(offsetText(-OffsetStepMs)), this,
                       [this] { setOffsetMs(m_offsetMs - OffsetStepMs); });
        QAction *reset = menu.addAction(tr("Reset timing"), this, [this] { setOffsetMs(0); });
        reset->setEnabled(m_offsetMs != 0);
    }

    menu.addSeparator();
    QAction *open = menu.addAction(tr("Open lyrics file..."), this, &LyricsWindow::openLyricsFile);
    open->setEnabled(m_service.hasTrack());
    QAction *copy = menu.addAction(tr("Copy lyrics"), this,
                                   [this] { QGuiApplication::clipboard()->setText(m_service.lyrics().plainText()); });
    copy->setEnabled(!lyrics.isEmpty());
    menu.exec(event->globalPos());
}

} // namespace newamp
