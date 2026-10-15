#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

class QPluginLoader;

namespace newamp {

class MediaSource;
class PluginHost;
class SourcePlugin;

struct PluginInfo
{
    QString id;
    QString name;
    QString description;
    QString filePath;
};

struct LoadedPlugin
{
    PluginInfo info;
    SourcePlugin *plugin = nullptr;
};

// Finds source plugins and loads only the ones the user has enabled.
// Discovery reads plugin metadata without loading the library.
class PluginManager
{
public:
    PluginManager();
    ~PluginManager();

    // Defaults: the bundle's PlugIns/newamp (macOS) or <app dir>/plugins,
    // plus any paths in NEWAMP_PLUGIN_PATH.
    static QStringList defaultSearchPaths();

    void discover(const QStringList &searchPaths = defaultSearchPaths());
    const QList<PluginInfo> &available() const { return m_available; }

    // Loads each enabled plugin, initialises it with the host (when given)
    // and returns the sources they provide.
    QList<MediaSource *> loadEnabled(const QSet<QString> &enabledIds, PluginHost *host = nullptr);
    const QList<LoadedPlugin> &loaded() const { return m_loaded; }

private:
    QList<PluginInfo> m_available;
    QList<QPluginLoader *> m_loaders;
    QList<LoadedPlugin> m_loaded;
};

} // namespace newamp
