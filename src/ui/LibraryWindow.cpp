#include "ui/LibraryWindow.h"

#include "plugins/PluginHost.h"
#include "skin/Skin.h"
#include "ui/SkinElements.h"
#include "ui/SkinListView.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPointer>

#include <memory>

namespace newamp {

using namespace sprites;

namespace {

constexpr int BarHeight = 13;
constexpr int ScrollBarWidth = 8;
constexpr int ButtonWidth = 30;

QFont listFont(const Skin &skin)
{
    const QString family = skin.playlistStyle().fontFamily;
    QFont font(family.isEmpty() ? QStringLiteral("Helvetica") : family);
    font.setPixelSize(9);
    return font;
}

// A flat text button in the playlist colours.
void drawButton(QPainter &painter, const QRect &rect, const QString &text, const PlaylistStyle &style)
{
    QColor edge = style.normal;
    edge.setAlphaF(0.5);
    painter.fillRect(rect, style.selectedBackground);
    painter.setPen(edge);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));
    painter.setPen(style.normal);
    painter.drawText(rect, Qt::AlignCenter, text);
}

} // namespace

LibraryWindow::LibraryWindow(const Skin *skin, PluginHost &host, QWidget *parent)
    : GenericWindow(skin, tr("Media Library"), parent, GenMinimumSize + QSize(4 * GenSizeStep.width(), 4 * GenSizeStep.height()))
    , m_host(host)
{
    setObjectName(QStringLiteral("library"));
    setSkinSize(skinSize());

    m_list = addElement<SkinListView>([this] { return int(m_visible.size()); },
                                      [this](int row) -> SkinListView::Row {
                                          const LibraryItem &item = itemAt(row);
                                          // Folders get a trailing slash, like a path.
                                          return {item.isFolder ? item.title + QStringLiteral(" /") : item.title,
                                                  item.detail, false};
                                      });
    m_list->onActivated = [this](int row) { open(row); };
    m_list->onScrolled = [this] { updateScrollBar(); };

    m_scrollBar = addElement<SkinSlider>(QRect(), Sheet::PlEdit, SkinSlider::BackgroundFn(), PlScrollHandle,
                                         Qt::Vertical);
    m_scrollBar->onMoved = [this](double v) { m_list->setFirstRow(qRound(v * m_list->maxFirstRow())); };
    m_scrollBar->onReleased = m_scrollBar->onMoved;

    m_backButton = addElement<SkinHotspot>(QRect(), [this] { back(); });
    m_playButton = addElement<SkinHotspot>(QRect(), [this] { useSelection(true); });
    m_addButton = addElement<SkinHotspot>(QRect(), [this] { useSelection(false); });

    layoutElements();
    goTo({});
}

void LibraryWindow::addLibrary(LibraryProvider *library)
{
    if (!library || m_libraries.contains(library))
        return;
    m_libraries.append(library);
    // With one library there's no list of libraries to choose from; with
    // more, the top level becomes that list.
    if (m_path.size() <= 1)
        goTo({});
}

void LibraryWindow::reload(LibraryProvider *library)
{
    if (!m_path.isEmpty() && m_path.first().library == library)
        goTo({m_path.first()});
    else
        update(); // its status may show in the list of libraries
}

void LibraryWindow::showLibrary(LibraryProvider *library)
{
    if (m_path.isEmpty() || m_path.first().library != library)
        goTo({Level{library, {}, library->libraryName()}});
    show();
    raise();
    activateWindow();
}

void LibraryWindow::goTo(const QList<Level> &path)
{
    m_path = path;
    if (m_path.isEmpty() && m_libraries.size() == 1)
        m_path = {Level{m_libraries.first(), {}, m_libraries.first()->libraryName()}};
    m_path.detach();
    if (!m_path.isEmpty()) {
        m_path.first().firstRow = 0;
        m_path.first().anchor = 0;
    }
    m_filter.clear();
    load();
}

void LibraryWindow::load()
{
    const int generation = ++m_generation;
    // m_visible indexes m_items, so they're cleared together: rows must not
    // point into an empty list while the folder loads.
    m_items.clear();
    m_visible.clear();
    m_message.clear();
    m_list->reset();

    if (m_path.isEmpty()) {
        // The top level lists the libraries themselves.
        m_loading = false;
        for (int i = 0; i < m_libraries.size(); ++i)
            m_items.append({QString::number(i), m_libraries[i]->libraryName(), {}, true, {}});
        applyFilter();
        return;
    }

    m_loading = true;
    const Level level = m_path.last();
    QPointer<LibraryWindow> self(this);
    level.library->listItems(level.folderId, [self, generation, level](const QList<LibraryItem> &items) {
        if (!self || generation != self->m_generation)
            return;
        self->m_loading = false;
        self->m_items = items;
        self->applyFilter();
        self->m_list->select(qMin(level.anchor, int(self->m_visible.size()) - 1));
        self->m_list->setFirstRow(level.firstRow);
    });
    update();
}

