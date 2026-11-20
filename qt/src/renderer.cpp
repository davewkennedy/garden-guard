#include "renderer.h"

#include "data.h"
#include "game.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr double TAU = 2 * M_PI;
constexpr double TILE = data::TILE;
constexpr double W = data::W;
constexpr double H = data::H;

constexpr double GLYPH_PAD = 1.3;     // glyph image is this much bigger than the font size
constexpr double KICK_TIME = 0.16;    // tower shot "kick" duration (s)
constexpr double FLASH_TIME = 0.1;    // enemy hit flash duration (s)
constexpr double MORTAR_ARC = 60;     // px peak height of a mortar shell
constexpr double ENEMY_SIZE = TILE * 0.55;
constexpr double BOSS_SIZE = ENEMY_SIZE * 1.6;
constexpr double TOWER_ICON = TILE * 0.5;
constexpr double POT_R = TILE * 0.34;

const char *const EMOJI_FONT = "Noto Color Emoji";
const char *const FLOWER_COLORS[] = {"#ffffff", "#ffd84d", "#ff8fb1", "#b98cff", "#ff9f5a"};

// Small seeded PRNG (mulberry32), same as the web version, so the garden looks identical.
class Rng {
public:
    explicit Rng(uint32_t seed) : a_(seed) {}
    double operator()() {
        a_ += 0x6D2B79F5u;
        uint32_t t = a_;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return double(t ^ (t >> 14)) / 4294967296.0;
    }
private:
    uint32_t a_;
};

QColor rgba(int r, int g, int b, double a) {
    QColor c(r, g, b);
    c.setAlphaF(std::clamp(a, 0.0, 1.0));
    return c;
}

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double easeOut(double u) { double k = 1 - u; return 1 - k * k; }
double deg(double rad) { return rad * 180 / M_PI; }

// Canvas-style radial gradient: from circle (x0,y0,r0) to circle (x1,y1,r1).
QRadialGradient radial(double x0, double y0, double r0, double x1, double y1, double r1) {
    return QRadialGradient(QPointF(x1, y1), r1, QPointF(x0, y0), r0);
}

void fillEllipse(QPainter &g, double x, double y, double rx, double ry, double rot, const QBrush &b) {
    g.save();
    g.translate(x, y);
    g.rotate(deg(rot));
    g.setPen(Qt::NoPen);
    g.setBrush(b);
    g.drawEllipse(QPointF(0, 0), rx, ry);
    g.restore();
}

void fillCircle(QPainter &g, double x, double y, double r, const QBrush &b) {
    g.setPen(Qt::NoPen);
    g.setBrush(b);
    g.drawEllipse(QPointF(x, y), r, r);
}

void strokeCircle(QPainter &g, double x, double y, double r, const QColor &c, double width) {
    g.setPen(QPen(c, width));
    g.setBrush(Qt::NoBrush);
    g.drawEllipse(QPointF(x, y), r, r);
}

QPen roundPen(const QColor &c, double width) {
    QPen pen(c, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

void rangeCircle(QPainter &p, double x, double y, double r, bool ok) {
    fillCircle(p, x, y, r, ok ? rgba(255, 255, 255, 0.12) : rgba(255, 70, 70, 0.1));
    QPen pen(ok ? rgba(255, 255, 255, 0.8) : rgba(255, 140, 140, 0.85), 2);
    pen.setDashPattern({3.5, 3});   // in pen widths: 7px dash, 6px gap
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(x, y), r, r);
}

}  // namespace

// ---------- scaling & caches ----------

void Renderer::setupScale(double scale, const Game &game) {
    // Round to quarter steps so window resizing doesn't rebuild the caches constantly.
    double want = std::clamp(std::round(scale * 4) / 4, 0.5, 4.0);
    if (want != dpr_) {
        dpr_ = want;
        glyphs_.clear();
        pots_.clear();
        bg_ = QPixmap();
    }
    if (bg_.isNull() || bgMap_ != game.mapIndex) {
        bgMap_ = game.mapIndex;
        bg_ = buildBackground(game);
    }
}

QPixmap Renderer::glyph(const QString &ch, double size) {
    QString key = ch + QString::number(size);
    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) return *it;

    int px = int(std::ceil(size * GLYPH_PAD * dpr_));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::TextAntialiasing);
    QFont f(EMOJI_FONT);
    f.setPixelSize(std::max(1, int(std::round(size * dpr_))));
    g.setFont(f);
    QRectF box(0, size * dpr_ * 0.06, px, px);
    g.drawText(box, Qt::AlignCenter, ch);
    g.end();
    return *glyphs_.insert(key, pm);
}

QPixmap Renderer::tint(const QString &ch, double size, const QColor &color) {
    QString key = ch + QString::number(size) + color.name();
    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) return *it;

    QPixmap pm = glyph(ch, size).copy();
    QPainter g(&pm);
    g.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    g.fillRect(pm.rect(), color);
    g.end();
    return *glyphs_.insert(key, pm);
}

void Renderer::drawGlyph(QPainter &p, const QString &ch, double size, double x, double y, double s) {
    double box = size * GLYPH_PAD * s;
    const QPixmap &pm = glyph(ch, size);
    p.drawPixmap(QRectF(x - box / 2, y - box / 2, box, box), pm, pm.rect());
}

QPixmap Renderer::pot(int type) {
    auto it = pots_.find(type);
    if (it != pots_.end()) return *it;

    int px = int(std::ceil(TILE * dpr_));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    g.scale(dpr_, dpr_);
    const double cx = TILE / 2, cy = TILE / 2, r = POT_R;

    fillEllipse(g, cx, cy + r * 0.6, r * 1.05, r * 0.45, 0, rgba(0, 0, 0, 0.22));

    QRadialGradient body = radial(cx - r * 0.35, cy - r * 0.4, 2, cx, cy, r);
    body.setColorAt(0, QColor("#f0a274"));
    body.setColorAt(1, QColor("#b25a31"));
    g.setBrush(body);
    g.setPen(QPen(QColor("#7a3a1c"), 2));
    g.drawEllipse(QPointF(cx, cy), r, r);

    strokeCircle(g, cx, cy, r - 4, QColor(data::TOWERS[type].color), 3);

    QRadialGradient soil = radial(cx, cy - 2, 1, cx, cy, r - 6);
    soil.setColorAt(0, QColor("#6e482a"));
    soil.setColorAt(1, QColor("#3d2615"));
    fillCircle(g, cx, cy, r - 6.5, soil);

    fillEllipse(g, cx - r * 0.55, cy - r * 0.55, r * 0.22, r * 0.12, -0.8, rgba(255, 255, 255, 0.28));
    g.end();
    return *pots_.insert(type, pm);
}

// ---------- background ----------

