# Architecture

NewAmp is split into small layers. Each one depends only on the layers above it.

```
audio/    AudioBackend, DspBackend (decoder -> DSP -> sink), MediaPlayerBackend, DSP, tags, spectrum
core/     Track, MediaSource, Playlist, PlaybackEngine, PlayerController, EqualizerSettings
plugins/  SourcePlugin, PluginHost, DspEffect, Visualization, PluginManager; optional plugins
skin/     Skin (sprite sheets + bitmap font), SkinSprites (classic geometry), DefaultSkin
ui/       SkinElement and its subclasses, SkinnedWindow, WindowManager, concrete windows
```

## Sources and plugins

Every track belongs to a `MediaSource`, which turns it into a stream URL when it
is played. Local files are built in (`LocalFileSource`). Everything else, such as
Plex and Spotify, is a Qt plugin implementing `SourcePlugin`.

`PluginManager` reads each plugin's JSON metadata without loading the library,
and loads only the plugins listed in the `plugins/enabled` setting. A default
install therefore runs no plugin code at all.

Plugins are searched for in `NewAmp.app/Contents/PlugIns/newamp` on macOS,
`<app dir>/plugins` elsewhere, and any paths in `NEWAMP_PLUGIN_PATH`. The build
copies each plugin there, and CI passes them to `macdeployqt` so the `.dmg`
carries their Qt dependencies.

A loaded plugin is handed a `PluginHost` (`initialize()`). Through it a plugin
adds or plays tracks, parents its dialogs, keeps settings in its own group
(`plugins/<id>/...`) and registers a `PlaybackObserver` for track started,
state, position and track finished events. `PluginHost` is header-only and not
a `QObject`, so plugins need no symbols from the executable. `AppPluginHost`
implements it from the playlist and engine signals. A plugin's `actions()`
appear in a submenu of the main window's context menu, and the "Plugins"
submenu toggles `plugins/enabled` (applied on restart).

A plugin with music to browse registers a `LibraryProvider` with
`addLibrary()`. The provider only answers questions (the items in a folder,
every track under a folder, a status line, menu actions); the host draws it in
the skinned Media Library window, so no plugin carries its own UI.

### Plugin kinds

Every plugin implements the same `SourcePlugin` interface and is whatever it
offers to the host, so one plugin can be several kinds at once. Its JSON
`category` only groups it in the Plugins menu.

| Kind | Offers | Winamp's name |
| --- | --- | --- |
| Source | `source()`, usually with `addLibrary()` | input |
| DSP | `addDspEffect(DspEffect *)` | dsp_ |
| Visualization | `addVisualization(Visualization *)` | vis_ |
| General | anything else: observers, `actions()` | gen_ |

`DspEffect` (header-only) processes interleaved float frames in place on the
audio thread. `DspRack`, owned by the engine, holds every effect and whether
it is on (`dsp/enabled` in settings), and `DspBackend` runs the enabled ones,
in order, after decoding and before the equalizer, balance and volume. The rack
configures an effect before its first buffer and on format changes, and resets
it on seeks and new tracks. Effects can't change the number of frames, and
like the equalizer they don't hear tracks played by `QMediaPlayer` or a
`RemotePlayer`. The main window's *DSP effects* menu switches them on and off
and shows each effect's own `actions()`.

`Visualization` (header-only) draws a frame into an RGB32 `QImage` from a
`VisualizationInput` (waveform, spectrum, bass/mid/treble, beats). The
Visualization window lists plugin visualizations after its own effects, gives
them an image at screen resolution, and calls `activate()` before the first
frame and `deactivate()` when another is picked or the window hides, so a
heavy one (a GL context) costs nothing until it is used.

`plugins/effects` builds `newamp_effects`, a small DSP plugin (vocal remover,
stereo widener, headphone crossfeed) that also serves as the example.

### Plex

`plugins/plex` builds `newamp_plex`.

- `PlexApi`: pure JSON parsing and URL building, unit-tested from fixtures.
- `PlexClient`: plex.tv PIN sign-in, server discovery (each connection is
  probed in order local, remote, relay), JSON requests (a request whose
  connection stops answering finds another and retries once); also the
  `MediaSource` that turns a track into a direct-play URL, with the server's
  universal transcoder (HLS, played by `QMediaPlayer`) as its fallback.
- `PlexReporter`: timeline reports (every 10 s and on state changes) and a
  scrobble once a track passes 90 %.
- `PlexLibrary`: the `LibraryProvider` for servers, music sections, artists,
  albums, tracks and audio playlists. Folder ids carry their server, so
  several servers can be browsed without a "current" one.

Plex tracks are stored as `plex://<machineIdentifier><partKey>?ratingKey=<key>`,
so they are resolved against whichever connection to that server works now.
The auth token lives in the plugin's settings group for now.

### Jellyfin

`plugins/jellyfin` builds `newamp_jellyfin`, laid out like the Plex plugin.

