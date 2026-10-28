#pragma once

#include "ui/GenericWindow.h"

#include <QElapsedTimer>
#include <QList>

class QTimer;

namespace newamp {

class LyricsService;

// Lyrics for the playing track. Synced lyrics scroll along with the song,
// the line being sung drawn in the skin's "current" playlist colour; click
// a line to jump there. Plain lyrics scroll with the wheel. Right-click for
// text size, timing nudges (saved per track), opening a .lrc file, and copy.
class LyricsWindow : public GenericWindow
{
    Q_OBJECT

public:
    static constexpr QSize DefaultSize{275, 116 + 6 * 29};
    static constexpr int TextSizes[] = {9, 12, 16};
    static constexpr qint64 OffsetStepMs = 500;

    LyricsWindow(const Skin *skin, LyricsService &service, QWidget *parent = nullptr);

    void setPosition(qint64 positionMs);
    void setPlaying(bool playing);

    // The line being sung, or -1 (before the first line, or plain lyrics).
    int currentLine() const { return m_current; }
    int textSize() const { return m_textSize; }
    void setTextSize(int pixels);

    // Moves the lyrics against the song for the current track: positive
    // shows them earlier. Saved per track.
    qint64 offsetMs() const { return m_offsetMs; }
    void setOffsetMs(qint64 offsetMs);

    // Advances the scroll animation and the current line; the timer calls it.
    void tick();

protected:
    void paintContent(QPainter &painter, const Skin &skin, const QRect &content) override;
    void layoutContent(const QRect &content) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct LineBox
    {
        int top = 0;
        int height = 0;
    };

    void lyricsChanged();
    void updateCurrentLine();
    void ensureLayout(int width) const;
    void updateTimer();
    qreal targetScroll() const;
    qreal maxScroll() const;
    int lineAtY(int y) const;
    int lineTop(int index) const; // in skin coordinates, at the current scroll
    QString statusText() const;
    QString offsetKey() const;
    void openLyricsFile();

    LyricsService &m_service;
    QTimer *m_timer;
    int m_textSize = TextSizes[1];
    qint64 m_offsetMs = 0;

    qint64 m_positionMs = 0;
    QElapsedTimer m_positionClock; // since the last position update
    bool m_playing = false;
    int m_current = -1;

    qreal m_scroll = 0; // synced: the layout y at the window's middle; plain: the y at its top
    QElapsedTimer m_manualScroll; // the wheel pauses following for a moment

    mutable QList<LineBox> m_boxes;
    mutable int m_layoutWidth = -1;
    mutable QString m_layoutFamily;
    mutable int m_layoutHeight = 0;
};

} // namespace newamp
