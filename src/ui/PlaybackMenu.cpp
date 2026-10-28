#include "ui/PlaybackMenu.h"

#include "core/PlaybackSettings.h"

#include <QActionGroup>
#include <QAudioDevice>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QMenu>

namespace newamp {

namespace {

QString tr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("PlaybackMenu", text, disambiguation, n);
}

QAction *addChoice(QMenu *menu, QActionGroup *group, const QString &text, bool checked)
{
    QAction *action = menu->addAction(text);
    action->setCheckable(true);
    action->setChecked(checked);
    group->addAction(action);
    return action;
}

} // namespace

void populatePlaybackMenu(QMenu *menu, PlaybackSettings &settings)
{
    const PlaybackOptions &options = settings.options();
    PlaybackSettings *target = &settings;

    QAction *gapless = menu->addAction(tr("Gapless playback"));
    gapless->setCheckable(true);
    gapless->setChecked(options.gapless);
    QObject::connect(gapless, &QAction::toggled, target, &PlaybackSettings::setGapless);

    QMenu *crossfade = menu->addMenu(tr("Crossfade"));
    auto *fades = new QActionGroup(crossfade);
    for (const int seconds : {0, 1, 2, 3, 5, 8, 10}) {
        const QString text = seconds == 0 ? tr("Off") : tr("%n second(s)", nullptr, seconds);
        QAction *action = addChoice(crossfade, fades, text, options.crossfadeMs == seconds * 1000);
        QObject::connect(action, &QAction::triggered, target, [target, seconds] { target->setCrossfadeMs(seconds * 1000); });
    }

    using Mode = PlaybackOptions::ReplayGain;
    QMenu *replayGain = menu->addMenu(tr("ReplayGain"));
    auto *modes = new QActionGroup(replayGain);
    const std::pair<Mode, QString> Modes[] = {
        {Mode::Off, tr("Off")},
        {Mode::Track, tr("Track gain")},
        {Mode::Album, tr("Album gain")},
    };
    for (const auto &[mode, text] : Modes) {
        QAction *action = addChoice(replayGain, modes, text, options.replayGain == mode);
        QObject::connect(action, &QAction::triggered, target, [target, mode = mode] { target->setReplayGain(mode); });
    }
    replayGain->addSeparator();
    QMenu *preamp = replayGain->addMenu(tr("Preamp"));
    preamp->setEnabled(options.replayGain != Mode::Off);
    auto *preamps = new QActionGroup(preamp);
    for (const int db : {6, 3, 0, -3, -6, -9, -12}) {
        const QString text = db == 0 ? tr("0 dB") : QStringLiteral("%1%2 dB").arg(db > 0 ? QLatin1String("+") : QLatin1String()).arg(db);
        QAction *action = addChoice(preamp, preamps, text, qFuzzyCompare(options.replayGainPreampDb + 100, db + 100.0));
        QObject::connect(action, &QAction::triggered, target, [target, db] { target->setReplayGainPreamp(db); });
    }
    QAction *clipping = replayGain->addAction(tr("Prevent clipping"));
    clipping->setCheckable(true);
    clipping->setChecked(options.preventClipping);
    clipping->setEnabled(options.replayGain != Mode::Off);
    QObject::connect(clipping, &QAction::toggled, target, &PlaybackSettings::setPreventClipping);

    menu->addSeparator();
    QMenu *output = menu->addMenu(tr("Output device"));
    auto *devices = new QActionGroup(output);
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    bool chosenPresent = false;
    for (const QAudioDevice &device : outputs)
        chosenPresent = chosenPresent || device.id() == options.outputDevice;
    QAction *system = addChoice(output, devices, tr("System default"), options.outputDevice.isEmpty() || !chosenPresent);
    QObject::connect(system, &QAction::triggered, target, [target] { target->setOutputDevice({}); });
    output->addSeparator();
    for (const QAudioDevice &device : outputs) {
        QAction *action = addChoice(output, devices, device.description(), device.id() == options.outputDevice);
        QObject::connect(action, &QAction::triggered, target, [target, id = device.id()] { target->setOutputDevice(id); });
    }
    if (outputs.isEmpty())
        output->addAction(tr("No output devices found"))->setEnabled(false);
}

} // namespace newamp
