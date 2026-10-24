#pragma once

#include "core/MetadataEditor.h"

namespace newamp {

// Tags of local files through TagLib: ID3v1/v2 (MP3, WAV, AIFF), Vorbis
// comments (FLAC, Ogg Vorbis, Opus), MP4 atoms (M4A) and ASF (WMA). Both
// calls finish before they return.
class LocalTagEditor final : public MetadataEditor
{
public:
    void readInfo(const Track &track, const InfoCallback &done) override;
    void writeTags(const Track &track, const QList<TagField> &changes, const SaveCallback &done) override;

    // The same for a path, for tests and the editor itself.
    static TrackInfo read(const QString &path);
    // Returns an error, or an empty string once saved.
    static QString write(const QString &path, const QList<TagField> &changes);
};

} // namespace newamp
