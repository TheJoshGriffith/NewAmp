# Architecture

NewAmp is split into small layers. Each one depends only on the layers above it.

```
core/     Track, MediaSource, Playlist, PlaybackEngine, PlayerController
plugins/  SourcePlugin interface and PluginManager
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
`<app dir>/plugins` elsewhere, and any paths in `NEWAMP_PLUGIN_PATH`.

## Playback

`PlaybackEngine` wraps `QMediaPlayer`. `PlayerController` holds the transport
rules (what play, pause, next and end-of-track mean) so every window behaves the
same. Windows call the controller, never the engine's transport directly.

## Skins

`SkinSprites.h` records the classic skin layout: which bitmap each sprite is in
and where it is drawn. The built-in skin is drawn in code by `DefaultSkin` in
exactly that layout, so built-in and loaded `.wsz` skins share one renderer. No
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
