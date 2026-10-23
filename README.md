# NewAmp

An open-source, Qt 6 music player in the spirit of classic WinAmp: small, fast,
skinnable, and multi-window, with Plex integration planned in the style of PlexAmp.

![NewAmp's main window, equalizer, playlist, media library and visualizer, docked together](docs/screenshots/newamp.png)

## Screenshots

NewAmp comes with seven colour themes of its own, all drawn in code:

![The main window, equalizer and playlist in each built-in theme](docs/screenshots/themes.png)

The visualizer window has feedback trails, a kaleidoscope, a tunnel,
fireworks, a Mandelbrot zoom, and big classic bars and scope:

![Each visualizer effect](docs/screenshots/visualizations.png)

## Status

Early development. See the roadmap below.

## Roadmap

1. **Foundation and local playback** — Qt 6 / C++ / CMake project, skinned main
   window with an original default skin, local file playback.
2. **Classic skins and multi-window** — load classic `.wsz` skins, playlist and
   equalizer windows, window snapping and docking.
3. **Plex** — sign in with plex.tv, browse libraries, stream, report plays.
4. **Jellyfin (optional plugin)** — sign in with a password or API key, browse
   and search music libraries, stream, report plays.
5. **DLNA / UPnP (optional plugin)** — finds media servers on the local network
   (MiniDLNA, Plex's DLNA server, most NAS boxes), browses and searches them,
   and plays their music.
6. **Spotify (optional plugin)** — loaded only when enabled, using the system web view.

NewAmp ships only its own original artwork. It can read the classic skin format,
but no third-party skins are bundled.

## Downloads

Every push to `main` builds a macOS disk image (`NewAmp-macOS`), a Linux
AppImage (`NewAmp-Linux`) and a Windows zip (`NewAmp-Windows`). Find them under
*Artifacts* on the latest run of the CI workflow in the Actions tab. The builds
are unsigned: on macOS, right-click the app and choose Open the first time; on
Windows, choose More info, then Run anyway.

## Building

Requires Qt 6.4+ (Core, Gui, Widgets, Multimedia, Network) and CMake 3.21+.
zlib comes from the system where there is one and is built from source
otherwise (Windows).

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
