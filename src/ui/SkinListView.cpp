#include "ui/SkinListView.h"

#include "skin/Skin.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>

#include <algorithm>

namespace newamp {

SkinListView::SkinListView(SkinnedWindow *window, std::function<int()> rowCount, std::function<Row(int)> row)
    : SkinElement(window, {})
    , m_rowCount(std::move(rowCount))
    , m_row(std::move(row))
{
}

void SkinListView::setFirstRow(int row)
{
    row = qBound(0, row, maxFirstRow());
    if (row == m_firstRow)
        return;
    m_firstRow = row;
    update();
    if (onScrolled)
        onScrolled();
}

void SkinListView::refresh()
{
    QSet<int> valid;
    for (int row : std::as_const(m_selected))
        if (row < rowCount())
            valid.insert(row);
    m_selected = valid;
    setFirstRow(m_firstRow);
    update();
}

void SkinListView::reset()
{
    m_selected.clear();
    m_anchor = 0;
    m_firstRow = 0;
    update();
    if (onScrolled)
        onScrolled();
}

QList<int> SkinListView::selectedRows() const
{
    QList<int> rows(m_selected.begin(), m_selected.end());
    std::sort(rows.begin(), rows.end());
    return rows;
}

void SkinListView::selectAll()
{
    m_selected.clear();
    for (int i = 0; i < rowCount(); ++i)
        m_selected.insert(i);
    update();
}

void SkinListView::selectNone()
{
    m_selected.clear();
    update();
}

void SkinListView::select(int row)
{
    if (row < 0 || row >= rowCount())
        return;
    m_selected = {row};
    m_anchor = row;
    ensureVisible(row);
    update();
}

void SkinListView::ensureVisible(int row)
{
    if (row < m_firstRow)
        setFirstRow(row);
    else if (row >= m_firstRow + visibleRows())
        setFirstRow(row - visibleRows() + 1);
}

void SkinListView::moveCursor(int delta)
{
    if (rowCount() == 0)
        return;
    select(qBound(0, m_anchor + delta, rowCount() - 1));
}

int SkinListView::rowAt(const QPoint &pos) const
{
    if (!rect().contains(pos))
        return -1;
    const int row = m_firstRow + (pos.y() - rect().top()) / RowHeight;
    return row < rowCount() ? row : -1;
}

void SkinListView::paint(QPainter &painter, const Skin &skin) const
{
    const PlaylistStyle &style = skin.playlistStyle();
    painter.fillRect(rect(), style.normalBackground);

    QFont font(style.fontFamily.isEmpty() ? QStringLiteral("Helvetica") : style.fontFamily);
    font.setPixelSize(9);
    painter.save();
    painter.setFont(font);
    painter.setClipRect(rect());
    const QFontMetrics metrics(font);
    const int last = qMin(rowCount(), m_firstRow + visibleRows() + 1);
    for (int index = m_firstRow; index < last; ++index) {
        const QRect line(rect().left(), rect().top() + (index - m_firstRow) * RowHeight, rect().width(), RowHeight);
        if (m_selected.contains(index))
            painter.fillRect(line, style.selectedBackground);
        const Row row = m_row(index);
        painter.setPen(row.highlighted ? style.current : style.normal);
        const int detailWidth = metrics.horizontalAdvance(row.detail);
        int indent = 2;
        if (row.failed) {
            const QRect cross(line.left() + 2, line.top() + 3, 6, 6);
            painter.save();
            painter.setPen(QPen(QColor(220, 40, 40), 1.5));
            painter.setRenderHint(QPainter::Antialiasing);
            painter.drawLine(cross.topLeft(), cross.bottomRight() + QPoint(1, 1));
            painter.drawLine(cross.topRight() + QPoint(1, 0), cross.bottomLeft() + QPoint(0, 1));
            painter.restore();
            indent += 9;
        }
        const QRect textRect = line.adjusted(indent, 0, -detailWidth - 6, 0);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(row.text, Qt::ElideRight, textRect.width()));
        painter.drawText(line.adjusted(0, 0, -2, 0), Qt::AlignRight | Qt::AlignVCenter, row.detail);
    }
    painter.restore();
}

void SkinListView::mousePress(const QPoint &pos)
{
    const int row = rowAt(pos);
    const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
    if (row < 0) {
        selectNone();
        return;
    }
    if (mods & Qt::ShiftModifier) {
        m_selected.clear();
        for (int i = qMin(m_anchor, row); i <= qMax(m_anchor, row); ++i)
            m_selected.insert(i);
    } else if (mods & (Qt::ControlModifier | Qt::MetaModifier)) {
        if (!m_selected.remove(row))
            m_selected.insert(row);
        m_anchor = row;
    } else {
        m_selected = {row};
        m_anchor = row;
    }
    update();
}

void SkinListView::mouseDoubleClick(const QPoint &pos)
{
    const int row = rowAt(pos);
    if (row >= 0 && onActivated)
        onActivated(row);
}

bool SkinListView::wheel(int angleDeltaY)
{
    setFirstRow(m_firstRow - angleDeltaY / 40);
    return true;
}

} // namespace newamp
