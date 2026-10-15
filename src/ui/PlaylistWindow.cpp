#include "ui/PlaylistWindow.h"

#include "core/LocalFileSource.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "skin/Skin.h"
#include "ui/SkinElements.h"
#include "ui/SkinListView.h"

#include <QCursor>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QStandardPaths>

namespace newamp {

using namespace sprites;

namespace {

QString formatDuration(qint64 ms)
{
    if (ms < 0)
        return {};
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

PlaylistWindow::PlaylistWindow(const Skin *skin, PlayerController &controller, QWidget *parent)
    : SkinnedWindow(skin, PlMinimumSize, parent, Qt::Tool)
    , m_controller(controller)
{
    setObjectName(QStringLiteral("playlist"));
    setWindowTitle(tr("Playlist"));
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);

    m_view = addElement<SkinListView>([this] { return m_controller.playlist().count(); },
                                      [this](int row) -> SkinListView::Row {
                                          const Playlist &playlist = m_controller.playlist();
                                          const Track &track = playlist.at(row);
                                          return {QStringLiteral("%1. %2").arg(row + 1).arg(track.displayTitle()),
                                                  formatDuration(track.durationMs),
                                                  row == playlist.currentIndex()};
                                      });
    m_view->onScrolled = [this] { updateScrollBar(); };
    m_view->onActivated = [this](int row) { m_controller.playIndex(row); };

    m_scrollBar = addElement<SkinSlider>(QRect(), Sheet::PlEdit, SkinSlider::BackgroundFn(), PlScrollHandle,
                                         Qt::Vertical);
    m_scrollBar->onMoved = [this](double v) { m_view->setFirstRow(qRound(v * m_view->maxFirstRow())); };
    m_scrollBar->onReleased = m_scrollBar->onMoved;

    // The close button's normal look is part of the top-right corner art.
    const QRect closeNormal(PlTopRight.active.topLeft() + QPoint(14, 3), QSize(9, 9));
    m_close = addElement<SkinButton>(QPoint(), Sheet::PlEdit, Sprite{closeNormal, PlClosePressed},
                                     [this] { hide(); });

    Playlist &playlist = m_controller.playlist();
    m_menus[0] = addElement<SkinHotspot>(QRect(), [this] {
        showMenu({{tr("Add files..."), [this] { addFiles(); }}, {tr("Add folder..."), [this] { addFolder(); }}});
    });
    m_menus[1] = addElement<SkinHotspot>(QRect(), [this, &playlist] {
        showMenu({{tr("Remove selected"), [this, &playlist] { playlist.remove(m_view->selectedRows()); }},
                  {tr("Remove all"), [&playlist] { playlist.clear(); }}});
    });
    m_menus[2] = addElement<SkinHotspot>(QRect(), [this] {
        showMenu({{tr("Select all"), [this] { m_view->selectAll(); }},
                  {tr("Select none"), [this] { m_view->selectNone(); }}});
    });
    m_menus[3] = addElement<SkinHotspot>(QRect(), [this, &playlist] {
        showMenu({{tr("Sort by title"),
                   [&playlist] {
                       playlist.sortBy([](const Track &a, const Track &b) {
                           return a.displayTitle().compare(b.displayTitle(), Qt::CaseInsensitive) < 0;
                       });
                   }},
                  {tr("Sort by file name"), [&playlist] {
                       playlist.sortBy([](const Track &a, const Track &b) {
                           return a.location.fileName().compare(b.location.fileName(), Qt::CaseInsensitive) < 0;
                       });
                   }}});
    });
    m_menus[4] = addElement<SkinHotspot>(QRect(), [this, &playlist] {
        showMenu({{tr("Clear playlist"), [&playlist] { playlist.clear(); }}});
    });
    m_info = addElement<SkinText>(QRect());

    connect(&playlist, &Playlist::tracksChanged, this, [this] {
        m_view->refresh();
        updateScrollBar();
        updateInfo();
    });
    connect(&playlist, &Playlist::currentIndexChanged, this, [this](int index) {
        if (index >= 0)
            m_view->ensureVisible(index);
        update();
    });

    layoutElements();
    updateInfo();
}

QSize PlaylistWindow::constrainSize(const QSize &size) const
{
    // Grow in whole tiles from the minimum size.
    const int stepsW = qMax(0, (size.width() - PlMinimumSize.width() + PlSizeStep.width() / 2) / PlSizeStep.width());
    const int stepsH =
        qMax(0, (size.height() - PlMinimumSize.height() + PlSizeStep.height() / 2) / PlSizeStep.height());
    return {PlMinimumSize.width() + stepsW * PlSizeStep.width(),
            PlMinimumSize.height() + stepsH * PlSizeStep.height()};
}

void PlaylistWindow::layoutElements()
{
    if (!m_view)
        return; // called from the base constructor
    const int w = skinSize().width();
    const int h = skinSize().height();
    m_view->setRect(QRect(PlLeftWidth, PlTopHeight, w - PlLeftWidth - PlRightWidth, h - PlTopHeight - PlBottomHeight));
    m_scrollBar->setRect(QRect(w - 15, PlTopHeight, PlScrollHandle.normal.width(), h - PlTopHeight - PlBottomHeight));
    m_close->setRect(QRect(QPoint(w - 11, 3), QSize(9, 9)));
    for (int i = 0; i < 4; ++i)
        m_menus[i]->setRect(QRect(14 + i * 29, h - 30, 22, 18));
    m_menus[4]->setRect(QRect(w - 44, h - 30, 22, 18));
    m_info->setRect(QRect(w - 140, h - 24, 85, 6));
    m_view->refresh();
    updateScrollBar();
}

void PlaylistWindow::paintBackground(QPainter &painter, const Skin &skin)
{
    const bool active = isActiveWindow();
    const auto pick = [active](const ActiveSprite &s) { return active ? s.active : s.inactive; };
    const int w = skinSize().width();
    const int h = skinSize().height();

    // Top: corners, tiles, and the title centred over them.
    skin.drawSprite(painter, {0, 0}, Sheet::PlEdit, pick(PlTopLeft));
    for (int x = PlTopLeft.active.width(); x < w - PlTopRight.active.width(); x += PlTopTile.active.width())
        skin.drawSprite(painter, {x, 0}, Sheet::PlEdit, pick(PlTopTile));
    skin.drawSprite(painter, {(w - PlTitle.active.width()) / 2, 0}, Sheet::PlEdit, pick(PlTitle));
    skin.drawSprite(painter, {w - PlTopRight.active.width(), 0}, Sheet::PlEdit, pick(PlTopRight));

    // Sides, clipped so the last tile doesn't spill into the bottom bar.
    painter.save();
    painter.setClipRect(QRect(0, PlTopHeight, w, h - PlTopHeight - PlBottomHeight));
    for (int y = PlTopHeight; y < h - PlBottomHeight; y += PlLeftTile.height()) {
        skin.drawSprite(painter, {0, y}, Sheet::PlEdit, PlLeftTile);
        skin.drawSprite(painter, {w - PlRightWidth, y}, Sheet::PlEdit, PlRightTile);
    }
    painter.restore();

    // Bottom: left and right corner pieces with tiles between.
    const int bottom = h - PlBottomHeight;
    for (int x = PlBottomLeft.width(); x < w - PlBottomRight.width(); x += PlBottomTile.width())
        skin.drawSprite(painter, {x, bottom}, Sheet::PlEdit, PlBottomTile);
    skin.drawSprite(painter, {0, bottom}, Sheet::PlEdit, PlBottomLeft);
    skin.drawSprite(painter, {w - PlBottomRight.width(), bottom}, Sheet::PlEdit, PlBottomRight);
}

bool PlaylistWindow::isDragArea(const QPoint &skinPos) const
{
    return skinPos.y() < PlTopHeight || skinPos.y() >= skinSize().height() - PlBottomHeight;
}

bool PlaylistWindow::isResizeArea(const QPoint &skinPos) const
{
    return skinPos.x() >= skinSize().width() - 20 && skinPos.y() >= skinSize().height() - 20;
}

void PlaylistWindow::updateScrollBar()
{
    const int max = m_view->maxFirstRow();
    m_scrollBar->setVisible(max > 0);
    m_scrollBar->setValue(max > 0 ? double(m_view->firstRow()) / max : 0.0);
}

void PlaylistWindow::updateInfo()
{
    qint64 total = 0;
    bool complete = true;
    for (const Track &track : m_controller.playlist().tracks()) {
        if (track.durationMs >= 0)
            total += track.durationMs;
        else
            complete = false;
    }
    const qint64 s = total / 1000;
    QString text = QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    if (!complete)
        text += QLatin1Char('+');
    m_info->setText(QStringLiteral("%1 TRACKS %2").arg(m_controller.playlist().count()).arg(text));
}

void PlaylistWindow::showMenu(const QList<QPair<QString, std::function<void()>>> &items)
{
    QMenu menu(this);
    for (const auto &item : items)
        menu.addAction(item.first, this, item.second);
    menu.exec(QCursor::pos());
}

void PlaylistWindow::addFiles()
{
    const QString filter = tr("Audio files (%1)").arg(LocalFileSource::fileFilters().join(QLatin1Char(' ')));
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add files"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation), filter);
    m_controller.playlist().append(LocalFileSource::tracksForPaths(files));
}

void PlaylistWindow::addFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add folder"));
    if (!dir.isEmpty())
        m_controller.playlist().append(LocalFileSource::tracksForPaths({dir}));
}

void PlaylistWindow::keyPressEvent(QKeyEvent *event)
{
    Playlist &playlist = m_controller.playlist();
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: playlist.remove(m_view->selectedRows()); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!playlist.isEmpty())
            m_controller.playIndex(m_view->anchor());
        break;
    case Qt::Key_Up: m_view->moveCursor(-1); break;
    case Qt::Key_Down: m_view->moveCursor(1); break;
    case Qt::Key_PageUp: m_view->moveCursor(-m_view->visibleRows()); break;
    case Qt::Key_PageDown: m_view->moveCursor(m_view->visibleRows()); break;
    case Qt::Key_A:
        if (event->modifiers() & Qt::ControlModifier) {
            m_view->selectAll();
            break;
        }
        Q_FALLTHROUGH();
    default: SkinnedWindow::keyPressEvent(event); return;
    }
    event->accept();
}

void PlaylistWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void PlaylistWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    m_controller.playlist().append(LocalFileSource::tracksForPaths(paths));
    event->acceptProposedAction();
}

} // namespace newamp
