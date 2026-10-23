# Architecture

NewAmp is split into small layers. Each one depends only on the layers above it.

```
audio/    AudioBackend, DspBackend (decoder -> DSP -> sink), MediaPlayerBackend, DSP, tags, spectrum
core/     Track, MediaSource, Playlist, PlaybackEngine, PlayerController, EqualizerSettings
plugins/  SourcePlugin, PluginHost, PluginManager; optional plugins (plex/)
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
  Spotify Connect device (the user's choice, else the active one, else the
  first open one) by remote control, sending commands one at a time and in
  order, and polls the device to report position and the end of each track.
- `SpotifyLibrary`: the `LibraryProvider` for playlists, liked songs, saved
  albums, followed artists (and their albums) and search.

Spotify's audio is protected, so it never passes through NewAmp: there is no
equalizer or visualizer for it, and playback needs Premium. Each user
registers their own app in Spotify's developer dashboard and enters its
client ID (development-mode apps take five users; a build can carry a
default with `-DNEWAMP_SPOTIFY_CLIENT_ID=...`). Development-mode apps may only
read the contents of playlists the user owns or collaborates on, and get at
most 10 search results of each kind. The refresh token lives in the
plugin's settings group for now.

An in-app player is the next step: the Web Playback SDK in the system web
view registers itself as a Connect device, and `SpotifyConnect` plays on it
like any other.

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
