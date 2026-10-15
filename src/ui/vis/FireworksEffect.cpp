#include "ui/vis/VisEffect.h"

#include <cmath>
#include <cstdint>

namespace newamp {

namespace {

// Fireworks over a starfield. Every beat bursts a shell of sparks that fall
// and fade, leaving trails; the bass drives the stars towards the viewer
// until they streak, and treble adds glitter.
class FireworksEffect : public VisEffect
{
public:
    QString name() const override { return QStringLiteral("Fireworks"); }

    void render(QImage &image, const VisFrame &f) override
    {
        const float w = float(image.width());
        const float h = float(image.height());
        vis::fade(image, 205);
        drawStars(image, f, w, h);

        if (f.beat)
            burst(f, w, h);
        // Glitter keeps quiet passages alive between beats.
        for (float n = f.treble * f.treble * 3.0f; n > 0 && !f.silent; n -= 1.0f)
            if (n >= 1.0f || random() < n)
                spawn(random() * w, random() * h, 0, 0, vis::hsv(random(), 0.3f, 1.0f), 2.5f);

        const float gravity = h * 0.35f;
        const float drag = std::exp(-f.dt * 1.2f);
        size_t alive = 0;
        for (Particle &p : m_particles) {
            p.life -= p.decay * f.dt;
            if (p.life <= 0)
                continue;
            p.vx *= drag;
            p.vy = p.vy * drag + gravity * f.dt;
            const float fromX = p.x;
            const float fromY = p.y;
            p.x += p.vx * f.dt;
            p.y += p.vy * f.dt;
            // Each spark streaks along its path, and flickers as it dies.
            const float flicker = p.life < 0.3f ? 0.4f + 0.6f * random() : 1.0f;
            const QRgb color = vis::scaled(p.color, unsigned(200.0f * std::min(1.0f, p.life) * flicker));
            vis::line(image, fromX, fromY, p.x, p.y, color);
            m_particles[alive++] = p;
        }
        m_particles.resize(alive);
    }

private:
    static constexpr size_t MaxParticles = 2500;
    static constexpr int StarCount = 140;

    struct Particle
    {
        float x, y, vx, vy;
        float life, decay; // life runs from 1 to 0 at `decay` per second
        QRgb color;
    };
    struct Star
    {
        float x, y, z; // x and y in -1..1, z in 0..1 (nearer is smaller)
    };

    // A small fast generator; the effect needs no quality randomness.
    float random()
    {
        m_seed ^= m_seed << 13;
        m_seed ^= m_seed >> 17;
        m_seed ^= m_seed << 5;
        return float(m_seed & 0xffffff) / float(0x1000000);
    }

    void spawn(float x, float y, float vx, float vy, QRgb color, float decay)
    {
        if (m_particles.size() < MaxParticles)
            m_particles.push_back({x, y, vx, vy, 1.0f, decay, color});
    }

    void burst(const VisFrame &f, float w, float h)
    {
        const float x = w * (0.15f + 0.7f * random());
        const float y = h * (0.12f + 0.45f * random());
        const float hue = random();
        const bool ring = random() < 0.35f; // some shells are perfect rings
        const int count = int(60 + 160 * f.beatStrength);
        const float speed = std::min(w, h) * (0.6f + 0.6f * f.beatStrength);
        for (int i = 0; i < count; ++i) {
            const float angle = random() * 6.2831853f;
            const float v = speed * (ring ? 0.95f + 0.05f * random() : 0.2f + 0.8f * std::sqrt(random()));
            // Two-tone shells: a few sparks take the opposite hue.
            const float sparkHue = random() < 0.2f ? hue + 0.5f : hue + 0.06f * random();
            spawn(x, y, v * vis::fastCos(angle), v * vis::fastSin(angle), vis::hsv(sparkHue, 0.7f, 1.0f),
                  0.5f + 0.5f * random());
        }
    }

    void drawStars(QImage &image, const VisFrame &f, float w, float h)
    {
        if (m_stars.empty()) {
            m_stars.resize(StarCount);
            for (Star &s : m_stars)
                s = {random() * 2 - 1, random() * 2 - 1, 0.05f + random() * 0.95f};
        }
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        const float scale = std::max(w, h) * 0.5f;
        const float speed = 0.12f + 0.5f * f.bass + 1.2f * f.pulse;
        for (Star &s : m_stars) {
            const float px = cx + s.x / s.z * scale;
            const float py = cy + s.y / s.z * scale;
            s.z -= f.dt * speed;
            const float x = cx + s.x / std::max(s.z, 0.01f) * scale;
            const float y = cy + s.y / std::max(s.z, 0.01f) * scale;
            if (s.z <= 0.02f || x < 0 || y < 0 || x >= w || y >= h) {
                s = {random() * 2 - 1, random() * 2 - 1, 1.0f};
                continue;
            }
            const unsigned level = unsigned(std::min(1.0f, (1.0f - s.z) * 1.3f) * 130.0f);
            vis::line(image, px, py, x, y, vis::scaled(qRgb(190, 205, 255), level));
        }
    }

    std::vector<Particle> m_particles;
    std::vector<Star> m_stars;
    std::uint32_t m_seed = 0x9e3779b9u;
};

} // namespace

std::unique_ptr<VisEffect> createFireworksEffect()
{
    return std::make_unique<FireworksEffect>();
}

} // namespace newamp