QPixmap Renderer::buildBackground(const Game &game) {
    const data::MapDef &map = game.map();
    QPixmap pm(int(std::ceil(W * dpr_)), int(std::ceil(H * dpr_)));
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    g.scale(dpr_, dpr_);
    Rng rand(map.seed);
    const QColor grassA(map.grassA), grassB(map.grassB);

    // Grass tiles with subtle variation.
    for (int r = 0; r < data::ROWS; r++) {
        for (int c = 0; c < data::COLS; c++) {
            QRectF tile(c * TILE, r * TILE, TILE, TILE);
            g.fillRect(tile, (r + c) % 2 ? grassA : grassB);
            double v = rand();
            g.fillRect(tile, v < 0.5 ? rgba(255, 255, 220, (0.5 - v) * 0.12)
                                     : rgba(0, 50, 0, (v - 0.5) * 0.1));
        }
    }

    // Grass blades.
    for (int i = 0; i < 1100; i++) {
        double x = rand() * W, y = rand() * H;
        double shade = rand();
        double lean = (rand() - 0.5) * 3;
        double len = 3 + rand() * 4;
        g.setPen(roundPen(shade < 0.5 ? rgba(40, 110, 40, 0.35) : rgba(190, 240, 140, 0.35), 1.2));
        g.drawLine(QPointF(x, y), QPointF(x + lean, y - len));
    }

    // Dirt path.
    const std::vector<QPointF> &pathPts = game.path;
    QPainterPath route(pathPts[0]);
    for (size_t i = 1; i < pathPts.size(); i++) route.lineTo(pathPts[i]);
    g.setBrush(Qt::NoBrush);
    for (auto [width, color] : {std::pair{0.92, rgba(50, 35, 15, 0.22)},
                                std::pair{0.84, QColor("#93653b")},
                                std::pair{0.74, QColor("#c99a62")},
                                std::pair{0.40, rgba(228, 190, 135, 0.45)}}) {
        g.setPen(roundPen(color, TILE * width));
        g.drawPath(route);
    }

    // Dirt specks and pebbles along the path.
    std::vector<double> segLen;
    double total = 0;
    for (size_t i = 0; i + 1 < pathPts.size(); i++) {
        double l = std::hypot(pathPts[i + 1].x() - pathPts[i].x(), pathPts[i + 1].y() - pathPts[i].y());
        segLen.push_back(l);
        total += l;
    }
    for (int i = 0; i < 520; i++) {
        double d = rand() * total;
        size_t s = 0;
        while (s < segLen.size() - 1 && d > segLen[s]) { d -= segLen[s]; s++; }
        const QPointF &a = pathPts[s], &b = pathPts[s + 1];
        double L = segLen[s] > 0 ? segLen[s] : 1;
        double f = segLen[s] > 0 ? d / segLen[s] : 0;
        double nx = -(b.y() - a.y()) / L, ny = (b.x() - a.x()) / L;
        double off = (rand() - 0.5) * TILE * 0.6;
        double x = a.x() + (b.x() - a.x()) * f + nx * off;
        double y = a.y() + (b.y() - a.y()) * f + ny * off;
        double kind = rand();
        if (kind < 0.08) {
            double rx = 2.5 + rand() * 2, ry = 1.8 + rand() * 1.2, rot = rand() * 3;
            fillEllipse(g, x, y, rx, ry, rot, QColor("#a49c90"));
        } else {
            QColor c = kind < 0.55 ? rgba(120, 80, 40, 0.45) : rgba(235, 205, 160, 0.5);
            fillCircle(g, x, y, 0.7 + rand() * 1.5, c);
        }
    }

    // Decorations on grass tiles.
    for (int r = 0; r < data::ROWS; r++) {
        for (int c = 0; c < data::COLS; c++) {
            double roll = rand();
            double cx = c * TILE + 12 + rand() * (TILE - 24);
            double cy = r * TILE + 12 + rand() * (TILE - 24);
            if (game.isPathTile(c, r)) continue;

            if (roll < 0.17) {   // flowers
                int n = 1 + int(rand() * 3);
                for (int i = 0; i < n; i++) {
                    double fx = cx + (rand() - 0.5) * 14, fy = cy + (rand() - 0.5) * 14;
                    QColor color(FLOWER_COLORS[int(rand() * 5)]);
                    double rot = rand() * TAU;
                    g.setPen(roundPen(rgba(40, 110, 40, 0.6), 1.2));
                    g.drawLine(QPointF(fx, fy), QPointF(fx, fy + 5));
                    for (int k = 0; k < 5; k++) {
                        double a = rot + k * TAU / 5;
                        fillCircle(g, fx + std::cos(a) * 2.8, fy + std::sin(a) * 2.8, 2.3, color);
                    }
                    fillCircle(g, fx, fy, 1.7, QColor("#ffb627"));
                }
            } else if (roll < 0.27) {   // pebbles
                int n = 2 + int(rand() * 2);
                for (int i = 0; i < n; i++) {
                    double px = cx + (rand() - 0.5) * 14, py = cy + (rand() - 0.5) * 10;
                    double rx = 2.5 + rand() * 2.5;
                    double rot = rand() * 3;
                    g.save();
                    g.translate(px, py);
                    g.rotate(deg(rot));
                    g.setPen(QPen(QColor("#7d776f"), 0.8));
                    g.setBrush(QColor("#b3ada3"));
                    g.drawEllipse(QPointF(0, 0), rx, rx * 0.7);
                    g.restore();
                    fillCircle(g, px - rx * 0.3, py - rx * 0.25, rx * 0.3, rgba(255, 255, 255, 0.35));
                }
            } else if (roll < 0.45) {   // grass tuft
                g.setPen(roundPen(QColor("#4f9a3b"), 1.6));
                g.setBrush(Qt::NoBrush);
                for (int k = 0; k < 5; k++) {
                    double a = -M_PI / 2 + (k - 2) * 0.32 + (rand() - 0.5) * 0.2;
                    double len = 6 + rand() * 4;
                    QPainterPath blade(QPointF(cx, cy + 4));
                    blade.quadTo(cx + std::cos(a) * len * 0.5, cy + 4 + std::sin(a) * len * 0.6,
                                 cx + std::cos(a) * len, cy + 4 + std::sin(a) * len);
                    g.drawPath(blade);
                }
            }
        }
    }

    // Bug nest just inside the left edge, where the path enters.
    {
        const QPointF &first = pathPts[0], &second = pathPts[1];
        double dir = second.x() > first.x() ? 1 : (second.x() < first.x() ? -1 : 0);
        double nx = std::clamp(first.x() + dir * TILE * 1.3, TILE * 0.32, W - TILE * 0.32);
        double ny = first.y();
        fillEllipse(g, nx, ny, 30, 24, 0, QColor("#7a5432"));
        QRadialGradient hole = radial(nx, ny + 2, 2, nx, ny, 24);
        hole.setColorAt(0, QColor("#120a05"));
        hole.setColorAt(0.7, QColor("#2e1d10"));
        hole.setColorAt(1, QColor("#5a3a20"));
        fillEllipse(g, nx, ny, 23, 18, 0, hole);
        for (int k = 0; k < 16; k++) {
            double a = k * TAU / 16;
            double x = nx + std::cos(a) * 27, y = ny + std::sin(a) * 21;
            g.setPen(roundPen(QColor(k % 2 ? "#8a5f36" : "#5e3f22"), 2.4));
            g.drawLine(QPointF(x - std::sin(a) * 6, y + std::cos(a) * 5),
                       QPointF(x + std::sin(a) * 6, y - std::cos(a) * 5));
        }
    }

    // House at the path's exit, on the right edge.
    {
        const QPointF &last = pathPts.back();
        // Far enough from the edge that the whole emoji (and its tree) stays on the board.
        double hx = std::min(W - TILE * 0.72, last.x());
        double hy = last.y() - 6;
        fillEllipse(g, hx, hy + 24, 36, 11, 0, rgba(0, 0, 0, 0.2));
        fillEllipse(g, hx, hy + 20, 32, 9, 0, QColor("#d9c7a0"));
        drawGlyph(g, QStringLiteral("🏡"), 56, hx, hy);
    }

    drawSkyLanes(g, game);

    if (map.night) {
        // Moonlight: a cool blue wash, a soft glow in the corner and the moon itself.
        g.fillRect(QRectF(0, 0, W, H), rgba(10, 20, 60, 0.38));
        QRadialGradient glow = radial(W - 70, 60, 0, W - 70, 60, 260);
        glow.setColorAt(0, rgba(220, 230, 255, 0.28));
        glow.setColorAt(1, rgba(220, 230, 255, 0));
        g.fillRect(QRectF(0, 0, W, H), glow);
        drawGlyph(g, QStringLiteral("🌙"), 40, W - 70, 60);
    }

    // Soft vignette.
    QRadialGradient vg = radial(W / 2, H / 2, H * 0.45, W / 2, H / 2, W * 0.68);
    vg.setColorAt(0, rgba(20, 40, 10, 0));
    vg.setColorAt(1, map.night ? rgba(0, 0, 20, 0.45) : rgba(20, 40, 10, 0.22));
    g.fillRect(QRectF(0, 0, W, H), vg);
    g.end();
    return pm;
}