void LibraryWindow::open(int row)
{
    if (row < 0 || row >= m_visible.size())
        return;
    const LibraryItem item = itemAt(row);
    if (!item.isFolder) {
        m_host.playTracks({item.track});
        return;
    }
    if (!m_path.isEmpty()) {
        m_path.last().firstRow = m_list->firstRow();
        m_path.last().anchor = m_list->anchor();
    }
    Level level{m_path.isEmpty() ? m_libraries.value(item.id.toInt()) : m_path.last().library,
                m_path.isEmpty() ? QString() : item.id, item.title};
    if (!level.library)
        return;
    m_path.append(level);
    m_filter.clear();
    load();
}

void LibraryWindow::back()
{
    if (!m_filter.isEmpty()) {
        setFilter({});
        return;
    }
    // A single library is the top level; there's nothing above it to show.
    const int top = m_libraries.size() == 1 ? 1 : 0;
    if (m_path.size() <= top)
        return;
    m_path.removeLast();
    load();
}

void LibraryWindow::useSelection(bool play)
{
    QList<int> rows = m_list->selectedRows();
    if (rows.isEmpty() && m_list->rowCount() > 0 && !m_path.isEmpty() && !m_items.isEmpty() && !m_items.first().isFolder)
        rows = {m_list->anchor()}; // a track list with nothing selected: use the cursor
    if (rows.isEmpty() || m_path.isEmpty())
        return;

    // Collect each item's tracks (folders need a request) and hand them all
    // over in order once every part has arrived.
    struct Pending
    {
        QList<QList<Track>> parts;
        int remaining = 0;
    };
    auto pending = std::make_shared<Pending>();
    pending->parts.resize(rows.size());
    pending->remaining = int(rows.size());
    LibraryProvider *library = m_path.last().library;
    QPointer<LibraryWindow> self(this);
    for (int i = 0; i < rows.size(); ++i) {
        const LibraryItem item = itemAt(rows[i]);
        auto deliver = [self, pending, i, play](const QList<Track> &tracks) {
            pending->parts[i] = tracks;
            if (--pending->remaining > 0 || !self)
                return;
            QList<Track> all;
            for (const QList<Track> &part : std::as_const(pending->parts))
                all += part;
            if (all.isEmpty())
                return;
            if (play)
                self->m_host.playTracks(all);
            else
                self->m_host.addTracks(all);
            self->m_message = tr("%n track(s) added", nullptr, int(all.size()));
            self->update();
        };
        if (item.isFolder)
            library->folderTracks(item.id, deliver);
        else
            deliver({item.track});
    }
}

void LibraryWindow::setFilter(const QString &filter)
{
    m_filter = filter;
    applyFilter();
    m_list->reset();
    if (m_list->rowCount() > 0)
        m_list->select(0);
}

void LibraryWindow::applyFilter()
{
    m_visible.clear();
    for (int i = 0; i < m_items.size(); ++i)
        if (m_filter.isEmpty() || m_items[i].title.contains(m_filter, Qt::CaseInsensitive))
            m_visible.append(i);
    m_list->refresh();
    updateScrollBar();
    update();
}

void LibraryWindow::updateScrollBar()
{
    const int max = m_list->maxFirstRow();
    m_scrollBar->setVisible(max > 0);
    m_scrollBar->setValue(max > 0 ? double(m_list->firstRow()) / max : 0.0);
}

QString LibraryWindow::pathText() const
{
    QStringList parts;
    if (m_libraries.size() != 1)
        parts << tr("Library");
    for (const Level &level : m_path)
        parts << level.title;
    QString text = parts.join(QStringLiteral(" / "));
    if (!m_filter.isEmpty())
        text += QStringLiteral("   [%1]").arg(m_filter);
    return text;
}

QString LibraryWindow::statusText() const
{
    if (m_libraries.isEmpty())
        return tr("No libraries. Turn on Plex under Plugins.");
    if (!m_message.isEmpty())
        return m_message;
    const QString status = m_path.isEmpty() ? QString() : m_path.first().library->status();
    if (m_loading)
        return status.isEmpty() ? tr("Loading...") : status;
    if (!status.isEmpty() && m_items.isEmpty())
        return status;
    return tr("%n item(s)", nullptr, int(m_visible.size()));
}

