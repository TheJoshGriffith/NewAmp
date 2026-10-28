#pragma once

#include "ui/vis/VisAnalyzer.h"

#include <QImage>
#include <QList>
#include <QString>

#include <algorithm>
#include <memory>
#include <vector>

class QAction;

namespace newamp {

// One visualization. Effects draw on the CPU into a small RGB32 image that
// the window scales up, so each frame costs a few milliseconds at most.
//
// The image persists between frames, so feedback effects can build on the
// previous one. The window clears it to black when the effect starts or
// the size changes. To add an effect, subclass this and list it in
// createVisEffects().
class VisEffect
{
public:
    virtual ~VisEffect() = default;

    virtual QString name() const = 0;
    virtual void render(QImage &image, const VisFrame &frame) = 0;

    // The image size to render at for a content area of `content` skin
    // pixels. By default about 256x192 pixels' worth, keeping the aspect.
    virtual QSize frameSize(const QSize &content) const;
    // Smooth scaling suits soft effects; pixel-art ones keep hard edges.
    virtual bool smoothScaling() const { return true; }

    // Before the first frame after the effect becomes current, and after it
    // stops being current or the window hides. Only plugin
    // visualizations hold anything worth freeing.
    virtual void activate() {}
    virtual void deactivate() {}
    // Extra menu entries while the effect is current.
    virtual QList<QAction *> actions() { return {}; }
};

// Every effect, in menu order.
std::vector<std::unique_ptr<VisEffect>> createVisEffects();

// Drawing helpers shared by the effects. They write straight into RGB32
// image memory and clip to the image.
namespace vis {

// Hue, saturation and value in 0..1; hue wraps.
QRgb hsv(float h, float s, float v);
// Scales a colour by f in 0..256.
inline QRgb scaled(QRgb c, unsigned f)
{
    return (((c & 0xff00ffu) * f >> 8) & 0xff00ffu) | (((c & 0x00ff00u) * f >> 8) & 0x00ff00u) | 0xff000000u;
}
// Adds two colours, saturating each channel.
inline QRgb added(QRgb a, QRgb b)
{
    const unsigned r = std::min(255u, ((a >> 16) & 0xff) + ((b >> 16) & 0xff));
    const unsigned g = std::min(255u, ((a >> 8) & 0xff) + ((b >> 8) & 0xff));
    const unsigned bl = std::min(255u, (a & 0xff) + (b & 0xff));
    return 0xff000000u | (r << 16) | (g << 8) | bl;
}

// sin() from a 4096-entry table: plenty for colours and wobble.
float fastSin(float x);
inline float fastCos(float x)
{
    return fastSin(x + 1.5707963f);
}

// Multiplies every pixel by f in 0..256: trails that fade out.
void fade(QImage &image, unsigned f);
// Adds a colour at a pixel.
void plot(QImage &image, int x, int y, QRgb color);
// A soft dot: full colour in the middle, a quarter around it.
void glow(QImage &image, float x, float y, QRgb color);
// An additive line about two pixels wide.
void line(QImage &image, float x0, float y0, float x1, float y1, QRgb color);

} // namespace vis

} // namespace newamp
