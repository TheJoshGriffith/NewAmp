#include "ui/UpdatePrompt.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTimer>

namespace newamp {

namespace {

const QString AutomaticKey = QStringLiteral("updates/automatic");
const QString PrereleasesKey = QStringLiteral("updates/prereleases");
const QString LastCheckKey = QStringLiteral("updates/lastCheck");
const QString SkippedKey = QStringLiteral("updates/skipped");

constexpr int StartupDelayMs = 15000;
constexpr qint64 CheckIntervalSecs = 24 * 60 * 60;

} // namespace

UpdatePrompt::UpdatePrompt(Updater &updater, QWidget *dialogParent)
    : QObject(dialogParent)
    , m_updater(updater)
    , m_parent(dialogParent)
{
    QSettings settings;
    // Someone running a release candidate keeps getting them until they say
    // otherwise.
    m_updater.setIncludePrereleases(
        settings.value(PrereleasesKey, Updater::isPrerelease(updater.currentVersion())).toBool());

    auto *checkNow = new QAction(tr("Check for updates..."), this);
    connect(checkNow, &QAction::triggered, this, [this] { check(true); });
    auto *automatic = new QAction(tr("Check for updates automatically"), this);
    automatic->setCheckable(true);
    automatic->setChecked(settings.value(AutomaticKey, true).toBool());
    connect(automatic, &QAction::toggled, this, [](bool on) { QSettings().setValue(AutomaticKey, on); });
    auto *prereleases = new QAction(tr("Include release candidates"), this);
    prereleases->setCheckable(true);
    prereleases->setChecked(m_updater.includePrereleases());
    connect(prereleases, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(PrereleasesKey, on);
        m_updater.setIncludePrereleases(on);
    });
    m_actions = {checkNow, automatic, prereleases};

    connect(&m_updater, &Updater::updateAvailable, this, [this](const UpdateRelease &release) {
        QSettings().setValue(LastCheckKey, QDateTime::currentDateTimeUtc());
        if (m_manual || QSettings().value(SkippedKey).toString() != release.version)
            offer(release);
    });
    connect(&m_updater, &Updater::upToDate, this, [this] {
        QSettings().setValue(LastCheckKey, QDateTime::currentDateTimeUtc());
        if (m_manual)
            QMessageBox::information(m_parent, tr("NewAmp updates"),
                                     tr("NewAmp %1 is the newest version.").arg(m_updater.currentVersion()));
    });
    // Automatic checks fail quietly: being offline isn't worth a dialog.
    connect(&m_updater, &Updater::checkFailed, this, [this](const QString &error) {
        if (m_manual)
            QMessageBox::warning(m_parent, tr("NewAmp updates"), tr("Could not check for updates: %1").arg(error));
    });
    connect(&m_updater, &Updater::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (!m_progress)
            return;
        m_progress->setMaximum(total > 0 ? 1000 : 0);
        if (total > 0)
            m_progress->setValue(int(received * 1000 / total));
    });
    connect(&m_updater, &Updater::downloadFailed, this, [this](const QString &error) {
        delete m_progress;
        showError(error, m_downloading);
    });
    connect(&m_updater, &Updater::downloaded, this, [this] {
        if (!m_progress)
            return; // cancelled
        m_progress->setLabelText(tr("Installing NewAmp %1...").arg(m_downloading.version));
        m_progress->setCancelButton(nullptr);
        QApplication::processEvents();
        QString error;
        const bool installed = m_updater.install(&error);
        delete m_progress;
        if (installed)
            QApplication::quit(); // the helper starts the new version
        else
            showError(error, m_downloading);
    });
}

void UpdatePrompt::checkLater()
{
    QSettings settings;
    if (!settings.value(AutomaticKey, true).toBool())
        return;
    const QDateTime last = settings.value(LastCheckKey).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < CheckIntervalSecs)
        return;
    QTimer::singleShot(StartupDelayMs, this, [this] { check(false); });
}

void UpdatePrompt::check(bool manual)
{
    m_manual = manual;
    m_updater.check();
}

void UpdatePrompt::offer(const UpdateRelease &release)
{
    const bool canInstall = release.asset.isValid() && m_updater.canInstall();
    QMessageBox box(m_parent);
    box.setWindowTitle(tr("NewAmp updates"));
    box.setIcon(QMessageBox::Information);
    box.setText((release.prerelease ? tr("NewAmp %1, a release candidate, is available. You have %2.")
                                    : tr("NewAmp %1 is available. You have %2."))
                    .arg(release.version, m_updater.currentVersion()));
    box.setInformativeText(canInstall ? tr("Install it now? NewAmp will restart.")
                                      : tr("Download it from GitHub?"));
    if (!release.notes.isEmpty())
        box.setDetailedText(release.notes);
    QPushButton *accept = box.addButton(canInstall ? tr("Install and restart") : tr("Open download page"),
                                        QMessageBox::AcceptRole);
    QPushButton *skip = box.addButton(tr("Skip this version"), QMessageBox::DestructiveRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(accept);
    box.exec();
    if (box.clickedButton() == skip) {
        QSettings().setValue(SkippedKey, release.version);
    } else if (box.clickedButton() == accept) {
        if (canInstall)
            install(release);
        else
            QDesktopServices::openUrl(release.page);
    }
}

void UpdatePrompt::install(const UpdateRelease &release)
{
    m_downloading = release;
    m_progress = new QProgressDialog(tr("Downloading NewAmp %1...").arg(release.version), tr("Cancel"), 0, 0, m_parent);
    m_progress->setWindowTitle(tr("NewAmp updates"));
    m_progress->setAttribute(Qt::WA_DeleteOnClose);
    m_progress->setMinimumDuration(0);
    connect(m_progress, &QProgressDialog::canceled, &m_updater, &Updater::cancel);
    connect(m_progress, &QProgressDialog::canceled, m_progress, &QWidget::close);
    m_progress->show();
    m_updater.download(release);
}

void UpdatePrompt::showError(const QString &error, const UpdateRelease &release)
{
    QMessageBox box(QMessageBox::Warning, tr("NewAmp updates"), tr("Could not install the update: %1").arg(error),
                    QMessageBox::Close, m_parent);
    QPushButton *page = release.page.isValid() ? box.addButton(tr("Open download page"), QMessageBox::AcceptRole)
                                               : nullptr;
    box.exec();
    if (page && box.clickedButton() == page)
        QDesktopServices::openUrl(release.page);
}

} // namespace newamp
