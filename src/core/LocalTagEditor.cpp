#include "core/LocalTagEditor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>

#include <aifffile.h>
#include <asffile.h>
#include <fileref.h>
#include <flacfile.h>
#include <id3v2tag.h>
#include <mp4file.h>
#include <mp4properties.h>
#include <mpegfile.h>
#include <opusfile.h>
#include <tpropertymap.h>
#include <vorbisfile.h>
#include <wavfile.h>

namespace newamp {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("LocalTagEditor", text);
}

// Several values of one tag (two artists) show as one line, split again on
// the way back.
const QString ValueSeparator = QStringLiteral("; ");

QString toQString(const TagLib::String &s)
{
    return QString::fromUtf8(s.toCString(true));
}

TagLib::String toTagString(const QString &s)
{
    return TagLib::String(s.toUtf8().constData(), TagLib::String::UTF8);
}

TagLib::FileRef open(const QString &path, bool readProperties)
{
#ifdef Q_OS_WIN
    const TagLib::FileName name(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
    const QByteArray encoded = QFile::encodeName(path);
    const TagLib::FileName name(encoded.constData());
#endif
    return TagLib::FileRef(name, readProperties, TagLib::AudioProperties::Average);
}

// The container and codec, and where the tags live in it.
QPair<QString, QString> describe(TagLib::File *file)
{
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        const TagLib::ID3v2::Tag *id3 = mpeg->hasID3v2Tag() ? mpeg->ID3v2Tag() : nullptr;
        QString tags = id3 ? QStringLiteral("ID3v2.%1").arg(id3->header()->majorVersion()) : QString();
        if (mpeg->hasID3v1Tag())
            tags += tags.isEmpty() ? QStringLiteral("ID3v1") : QStringLiteral(", ID3v1");
        return {QStringLiteral("MP3"), tags};
    }
    if (dynamic_cast<TagLib::FLAC::File *>(file))
        return {QStringLiteral("FLAC"), tr("Vorbis comments")};
    if (dynamic_cast<TagLib::Ogg::Vorbis::File *>(file))
        return {QStringLiteral("Ogg Vorbis"), tr("Vorbis comments")};
    if (dynamic_cast<TagLib::Ogg::Opus::File *>(file))
        return {QStringLiteral("Opus"), tr("Vorbis comments")};
    if (auto *mp4 = dynamic_cast<TagLib::MP4::File *>(file)) {
        const auto *props = mp4->audioProperties();
        const bool alac = props && props->codec() == TagLib::MP4::Properties::ALAC;
        return {alac ? QStringLiteral("ALAC (MP4)") : QStringLiteral("AAC (MP4)"), tr("MP4 atoms")};
    }
    if (dynamic_cast<TagLib::RIFF::WAV::File *>(file))
        return {QStringLiteral("WAV"), QStringLiteral("ID3v2, RIFF INFO")};
    if (dynamic_cast<TagLib::RIFF::AIFF::File *>(file))
        return {QStringLiteral("AIFF"), QStringLiteral("ID3v2")};
    if (dynamic_cast<TagLib::ASF::File *>(file))
        return {QStringLiteral("WMA"), QStringLiteral("ASF")};
    return {};
}

QString formatDuration(int ms)
{
    const int s = (ms + 500) / 1000;
    const auto twoDigits = [](int n) { return QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0')); };
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(twoDigits(s / 60 % 60), twoDigits(s % 60));
    return QStringLiteral("%1:%2").arg(s / 60).arg(twoDigits(s % 60));
}

QString pathOf(const Track &track)
{
    return track.location.isLocalFile() ? track.location.toLocalFile() : QString();
}

} // namespace

