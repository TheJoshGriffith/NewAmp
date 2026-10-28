#include "PlexEditor.h"

#include "PlexApi.h"
#include "PlexClient.h"

namespace newamp::plex {

namespace {

QString refusal(int status)
{
    return status == 401 || status == 403 ? PlexEditor::tr("Only the server's owner can edit tags in Plex.")
                                          : QString();
}

} // namespace

PlexEditor::PlexEditor(PlexClient &client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

void PlexEditor::readInfo(const Track &track, const InfoCallback &done)
{
    const TrackRef ref = parseTrackLocation(track.location);
    const PlexClient::ServerInfo *server = m_client.server(ref.machineIdentifier);
    if (ref.ratingKey.isEmpty() || !server) {
        TrackInfo info;
        info.error = tr("The Plex server for this track isn't connected.");
        done(info);
        return;
    }
    const QString serverName = server->name;
    const bool owned = server->owned;
    m_client.send(ref.machineIdentifier, "GET", metadataPath(ref.ratingKey), this,
                  [serverName, owned, done](int, const QByteArray &body, const QString &error) {
                      TrackInfo info;
                      if (!error.isEmpty()) {
                          info.error = tr("Plex request failed: %1").arg(error);
                          done(info);
                          return;
                      }
                      EditTarget target;
                      info = parseTrackInfo(body, &target);
                      if (info.error.isEmpty()) {
                          info.details.prepend({tr("Server"), serverName});
                          info.canEdit = owned && !target.sectionId.isEmpty();
                          info.note = owned ? tr("Saved to Plex, not to the file.")
                                            : tr("Only the server's owner can edit tags in Plex.");
                      }
                      done(info);
                  });
}

void PlexEditor::writeTags(const Track &track, const QList<TagField> &changes, const SaveCallback &done)
{
    const TrackRef ref = parseTrackLocation(track.location);
    if (ref.ratingKey.isEmpty() || !m_client.server(ref.machineIdentifier)) {
        done(tr("The Plex server for this track isn't connected."));
        return;
    }
    const QString machine = ref.machineIdentifier;
    // The section and the album's keys aren't in the location; ask for them.
    m_client.send(machine, "GET", metadataPath(ref.ratingKey), this,
                  [this, machine, changes, done](int, const QByteArray &body, const QString &error) {
                      EditTarget target;
                      if (!error.isEmpty() || !parseTrackInfo(body, &target).error.isEmpty()
                          || target.sectionId.isEmpty()) {
                          done(tr("Plex request failed: %1").arg(error.isEmpty() ? tr("no track details") : error));
                          return;
                      }
                      // One PUT for the track and one for its album, in turn.
                      putEach(machine, editPaths(target, changes), done);
                  });
}

void PlexEditor::putEach(const QString &machineIdentifier, QStringList paths, const SaveCallback &done)
{
    if (paths.isEmpty()) {
        done({});
        return;
    }
    const QString path = paths.takeFirst();
    m_client.send(machineIdentifier, "PUT", path, this,
                  [this, machineIdentifier, paths, done](int status, const QByteArray &, const QString &error) {
                      if (error.isEmpty()) {
                          putEach(machineIdentifier, paths, done);
                          return;
                      }
                      const QString refused = refusal(status);
                      done(refused.isEmpty() ? tr("Plex didn't save the tags: %1").arg(error) : refused);
                  });
}

} // namespace newamp::plex
