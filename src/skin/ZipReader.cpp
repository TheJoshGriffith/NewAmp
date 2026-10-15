#include "skin/ZipReader.h"

#include <QFile>

#include <zlib.h>

namespace newamp {

namespace {

constexpr quint32 EndOfCentralDirSig = 0x06054b50;
constexpr quint32 CentralDirSig = 0x02014b50;
constexpr quint32 LocalHeaderSig = 0x04034b50;

quint16 u16(const QByteArray &d, qint64 at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return quint16(p[0] | (p[1] << 8));
}

quint32 u32(const QByteArray &d, qint64 at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

bool inRange(const QByteArray &d, qint64 at, qint64 length)
{
    return at >= 0 && length >= 0 && at + length <= d.size();
}

QByteArray inflateRaw(const char *data, quint32 size, quint32 expected)
{
    QByteArray out(int(expected), Qt::Uninitialized);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data));
    stream.avail_in = size;
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = expected;
    const int result = inflate(&stream, Z_FINISH);
    const bool ok = result == Z_STREAM_END && stream.total_out == expected;
    inflateEnd(&stream);
    return ok ? out : QByteArray();
}

} // namespace

bool ZipReader::open(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    if (file.size() > MaxArchiveSize) {
        m_error = QStringLiteral("Archive is too large");
        return false;
    }
    return openData(file.readAll());
}

bool ZipReader::openData(const QByteArray &data)
{
    m_data = data;
    m_entries.clear();
    m_order.clear();
    m_error.clear();
    if (parse())
        return true;
    m_entries.clear();
    m_order.clear();
    return false;
}

bool ZipReader::parse()
{
    // The end-of-central-directory record is in the last 64 KiB + 22 bytes.
    const qint64 minEnd = qMax<qint64>(0, m_data.size() - 0xffff - 22);
    qint64 eocd = -1;
    for (qint64 at = m_data.size() - 22; at >= minEnd; --at) {
        if (u32(m_data, at) == EndOfCentralDirSig) {
            eocd = at;
            break;
        }
    }
    if (eocd < 0) {
        m_error = QStringLiteral("Not a ZIP archive");
        return false;
    }

    const int count = u16(m_data, eocd + 10);
    qint64 at = u32(m_data, eocd + 16);
    for (int i = 0; i < count; ++i) {
        if (!inRange(m_data, at, 46) || u32(m_data, at) != CentralDirSig) {
            m_error = QStringLiteral("Corrupt central directory");
            return false;
        }
        Entry entry;
        const quint16 flags = u16(m_data, at + 8);
        entry.method = u16(m_data, at + 10);
        entry.compressedSize = u32(m_data, at + 20);
        entry.uncompressedSize = u32(m_data, at + 24);
        const int nameLength = u16(m_data, at + 28);
        const int extraLength = u16(m_data, at + 30);
        const int commentLength = u16(m_data, at + 32);
        entry.localHeaderOffset = u32(m_data, at + 42);
        if (!inRange(m_data, at + 46, nameLength)) {
            m_error = QStringLiteral("Corrupt central directory");
            return false;
        }
        const QByteArray rawName = m_data.mid(at + 46, nameLength);
        // Bit 11: name is UTF-8; otherwise it is CP437, close enough to Latin-1.
        const QString name = (flags & 0x800) ? QString::fromUtf8(rawName) : QString::fromLatin1(rawName);
        if (!(flags & 0x1) && !name.endsWith(QLatin1Char('/'))) {
            m_entries.insert(name, entry);
            m_order << name;
        }
        at += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

QStringList ZipReader::entryNames() const
{
    return m_order;
}

QByteArray ZipReader::read(const QString &name) const
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.cend())
        return {};
    const Entry &entry = *it;
    if (entry.uncompressedSize > MaxEntrySize)
        return {};

    const qint64 header = entry.localHeaderOffset;
    if (!inRange(m_data, header, 30) || u32(m_data, header) != LocalHeaderSig)
        return {};
    const qint64 dataStart = header + 30 + u16(m_data, header + 26) + u16(m_data, header + 28);
    if (!inRange(m_data, dataStart, entry.compressedSize))
        return {};

    const char *data = m_data.constData() + dataStart;
    switch (entry.method) {
    case 0:
        if (entry.compressedSize != entry.uncompressedSize)
            return {};
        return QByteArray(data, int(entry.compressedSize));
    case 8:
        if (entry.uncompressedSize == 0)
            return QByteArray("");
        return inflateRaw(data, entry.compressedSize, entry.uncompressedSize);
    default:
        return {};
    }
}

} // namespace newamp