- `JellyfinApi`: pure JSON parsing and URL building, unit-tested from fixtures.
- `JellyfinClient`: one server at a time. Sign-in checks
  `/System/Info/Public`, then `/Users/AuthenticateByName` with a password, or
  `/Users` with an API key (which belongs to no user, so NewAmp browses as the
  named user or the first). Every request sends the `MediaBrowser`
  Authorization header. The `MediaSource` streams the original file from
  `/Audio/<id>/stream.<container>?static=true`, so the suffix picks the
  decoder, with the server's MP3 conversion as fallback.
- `JellyfinReporter`: `/Sessions/Playing`, `/Progress` (every 10 s, pause,
  unpause) and `/Stopped`; Jellyfin marks a track played when it stops near
  the end.
- `JellyfinLibrary`: music libraries, album artists, albums, tracks and audio
  playlists, plus search (`/Items` for albums, playlists and tracks, `/Artists`
  for artists, within the library searched from). Targets Jellyfin 10.9+
  (`/UserViews`).
- `JellyfinSignInDialog`: address, user name, password or API key. Servers
  that answer the UDP discovery broadcast on port 7359 fill the address list.

Jellyfin tracks are stored as `jellyfin://<serverId>/<itemId>?container=<type>`.

### DLNA

`plugins/dlna` builds `newamp_dlna` for UPnP AV media servers (MiniDLNA,
Plex's and Jellyfin's DLNA servers, NAS boxes).

- `DlnaApi`: pure SSDP, device description, SOAP and DIDL-Lite parsing and
  building, unit-tested from fixtures.
- `DlnaClient`: SSDP M-SEARCH on every multicast interface, plus NOTIFY
  announcements when port 1900 can be shared; servers expire after their
  max-age. Sends ContentDirectory `Browse`, `Search` and
  `GetSearchCapabilities`. Servers discovery can't reach are added by
  description URL. The `MediaSource` plays the item's first audio resource,
  moved to the server's current host, with another resource (often an MP3
  conversion) as fallback.
- `DlnaLibrary`: one folder per server, then its containers, browsed in pages.
  Search uses only the properties the server can search on, within the folder
  searched from.

DLNA tracks are stored as `dlna:track?udn=…&res=…&alt=…&host=…`.

### Spotify

`plugins/spotify` builds `newamp_spotify`.

- `SpotifyApi`: pure helpers, unit-tested from fixtures: OAuth with PKCE (no
  client secret), JSON parsing for playlists, saved tracks and albums,
  followed artists, search, devices and playback state, and ids.
- `SpotifyClient`: sign-in through the browser with a loopback redirect to
  `http://127.0.0.1:43827/callback`, token refresh, and Web API requests
  (401 refreshes and retries once, 429 waits for `Retry-After`). Also the
  `MediaSource` for `spotify:track:<id>` tracks.
- `SpotifyConnect`: the source's `RemotePlayer`. It plays tracks on a
  Spotify Connect device by remote control, always naming the device: the
  user's choice, else the Spotify app on this computer (matched by host
  name), else the active one (`pickDevice()`). Commands go one at a time and
  in order; it polls the device to report position and the end of each
  track, and fails the track with what the device reports if it never
  starts it. `NEWAMP_SPOTIFY_DEBUG=1` logs every request and answer.
- `SpotifyLibrary`: the `LibraryProvider` for playlists, liked songs, saved
  albums, followed artists (and their albums) and search.

Spotify's audio is protected, so it never passes through NewAmp: there is no
equalizer or visualizer for it, and playback needs Premium. Each user
registers their own app in Spotify's developer dashboard and enters its
client ID (development-mode apps take five users; a build can carry a
default with `-DNEWAMP_SPOTIFY_CLIENT_ID=...`). Development-mode apps may only
read the contents of playlists the user owns or collaborates on, so any
other playlist opens as one entry, `spotify:playlist:<id>`, that the device
plays as a whole (followed by its playback context). Search gets at most 10
results of each kind. The refresh token lives in the
plugin's settings group for now.

- `SpotifyWebPlayer`: NewAmp's own player. Spotify's Web Playback SDK runs
  in a hidden system web view (`WebView`; WKWebView in
  `SpotifyWebView_mac.mm`, none elsewhere yet) and registers as the Connect
  device "NewAmp". `SpotifyConnect` prefers it whenever the user hasn't
  picked a device, so no Spotify app is needed. The page reports the SDK's
  errors and which DRM systems the web view has; they reach the status line.

## Playback

