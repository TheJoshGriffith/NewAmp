#pragma once

#include <QImage>
#include <QList>
#include <QString>

class QAction;

namespace newamp {

// The music, as a visualization sees it for one frame.
struct VisualizationInput
{
    const float *wave = nullptr; // mono samples, roughly -1..1, oldest first
    int waveSize = 0;
    const float *spectrum = nullptr; // log-spaced 40 Hz .. 16 kHz, 0..1
    int bands = 0;
    float bass = 0; // 0..1 levels, smoothed
    float mid = 0;
    float treble = 0;
    bool beat = false; // a beat starts on this frame
    double time = 0;   // seconds of audio seen so far
    float dt = 0;      // seconds since the previous frame
    bool silent = true;
};

// A visualization a plugin adds to the Visualization window's list, next to
// the built-in effects (vis plugins, in Winamp's terms). The window owns the
// frame and its skinned border; the plugin only fills the image.
//
// Header-only and free of QObject, like PluginHost, so plugins need no
// symbols from the executable. Every call comes from the main thread.
class Visualization
{
public:
    virtual ~Visualization() = default;

    // Shown in the window's menu and briefly over the picture.
    virtual QString name() const = 0;

    // The image size to render for an area of `pixels` screen pixels. The
    // window scales the image to fit.
    virtual QSize frameSize(const QSize &pixels) const { return pixels; }
    virtual bool smoothScaling() const { return true; }

    // The visualization was picked, or the window showed again; set up
    // anything costly here rather than when the plugin loads.
    virtual void activate() {}
    // Another one was picked, or the window hid; free what activate() made.
    virtual void deactivate() {}

    // Draw one frame into image, an RGB32 image of frameSize(). It keeps the
    // previous frame, and starts out black.
    virtual void render(QImage &image, const VisualizationInput &input) = 0;

    // Extra entries for the window's menu while this one is showing, such
    // as preset choices. Owned by the plugin.
    virtual QList<QAction *> actions() { return {}; }
};

} // namespace newamp
