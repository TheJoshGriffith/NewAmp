#pragma once

#include <QList>
#include <QString>

namespace newamp {

// One timed word of an enhanced LRC line (<mm:ss.xx>word).
struct LyricWord
{
    qint64 startMs = 0;
    QString text;
};

struct LyricLine
{
    qint64 startMs = -1; // -1 in unsynced lyrics
    QString text;
    QList<LyricWord> words; // only in enhanced (word-timed) LRC
};

// Lyrics for one track, synced (LRC) or plain. Every source hands over its
// lyrics as text, LRC or plain, and parse() turns that into lines.
struct Lyrics
{
    QList<LyricLine> lines;
    bool synced = false;
    QString source; // where they came from, shown in the window ("File", "Tags", ...)

    bool isEmpty() const { return lines.isEmpty(); }

    // The line being sung at positionMs: the last one starting at or before
    // it, or -1 before the first line and in unsynced lyrics.
    int lineAt(qint64 positionMs) const;
    // The lyrics as plain text, one line per line.
    QString plainText() const;

    // Parses LRC, with [mm:ss.xx] line tags (several per line allowed),
    // <mm:ss.xx> word tags, and [offset:ms]. Text without any time tags
    // becomes unsynced lyrics; blank lines between verses are kept.
    static Lyrics parse(const QString &text);
    // "[mm:ss.xx]" for a time, as LRC writes it.
    static QString lrcTime(qint64 ms);
};

} // namespace newamp