TrackInfo LocalTagEditor::read(const QString &path)
{
    TrackInfo info;
    const QFileInfo file(path);
    if (!file.exists()) {
        info.error = tr("The file is missing: %1").arg(QDir::toNativeSeparators(path));
        return info;
    }

    const TagLib::FileRef ref = open(path, true);
    if (ref.isNull()) {
        info.details = {{tr("Location"), QDir::toNativeSeparators(path)},
                        {tr("Size"), QLocale().formattedDataSize(file.size())}};
        info.note = tr("NewAmp can't read the tags in this kind of file.");
        return info;
    }

    const TagLib::PropertyMap properties = ref.file()->properties();
    for (const QString &key : standardTagKeys()) {
        QStringList values;
        const TagLib::PropertyMap::ConstIterator it = properties.find(toTagString(key));
        if (it != properties.end())
            for (const TagLib::String &value : it->second)
                values << toQString(value);
        info.tags.append({key, values.join(ValueSeparator)});
    }

    const auto [format, tagTypes] = describe(ref.file());
    if (!format.isEmpty())
        info.details.append({tr("Format"), format});
    if (const TagLib::AudioProperties *audio = ref.audioProperties()) {
        if (audio->lengthInMilliseconds() > 0)
            info.details.append({tr("Length"), formatDuration(audio->lengthInMilliseconds())});
        if (audio->bitrate() > 0)
            info.details.append({tr("Bitrate"), tr("%1 kbps").arg(audio->bitrate())});
        if (audio->sampleRate() > 0)
            info.details.append({tr("Sample rate"), tr("%1 Hz").arg(audio->sampleRate())});
        if (audio->channels() > 0)
            info.details.append({tr("Channels"), audio->channels() == 1   ? tr("Mono")
                                                 : audio->channels() == 2 ? tr("Stereo")
                                                                          : QString::number(audio->channels())});
    }
    if (!tagTypes.isEmpty())
        info.details.append({tr("Tags"), tagTypes});
    info.details.append({tr("Size"), QLocale().formattedDataSize(file.size())});
    info.details.append({tr("Location"), QDir::toNativeSeparators(file.absoluteFilePath())});

    info.canEdit = !ref.file()->readOnly() && file.isWritable();
    info.note = info.canEdit ? tr("Saved into the file.") : tr("The file is read-only.");
    return info;
}

QString LocalTagEditor::write(const QString &path, const QList<TagField> &changes)
{
    TagLib::FileRef ref = open(path, false);
    if (ref.isNull())
        return tr("NewAmp can't write tags in this kind of file.");
    TagLib::File *file = ref.file();
    if (file->readOnly())
        return tr("The file is read-only.");

    TagLib::PropertyMap properties = file->properties();
    for (const TagField &change : changes) {
        const TagLib::String key = toTagString(change.key);
        const QString value = change.value.trimmed();
        if (value.isEmpty()) {
            properties.erase(key);
            continue;
        }
        TagLib::StringList values;
        for (const QString &part : value.split(ValueSeparator, Qt::SkipEmptyParts))
            values.append(toTagString(part.trimmed()));
        properties.replace(key, values);
    }
    file->setProperties(properties);

    bool saved;
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        // Keep the ID3v2 version the file already has (v2.3 is the one older
        // players read), update an ID3v1 tag only if there is one, and leave
        // any other tags alone.
        const bool v3 = mpeg->hasID3v2Tag() && mpeg->ID3v2Tag()->header()->majorVersion() == 3;
        const int tags = TagLib::MPEG::File::ID3v2 | (mpeg->hasID3v1Tag() ? TagLib::MPEG::File::ID3v1 : 0);
        saved = mpeg->save(tags, TagLib::File::StripNone, v3 ? TagLib::ID3v2::v3 : TagLib::ID3v2::v4,
                           TagLib::File::DoNotDuplicate);
    } else {
        saved = file->save();
    }
    return saved ? QString() : tr("Couldn't save the tags. Is the file open in another program?");
}

void LocalTagEditor::readInfo(const Track &track, const InfoCallback &done)
{
    const QString path = pathOf(track);
    if (path.isEmpty()) {
        TrackInfo info;
        info.error = tr("Not a local file.");
        done(info);
        return;
    }
    done(read(path));
}

void LocalTagEditor::writeTags(const Track &track, const QList<TagField> &changes, const SaveCallback &done)
{
    const QString path = pathOf(track);
    done(path.isEmpty() ? tr("Not a local file.") : write(path, changes));
}

} // namespace newamp
