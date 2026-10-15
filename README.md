# NewAmp

An open-source, Qt 6 music player in the spirit of classic WinAmp: small, fast,
skinnable, and multi-window, with Plex integration planned in the style of PlexAmp.

## Status

Early development. See the roadmap below.

## Roadmap

1. **Foundation and local playback** — Qt 6 / C++ / CMake project, skinned main
   window with an original default skin, local file playback.
2. **Classic skins and multi-window** — load classic `.wsz` skins, playlist and
   equalizer windows, window snapping and docking.
3. **Plex** — sign in with plex.tv, browse libraries, stream, report plays.
4. **Spotify (optional plugin)** — loaded only when enabled, using the system web view.

NewAmp ships only its own original artwork. It can read the classic skin format,
but no third-party skins are bundled.

## Building

Requires Qt 6.4+ (Core, Gui, Widgets, Multimedia) and CMake 3.21+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

On macOS with Homebrew: `brew install qt cmake`, then pass
`-DCMAKE_PREFIX_PATH=$(brew --prefix qt)` to the first command.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
