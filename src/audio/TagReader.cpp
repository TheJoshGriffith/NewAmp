#include "audio/TagReader.h"

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

QString decodeText(const QByteArray &frame)
{
    if (frame.isEmpty())
        return {};
    const char encoding = frame.at(0);
    QByteArray body = frame.mid(1);
    QString text;
    switch (encoding) {
    case 1: { // UTF-16 with BOM
        QStringDecoder decoder(QStringDecoder::Utf16, QStringDecoder::Flag::ConvertInitialBom);
        if (body.size() >= 2 && uchar(body.at(0)) == 0xfe && uchar(body.at(1)) == 0xff)
            decoder = QStringDecoder(QStringDecoder::Utf16BE);
        else if (body.size() >= 2 && uchar(body.at(0)) == 0xff && uchar(body.at(1)) == 0xfe)
            decoder = QStringDecoder(QStringDecoder::Utf16LE);
        text = decoder.decode(body.size() >= 2 ? body.mid(2) : body);
        break;
    }
    case 2: text = QStringDecoder(QStringDecoder::Utf16BE).decode(body); break;
    case 3: text = QString::fromUtf8(body); break;
    default: text = QString::fromLatin1(body); break;
    }
    // Strings may be NUL-terminated or list several values.
    const int nul = text.indexOf(QChar(0));
    return (nul >= 0 ? text.left(nul) : text).trimmed();
}

void assign(Tags &tags, const QString &key, const QString &value)
{
    if (value.isEmpty())
        return;
    if (key == QLatin1String("TITLE") && tags.title.isEmpty())
        tags.title = value;
    else if (key == QLatin1String("ARTIST") && tags.artist.isEmpty())
        tags.artist = value;
    else if (key == QLatin1String("ALBUM") && tags.album.isEmpty())
        tags.album = value;
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
        at += int(size);
    }
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
    } else if (head.startsWith("OggS")) {
        // The comment header follows "\x03vorbis" near the start.
        const QByteArray start = device.read(64 * 1024);
        const int at = start.indexOf(QByteArray("\x03vorbis", 7));
        if (at >= 0)
            tags = parseVorbisComments(start.mid(at + 7));
    }

    // Fall back to ID3v1 at the end of MP3s.
    if (tags.title.isEmpty() && device.size() > 128 && device.seek(device.size() - 128)) {
        const Tags v1 = parseId3v1(device.read(128));
        if (!v1.title.isEmpty())
            tags = v1;
    }
    device.seek(0);
    return tags;
}

} // namespace newamp
