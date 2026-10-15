#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace newamp {

// Minimal read-only ZIP reader, enough for skin archives (.wsz): stored and
// deflated entries, no encryption, no ZIP64. Untrusted input is bounds
// checked throughout and oversized entries are refused.
class ZipReader
{
public:
    static constexpr qint64 MaxArchiveSize = 32 * 1024 * 1024;
    static constexpr qint64 MaxEntrySize = 16 * 1024 * 1024;

    bool open(const QString &path);
    bool openData(const QByteArray &data);
    QString errorString() const { return m_error; }

    QStringList entryNames() const;
    // Reads an entry by exact name. Returns a null QByteArray on failure.
    QByteArray read(const QString &name) const;

private:
    struct Entry
    {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localHeaderOffset = 0;
    };

    bool parse();

    QByteArray m_data;
    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QString m_error;
};

} // namespace newamp
