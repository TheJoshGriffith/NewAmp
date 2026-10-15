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

### Plex

`plugins/plex` builds `newamp_plex`, the only target that links Qt Network.

- `PlexApi`: pure JSON parsing and URL building, unit-tested from fixtures.
- `PlexClient`: plex.tv PIN sign-in, server discovery (each connection is
  probed in order local, remote, relay), JSON requests; also the
  `MediaSource` that turns a track into a direct-play URL.
- `PlexReporter`: timeline reports (every 10 s and on state changes) and a
  scrobble once a track passes 90 %.
- `PlexBrowser`: a plain Qt Widgets dialog for libraries, artists, albums,
  tracks and audio playlists.

Plex tracks are stored as `plex://<machineIdentifier><partKey>?ratingKey=<key>`,
so they are resolved against whichever connection to that server works now.
The auth token lives in the plugin's settings group for now.

## Playback

`PlaybackEngine` picks an `AudioBackend` per track. Local MP3, FLAC, WAV and Ogg
Vorbis files go through `DspBackend`, NewAmp's own pipeline: `AudioDecoder`
(miniaudio's decoders, vendored in `third_party/miniaudio`) feeds `DspChain`
(ten-band equalizer, preamp, balance, volume), which a `QAudioSink` pulls from.
It also keeps recent samples for the visualizer. Everything else, such as AAC,
Opus or network streams, falls back to `MediaPlayerBackend` (`QMediaPlayer`),
where only volume applies. `TagReader` reads ID3, FLAC and Vorbis tags for the
DSP path.

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
and `pledit.txt` sets the playlist colours.

## Windows

Every window derives from `SkinnedWindow`, which handles the frameless window,
scaling ("double size"), painting, mouse routing and dragging. A window only
declares its layout, by adding `SkinElement`s (`SkinButton`, `SkinSlider`,
`SkinImage`, `SkinTimeDisplay`, `SkinMarquee`) and implementing
`paintBackground()`.

`WindowManager` holds the shared skin and the global scale, snaps windows to each
other and to screen edges while they are dragged, moves windows docked to the
main window along with it, and saves and restores the layout.

Elements are plain objects rather than `QWidget`s. The window paints them in one
pass and routes input to them in unscaled skin pixels, which keeps memory use and
the binary small.
