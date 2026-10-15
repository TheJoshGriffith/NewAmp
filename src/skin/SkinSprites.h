#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

// Geometry of the classic skin format: which bitmap ("sheet") each sprite
// lives in, where it sits in that sheet, and where it is drawn on a window.
// The default skin is generated in exactly this layout so the renderer has
// one code path for built-in and user-loaded skins.
namespace newamp::sprites {

enum class Sheet {
    Main,      // main.bmp
    TitleBar,  // titlebar.bmp
    CButtons,  // cbuttons.bmp
    Numbers,   // numbers.bmp
    Text,      // text.bmp
    PosBar,    // posbar.bmp
    Volume,    // volume.bmp
    PlayPause, // playpaus.bmp
    Count
};

constexpr QSize sheetSize(Sheet sheet)
{
    switch (sheet) {
    case Sheet::Main: return {275, 116};
    case Sheet::TitleBar: return {344, 87};
    case Sheet::CButtons: return {136, 36};
    case Sheet::Numbers: return {99, 13};
    case Sheet::Text: return {155, 18};
    case Sheet::PosBar: return {307, 10};
    case Sheet::Volume: return {68, 433};
    case Sheet::PlayPause: return {42, 9};
    case Sheet::Count: break;
    }
    return {};
}

struct Sprite
{
    QRect normal;
    QRect pressed;
};

// Main window
constexpr QSize MainWindowSize{275, 116};
constexpr QRect TitleBarActive{27, 0, 275, 14};
constexpr QRect TitleBarInactive{27, 15, 275, 14};
constexpr int TitleBarHeight = 14;

// Title bar buttons (sheet: TitleBar)
constexpr Sprite OptionsButton{{0, 0, 9, 9}, {0, 9, 9, 9}};
constexpr Sprite MinimizeButton{{9, 0, 9, 9}, {9, 9, 9, 9}};
constexpr Sprite CloseButton{{18, 0, 9, 9}, {18, 9, 9, 9}};
constexpr QPoint OptionsButtonPos{6, 3};
constexpr QPoint MinimizeButtonPos{244, 3};
constexpr QPoint CloseButtonPos{264, 3};

// Transport buttons (sheet: CButtons)
constexpr Sprite PreviousButton{{0, 0, 23, 18}, {0, 18, 23, 18}};
constexpr Sprite PlayButton{{23, 0, 23, 18}, {23, 18, 23, 18}};
constexpr Sprite PauseButton{{46, 0, 23, 18}, {46, 18, 23, 18}};
constexpr Sprite StopButton{{69, 0, 23, 18}, {69, 18, 23, 18}};
constexpr Sprite NextButton{{92, 0, 22, 18}, {92, 18, 22, 18}};
constexpr Sprite EjectButton{{114, 0, 22, 16}, {114, 16, 22, 16}};
constexpr QPoint PreviousButtonPos{16, 88};
constexpr QPoint PlayButtonPos{39, 88};
constexpr QPoint PauseButtonPos{62, 88};
constexpr QPoint StopButtonPos{85, 88};
constexpr QPoint NextButtonPos{108, 88};
constexpr QPoint EjectButtonPos{136, 89};

// Time display (sheet: Numbers): 9x13 digits at x = digit * 9.
constexpr QSize DigitSize{9, 13};
constexpr QRect TimeDisplay{48, 26, 51, 13};
constexpr int TimeDigitOffsets[4] = {0, 12, 30, 42};

// Play state indicator (sheet: PlayPause)
constexpr QRect PlayingIndicator{0, 0, 9, 9};
constexpr QRect PausedIndicator{9, 0, 9, 9};
constexpr QRect StoppedIndicator{18, 0, 9, 9};
constexpr QPoint PlayStatePos{26, 28};

// Song title marquee (sheet: Text): 5x6 glyphs.
constexpr QSize GlyphSize{5, 6};
constexpr QRect Marquee{111, 27, 154, 6};

// Seek bar (sheet: PosBar)
constexpr QRect PosBarBackground{0, 0, 248, 10};
constexpr Sprite PosBarThumb{{248, 0, 29, 10}, {278, 0, 29, 10}};
constexpr QRect PosBar{16, 72, 248, 10};

// Volume (sheet: Volume): 28 backgrounds stacked 15px apart.
constexpr int VolumeSteps = 28;
constexpr QRect volumeBackground(int step) { return {0, step * 15, 68, 13}; }
constexpr Sprite VolumeThumb{{15, 422, 14, 11}, {0, 422, 14, 11}};
constexpr QRect VolumeBar{107, 57, 68, 13};

} // namespace newamp::sprites
