#pragma once

#include "core/MetadataEditor.h"

#include <QObject>

namespace newamp::plex {

class PlexClient;

// File Info for Plex tracks. Edits go to the server's database, not into
// the file, and only on the account's own servers.
class PlexEditor final : public QObject, public MetadataEditor
{
    Q_OBJECT

public:
    PlexEditor(PlexClient &client, QObject *parent = nullptr);

    void readInfo(const Track &track, const InfoCallback &done) override;
    void writeTags(const Track &track, const QList<TagField> &changes, const SaveCallback &done) override;

private:
    void putEach(const QString &machineIdentifier, QStringList paths, const SaveCallback &done);

    PlexClient &m_client;
};

} // namespace newamp::plex
