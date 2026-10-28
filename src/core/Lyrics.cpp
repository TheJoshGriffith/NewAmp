#include "core/Lyrics.h"

#include <QRegularExpression>

#include <algorithm>

namespace newamp {

namespace {

// [mm:ss], [mm:ss.x], [mm:ss.xx] or [mm:ss.xxx]; some files use ':' before
// the fraction. The same shape in <> marks a word.
const QString TimePattern = QStringLiteral(R"((\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?)");

qint64 toMs(const QRegularExpressionMatch &match)
{
    const qint64 minutes = match.captured(1).toLongLong();
    const qint64 seconds = match.captured(2).toLongLong();
    const QString fraction = match.captured(3);
    qint64 ms = 0;
    if (!fraction.isEmpty()) {
        ms = fraction.toLongLong();
        for (qsizetype i = fraction.size(); i < 3; ++i)
            ms *= 10;
    }
    return (minutes * 60 + seconds) * 1000 + ms;
}

} // namespace

int Lyrics::lineAt(qint64 positionMs) const
{
    if (!synced)
        return -1;
    // Lines are sorted by start; find the last one at or before the position.
    const auto after = std::upper_bound(lines.cbegin(), lines.cend(), positionMs,
                                        [](qint64 pos, const LyricLine &line) { return pos < line.startMs; });
    return int(after - lines.cbegin()) - 1;
}

QString Lyrics::plainText() const
{
    QStringList text;
    for (const LyricLine &line : lines)
        text.append(line.text);
    return text.join(QLatin1Char('\n'));
}

QString Lyrics::lrcTime(qint64 ms)
{
    ms = qMax<qint64>(0, ms);
    return QStringLiteral("[%1:%2.%3]")
        .arg(ms / 60000, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 10 % 100, 2, 10, QLatin1Char('0'));
}

Lyrics Lyrics::parse(const QString &text)
{
    static const QRegularExpression lineTag(QStringLiteral("\\[") + TimePattern + QStringLiteral("\\]"));
    static const QRegularExpression wordTag(QLatin1Char('<') + TimePattern + QLatin1Char('>'));
    static const QRegularExpression metaTag(QStringLiteral(R"(^\[([A-Za-z#]+):(.*)\]\s*$)"));

    QString body = text;
    if (body.startsWith(QChar(0xfeff)))
        body.remove(0, 1);
    const QStringList rawLines = body.split(QRegularExpression(QStringLiteral("\r\n|\r|\n")));

    Lyrics lyrics;
    QList<LyricLine> plain;
    qint64 offset = 0;
    for (const QString &raw : rawLines) {
        QList<qint64> starts;
        qsizetype at = 0;
        for (;;) {
            const QRegularExpressionMatch match =
                lineTag.match(raw, at, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            if (!match.hasMatch())
                break;
            starts.append(toMs(match));
            at = match.capturedEnd();
        }
        const QString rest = raw.mid(at);

        if (starts.isEmpty()) {
            const QRegularExpressionMatch meta = metaTag.match(raw.trimmed());
            if (meta.hasMatch()) {
                if (meta.captured(1).compare(QLatin1String("offset"), Qt::CaseInsensitive) == 0)
                    offset = meta.captured(2).trimmed().toLongLong();
                continue; // [ar:], [ti:], [length:] and friends
            }
            plain.append({-1, raw.trimmed(), {}});
            continue;
        }

        // Word tags: each one starts the text up to the next tag.
        LyricLine line;
        QRegularExpressionMatchIterator words = wordTag.globalMatch(rest);
        if (words.hasNext()) {
            QString lineText = rest.left(words.peekNext().capturedStart());
            while (words.hasNext()) {
                const QRegularExpressionMatch word = words.next();
                const qsizetype from = word.capturedEnd();
                const qsizetype to = words.hasNext() ? words.peekNext().capturedStart() : rest.size();
                const QString wordText = rest.mid(from, to - from);
                if (!wordText.isEmpty())
                    line.words.append({toMs(word), wordText});
                lineText += wordText;
            }
            line.text = lineText.trimmed();
        } else {
            line.text = rest.trimmed();
        }
        for (const qint64 start : starts) {
            LyricLine copy = line;
            copy.startMs = start;
            lyrics.lines.append(copy);
        }
    }

    if (lyrics.lines.isEmpty()) {
        // Unsynced: keep the text, without blank lines at either end.
        while (!plain.isEmpty() && plain.first().text.isEmpty())
            plain.removeFirst();
        while (!plain.isEmpty() && plain.last().text.isEmpty())
            plain.removeLast();
        lyrics.lines = plain;
        return lyrics;
    }

    // A positive offset shows the lyrics earlier.
    lyrics.synced = true;
    for (LyricLine &line : lyrics.lines) {
        line.startMs = qMax<qint64>(0, line.startMs - offset);
        for (LyricWord &word : line.words)
            word.startMs = qMax<qint64>(0, word.startMs - offset);
    }
    std::stable_sort(lyrics.lines.begin(), lyrics.lines.end(),
                     [](const LyricLine &a, const LyricLine &b) { return a.startMs < b.startMs; });
    return lyrics;
}

} // namespace newamp
