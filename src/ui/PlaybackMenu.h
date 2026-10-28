#pragma once

class QMenu;

namespace newamp {

class PlaybackSettings;

// Fills a menu with the playback options: crossfade, gapless, ReplayGain
// and the output device. Built each time the menu opens, so the device list
// is current.
void populatePlaybackMenu(QMenu *menu, PlaybackSettings &settings);

} // namespace newamp