`PlaybackEngine` picks an `AudioBackend` per track. Local files go through
`DspBackend`, NewAmp's own pipeline: a `PcmSource` feeds `DspChain` (ten-band
equalizer, preamp, balance, volume), which a `QAudioSink` pulls from. It also
keeps recent samples for the visualizer. MP3, FLAC, WAV and Ogg Vorbis are
decoded by `AudioDecoder` (miniaudio's decoders, vendored in
`third_party/miniaudio`), with sample-accurate seeking. Anything else the
platform can decode, such as AAC/M4A, ALAC or Opus, comes from
`QtDecoderSource`: `QAudioDecoder` decodes the whole file in the background (on
the main thread, through its signals) into memory as 16-bit samples, about
10 MB a minute, freed on stop. Playback starts as soon as the first audio
arrives; a seek past what has been decoded so far stops at the decoded end.
HTTP(S) tracks, such as Plex direct-play URLs, are fetched whole with Qt
Network and then decoded from memory the same way, so they get the equalizer
and visualizer too. Only what neither decoder handles falls back to
`MediaPlayerBackend` (`QMediaPlayer`), where only volume applies; for a remote
track that is decided once it has arrived (`DspBackend::cannotDecode`).
Decoding through `QAudioDecoder` processes events while it waits for the first
audio, so a request for another track that arrives meanwhile replaces the one
being decoded rather than re-entering it. When a track fails to load, the
engine tries once more with its source's `fallbackUrl()`, if it offers one,
before reporting an error. A source whose audio NewAmp can't decode at all
(Spotify) offers a `RemotePlayer` (`MediaSource::remotePlayer()`), a
header-only interface the engine drives through `RemoteBackend`; only volume
applies there. HLS and DASH playlists always go to
`QMediaPlayer`. `TagReader`
reads ID3, FLAC, Vorbis and MP4 (`ilst`) tags for the DSP path.

`PlayerController` holds the transport
rules (what play, pause, next and end-of-track mean) so every window behaves the
same. Windows call the controller, never the engine's transport directly.

## Skins

`SkinSprites.h` records the classic skin layout: which bitmap each sprite is in
and where it is drawn. The built-in skin is drawn in code by `DefaultSkin` in
exactly that layout, in several colour themes, so built-in and loaded `.wsz` skins share one renderer. No
third-party artwork is shipped.

`SkinLoader` reads `.wsz` archives (with a small bounds-checked ZIP reader,
`ZipReader`) or unpacked skin folders. File names are matched case-insensitively
and anywhere inside the archive. Missing bitmaps fall back to the default skin,
and `pledit.txt` sets the playlist colours. `gen.bmp` frames every window
beyond the classic three; its variable-width title letters are found by
scanning the bitmap.

## Windows

Every window derives from `SkinnedWindow`, which handles the frameless window,
scaling ("double size"), painting, mouse routing and dragging. A window only
declares its layout, by adding `SkinElement`s (`SkinButton`, `SkinSlider`,
`SkinImage`, `SkinTimeDisplay`, `SkinMarquee`) and implementing
`paintBackground()`.

Windows other than the classic three derive from `GenericWindow`, which draws
the resizable `gen.bmp` frame with its title and close button and leaves the
content area to the subclass (`LibraryWindow`, `VisualizerWindow`). Lists use `SkinListView`, the
playlist's row list with selection and scrolling, fed by callbacks.

`WindowManager` holds the shared skin and the global scale, snaps windows to each
other and to screen edges while they are dragged, moves windows docked to the
main window along with it, and saves and restores the layout.

Elements are plain objects rather than `QWidget`s. The window paints them in one
pass and routes input to them in unscaled skin pixels, which keeps memory use and
the binary small.

## Visualizations

`VisualizerWindow` shows full-window effects. It pulls the engine's recent
samples ~30 times a second, only while it is shown and music is playing.
`VisAnalyzer` (in `ui/vis/`) turns them into a `VisFrame`: the waveform, 32
spectrum bands from `Spectrum`, bass, mid and treble levels, and beats, found
when bass energy jumps well above its running average.

Each effect is a `VisEffect` that draws a `VisFrame` into a small RGB32 image
(about 256x192) on the CPU, which the window scales into its content area; no
GPU code is involved, and a frame costs around a millisecond (Mandelbrot,
the heaviest, several). The image
persists, so feedback effects build on the previous frame. Feedback,
Kaleidoscope, Tunnel, Fireworks and Mandelbrot (an endless zoom, rendered
at about 160x120 since each pixel can take hundreds of iterations) are the
psychedelic ones; Spectrum and
Oscilloscope are the classic displays in the skin's `viscolor.txt` colours.
To add an effect, subclass `VisEffect` and list it in `createVisEffects()`.

## Lyrics

`LyricsService` (in `core/`) finds lyrics for the playing track by asking a
list of `LyricsProvider`s in turn until one answers: `SidecarLyrics` (a
`.lrc` or `.txt` beside a local file), then `TagLyrics` (`TagReader`'s
`USLT`, `SYLT`, Vorbis `LYRICS` and MP4 `©lyr`). Providers answer with text,
LRC or plain, and `Lyrics::parse()` turns it into timed lines, word timings
from enhanced LRC included. `LyricsProvider` is header-only so plugins can
offer providers later (media servers, online databases). Lookups happen only
while the lyrics window is open, and answers for a track that is no longer
playing are dropped.

`LyricsWindow` follows the engine's position, extrapolating between updates,
and eases the line being sung to the middle of the window. Per-track timing
offsets are saved under `lyrics/offsets/` in the settings.
