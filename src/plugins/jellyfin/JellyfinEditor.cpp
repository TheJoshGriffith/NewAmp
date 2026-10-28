#include "JellyfinEditor.h"

#include "JellyfinApi.h"
#include "JellyfinClient.h"

namespace newamp::jellyfin {

JellyfinEditor::JellyfinEditor(JellyfinClient &client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    connect(&client, &JellyfinClient::signedInChanged, this, [this] { m_isAdministrator.reset(); });
}

void JellyfinEditor::checkAdministrator(const std::function<void(bool)> &done)
{
    if (m_client.usesApiKey())
        m_isAdministrator = true; // only administrators can make API keys
    if (m_isAdministrator) {
        done(*m_isAdministrator);
        return;
    }
    m_client.send("GET", QStringLiteral("/Users/Me"), {}, this,
                  [this, done](int, const QByteArray &body, const QString &error) {
                      // A failed check isn't remembered, so the next look asks again.
                      if (error.isEmpty())
                          m_isAdministrator = parseIsAdministrator(body);
                      done(error.isEmpty() && parseIsAdministrator(body));
                  });
}

void JellyfinEditor::readInfo(const Track &track, const InfoCallback &done)
{
    const TrackRef ref = m_client.playable(track);
    if (!ref.isValid()) {
        TrackInfo info;
        info.error = tr("This track isn't from the Jellyfin server you're signed in to.");
        done(info);
        return;
    }
    m_client.send("GET", itemPath(m_client.userId(), ref.itemId), {}, this,
                  [this, done](int, const QByteArray &body, const QString &error) {
                      TrackInfo info;
                      if (!error.isEmpty()) {
                          info.error = tr("Jellyfin request failed: %1").arg(error);
                          done(info);
                          return;
                      }
                      info = parseItemInfo(body);
                      if (!info.error.isEmpty()) {
                          done(info);
                          return;
                      }
                      info.details.prepend({tr("Server"), m_client.serverName()});
                      checkAdministrator([info, done](bool admin) mutable {
                          info.canEdit = admin;
                          info.note = admin ? tr("Saved to Jellyfin's library, not to the file.")
                                            : tr("Only a Jellyfin administrator can edit tags.");
                          done(info);
                      });
                  });
}

void JellyfinEditor::writeTags(const Track &track, const QList<TagField> &changes, const SaveCallback &done)
{
    const TrackRef ref = m_client.playable(track);
    if (!ref.isValid()) {
        done(tr("This track isn't from the Jellyfin server you're signed in to."));
        return;
    }
    // Fetch the item fresh: Jellyfin replaces it with what it's sent.
    m_client.send(
        "GET", itemPath(m_client.userId(), ref.itemId), {}, this,
        [this, ref, changes, done](int, const QByteArray &body, const QString &error) {
            const QByteArray item = error.isEmpty() ? editedItem(body, changes) : QByteArray();
            if (item.isEmpty()) {
                done(tr("Jellyfin request failed: %1").arg(error.isEmpty() ? tr("no item") : error));
                return;
            }
            m_client.send("POST", updateItemPath(ref.itemId), item, this,
                          [done](int status, const QByteArray &, const QString &error) {
                              if (status == 401 || status == 403)
                                  done(tr("Only a Jellyfin administrator can edit tags."));
                              else if (!error.isEmpty())
                                  done(tr("Jellyfin didn't save the tags: %1").arg(error));
                              else
                                  done({});
                          });
        });
}

} // namespace newamp::jellyfin
