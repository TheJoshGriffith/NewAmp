#pragma once

#include "core/Updater.h"

#include <QObject>
#include <QPointer>
#include <QProgressDialog>

class QAction;
class QWidget;

namespace newamp {

// The updater's side of the interface: menu actions, the once a day check,
// and the dialogs that offer, download and install a release.
class UpdatePrompt : public QObject
{
    Q_OBJECT

public:
    UpdatePrompt(Updater &updater, QWidget *dialogParent);

    // Check now, check automatically, and include release candidates.
    QList<QAction *> actions() const { return m_actions; }
    // Checks a little after startup, at most once a day, unless turned off.
    void checkLater();

private:
    void check(bool manual);
    void offer(const UpdateRelease &release);
    void install(const UpdateRelease &release);
    void showError(const QString &error, const UpdateRelease &release);

    Updater &m_updater;
    QWidget *m_parent;
    QList<QAction *> m_actions;
    QPointer<QProgressDialog> m_progress;
    UpdateRelease m_downloading;
    bool m_manual = false;
};

} // namespace newamp