void LibraryWindow::paintContent(QPainter &painter, const Skin &skin, const QRect &content)
{
    const PlaylistStyle &style = skin.playlistStyle();
    painter.fillRect(content, style.normalBackground);
    painter.setFont(listFont(skin));
    const QFontMetrics metrics(painter.font());

    // Path bar with a back arrow.
    QColor line = style.normal;
    line.setAlphaF(0.35);
    painter.fillRect(QRect(m_pathBar.left(), m_pathBar.bottom(), m_pathBar.width(), 1), line);
    const bool canGoBack = !m_filter.isEmpty() || m_path.size() > (m_libraries.size() == 1 ? 1 : 0);
    painter.setPen(canGoBack ? style.current : line);
    painter.drawText(m_backButton->rect(), Qt::AlignCenter, QStringLiteral("<"));
    painter.setPen(style.current);
    const QRect path = m_pathBar.adjusted(m_backButton->rect().width() + 2, 0, -2, -1);
    painter.drawText(path, Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(pathText(), Qt::ElideLeft, path.width()));

    // Scroll groove, and the bottom bar with its buttons and status.
    painter.fillRect(m_scrollBar->rect().adjusted(3, 0, -3, 0), line);
    painter.fillRect(QRect(m_bottomBar.left(), m_bottomBar.top(), m_bottomBar.width(), 1), line);
    drawButton(painter, m_playButton->rect(), tr("Play"), style);
    drawButton(painter, m_addButton->rect(), tr("Add"), style);
    painter.setPen(style.normal);
    const QRect status = m_bottomBar.adjusted(m_addButton->rect().right() - m_bottomBar.left() + 6, 1, -2, 0);
    painter.drawText(status, Qt::AlignRight | Qt::AlignVCenter,
                     metrics.elidedText(statusText(), Qt::ElideRight, status.width()));

    if (m_visible.isEmpty() && (m_loading || !m_filter.isEmpty())) {
        painter.setPen(line);
        painter.drawText(m_list->rect(), Qt::AlignCenter, m_loading ? tr("Loading...") : tr("No matches"));
    }
}

void LibraryWindow::layoutContent(const QRect &content)
{
    if (!m_list)
        return; // called from the base constructor
    m_pathBar = QRect(content.left(), content.top(), content.width(), BarHeight);
    m_bottomBar = QRect(content.left(), content.bottom() - BarHeight + 1, content.width(), BarHeight);
    m_backButton->setRect(QRect(m_pathBar.topLeft(), QSize(BarHeight, BarHeight - 1)));
    const QRect body(content.left(), m_pathBar.bottom() + 2, content.width(),
                     m_bottomBar.top() - m_pathBar.bottom() - 3);
    m_list->setRect(body.adjusted(0, 0, -ScrollBarWidth - 1, 0));
    m_scrollBar->setRect(QRect(body.right() - ScrollBarWidth + 1, body.top(), ScrollBarWidth, body.height()));
    m_playButton->setRect(QRect(m_bottomBar.left() + 2, m_bottomBar.top() + 2, ButtonWidth, BarHeight - 3));
    m_addButton->setRect(m_playButton->rect().translated(ButtonWidth + 3, 0));
    m_list->refresh();
    updateScrollBar();
}

void LibraryWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: open(m_list->anchor()); break;
    case Qt::Key_Backspace:
        if (!m_filter.isEmpty())
            setFilter(m_filter.chopped(1));
        else
            back();
        break;
    case Qt::Key_Escape: setFilter({}); break;
    case Qt::Key_Left: back(); break;
    case Qt::Key_Right:
        if (m_list->rowCount() > 0 && itemAt(m_list->anchor()).isFolder)
            open(m_list->anchor());
        break;
    case Qt::Key_Up: m_list->moveCursor(-1); break;
    case Qt::Key_Down: m_list->moveCursor(1); break;
    case Qt::Key_PageUp: m_list->moveCursor(-m_list->visibleRows()); break;
    case Qt::Key_PageDown: m_list->moveCursor(m_list->visibleRows()); break;
    default:
        if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_A) {
            m_list->selectAll();
            break;
        }
        // Typing filters the current folder by title.
        if (!event->text().isEmpty() && event->text().at(0).isPrint()
            && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
            setFilter(m_filter + event->text());
            break;
        }
        GenericWindow::keyPressEvent(event);
        return;
    }
    event->accept();
}

void LibraryWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    const bool hasSelection = !m_list->selectedRows().isEmpty() && !m_path.isEmpty();
    menu.addAction(tr("Play"), this, [this] { useSelection(true); })->setEnabled(hasSelection);
    menu.addAction(tr("Add to playlist"), this, [this] { useSelection(false); })->setEnabled(hasSelection);
    menu.addAction(tr("Back"), this, [this] { back(); });
    menu.addAction(tr("Refresh"), this, [this] { load(); });
    if (!m_path.isEmpty()) {
        const QList<QAction *> actions = m_path.first().library->libraryActions();
        if (!actions.isEmpty()) {
            menu.addSeparator();
            menu.addActions(actions);
        }
    }
    menu.exec(event->globalPos());
}

} // namespace newamp
