# NewAmp

An open-source, Qt 6 music player in the spirit of classic WinAmp: small, fast,
skinnable, and multi-window. Out of the box it plays local files; Plex, Jellyfin,
DLNA and Spotify are optional plugins, each loaded only when you turn it on.

![NewAmp's main window, equalizer, playlist, media library and visualizer, docked together](docs/screenshots/newamp.png)

## Screenshots

NewAmp comes with seven colour themes of its own, all drawn in code:

![The main window, equalizer and playlist in each built-in theme](docs/screenshots/themes.png)

The visualizer window has feedback trails, a kaleidoscope, a tunnel,
fireworks, a Mandelbrot zoom, and big classic bars and scope:

![Each visualizer effect](docs/screenshots/visualizations.png)

## Features

- **Classic WinAmp layout.** Main window, equalizer and playlist, plus a Media
  Library and a visualizer. Windows snap to each other and to the screen edges,
  and windows docked to the main window move with it. The layout is saved
  between runs.
- **Skins.** Seven colour themes of NewAmp's own (NewAmp Slate, Amber
  Terminal, Glacier, Ember, Synthwave, Forest and Paper), all drawn in code.
  NewAmp also loads classic WinAmp `.wsz` skins and unpacked skin folders:
  right-click, then *Skins* › *Load skin...*, or drop them into the skins
  folder. *Get more skins...* opens [skins.webamp.org](https://skins.webamp.org).
  No third-party skins are bundled. Besides the bitmaps, NewAmp reads a skin's
  window shapes (`region.txt`), cursors (`.cur`), playlist and visualizer
  colours (`pledit.txt`, `viscolor.txt`), Media Library colours (`genex.bmp`)
  and the extended digits in `nums_ex.bmp`.
- **Song details.** The main window shows the title with its track number and
  length, the bitrate, sample rate and mono/stereo. Drag the title to scroll
  it; *Song title* in the right-click menu turns autoscroll, the number and
  the length on or off. Dragging the seek bar, volume or balance shows where
  it's going. The O A I D V buttons left of the time open the menu, keep the
  windows on top, show file info, toggle double size and pick the visualizer.
- **Scale.** The whole interface scales from 1× to 4×, under *Scale* in the
  right-click menu.
- **Windowshade.** The main window, equalizer and playlist each roll up to a
  slim bar: click the button beside close, double-click the title bar, or
  press Ctrl+W. The main bar keeps transport, time and seek; the equalizer's
  keeps volume and balance; the playlist's shows the current track. Windows
  docked underneath move up and down with it.
- **Time display.** Click it to switch between elapsed and remaining time.
- **Always on top.** *Options* › *Always on top* (Ctrl+A in the main window)
  keeps every NewAmp window above other apps.
- **Media keys and Now Playing.** The keyboard's play/pause, next, previous
  and stop keys control NewAmp from any app. On macOS NewAmp also shows in
  Control Centre's Now Playing, and AirPods and headphone buttons work; on
  Linux it speaks MPRIS, so the desktop's sound menu, lock screen and
  `playerctl` see and control it.
- **One player.** Opening files from Finder, Explorer or a file manager while
  NewAmp is running adds them to the running player instead of starting
  another. *Options* › *Allow multiple instances* turns this off.
- **Tray and Dock menu.** Play, pause, stop, next and previous from the
  notification area icon on Windows and Linux (*Options* › *Show tray icon*),
  or from the Dock icon's menu on macOS.
- **Local playback.** MP3, FLAC, WAV and Ogg Vorbis through NewAmp's own
  decoder, and anything else the platform can decode (AAC/M4A, ALAC, Opus...).
  Open files or folders from the right-click menu, or drop them on the main
  window or playlist.
- **Playlists.** *Load list* and *Save list* in the playlist's LIST OPTS menu.
  A playlist can mix sources: local files, web streams, and tracks from Plex,
  Jellyfin, DLNA and Spotify, kept in NewAmp's own format (`.nap`). M3U, M3U8
  and PLS open and save too, for other players; they hold local files and web
  streams only, so plugin tracks are left out of them (NewAmp says how many).
  When a track's source isn't available (its plugin is off, you're signed out,
  or the server can't be reached), the track is marked and skipped, and the
  next one plays.
- **Internet radio.** *Open URL* (Ctrl+L) in the main menu, or *Add URL* in
  the playlist, takes a stream address or a station's `.pls` / `.m3u` link.
  Station and song titles come from the stream (SHOUTcast/Icecast metadata),
  read over a second connection to the station, which costs as much data as
  the stream itself. Radio plays through the platform's player, so the
  equalizer and visualizer don't apply to it yet.
- **Equalizer.** Ten bands, preamp and balance, applied to local files and to
  music streamed from Plex, Jellyfin and DLNA servers. *Presets* loads
  NewAmp's built-in presets and your own, saves the current settings as a
  preset, and reads and writes WinAmp's `.eqf` preset files and `.q1`
  libraries. *Save > Auto-load preset for this track* remembers settings for
  the playing track; with *Auto* on they come back whenever it plays, and
  your usual settings return for tracks without one.
  The equalizer doesn't apply to Spotify: Spotify's audio is DRM-protected
  and plays inside Spotify's own player, which NewAmp only controls (play,
  pause, seek, volume) and can't process. It also doesn't apply to the
  rare streams NewAmp hands to the system player, such as a Plex server's
  transcoding fallback.
- **Visualizations.** Feedback trails, Kaleidoscope, Tunnel, Fireworks, a
  Mandelbrot zoom, and the classic Spectrum and Oscilloscope. Click the window
  for the next effect; right-click to pick one, change effect on each new
  track, or cycle every 10 to 60 seconds.
- **Playback options.** Right-click › *Playback*: gapless playback (on by
  default), crossfade from 1 to 10 seconds, ReplayGain by track or album
  (with a preamp and clipping protection; ReplayGain and R128 tags in MP3,
  FLAC, Ogg, Opus and M4A files), and which output device to play through.
  Gapless, crossfade and ReplayGain work wherever the equalizer does: local
  files and music from Plex, Jellyfin and DLNA servers. Spotify, live streams
  and anything else played through the system's media player skip them.
- **Media Library.** Browses the music the plugins below offer. Type to filter
  the current folder; press Ctrl+F (⌘F on macOS) or the *Find* button to
  search the whole library, where the service supports it.
- **Lyrics.** Synced lyrics scroll along with the song, the line being sung
  highlighted; click a line to jump to it. Lyrics come from a `.lrc` (or
  `.txt`) file with the same name as the track, or from the file's own tags
  (ID3 `USLT`/`SYLT`, Vorbis `LYRICS`, MP4). Right-click to change the text
  size, nudge the timing for badly timed files, or open a `.lrc` by hand.
  Spotify's own lyrics stay in Spotify's player: Spotify has no public API
  for them.
- **File info and tag editing.** Alt+3 (⌘I on macOS), or *File info...* in
  the playlist's right-click menu, shows the selected track's tags, format,
  bitrate and sample rate. For local files the title, artists, album, track,
  disc, year, genre and comment can be edited and saved into the file (MP3,
  FLAC, Ogg Vorbis, Opus, M4A, WAV, AIFF and WMA, through
  [TagLib](https://taglib.org)).
  Plex and Jellyfin tracks can be edited too, on your own Plex server or as a
  Jellyfin administrator. Those edits are saved in the server's library, not
  in the files. On Plex, the album title changes for the whole album, and the
  album artist, year and genre are read-only.

## Plugins

NewAmp plays local files out of the box. Everything else is an optional
plugin, built and shipped alongside the app but loaded only when you turn it
on: right-click, then *Plugins*, tick the one you want and restart NewAmp.
Each plugin then adds its own submenu to the right-click menu, for signing
in and browsing.

- **Plex.** Sign in with your plex.tv account; NewAmp finds your servers
  (local, remote or relay, whichever answers) and browses artists, albums,
  tracks and playlists. Plays are reported back to Plex, like PlexAmp. On
  macOS 15 and later, allow NewAmp on the local network when asked.
- **Jellyfin.** Sign in with a user name and password, or an API key. Servers
  on your network are found automatically. Browse and search music libraries
  and playlists; plays are reported back to Jellyfin (10.9 or later).
- **DLNA / UPnP.** Finds media servers on the local network (MiniDLNA, Plex's
  and Jellyfin's DLNA servers, most NAS boxes), browses and searches them.
  Servers that discovery can't reach can be added by address.
- **Spotify.** Browse and search your playlists, liked songs, saved albums and
  followed artists, and play them. Needs Spotify Premium. Spotify's audio is
  protected, so the equalizer and visualizer don't apply to it.
- **Sound effects** (DSP). A vocal remover, a stereo widener and headphone
  crossfeed. Once the plugin is on, switch each effect on or off under
  *DSP effects* in the right-click menu. Like the equalizer, they apply to
  everything NewAmp decodes itself, which is all but Spotify and live streams.

Plugins come in four kinds, grouped in the *Plugins* menu: music sources,
DSP effects, visualizations, and general purpose plugins that add anything
else. One plugin can be several kinds at once. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#sources-and-plugins) to write one.

### Setting up Spotify

Spotify only lets an app talk to it once someone has registered it, so each
user registers their own (it's free):

1. Open the [Spotify developer dashboard](https://developer.spotify.com/dashboard)
   and create an app.
2. Add the redirect URI `http://127.0.0.1:43827/callback`.
3. Tick *Web API*, save, and copy the app's client ID.
4. In NewAmp, open *Spotify* › *Spotify client ID...* from the right-click
   menu, paste the ID, and sign in in the browser window that opens.

On macOS, NewAmp plays Spotify itself, through the system web view, so you
don't need the Spotify app. On every platform it can also play through
Spotify Connect: the Spotify app on this computer, your phone, or a speaker.
Pick one under *Spotify* › *Play on...*.

Spotify keeps apps in development mode to five users, and lets them read only
the playlists you own or collaborate on. Other playlists still play, as a
whole.

## Keyboard

In the main window, equalizer, playlist and visualizer:

| Key | Action |
| --- | --- |
| Z, X, C, V, B | Previous, play, pause, stop, next |
| Space | Play or pause |
| ← / → | Seek back / forward 5 seconds |
| ↑ / ↓ | Volume up / down |
| J | Jump to file: search the playlist and play or queue a track |
| L | Open files (main window) |
| Ctrl+L | Open URL |
| Ctrl+Shift+L | Show or hide the Media Library |
| Ctrl+Shift+V | Show or hide the visualizer |
| Alt+L | Show or hide the lyrics |
| Alt+3 (⌘I on macOS) | File info for the selected or current track |
| Ctrl+= / Ctrl+- | Larger / smaller interface |
| Ctrl+D | Double size |
| Ctrl+W | Windowshade the focused window |
| Ctrl+A | Always on top (main window) |
| Media keys | Play/pause, stop, next, previous, from any app |

In the playlist, ↑ and ↓ move the selection, Enter plays it, Delete removes it,
Q queues it to play next (queued tracks show their place, like `[1]`) and Ctrl+A
selects everything. The REM, SEL and MISC buttons hold the classic crop, remove
duplicates and dead files, invert selection, sort, reverse and randomize
commands. In the Media Library, typing filters, Ctrl+F
searches, Enter or → opens, and ← or Backspace goes back. In File Info, Tab
moves between fields, Enter saves and Esc reverts, then closes. On macOS, Ctrl
means ⌘.

## Status and roadmap

NewAmp is in early development, but everything above works today.

Done:

1. **Foundation and local playback**: Qt 6 / C++ / CMake, skinned main window,
   NewAmp's own audio pipeline with equalizer and balance.
2. **Classic skins and multi-window**: `.wsz` skins, playlist and equalizer
   windows, snapping and docking, interface scaling.
3. **Plex (optional plugin)**, in the style of PlexAmp.
4. **Jellyfin (optional plugin)**.
5. **DLNA / UPnP (optional plugin)**.
6. **Spotify (optional plugin)**, through Spotify Connect, and NewAmp's own
   player on macOS.
7. **Media Library and visualizations**.

Next: NewAmp's own Spotify player on Windows and Linux, keeping sign-in tokens
in the system keychain rather than the settings file, and more original
themes.

## Downloads

Every push to `main` builds:

- **macOS**: a universal disk image (`NewAmp-macOS`), for Apple silicon and
  Intel.
- **Linux**: an AppImage (`NewAmp-Linux`).
- **Windows**: a zip (`NewAmp-Windows`); unzip it and run `NewAmp.exe`.

Find them under *Artifacts* on the latest run of the
[CI workflow](https://github.com/TheJoshGriffith/NewAmp/actions/workflows/ci.yml).
The builds are unsigned: on macOS, right-click the app and choose Open the
first time; on Windows, choose More info, then Run anyway.

## Building

Requires Qt 6.4+ (Core, Gui, Widgets, Multimedia, Network) and CMake 3.21+.
zlib comes from the system where there is one and is built from source
otherwise (Windows). TagLib is downloaded and built as part of the build, with
only the formats NewAmp plays.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The screenshots above are rendered headless by a small tool. To refresh them,
configure with `-DNEWAMP_BUILD_SCREENSHOTS=ON`, build, and run
`QT_QPA_PLATFORM=offscreen build/newamp_screenshots docs/screenshots`. With an
audio device (a PulseAudio null sink is enough), the time and meters show
playback.

On macOS with Homebrew: `brew install qt cmake`, then pass
`-DCMAKE_PREFIX_PATH=$(brew --prefix qt)` to the first command. On Windows, use
the Qt online installer (MSVC 2022 64-bit, plus Qt Multimedia) and build from a
Visual Studio developer prompt with `-G Ninja`.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
