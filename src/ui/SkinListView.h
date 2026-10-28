#pragma once

#include "ui/SkinElement.h"

#include <QList>
#include <QSet>
#include <QString>

#include <functional>

namespace newamp {

// A scrolling list of one-line rows in the skin's playlist colours and font,
// with a right-aligned detail column and multiple selection. The playlist
// and the media library both use it; rows come from callbacks, so the list
// holds no data of its own.
class SkinListView : public SkinElement
{
public:
    struct Row
    {
        QString text;
        QString detail;
        bool highlighted = false; // drawn in the "current" colour
        bool failed = false;      // marked with a red cross
    };

    static constexpr int RowHeight = 13;

    SkinListView(SkinnedWindow *window, std::function<int()> rowCount, std::function<Row(int)> row);

    std::function<void(int row)> onActivated; // double-click
    std::function<void()> onScrolled;

    int rowCount() const { return m_rowCount(); }
    int visibleRows() const { return qMax(1, rect().height() / RowHeight); }
    int maxFirstRow() const { return qMax(0, rowCount() - visibleRows()); }
    int firstRow() const { return m_firstRow; }
    void setFirstRow(int row);

    // Call after the rows change: drops stale selections and clamps scrolling.
    void refresh();
    // Back to the top with nothing selected, e.g. for a new set of rows.
    void reset();

    QList<int> selectedRows() const; // sorted
    void selectAll();
    void selectNone();
    void invertSelection();
    void select(int row);
    void ensureVisible(int row);
    void moveCursor(int delta);
    int anchor() const { return m_anchor; }
    int rowAt(const QPoint &pos) const;

    void paint(QPainter &painter, const Skin &skin) const override;
    bool acceptsMouse() const override { return true; }
    void mousePress(const QPoint &pos) override;
    void mouseDoubleClick(const QPoint &pos) override;
    bool wheel(int angleDeltaY) override;

private:
    std::function<int()> m_rowCount;
    std::function<Row(int)> m_row;
    QSet<int> m_selected;
    int m_firstRow = 0;
    int m_anchor = 0;
};

} // namespace newamp
