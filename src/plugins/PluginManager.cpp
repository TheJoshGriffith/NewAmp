#include "plugins/PluginManager.h"

#include "plugins/SourcePlugin.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>

namespace newamp {

PluginManager::PluginManager() = default;

PluginManager::~PluginManager()
{
    m_loaded.clear();
    for (QPluginLoader *loader : m_loaders) {
        loader->unload();
        delete loader;
    }
}

QStringList PluginManager::defaultSearchPaths()
{
    QStringList paths;
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    paths << QDir(appDir).absoluteFilePath(QStringLiteral("../PlugIns/newamp"));
#endif
    paths << QDir(appDir).absoluteFilePath(QStringLiteral("plugins"));
    const QString extra = qEnvironmentVariable("NEWAMP_PLUGIN_PATH");
    if (!extra.isEmpty())
        paths += extra.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    return paths;
}

void PluginManager::discover(const QStringList &searchPaths)
{
    m_available.clear();
    for (const QString &path : searchPaths) {
        const QDir dir(path);
        for (const QString &file : dir.entryList(QDir::Files)) {
            const QString filePath = dir.absoluteFilePath(file);
            if (!QLibrary::isLibrary(filePath))
                continue;
            const QJsonObject meta = QPluginLoader(filePath).metaData();
            if (meta.value(QStringLiteral("IID")).toString() != QLatin1String(NewAmpSourcePlugin_iid))
                continue;
            const QJsonObject data = meta.value(QStringLiteral("MetaData")).toObject();
            PluginInfo info;
            info.id = data.value(QStringLiteral("id")).toString();
            info.name = data.value(QStringLiteral("name")).toString();
            info.description = data.value(QStringLiteral("description")).toString();
            info.filePath = filePath;
            if (!info.id.isEmpty())
                m_available << info;
        }
    }
}

QList<MediaSource *> PluginManager::loadEnabled(const QSet<QString> &enabledIds, PluginHost *host)
{
    QList<MediaSource *> sources;
    for (const PluginInfo &info : m_available) {
        if (!enabledIds.contains(info.id))
            continue;
        auto *loader = new QPluginLoader(info.filePath);
        auto *plugin = qobject_cast<SourcePlugin *>(loader->instance());
        if (!plugin) {
            qWarning("NewAmp: failed to load plugin %s: %s", qPrintable(info.filePath),
                     qPrintable(loader->errorString()));
            delete loader;
            continue;
        }
        m_loaders << loader;
        m_loaded << LoadedPlugin{info, plugin};
        if (host)
            plugin->initialize(*host);
        if (MediaSource *source = plugin->source())
            sources << source;
    }
    return sources;
}

} // namespace newamp
