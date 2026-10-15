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
    ShufRep,   // shufrep.bmp
    MonoSter,  // monoster.bmp
    Balance,   // balance.bmp
    EqMain,    // eqmain.bmp
    PlEdit,    // pledit.bmp
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
    case Sheet::ShufRep: return {92, 85};
    case Sheet::MonoSter: return {58, 24};
    case Sheet::Balance: return {68, 433};
    case Sheet::EqMain: return {275, 315};
    case Sheet::PlEdit: return {280, 186};
    case Sheet::Count: break;
    }
    return {};
}

struct Sprite
{
    QRect normal;
    QRect pressed;
};

// A button with an on/off state, e.g. shuffle.
struct ToggleSprite
{
    QRect normal;
    QRect pressed;
    QRect active;
    QRect activePressed;
};

// Classic windows snap to each other and resize in these steps.
constexpr int SnapDistance = 10;

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

// Balance (sheet: Balance): 28 backgrounds, centre is step 0.
constexpr QRect balanceBackground(int step) { return {9, step * 15, 38, 13}; }
constexpr Sprite BalanceThumb{{15, 422, 14, 11}, {0, 422, 14, 11}};
constexpr QRect BalanceBar{177, 57, 38, 13};

// Mode toggles (sheet: ShufRep)
constexpr ToggleSprite RepeatButton{{0, 0, 28, 15}, {0, 15, 28, 15}, {0, 30, 28, 15}, {0, 45, 28, 15}};
constexpr ToggleSprite ShuffleButton{{28, 0, 47, 15}, {28, 15, 47, 15}, {28, 30, 47, 15}, {28, 45, 47, 15}};
constexpr ToggleSprite EqToggleButton{{0, 61, 23, 12}, {46, 61, 23, 12}, {0, 73, 23, 12}, {46, 73, 23, 12}};
constexpr ToggleSprite PlaylistToggleButton{{23, 61, 23, 12}, {69, 61, 23, 12}, {23, 73, 23, 12}, {69, 73, 23, 12}};
constexpr QPoint ShuffleButtonPos{164, 89};
constexpr QPoint RepeatButtonPos{210, 89};
constexpr QPoint EqToggleButtonPos{219, 58};
constexpr QPoint PlaylistToggleButtonPos{242, 58};

// Channel indicators (sheet: MonoSter): lit and unlit.
constexpr QRect StereoLit{0, 0, 29, 12};
constexpr QRect StereoUnlit{0, 12, 29, 12};
constexpr QRect MonoLit{29, 0, 27, 12};
constexpr QRect MonoUnlit{29, 12, 27, 12};
constexpr QPoint StereoPos{239, 41};
constexpr QPoint MonoPos{212, 41};

// Bitrate text on the main window (sheet: Text)
constexpr QRect KbpsText{111, 43, 15, 6};

// Equalizer window (sheet: EqMain)
constexpr QSize EqWindowSize{275, 116};
constexpr QRect EqBackground{0, 0, 275, 116};
constexpr QRect EqTitleBarActive{0, 134, 275, 14};
constexpr QRect EqTitleBarInactive{0, 149, 275, 14};
constexpr Sprite EqCloseButton{{0, 116, 9, 9}, {0, 125, 9, 9}};
constexpr QPoint EqCloseButtonPos{264, 3};
constexpr ToggleSprite EqOnButton{{10, 119, 26, 12}, {128, 119, 26, 12}, {69, 119, 26, 12}, {187, 119, 26, 12}};
constexpr ToggleSprite EqAutoButton{{36, 119, 32, 12}, {154, 119, 32, 12}, {95, 119, 32, 12}, {213, 119, 32, 12}};
constexpr Sprite EqPresetsButton{{224, 164, 44, 12}, {224, 176, 44, 12}};
constexpr QPoint EqOnButtonPos{14, 18};
constexpr QPoint EqAutoButtonPos{40, 18};
constexpr QPoint EqPresetsButtonPos{217, 18};
constexpr QRect EqGraphBackground{0, 294, 113, 19};
constexpr QRect EqGraphLineColors{115, 294, 1, 19};
constexpr QRect EqPreampLine{0, 314, 113, 1};
constexpr QPoint EqGraphPos{86, 17};
constexpr QSize EqSliderSize{14, 63};
constexpr int EqSliderSteps = 28;
// Step 0 is the bottom (-12 dB) position.
constexpr QRect eqSliderBackground(int step)
{
    return {13 + (step % 14) * 15, 164 + (step / 14) * 65, 14, 63};
}
constexpr Sprite EqSliderThumb{{0, 164, 11, 11}, {0, 176, 11, 11}};
constexpr QPoint EqPreampPos{21, 38};
constexpr int EqBandCount = 10;
constexpr QPoint eqBandPos(int band) { return {78 + band * 18, 38}; }

// Playlist window (sheet: PlEdit). Pieces are tiled to fit the window.
struct ActiveSprite
{
    QRect active;
    QRect inactive;
};
constexpr ActiveSprite PlTopLeft{{0, 0, 25, 20}, {0, 21, 25, 20}};
constexpr ActiveSprite PlTitle{{26, 0, 100, 20}, {26, 21, 100, 20}};
constexpr ActiveSprite PlTopTile{{127, 0, 25, 20}, {127, 21, 25, 20}};
constexpr ActiveSprite PlTopRight{{153, 0, 25, 20}, {153, 21, 25, 20}};
constexpr QRect PlLeftTile{0, 42, 12, 29};
constexpr QRect PlRightTile{31, 42, 20, 29};
constexpr QRect PlBottomLeft{0, 72, 125, 38};
constexpr QRect PlBottomRight{126, 72, 150, 38};
constexpr QRect PlBottomTile{179, 0, 25, 38};
constexpr QRect PlClosePressed{52, 42, 9, 9};
constexpr Sprite PlScrollHandle{{52, 53, 8, 18}, {61, 53, 8, 18}};
constexpr QSize PlMinimumSize{275, 116};
constexpr QSize PlSizeStep{25, 29};
constexpr int PlTopHeight = 20;
constexpr int PlBottomHeight = 38;
constexpr int PlLeftWidth = 12;
constexpr int PlRightWidth = 20;

} // namespace newamp::sprites
