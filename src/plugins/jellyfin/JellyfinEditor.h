#pragma once

#include "core/MetadataEditor.h"

#include <QObject>

#include <optional>

namespace newamp::jellyfin {

class JellyfinClient;

// File Info for Jellyfin tracks. Edits go to the server's library, not into
// the file, and only an administrator may make them.
class JellyfinEditor final : public QObject, public MetadataEditor
{
    Q_OBJECT

public:
    JellyfinEditor(JellyfinClient &client, QObject *parent = nullptr);

    void readInfo(const Track &track, const InfoCallback &done) override;
    void writeTags(const Track &track, const QList<TagField> &changes, const SaveCallback &done) override;

private:
    // Whether the signed-in user may edit; asked once per sign-in.
    void checkAdministrator(const std::function<void(bool)> &done);

    JellyfinClient &m_client;
    std::optional<bool> m_isAdministrator;
};

} // namespace newamp::jellyfin
