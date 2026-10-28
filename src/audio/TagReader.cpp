#include "audio/TagReader.h"

#include "core/Lyrics.h"

#include <QFile>
#include <QStringDecoder>

namespace newamp {

namespace {

constexpr qint64 MaxTagBytes = 1024 * 1024;

quint32 be32(const QByteArray &d, int at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
}

quint32 le32(const QByteArray &d, qint64 at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint32 syncsafe(const QByteArray &d, int at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return (quint32(p[0] & 0x7f) << 21) | (quint32(p[1] & 0x7f) << 14) | (quint32(p[2] & 0x7f) << 7) | (p[3] & 0x7f);
}

// Text in an ID3 encoding: 0 Latin-1, 1 UTF-16 with BOM, 2 UTF-16BE, 3 UTF-8.
QString decodeBody(char encoding, const QByteArray &body)
{
    switch (encoding) {
    case 1: { // UTF-16 with BOM
        QStringDecoder decoder(QStringDecoder::Utf16, QStringDecoder::Flag::ConvertInitialBom);
        if (body.size() >= 2 && uchar(body.at(0)) == 0xfe && uchar(body.at(1)) == 0xff)
            decoder = QStringDecoder(QStringDecoder::Utf16BE);
        else if (body.size() >= 2 && uchar(body.at(0)) == 0xff && uchar(body.at(1)) == 0xfe)
            decoder = QStringDecoder(QStringDecoder::Utf16LE);
        return decoder.decode(body.size() >= 2 ? body.mid(2) : body);
    }
    case 2: return QStringDecoder(QStringDecoder::Utf16BE).decode(body);
    case 3: return QString::fromUtf8(body);
    default: return QString::fromLatin1(body);
    }
}

QString decodeText(const QByteArray &frame)
{
    if (frame.isEmpty())
        return {};
    const QString text = decodeBody(frame.at(0), frame.mid(1));
    // Strings may be NUL-terminated or list several values.
    const int nul = text.indexOf(QChar(0));
    return (nul >= 0 ? text.left(nul) : text).trimmed();
}

// Splits off a NUL-terminated string in the frame's encoding (two NULs on
// a 16-bit boundary for UTF-16); `at` moves past it. Not trimmed: SYLT
// marks new lines with a leading line break.
QString takeTerminated(const QByteArray &frame, char encoding, int &at)
{
    const bool wide = encoding == 1 || encoding == 2;
    int end = at;
    if (wide) {
        while (end + 1 < frame.size() && (frame.at(end) != 0 || frame.at(end + 1) != 0))
            end += 2;
    } else {
        while (end < frame.size() && frame.at(end) != 0)
            ++end;
    }
    const QString text = decodeBody(encoding, frame.mid(at, end - at));
    at = qMin<int>(int(frame.size()), end + (wide ? 2 : 1));
    return text;
}

// USLT: encoding, language, descriptor, then the text, line breaks and all.
QString decodeUnsyncedLyrics(const QByteArray &frame)
{
    if (frame.size() < 5)
        return {};
    const char encoding = frame.at(0);
    int at = 4;
    takeTerminated(frame, encoding, at); // descriptor
    return takeTerminated(frame, encoding, at).trimmed();
}

// SYLT: encoding, language, timestamp format (2 = milliseconds), content
// type, descriptor, then (text, 32-bit time) pairs. Becomes LRC; MPEG frame
// timestamps can't be turned into times without decoding, so are skipped.
QString decodeSyncedLyrics(const QByteArray &frame)
{
    if (frame.size() < 7 || frame.at(4) != 2)
        return {};
    const char encoding = frame.at(0);
    int at = 6;
    takeTerminated(frame, encoding, at); // descriptor
    QString lrc;
    while (at < frame.size()) {
        QString text = takeTerminated(frame, encoding, at);
        if (at + 4 > frame.size())
            break;
        const qint64 ms = be32(frame, at);
        at += 4;
        // Each entry may start with a line break that marks a new line;
        // entries without one continue the previous line (word by word).
        const bool newLine = lrc.isEmpty() || text.startsWith(QLatin1Char('\n')) || text.startsWith(QLatin1Char('\r'));
        text.remove(QLatin1Char('\r')).remove(QLatin1Char('\n'));
        if (newLine) {
            if (!lrc.isEmpty())
                lrc += QLatin1Char('\n');
            lrc += Lyrics::lrcTime(ms);
        }
        lrc += text;
    }
    return lrc;
}

// Every string in a text frame: TXXX holds a description and then the value.
QStringList decodeTexts(const QByteArray &frame)
{
    if (frame.isEmpty())
        return {};
    const char encoding = frame.at(0);
    const QByteArray body = frame.mid(1);
    QString text;
    switch (encoding) {
    case 1: {
        // Each string may carry its own BOM; the first one sets the order.
        const bool bigEndian = body.size() >= 2 && uchar(body.at(0)) == 0xfe && uchar(body.at(1)) == 0xff;
        text = QStringDecoder(bigEndian ? QStringDecoder::Utf16BE : QStringDecoder::Utf16LE).decode(body);
        break;
    }
    case 2: text = QStringDecoder(QStringDecoder::Utf16BE).decode(body); break;
    case 3: text = QString::fromUtf8(body); break;
    default: text = QString::fromLatin1(body); break;
    }
    text.remove(QChar(0xfeff));
    QStringList parts = text.split(QChar(0));
    for (QString &part : parts)
        part = part.trimmed();
    return parts;
}

void assign(Tags &tags, const QString &key, const QString &value)
{
    if (value.isEmpty() || tags.replayGain.assign(key, value))
        return;
    if (key == QLatin1String("TITLE") && tags.title.isEmpty())
        tags.title = value;
    else if (key == QLatin1String("ARTIST") && tags.artist.isEmpty())
        tags.artist = value;
    else if (key == QLatin1String("ALBUM") && tags.album.isEmpty())
        tags.album = value;
    else if ((key == QLatin1String("LYRICS") || key == QLatin1String("UNSYNCEDLYRICS")) && tags.lyrics.isEmpty())
        tags.lyrics = value;
}

// MP4/M4A metadata lives in moov/udta/meta/ilst. The atoms are walked with
// seeks, so neither the audio (mdat) nor cover art is read.
struct Atom
{
    QByteArray type;
    qint64 payload = 0; // first byte after the header
    qint64 end = 0;
};

// Reads the header of the atom at `at`, which must fit before `end`.
bool readAtom(QIODevice &device, qint64 at, qint64 end, Atom &atom)
{
    if (at + 8 > end || !device.seek(at))
        return false;
    const QByteArray header = device.read(8);
    if (header.size() < 8)
        return false;
    qint64 size = be32(header, 0);
    qint64 headerLength = 8;
    if (size == 1) { // 64-bit size follows
        const QByteArray large = device.read(8);
        if (large.size() < 8)
            return false;
        size = qint64((quint64(be32(large, 0)) << 32) | be32(large, 4));
        headerLength = 16;
    } else if (size == 0) { // runs to the end
        size = end - at;
    }
    if (size < headerLength || size > end - at)
        return false;
    atom = {header.mid(4, 4), at + headerLength, at + size};
    return true;
}

// Finds the first atom of `type` among those in [begin, end).
bool findAtom(QIODevice &device, qint64 begin, qint64 end, const char *type, Atom &atom)
{
    for (qint64 at = begin; readAtom(device, at, end, atom); at = atom.end) {
        if (atom.type == type)
            return true;
    }
    return false;
}

Tags readMp4(QIODevice &device)
{
    Tags tags;
    Atom moov, udta, meta, ilst;
    if (!findAtom(device, 0, device.size(), "moov", moov) || !findAtom(device, moov.payload, moov.end, "udta", udta)
        || !findAtom(device, udta.payload, udta.end, "meta", meta))
        return tags;
    // ISO "meta" is a full box with 4 bytes of version and flags before its
    // children; QuickTime's isn't. Its first child is always "hdlr".
    device.seek(meta.payload + 4);
    if (device.read(4) != "hdlr")
        meta.payload += 4;
    if (!findAtom(device, meta.payload, meta.end, "ilst", ilst))
        return tags;

    static const QByteArray Title("\xa9nam"), Artist("\xa9" "ART"), Album("\xa9" "alb"), LyricsKey("\xa9lyr");
    Atom item;
    for (qint64 at = ilst.payload; readAtom(device, at, ilst.end, item); at = item.end) {
        // Freeform "----" items (mean, name, data) carry ReplayGain as
        // com.apple.iTunes:replaygain_track_gain and friends.
        QString freeform;
        Atom name;
        if (item.type == "----" && findAtom(device, item.payload, item.end, "name", name)
            && name.end - name.payload > 4) {
            device.seek(name.payload + 4); // version and flags
            freeform = QString::fromUtf8(device.read(qMin<qint64>(name.end - name.payload - 4, 256)));
        }
        const char *key = item.type == Title ? "TITLE"
            : item.type == Artist            ? "ARTIST"
            : item.type == Album             ? "ALBUM"
            : item.type == LyricsKey         ? "LYRICS"
                                             : nullptr;
        Atom data;
        if ((!key && freeform.isEmpty()) || !findAtom(device, item.payload, item.end, "data", data)
            || data.end - data.payload < 8)
            continue;
        // 4 bytes of type (1 = UTF-8, 2 = UTF-16BE), 4 of locale, then the text.
        device.seek(data.payload);
        const QByteArray head = device.read(8);
        const qint64 limit = item.type == LyricsKey ? 256 * 1024 : 4096;
        const QByteArray value = device.read(qMin<qint64>(data.end - data.payload - 8, limit));
        const quint32 kind = be32(head, 0) & 0xffffff;
        const QString text = kind == 2 ? QString(QStringDecoder(QStringDecoder::Utf16BE).decode(value))
                                       : QString::fromUtf8(value);
        if (key)
            assign(tags, QString::fromLatin1(key), text.trimmed());
        else
            tags.replayGain.assign(freeform, text.trimmed());
    }
    return tags;
}

} // namespace

Tags TagReader::parseId3v2(const QByteArray &data)
{
    Tags tags;
    if (data.size() < 10 || !data.startsWith("ID3"))
        return tags;
    const int version = data.at(3);
    const int flags = uchar(data.at(5));
    const int end = qMin<qint64>(10 + syncsafe(data, 6), data.size());
    int at = 10;
    if ((flags & 0x40) && version >= 3) // extended header
        at += version == 4 ? int(syncsafe(data, at)) : int(be32(data, at)) + 4;

    QString unsynced, synced;
    const int idLength = version == 2 ? 3 : 4;
    const int headerLength = version == 2 ? 6 : 10;
    while (at + headerLength <= end) {
        const QByteArray id = data.mid(at, idLength);
        if (id.at(0) == 0)
            break; // padding
        quint32 size;
        if (version == 2)
            size = (quint32(uchar(data.at(at + 3))) << 16) | (quint32(uchar(data.at(at + 4))) << 8) | uchar(data.at(at + 5));
        else
            size = version == 4 ? syncsafe(data, at + 4) : be32(data, at + 4);
        at += headerLength;
        if (size == 0 || at + qint64(size) > end)
            break;
        const QByteArray frame = data.mid(at, int(size));
        if (id == "TIT2" || id == "TT2")
            assign(tags, QStringLiteral("TITLE"), decodeText(frame));
        else if (id == "TPE1" || id == "TP1")
            assign(tags, QStringLiteral("ARTIST"), decodeText(frame));
        else if (id == "TALB" || id == "TAL")
            assign(tags, QStringLiteral("ALBUM"), decodeText(frame));
        else if (id == "USLT" || id == "ULT")
            unsynced = unsynced.isEmpty() ? decodeUnsyncedLyrics(frame) : unsynced;
        else if (id == "SYLT" || id == "SLT")
            synced = synced.isEmpty() ? decodeSyncedLyrics(frame) : synced;
        else if (id == "TXXX" || id == "TXX") {
            const QStringList texts = decodeTexts(frame);
            if (texts.size() >= 2)
                tags.replayGain.assign(texts.at(0), texts.at(1));
        }
        at += int(size);
    }
    tags.lyrics = synced.isEmpty() ? unsynced : synced;
    return tags;
}

Tags TagReader::parseId3v1(const QByteArray &last128)
{
    Tags tags;
    if (last128.size() != 128 || !last128.startsWith("TAG"))
        return tags;
    auto field = [&](int at, int length) {
        QByteArray raw = last128.mid(at, length);
        const int nul = raw.indexOf('\0');
        if (nul >= 0)
            raw.truncate(nul);
        return QString::fromLatin1(raw).trimmed();
    };
    tags.title = field(3, 30);
    tags.artist = field(33, 30);
    tags.album = field(63, 30);
    return tags;
}

Tags TagReader::parseVorbisComments(const QByteArray &block)
{
    // vendor length, vendor, count, then count x (length, "KEY=value").
    Tags tags;
    if (block.size() < 8)
        return tags;
    qint64 at = 4 + qint64(le32(block, 0));
    if (at + 4 > block.size())
        return tags;
    const quint32 count = le32(block, at);
    at += 4;
    for (quint32 i = 0; i < count && at + 4 <= block.size(); ++i) {
        const qint64 length = le32(block, at);
        at += 4;
        if (at + length > block.size())
            break;
        const QString comment = QString::fromUtf8(block.mid(at, int(length)));
        at += length;
        const int eq = comment.indexOf(QLatin1Char('='));
        if (eq > 0)
            assign(tags, comment.left(eq).toUpper(), comment.mid(eq + 1).trimmed());
    }
    return tags;
}

Tags TagReader::read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return read(file);
}

Tags TagReader::read(QIODevice &device)
{
    device.seek(0);
    const QByteArray head = device.peek(10);
    Tags tags;

    if (head.startsWith("ID3")) {
        const qint64 size = qMin<qint64>(10 + syncsafe(head, 6), MaxTagBytes);
        tags = parseId3v2(device.read(size));
    } else if (head.startsWith("fLaC")) {
        device.read(4);
        // Walk metadata blocks until the Vorbis comment (type 4) or the last one.
        for (int guard = 0; guard < 64; ++guard) {
            const QByteArray header = device.read(4);
            if (header.size() < 4)
                break;
            const bool last = uchar(header.at(0)) & 0x80;
            const int type = uchar(header.at(0)) & 0x7f;
            const qint64 length = (qint64(uchar(header.at(1))) << 16) | (qint64(uchar(header.at(2))) << 8) | uchar(header.at(3));
            if (type == 4) {
                tags = parseVorbisComments(device.read(qMin(length, MaxTagBytes)));
                break;
            }
            if (last || !device.seek(device.pos() + length))
                break;
        }
    } else if (head.mid(4, 4) == "ftyp") {
        tags = readMp4(device);
    } else if (head.startsWith("OggS")) {
        // The comment header follows "\x03vorbis" (or, for Opus, "OpusTags")
        // near the start.
        const QByteArray start = device.read(64 * 1024);
        const int at = start.indexOf(QByteArray("\x03vorbis", 7));
        const int opus = start.indexOf("OpusTags");
        if (at >= 0)
            tags = parseVorbisComments(start.mid(at + 7));
        else if (opus >= 0)
            tags = parseVorbisComments(start.mid(opus + 8));
    }

    // Fall back to ID3v1 at the end of MP3s.
    if (tags.title.isEmpty() && device.size() > 128 && device.seek(device.size() - 128)) {
        const Tags v1 = parseId3v1(device.read(128));
        if (!v1.title.isEmpty()) {
            const ReplayGainInfo replayGain = tags.replayGain;
            tags = v1;
            tags.replayGain = replayGain;
        }
    }
    device.seek(0);
    return tags;
}

} // namespace newamp