// Fireflies drifting over night maps: each follows its own slow looping path and blinks.
void Renderer::drawFireflies(QPainter &p, const Game &g) {
    if (!g.map().night) return;
    const double t = g.time;
    for (int i = 0; i < 28; i++) {
        double phase = i * 2.39996;   // golden angle spreads them out
        double x = std::fmod(80 + i * 137.0 + 40 * std::sin(t * 0.31 + phase) + t * (6 + i % 5), W);
        double y = std::fmod(40 + i * 89.0 + 30 * std::cos(t * 0.27 + phase * 1.3), H);
        if (y < 0) y += H;
        double blink = 0.5 + 0.5 * std::sin(t * (1.6 + (i % 7) * 0.23) + phase);
        if (blink < 0.15) continue;
        QRadialGradient glow(QPointF(x, y), 9);
        glow.setColorAt(0, rgba(255, 250, 170, 0.75 * blink));
        glow.setColorAt(0.35, rgba(220, 255, 120, 0.35 * blink));
        glow.setColorAt(1, rgba(200, 255, 100, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(QPointF(x, y), 9, 9);
    }
}

// ---------- layers ----------

void Renderer::drawGrid(QPainter &p) {
    p.setPen(QPen(rgba(255, 255, 255, 0.16), 1));
    for (int c = 1; c < data::COLS; c++) p.drawLine(QPointF(c * TILE, 0), QPointF(c * TILE, H));
    for (int r = 1; r < data::ROWS; r++) p.drawLine(QPointF(0, r * TILE), QPointF(W, r * TILE));
}

void Renderer::drawPreview(QPainter &p, const View &view) {
    const auto &h = *view.hover;
    if (h.col < 0 || h.col >= data::COLS || h.row < 0 || h.row >= data::ROWS) return;
    const data::TowerDef &def = data::TOWERS[view.placing];
    double x = h.col * TILE, y = h.row * TILE;
    double cx = x + TILE / 2, cy = y + TILE / 2;

    p.setPen(QPen(h.valid ? rgba(220, 255, 220, 0.95) : rgba(255, 180, 180, 0.95), 2));
    p.setBrush(h.valid ? rgba(130, 255, 130, 0.3) : rgba(255, 80, 80, 0.32));
    p.drawRoundedRect(QRectF(x + 3, y + 3, TILE - 6, TILE - 6), 10, 10);

    if (def.levels[0].range > 0) rangeCircle(p, cx, cy, def.levels[0].range * TILE, h.valid);
    p.setOpacity(0.6);
    const QPixmap &pm = pot(view.placing);
    p.drawPixmap(QRectF(x, y, TILE, TILE), pm, pm.rect());
    drawGlyph(p, def.icon, TOWER_ICON, cx, cy - 2);
    p.setOpacity(1);
}

void Renderer::drawRanges(QPainter &p, const Game &g, const View &view) {
    if (view.selectedId < 0) return;
    for (const auto &t : g.towers) {
        if (t.id != view.selectedId) continue;
        double range = data::TOWERS[t.type].levels[t.level].range;
        if (range > 0) rangeCircle(p, t.x, t.y, range * TILE, true);
        break;
    }
}

void Renderer::drawTowers(QPainter &p, const Game &g, const View &view) {
    const bool showHover = view.placing < 0 && view.hover.has_value();
    for (const auto &t : g.towers) {
        const data::TowerDef &def = data::TOWERS[t.type];
        const double cx = t.x, cy = t.y;
        const double ca = std::cos(t.angle), sa = std::sin(t.angle);

        if (t.id == view.selectedId) {
            strokeCircle(p, cx, cy, POT_R + 6, rgba(255, 240, 140, 0.35), 7);
            strokeCircle(p, cx, cy, POT_R + 6, QColor("#fff1a0"), 3);
        } else if (showHover && view.hover->col == t.col && view.hover->row == t.row) {
            strokeCircle(p, cx, cy, POT_R + 5, rgba(255, 255, 255, 0.7), 2);
        }

        const QPixmap &pm = pot(t.type);
        p.drawPixmap(QRectF(cx - TILE / 2, cy - TILE / 2, TILE, TILE), pm, pm.rect());

        const bool attacks = def.projectile != data::Projectile::None &&
                             def.projectile != data::Projectile::Aura;
        double scale = 1, ox = 0, oy = 0;

        if (def.projectile == data::Projectile::Aura && !t.stunned(g.time)) {   // gooed clovers lose their aura
            // Clover: a soft green aura ripples out to the edge of its range.
            const double R = def.levels[t.level].range * TILE;
            double ph = std::fmod(g.time * 0.6 + t.id * 0.37, 1.0);
            strokeCircle(p, cx, cy, R * ph, rgba(90, 220, 130, 0.45 * (1 - ph)), 2.5);
            strokeCircle(p, cx, cy, R, rgba(90, 220, 130, 0.18), 1.5);
        }
        if (t.level >= 2) {
            // Max level flourish: three golden sparkles orbit the pot.
            for (int i = 0; i < 3; i++) {
                double a = g.time * 1.8 + i * 2 * M_PI / 3 + t.id;
                double sx = cx + std::cos(a) * (POT_R + 7), sy = cy + std::sin(a) * (POT_R + 4);
                double tw = 0.6 + 0.4 * std::sin(g.time * 7 + i * 2.1);
                fillCircle(p, sx, sy, 3.2 * tw, rgba(255, 225, 90, 0.35));
                fillCircle(p, sx, sy, 1.6 * tw, rgba(255, 250, 200, 0.95));
            }
        }

        if (attacks) {
            // Aim indicator on the rim.
            double ax = cx + ca * (POT_R + 1), ay = cy + sa * (POT_R + 1);
            QPolygonF tri;
            tri << QPointF(ax + ca * 6, ay + sa * 6)
                << QPointF(ax - ca * 3 - sa * 4.5, ay - sa * 3 + ca * 4.5)
                << QPointF(ax - ca * 3 + sa * 4.5, ay - sa * 3 - ca * 4.5);
            p.setPen(QPen(rgba(255, 255, 255, 0.9), 1.2));
            p.setBrush(QColor(def.color));
            p.drawPolygon(tri);
        } else {
            // Sunflowers gently sway instead of aiming.
            scale = 1 + 0.05 * std::sin(g.time * 2.2 + t.id);
            oy = 1.5 * std::sin(g.time * 1.7 + t.id * 0.7);
        }

        // Plant, with a little "kick" right after a shot.
        double k = g.time - t.shotTime;
        if (t.shotTime >= 0 && k >= 0 && k < KICK_TIME) {
            double f = 1 - k / KICK_TIME;
            scale = 1 + 0.22 * f;
            ox = -ca * 3 * f;
            oy = -sa * 3 * f;
        }
        drawGlyph(p, def.icon, TOWER_ICON, cx + ox, cy - 2 + oy, scale);

        if (t.stunned(g.time)) {
            // Gooed by a scorpion: greyed out, a green blob on top, and a ring showing time left.
            const double left = clamp01((t.stunnedUntil - g.time) / std::max(0.1, data::ENEMIES[data::Scorpion].stunTime));
            // Grey wash keeps the plant recognizable; the goo sits on its top-left like a splat.
            fillCircle(p, cx, cy, POT_R + 1, rgba(60, 66, 60, 0.6));
            const double bx = cx - 9, by = cy - 11;
            p.setPen(QPen(QColor("#2f6b12"), 1.5));
            p.setBrush(QColor("#8fdc3c"));
            QPainterPath blob;
            blob.addEllipse(QPointF(bx, by), 8, 6);
            blob.addEllipse(QPointF(bx - 5, by + 4), 4, 4);
            blob.addEllipse(QPointF(bx + 6, by + 3), 4, 3.5);
            p.drawPath(blob.simplified());
            double drip = 3 + 5 * std::fmod(g.time * 1.5 + t.id, 1.0);
            fillCircle(p, bx - 4, by + 7 + drip, 2.2, QColor("#8fdc3c"));
            fillCircle(p, bx - 2, by - 2, 1.8, rgba(255, 255, 255, 0.8));
            // Time-left ring: dark backing so it reads on any grass.
            const QRectF ring(cx - POT_R - 4, cy - POT_R - 4, 2 * POT_R + 8, 2 * POT_R + 8);
            p.setBrush(Qt::NoBrush);
            p.setPen(roundPen(rgba(20, 40, 10, 0.55), 5));
            p.drawEllipse(ring);
            p.setPen(roundPen(QColor("#b6ff6a"), 3));
            p.drawArc(QRectF(cx - POT_R - 4, cy - POT_R - 4, 2 * POT_R + 8, 2 * POT_R + 8), 90 * 16, int(left * 360 * 16));
        }

        // Level pips.
        int n = std::clamp(t.level + 1, 1, 3);
        double py = cy + TILE * 0.4;
        p.setPen(QPen(QColor("#6b4a12"), 1));
        p.setBrush(QColor("#ffd84d"));
        for (int i = 0; i < n; i++)
            p.drawEllipse(QPointF(cx + (i - (n - 1) / 2.0) * 8, py), 2.7, 2.7);
    }
}

void Renderer::drawEnemies(QPainter &p, const Game &g) {
    const double time = g.time;
    // Ground bugs first, flyers on top.
    std::vector<const Enemy *> order;
    for (const auto &e : g.enemies) if (!data::ENEMIES[e.type].flying) order.push_back(&e);
    for (const auto &e : g.enemies) if (data::ENEMIES[e.type].flying) order.push_back(&e);
    for (const Enemy *ep : order) {
        const Enemy &e = *ep;
        const data::EnemyDef &def = data::ENEMIES[e.type];
        const double size = (def.boss ? BOSS_SIZE : ENEMY_SIZE) * def.scale;
        const bool slowed = e.slowed(time);
        const bool poisoned = e.poisoned(time);
        const double bob = std::sin(time * (slowed ? 8 : 14) + e.id * 1.7);

        // Flyers hover above their shadow: butterflies flutter side to side, bats swoop up and down.
        double alt = 0, side = 0;
        if (def.flying) {
            alt = g.visualY(e);
            if (e.type == data::Bat) alt += 5 * std::sin(time * 2.4 + e.id);
            else side = 6 * std::sin(time * 3.5 + e.id);
        }
        const double gx = e.x - std::sin(e.angle) * side, gy = e.y + std::cos(e.angle) * side;

        // Ground shadow (smaller and fainter under flyers), frost ring and poison ring.
        if (def.flying) fillEllipse(p, gx, gy + size * 0.3, size * 0.26, size * 0.09, 0, rgba(0, 0, 0, 0.16));
        else fillEllipse(p, e.x, e.y + size * 0.3, size * 0.38, size * 0.14, 0, rgba(0, 0, 0, 0.2));
        p.setBrush(Qt::NoBrush);
        if (slowed) {
            p.setPen(QPen(rgba(170, 230, 255, 0.95), 2));
            p.drawEllipse(QPointF(gx, gy + size * 0.3), size * 0.5, size * 0.2);
        }
        if (poisoned) {
            p.setPen(QPen(rgba(150, 230, 80, 0.9), 2));
            p.drawEllipse(QPointF(gx, gy + size * 0.3), size * 0.58, size * 0.24);
        }

        p.save();
        // Cricket hop: a quick arc up off the ground (the shadow stays put).
        const double hopK = time - e.hopStart;
        const double lift = (e.hopStart >= 0 && hopK >= 0 && hopK < 0.35 ? std::sin(M_PI * hopK / 0.35) * 20 : 0) + alt;
        if (def.flying) {
            // Stay upright (front-view sprites) and flap: squash the wings in and out.
            // Soft pale halo so dark flyers (bats at night) read against the ground.
            QRadialGradient halo(QPointF(gx, gy - lift), size * 0.62);
            halo.setColorAt(0, rgba(255, 252, 225, 0.42));
            halo.setColorAt(1, rgba(255, 252, 225, 0.0));
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(QPointF(gx, gy - lift), size * 0.62, size * 0.62);
            p.translate(gx, gy - lift);
            double flap = std::abs(std::cos(time * (e.type == data::Bat ? 9 : 14) + e.id));
            p.scale(0.72 + 0.28 * flap, 1);
        } else if (def.facing == data::Facing::Up) {
            p.translate(e.x, e.y + bob * 0.9 - lift);
            // Top-down sprite: just turn its head toward the direction of travel.
            p.rotate(deg(e.angle + M_PI / 2 + bob * 0.07));
        } else {
            p.translate(e.x, e.y + bob * 0.9 - lift);
            // Side view: rotate, but keep it upright when walking leftward.
            p.rotate(deg(e.angle + bob * 0.07));
            if (std::cos(e.angle) < 0) p.scale(1, -1);
            if (def.facing == data::Facing::Left) p.scale(-1, 1);
        }
        const double box = size * GLYPH_PAD;
        const QRectF target(-box / 2, -box / 2, box, box);
        const QPixmap &pm = glyph(def.icon, size);
        p.drawPixmap(target, pm, pm.rect());
        if (slowed) {
            p.setOpacity(0.42);
            const QPixmap &ice = tint(def.icon, size, QColor("#7fd3ff"));
            p.drawPixmap(target, ice, ice.rect());
        }
        if (poisoned) {
            // Sickly green tint that pulses.
            p.setOpacity(0.3 + 0.12 * std::sin(time * 9 + e.id));
            const QPixmap &sick = tint(def.icon, size, QColor("#8be04e"));
            p.drawPixmap(target, sick, sick.rect());
        }
        if (def.golden) {
            // Shimmering gold leaf.
            p.setOpacity(0.55 + 0.2 * std::sin(time * 7 + e.id));
            const QPixmap &gold = tint(def.icon, size, QColor("#ffcc33"));
            p.drawPixmap(target, gold, gold.rect());
        }
        if (def.spitEvery > 0 && e.abilityTimer > 0 && e.abilityTimer < 0.45) {
            // Scorpion wind-up: a growing green glow just before it spits, so players can react.
            p.setOpacity(0.65 * (1 - e.abilityTimer / 0.45));
            const QPixmap &wind = tint(def.icon, size, QColor("#9cff4a"));
            p.drawPixmap(target, wind, wind.rect());
        }
        const double hk = time - e.healTime;
        if (e.healTime >= 0 && hk >= 0 && hk < 0.4) {
            p.setOpacity(0.6 * (1 - hk / 0.4));
            const QPixmap &glow = tint(def.icon, size, QColor("#7dffa8"));
            p.drawPixmap(target, glow, glow.rect());
        }
        double k = time - e.hitTime;
        if (e.hitTime >= 0 && k >= 0 && k < FLASH_TIME) {
            p.setOpacity(0.8 * (1 - k / FLASH_TIME));
            const QPixmap &flash = tint(def.icon, size, Qt::white);
            p.drawPixmap(target, flash, flash.rect());
        }
        p.setOpacity(1);
        p.restore();

        if (def.golden) {
            // Four twinkling sparkles orbiting the golden beetle.
            p.setPen(roundPen(QColor("#fff3a0"), 1.6));
            for (int s = 0; s < 4; s++) {
                double ang = time * 2.2 + s * TAU / 4 + e.id;
                double tw = 0.5 + 0.5 * std::sin(time * 9 + s * 2.1);
                double sx = e.x + std::cos(ang) * size * 0.55, sy = e.y + std::sin(ang) * size * 0.4 - lift;
                double r = 2 + 3 * tw;
                p.setOpacity(0.4 + 0.6 * tw);
                p.drawLine(QPointF(sx - r, sy), QPointF(sx + r, sy));
                p.drawLine(QPointF(sx, sy - r), QPointF(sx, sy + r));
            }
            p.setOpacity(1);
        }

        // HP bar.
        if (e.hp < e.maxHp && e.maxHp > 0) {
            double ratio = clamp01(e.hp / e.maxHp);
            double bw = size * 0.85, bh = def.boss ? 5 : 4;
            double bx = gx - bw / 2, by = gy - alt - size * 0.62 - bh;
            p.fillRect(QRectF(bx - 1, by - 1, bw + 2, bh + 2), rgba(20, 20, 20, 0.65));
            p.fillRect(QRectF(bx, by, bw * ratio, bh),
                       QColor(ratio > 0.5 ? "#5ee35e" : ratio > 0.25 ? "#ffd24d" : "#ff5a4d"));
        }
    }

    // Scorpion goo in flight: a lobbed green glob with a dotted trail and a ground shadow.
    for (const auto &s : g.spits) {
        const double arc = std::sin(M_PI * s.progress) * 26;
        fillEllipse(p, s.x, s.y + 4, 5, 2, 0, rgba(0, 0, 0, 0.22));
        for (int k = 1; k <= 3; k++) {
            double q = std::max(0.0, s.progress - k * 0.07);
            double tx = s.fromX + (s.toX - s.fromX) * q, ty = s.fromY + (s.toY - s.fromY) * q;
            fillCircle(p, tx, ty - std::sin(M_PI * q) * 26, 3.2 - k * 0.7, rgba(120, 200, 60, 0.6 - k * 0.15));
        }
        p.setPen(QPen(QColor("#2f6b12"), 1.5));
        p.setBrush(QColor("#8fdc3c"));
        p.drawEllipse(QPointF(s.x, s.y - arc), 5.5, 5);
        fillCircle(p, s.x - 1.8, s.y - arc - 1.8, 1.6, rgba(255, 255, 255, 0.7));
    }
}

void Renderer::drawShots(QPainter &p, const Game &g) {
    for (const auto &s : g.shots) {
        const double ca = std::cos(s.angle), sa = std::sin(s.angle);
        switch (s.kind) {
        case data::Projectile::Bolt:
            p.setPen(QPen(QColor("#2f7d1f"), 1.5));
            p.setBrush(QColor("#72d84a"));
            p.drawEllipse(QPointF(s.x, s.y), 4.5, 4.5);
            fillCircle(p, s.x - 1.4, s.y - 1.4, 1.4, rgba(255, 255, 255, 0.75));
            break;

        case data::Projectile::Frost: {
            p.setPen(roundPen(rgba(180, 235, 255, 0.5), 4));
            p.drawLine(QPointF(s.x - ca * 16, s.y - sa * 16), QPointF(s.x, s.y));
            QPolygonF diamond;
            diamond << QPointF(s.x + ca * 7, s.y + sa * 7) << QPointF(s.x - sa * 4.5, s.y + ca * 4.5)
                    << QPointF(s.x - ca * 7, s.y - sa * 7) << QPointF(s.x + sa * 4.5, s.y - ca * 4.5);
            p.setPen(QPen(QColor("#3fa9d6"), 1.5));
            p.setBrush(QColor("#e6f8ff"));
            p.drawPolygon(diamond);
            break;
        }

        case data::Projectile::Spike: {
            p.setPen(roundPen(QColor("#3b3a2a"), 2.4));
            p.drawLine(QPointF(s.x - ca * 9, s.y - sa * 9), QPointF(s.x + ca * 6, s.y + sa * 6));
            QPolygonF tip;
            tip << QPointF(s.x + ca * 11, s.y + sa * 11)
                << QPointF(s.x + ca * 5 - sa * 2.2, s.y + sa * 5 + ca * 2.2)
                << QPointF(s.x + ca * 5 + sa * 2.2, s.y + sa * 5 - ca * 2.2);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#26261b"));
            p.drawPolygon(tip);
            break;
        }

        case data::Projectile::Mortar: {
            double pr = clamp01(s.progress);
            double gx = s.fromX + (s.toX - s.fromX) * pr;
            double gy = s.fromY + (s.toY - s.fromY) * pr;
            double lift = std::sin(pr * M_PI) * MORTAR_ARC;
            double hf = lift / MORTAR_ARC;
            fillEllipse(p, gx, gy, 8 - 3.5 * hf, 3.2 - 1.4 * hf, 0, rgba(0, 0, 0, 0.32 - 0.16 * hf));
            p.setPen(QPen(QColor("#9b4a12"), 1.6));
            p.setBrush(QColor("#f0883e"));
            p.drawEllipse(QPointF(gx, gy - lift), 7, 7);
            fillCircle(p, gx - 2.2, gy - lift - 2.4, 2.2, rgba(255, 255, 255, 0.55));
            break;
        }

        case data::Projectile::Spore: {
            // Fuzzy purple puff that wobbles as it drifts.
            double wob = std::sin(g.time * 18 + s.id) * 0.8;
            fillCircle(p, s.x, s.y, 8 + wob, rgba(190, 120, 230, 0.25));
            p.setPen(QPen(QColor("#6d2f91"), 1.2));
            p.setBrush(QColor("#c08ae6"));
            p.drawEllipse(QPointF(s.x, s.y), 4.5, 4.5);
            fillCircle(p, s.x - ca * 6 - 2, s.y - sa * 6 + 1, 1.6, rgba(200, 150, 240, 0.6));
            fillCircle(p, s.x - ca * 9 + 2, s.y - sa * 9 - 1, 1.1, rgba(200, 150, 240, 0.4));
            break;
        }

        case data::Projectile::Thorn: {
            // Curved pink-green thorn with a faint petal trail.
            fillCircle(p, s.x - ca * 12, s.y - sa * 12, 2.4, rgba(240, 120, 160, 0.35));
            fillCircle(p, s.x - ca * 19, s.y - sa * 19, 1.6, rgba(240, 120, 160, 0.2));
            p.setPen(roundPen(QColor("#4c7a2a"), 2.6));
            p.drawLine(QPointF(s.x - ca * 8, s.y - sa * 8), QPointF(s.x + ca * 4, s.y + sa * 4));
            QPolygonF tip;
            tip << QPointF(s.x + ca * 10, s.y + sa * 10)
                << QPointF(s.x + ca * 3 - sa * 3, s.y + sa * 3 + ca * 3)
                << QPointF(s.x + ca * 3 + sa * 3, s.y + sa * 3 - ca * 3);
            p.setPen(QPen(QColor("#8a1c3f"), 1));
            p.setBrush(QColor("#d6336c"));
            p.drawPolygon(tip);
            break;
        }

        case data::Projectile::Kernel: {
            // Popcorn: a lumpy white puff that spins as it flies.
            double spin = g.time * 14 + s.id;
            p.setPen(QPen(QColor("#c89b1c"), 1));
            p.setBrush(QColor("#fff6d6"));
            for (int i = 0; i < 3; i++) {
                double a = spin + i * 2.1;
                p.drawEllipse(QPointF(s.x + std::cos(a) * 2.4, s.y + std::sin(a) * 2.4), 3, 3);
            }
            fillCircle(p, s.x, s.y, 1.8, QColor("#f5c518"));
            break;
        }

        case data::Projectile::Flame:   // drawn as an effect, not a projectile
        case data::Projectile::Aura:
        case data::Projectile::None:
            break;
        }
    }
}

void Renderer::drawEffects(QPainter &p, const Game &g) {
    for (const auto &fx : g.effects) {
        const double u = fx.ttl > 0 ? clamp01(fx.t / fx.ttl) : 1;
        const double a = 1 - u;
        const double eu = easeOut(u);
        switch (fx.kind) {
        case EffectKind::Hit: {
            p.setOpacity(a);
            p.setPen(roundPen(QColor("#fff6c8"), 2));
            double r0 = 3 + 7 * eu, r1 = r0 + 2 + 5 * a;
            for (int k = 0; k < 6; k++) {
                double ang = k * TAU / 6 + 0.3;
                double c = std::cos(ang), sn = std::sin(ang);
                p.drawLine(QPointF(fx.x + c * r0, fx.y + sn * r0), QPointF(fx.x + c * r1, fx.y + sn * r1));
            }
            break;
        }

        case EffectKind::Splash: {
            double R = (fx.radius > 0 ? fx.radius : TILE) * (0.35 + 0.65 * eu);
            p.setOpacity(0.4 * a);
            fillCircle(p, fx.x, fx.y, R, QColor("#ffb35c"));
            p.setOpacity(a);
            strokeCircle(p, fx.x, fx.y, R, QColor("#e0681c"), 1 + 3 * a);
            for (int k = 0; k < 8; k++) {
                double ang = k * TAU / 8 + 0.2;
                fillCircle(p, fx.x + std::cos(ang) * R * 0.8,
                           fx.y + std::sin(ang) * R * 0.8 - 10 * std::sin(u * M_PI), 2, QColor("#7a5030"));
            }
            break;
        }

        case EffectKind::Kill: {
            p.setOpacity(0.85 * a);
            double d = 4 + 14 * eu, r = 6 * (1 - u * 0.5);
            for (int k = 0; k < 5; k++) {
                double ang = k * TAU / 5 + 0.3;
                fillCircle(p, fx.x + std::cos(ang) * d, fx.y + std::sin(ang) * d, r, QColor("#f4f4f4"));
            }
            break;
        }

        case EffectKind::Coin: {
            p.setOpacity(u < 0.7 ? 1 : (1 - u) / 0.3);
            QFont f;
            f.setPixelSize(15);
            f.setBold(true);
            QFontMetricsF fm(f);
            double y = fx.y - 10 - 26 * eu;
            QPainterPath text;
            text.addText(fx.x - fm.horizontalAdvance(fx.text) / 2, y + (fm.ascent() - fm.descent()) / 2, f, fx.text);
            p.strokePath(text, roundPen(QColor("#5a3b00"), 3.5));
            p.fillPath(text, QColor("#ffd84d"));
            break;
        }

        case EffectKind::Leak: {
            double r = 10 + 36 * eu;
            p.setOpacity(0.25 * a);
            fillCircle(p, fx.x, fx.y, r, QColor("#ff4d4d"));
            p.setOpacity(a);
            strokeCircle(p, fx.x, fx.y, r, QColor("#ff3b3b"), 1 + 4 * a);
            break;
        }

        case EffectKind::Place:
            p.setOpacity(a);
            strokeCircle(p, fx.x, fx.y, 10 + 26 * eu, QColor("#c4ff9a"), 3);
            break;

        case EffectKind::Slow: {
            p.setOpacity(a);
            p.setPen(roundPen(QColor("#e2f7ff"), 1.5));
            double d = 8 + 10 * eu, sz = 1 + 3 * a;
            for (int k = 0; k < 4; k++) {
                double ang = k * TAU / 4 + u * 1.5;
                double x = fx.x + std::cos(ang) * d, y = fx.y + std::sin(ang) * d;
                p.drawLine(QPointF(x - sz, y), QPointF(x + sz, y));
                p.drawLine(QPointF(x, y - sz), QPointF(x, y + sz));
            }
            break;
        }

        case EffectKind::Poison: {
            // Green bubbles rising off the bug.
            p.setOpacity(a);
            for (int k = 0; k < 4; k++) {
                double bx = fx.x + (k - 1.5) * 6 + std::sin(u * 6 + k) * 2;
                double by = fx.y - 4 - (10 + k * 3) * eu;
                double r = 1.5 + (k % 2) * 1.2;
                p.setPen(QPen(QColor("#3f7d1a"), 1));
                p.setBrush(QColor("#9be05a"));
                p.drawEllipse(QPointF(bx, by), r, r);
            }
            break;
        }

        case EffectKind::Flame: {
            // A cone of fire that flares out and fades.
            const double R = fx.radius * (0.55 + 0.45 * eu);
            const double half = data::FLAME_HALF_ANGLE;
            QPainterPath cone(QPointF(fx.x, fx.y));
            QRectF box(fx.x - R, fx.y - R, 2 * R, 2 * R);
            // Qt angles: degrees, counter-clockwise; screen y points down, so negate.
            cone.arcTo(box, -deg(fx.angle + half), deg(2 * half));
            cone.closeSubpath();
            QRadialGradient fire(QPointF(fx.x, fx.y), R);
            fire.setColorAt(0, rgba(255, 245, 160, 0.95));
            fire.setColorAt(0.45, rgba(255, 150, 40, 0.85));
            fire.setColorAt(1, rgba(220, 40, 20, 0.0));
            p.setOpacity(a);
            p.setPen(Qt::NoPen);
            p.setBrush(fire);
            p.drawPath(cone);
            break;
        }

        case EffectKind::Heal: {
            // Mosquito pulse: a soft green ring with little crosses floating up.
            double R = fx.radius * (0.3 + 0.7 * eu);
            p.setOpacity(0.18 * a);
            fillCircle(p, fx.x, fx.y, R, QColor("#7dffa8"));
            p.setOpacity(0.8 * a);
            strokeCircle(p, fx.x, fx.y, R, QColor("#3ccf6e"), 2);
            p.setPen(roundPen(QColor("#2fbf5f"), 2.2));
            for (int k = 0; k < 3; k++) {
                double cx = fx.x + (k - 1) * 12, cy = fx.y - 10 - 18 * eu - (k % 2) * 6;
                p.drawLine(QPointF(cx - 3.5, cy), QPointF(cx + 3.5, cy));
                p.drawLine(QPointF(cx, cy - 3.5), QPointF(cx, cy + 3.5));
            }
            break;
        }

        case EffectKind::Hop: {
            // Dust kicked up at take-off.
            p.setOpacity(0.6 * a);
            for (int k = 0; k < 5; k++) {
                double ang = M_PI + (k - 2) * 0.45;
                double d = 4 + 12 * eu;
                fillCircle(p, fx.x + std::cos(ang) * d * (k % 2 ? 1 : -1), fx.y + 8 - 4 * eu + std::sin(ang) * 2,
                           3.5 * (1 - u * 0.5), QColor("#c9b48a"));
            }
            break;
        }

        case EffectKind::Split: {
            // Caterpillar bursts: green goo splat flinging droplets.
            p.setOpacity(0.7 * a);
            fillCircle(p, fx.x, fx.y, fx.radius * (0.4 + 0.3 * eu), QColor("#8fd14f"));
            for (int k = 0; k < 7; k++) {
                double ang = k * TAU / 7 + 0.4;
                double d = fx.radius * (0.3 + 1.1 * eu);
                fillCircle(p, fx.x + std::cos(ang) * d, fx.y + std::sin(ang) * d, 3 * (1 - u * 0.6), QColor("#5fa82e"));
            }
            break;
        }

        case EffectKind::Jackpot: {
            // Golden starburst plus a big floating "JACKPOT!".
            p.setOpacity(a);
            p.setPen(roundPen(QColor("#ffd84d"), 3));
            double r0 = 8 + 20 * eu, r1 = r0 + 10 * a;
            for (int k = 0; k < 12; k++) {
                double ang = k * TAU / 12 + u;
                double c = std::cos(ang), sn = std::sin(ang);
                p.drawLine(QPointF(fx.x + c * r0, fx.y + sn * r0), QPointF(fx.x + c * r1, fx.y + sn * r1));
            }
            for (int k = 0; k < 8; k++) {   // coins tumbling out
                double ang = k * TAU / 8 + 0.2;
                double d = 10 + 34 * eu;
                double cy = fx.y + std::sin(ang) * d - 30 * std::sin(u * M_PI) + 20 * u * u;
                p.setPen(QPen(QColor("#6b4a12"), 1));
                p.setBrush(QColor("#ffd84d"));
                p.drawEllipse(QPointF(fx.x + std::cos(ang) * d, cy), 4, 4);
            }
            p.setOpacity(u < 0.75 ? 1 : (1 - u) / 0.25);
            QFont f;
            f.setPixelSize(20);
            f.setBold(true);
            QFontMetricsF fm(f);
            double y = fx.y - 30 - 30 * eu;
            QPainterPath text;
            text.addText(fx.x - fm.horizontalAdvance(fx.text) / 2, y, f, fx.text);
            p.strokePath(text, roundPen(QColor("#5a3b00"), 4));
            p.fillPath(text, QColor("#ffe066"));
            break;
        }

        case EffectKind::FlyerFall: {
            // Flyer shot down: tilts, spins and drops to the ground while shrinking and fading.
            const double drop = fx.radius * u * u + 6 * u;
            p.save();
            p.setOpacity(a);
            p.translate(fx.x + 6 * std::sin(u * 5), fx.y + drop);
            p.rotate(deg(u * 2.4 * M_PI));
            const double sz = ENEMY_SIZE * (1 - 0.45 * u);
            const QPixmap &pm = glyph(fx.text, sz);
            p.drawPixmap(QRectF(-sz * GLYPH_PAD / 2, -sz * GLYPH_PAD / 2, sz * GLYPH_PAD, sz * GLYPH_PAD), pm, pm.rect());
            p.restore();
            p.setOpacity(0.5 * u * a * 2);
            fillCircle(p, fx.x, fx.y + fx.radius + ENEMY_SIZE * 0.3, 6 * u, rgba(200, 190, 160, 0.6));   // little puff on landing
            break;
        }

        case EffectKind::BossDust: {
            // Boss entrance: puffs of dust roll out from the nest and a soft ground ring.
            p.setOpacity(0.55 * a);
            strokeCircle(p, fx.x, fx.y, fx.radius * (0.3 + 0.9 * eu), QColor("#d9c49a"), 3 * a + 1);
            for (int k = 0; k < 9; k++) {
                double ang = k * TAU / 9 + 0.2;
                double d = fx.radius * (0.2 + 0.8 * eu);
                fillCircle(p, fx.x + std::cos(ang) * d, fx.y + std::sin(ang) * d * 0.6 - 6 * eu,
                           6 + 6 * eu, rgba(200, 180, 140, 0.7));
            }
            break;
        }

        case EffectKind::Goo: {
            // Goo lands on a plant: green splat with flung droplets.
            p.setOpacity(0.85 * a);
            fillCircle(p, fx.x, fx.y, fx.radius * (0.5 + 0.4 * eu), rgba(140, 220, 60, 0.5));
            for (int k = 0; k < 8; k++) {
                double ang = k * TAU / 8 + 0.3;
                double d = fx.radius * (0.4 + 0.9 * eu);
                fillCircle(p, fx.x + std::cos(ang) * d, fx.y + std::sin(ang) * d, 3.2 * (1 - u * 0.5), QColor("#6cbf2a"));
            }
            break;
        }
        }
    }
    p.setOpacity(1);
}

// ---------- public ----------

// Sky lanes: where flying bugs will come from, drawn as faint dashed trails with
// chevrons pointing toward the house, so players can plan anti-air coverage.
void Renderer::drawSkyLanes(QPainter &g, const Game &game) {
    for (const auto &lane : game.map().skyLanes) {
        if (lane.size() < 2) continue;
        QPainterPath trail;
        auto px = [](const std::array<double, 2> &t) { return QPointF(t[0] * TILE + TILE / 2, t[1] * TILE + TILE / 2); };
        trail.moveTo(px(lane[0]));
        for (size_t i = 1; i < lane.size(); i++) trail.lineTo(px(lane[i]));
        QPen glow = roundPen(rgba(180, 225, 255, 0.18), 14);
        g.setBrush(Qt::NoBrush);
        g.setPen(glow);
        g.drawPath(trail);
        QPen dash = roundPen(rgba(255, 255, 255, 0.55), 2.5);
        dash.setDashPattern({3, 3});
        g.setPen(dash);
        g.drawPath(trail);
        // Chevrons every ~1.5 tiles along the lane.
        const double len = trail.length();
        g.setPen(roundPen(rgba(255, 255, 255, 0.7), 2.2));
        for (double d = TILE; d < len - TILE * 0.5; d += TILE * 1.5) {
            double pct = trail.percentAtLength(d);
            QPointF at = trail.pointAtPercent(pct);
            double a = -trail.angleAtPercent(pct) * M_PI / 180;   // Qt: degrees, counter-clockwise
            QPointF tip = at + QPointF(std::cos(a), std::sin(a)) * 5;
            QPointF back = at - QPointF(std::cos(a), std::sin(a)) * 3;
            QPointF side(-std::sin(a) * 5, std::cos(a) * 5);
            g.drawLine(back + side, tip);
            g.drawLine(back - side, tip);
        }
    }
}

// Fire points: a target ring where each ambushing plant is watching, a dashed
// aim line for the selected plant, and a live preview while choosing one.
void Renderer::drawFirePoints(QPainter &p, const Game &g, const View &view) {
    auto reticle = [&](double x, double y, const QColor &c, double alpha) {
        QColor col = c;
        col.setAlphaF(alpha);
        QPen ring(col, 2);
        ring.setDashPattern({3, 2});
        p.setBrush(Qt::NoBrush);
        p.setPen(ring);
        p.drawEllipse(QPointF(x, y), FIRE_POINT_RADIUS, FIRE_POINT_RADIUS);
        p.setPen(QPen(col, 2));
        p.drawEllipse(QPointF(x, y), 7, 7);
        for (int k = 0; k < 4; k++) {
            double a = k * M_PI / 2;
            p.drawLine(QPointF(x + std::cos(a) * 10, y + std::sin(a) * 10),
                       QPointF(x + std::cos(a) * 16, y + std::sin(a) * 16));
        }
    };
    for (const Tower &t : g.towers) {
        if (!t.hasFirePoint) continue;
        const bool focus = t.id == view.selectedId || t.id == view.aimingId;
        if (focus) {
            QPen line(rgba(255, 90, 70, 0.8), 2);
            line.setDashPattern({4, 3});
            p.setPen(line);
            p.drawLine(QPointF(t.x, t.y), QPointF(t.fireX, t.fireY));
        }
        reticle(t.fireX, t.fireY, QColor("#ff5a46"), focus ? 0.95 : 0.5);
    }
    // Choosing a fire point: preview at the hovered tile (green = in range).
    if (view.aimingId >= 0 && view.hover) {
        for (const Tower &t : g.towers) {
            if (t.id != view.aimingId) continue;
            double x = view.hover->col * TILE + TILE / 2, y = view.hover->row * TILE + TILE / 2;
            double range = data::TOWERS[t.type].levels[t.level].range * TILE;
            bool ok = std::hypot(x - t.x, y - t.y) <= range;
            reticle(x, y, ok ? QColor("#7dff7d") : QColor("#ff6b6b"), 0.95);
        }
    }
}

void Renderer::draw(QPainter &p, const Game &game, const View &view, double scale) {
    setupScale(scale, game);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(1);
    p.drawPixmap(QRectF(0, 0, W, H), bg_, bg_.rect());

    if (view.placing >= 0) drawGrid(p);
    if (view.placing >= 0 && view.hover) drawPreview(p, view);
    drawRanges(p, game, view);
    drawFirePoints(p, game, view);
    drawTowers(p, game, view);
    drawEnemies(p, game);
    drawShots(p, game);
    drawEffects(p, game);
    drawFireflies(p, game);
    drawBossBar(p, game);
}

// Boss HP bar across the top of the board: name, then one segment per living boss.
void Renderer::drawBossBar(QPainter &p, const Game &g) {
    std::vector<const Enemy *> bosses;
    for (const auto &e : g.enemies)
        if (e.alive && data::ENEMIES[e.type].boss) bosses.push_back(&e);
    if (bosses.empty()) return;
    std::sort(bosses.begin(), bosses.end(), [](const Enemy *a, const Enemy *b) { return a->id < b->id; });

    const double bw = 340, bh = 12, gap = 6;
    const double x0 = (W - bw) / 2, y0 = H - 22;   // bottom edge: the top is used by toasts
    // Panel.
    p.setOpacity(1);
    p.setPen(Qt::NoPen);
    p.setBrush(rgba(30, 22, 14, 0.62));
    p.drawRoundedRect(QRectF(x0 - 10, y0 - 22, bw + 20, bh + 30), 9, 9);
    // Label.
    QFont f;
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor("#ffe7a8"));
    const QString name = data::ENEMIES[bosses.front()->type].icon + "  " + data::ENEMIES[bosses.front()->type].name +
                         (bosses.size() > 1 ? QStringLiteral(" ×%1").arg(bosses.size()) : QString());
    p.drawText(QRectF(x0, y0 - 20, bw, 18), Qt::AlignCenter, name);
    // Segments.
    const int n = int(bosses.size());
    const double segW = (bw - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        const Enemy &e = *bosses[i];
        const double sx = x0 + i * (segW + gap);
        const double ratio = clamp01(e.hp / std::max(1.0, e.maxHp));
        p.fillRect(QRectF(sx - 1, y0 - 1, segW + 2, bh + 2), rgba(10, 8, 6, 0.85));
        QLinearGradient grad(sx, y0, sx, y0 + bh);
        grad.setColorAt(0, QColor("#ff6a4d"));
        grad.setColorAt(1, QColor("#c22d1e"));
        p.fillRect(QRectF(sx, y0, segW * ratio, bh), grad);
        // Gold rim and a flash when hit.
        p.setPen(QPen(QColor("#e8b84a"), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(sx - 1, y0 - 1, segW + 2, bh + 2));
        double k = g.time - e.hitTime;
        if (e.hitTime >= 0 && k >= 0 && k < 0.12) {
            p.setOpacity(0.5 * (1 - k / 0.12));
            p.fillRect(QRectF(sx, y0, segW * ratio, bh), Qt::white);
            p.setOpacity(1);
        }
    }
}
