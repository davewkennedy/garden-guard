#include "renderer.h"

#include "data.h"
#include "game.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QTransform>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr double TAU = 2 * M_PI;
constexpr double TILE = data::TILE;
double W = 960;   // current board size in logical px (set per map in setupScale)
double H = 640;
int GRID_COLS = 15, GRID_ROWS = 10;   // current map grid (set per map in setupScale)
// Readability boost on big boards (set in setupScale): markers/text, plants, bugs.
double FX_UI = 1, FX_PLANT = 1, FX_BUG = 1;

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

// ---------- seasons ----------
// Flower palettes per season (index by data::Season). Winter has no flowers: snow instead.
const char *const SPRING_FLOWERS[] = {"#ffffff", "#ffc8dd", "#ff8fb1", "#e7c6ff", "#fff1a8"};
const char *const SUMMER_FLOWERS[] = {"#ff3b3b", "#ffd21f", "#ff7a00", "#ff4fa3", "#8a5cff"};
const char *const AUTUMN_FLOWERS[] = {"#e8a33a", "#c94f2a", "#f2c14e", "#9b5de5", "#d9622b"};
const char *const LEAF_COLORS[] = {"#d9622b", "#e8a33a", "#b5432a", "#c98a2e", "#8f4a22"};

// A small pointed leaf with a center vein, `size` = half its length.
void drawLeaf(QPainter &g, double x, double y, double size, double rot, const QColor &c) {
    g.save();
    g.translate(x, y);
    g.rotate(deg(rot));
    QPainterPath leaf(QPointF(-size, 0));
    leaf.quadTo(QPointF(0, -size * 0.75), QPointF(size, 0));
    leaf.quadTo(QPointF(0, size * 0.75), QPointF(-size, 0));
    g.setPen(Qt::NoPen);
    g.setBrush(c);
    g.drawPath(leaf);
    g.setPen(QPen(c.darker(150), 0.7));
    g.drawLine(QPointF(-size * 0.85, 0), QPointF(size * 0.8, 0));
    g.restore();
}

// Soft-edged snow drift.
void drawSnowPatch(QPainter &g, double x, double y, double rx, double ry) {
    QRadialGradient s(QPointF(0, 0), rx);
    s.setColorAt(0, rgba(252, 253, 255, 0.92));
    s.setColorAt(0.65, rgba(238, 245, 255, 0.75));
    s.setColorAt(1, rgba(238, 245, 255, 0));
    g.save();
    g.translate(x, y);
    g.scale(1, ry / rx);
    g.setPen(Qt::NoPen);
    g.setBrush(s);
    g.drawEllipse(QPointF(0, 0), rx, rx);
    g.restore();
}

// A five-petal blossom (spring trees drop these).
void drawBlossom(QPainter &g, double x, double y, double r, double rot, const QColor &c) {
    for (int k = 0; k < 5; k++) {
        double a = rot + k * TAU / 5;
        fillCircle(g, x + std::cos(a) * r, y + std::sin(a) * r, r * 0.85, c);
    }
    fillCircle(g, x, y, r * 0.55, QColor("#ffe27a"));
}

}  // namespace

// ---------- scaling & caches ----------

void Renderer::setupScale(double scale, const Game &game) {
    // This map's board size (grids differ per map; tiles are always 64 logical px).
    W = game.W;
    H = game.H;
    GRID_COLS = game.cols;
    GRID_ROWS = game.rows;
    // Big boards are shown smaller: grow sprites and markers gently so they stay readable,
    // capped so plants stay within ~0.8 tile and bugs ~0.75 tile.
    FX_UI = std::clamp(std::sqrt(W / 960.0), 1.0, 1.5);
    FX_PLANT = std::min(FX_UI, 0.8 / (2 * POT_R / TILE));
    FX_BUG = std::min(FX_UI, 0.75 / (ENEMY_SIZE / TILE));
    // Round to quarter steps so window resizing doesn't rebuild the caches constantly.
    // Big boards are drawn at a small scale; allow down to 0.25 so caches stay sharp enough.
    double want = std::clamp(std::round(scale * 4) / 4, 0.25, 4.0);
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
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
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

    // Season wash over the grass (under the path, so the path keeps its color).
    const data::Season season = map.season;
    Rng srand(map.seed ^ 0x5EA5011u);   // separate stream: seasons don't reshuffle the base layout
    switch (season) {
    case data::Season::Spring: break;
    case data::Season::Summer: g.fillRect(QRectF(0, 0, W, H), rgba(255, 225, 90, 0.07)); break;
    case data::Season::Autumn: g.fillRect(QRectF(0, 0, W, H), rgba(225, 130, 35, 0.08)); break;
    case data::Season::Winter: {
        g.fillRect(QRectF(0, 0, W, H), rgba(215, 228, 245, 0.22));
        // Frost: pale glints on the grass.
        for (int i = 0; i < GRID_COLS * GRID_ROWS * 5; i++)
            fillCircle(g, srand() * W, srand() * H, 0.6 + srand() * 0.9, rgba(255, 255, 255, 0.35 + srand() * 0.3));
        break;
    }
    }

    // Terrain: ponds (merged into blobs, icy in winter), lily pads, boulders.
    {
        Rng trand(map.seed ^ 0x7E44A1Du);
        const bool icy = season == data::Season::Winter;
        QPainterPath pond;
        std::vector<QPointF> lilies, rocks;
        for (int r = 0; r < game.rows; r++)
            for (int c = 0; c < game.cols; c++) {
                const int k = game.terrainAt(c, r);
                const QPointF ctr(c * TILE + TILE / 2, r * TILE + TILE / 2);
                if (k == data::TERRAIN_WATER || k == data::TERRAIN_LILY) {
                    QPainterPath tile;   // solid inset tiles; the thick round-join outline rounds the blob
                    tile.addRect(QRectF(c * TILE + 8, r * TILE + 8, TILE - 16, TILE - 16));
                    QPainterPath grow;   // bridge to the right/down neighbours so tiles merge seamlessly
                    auto isPond = [&](int cc, int rr) {
                        int kk = game.terrainAt(cc, rr);
                        return kk == data::TERRAIN_WATER || kk == data::TERRAIN_LILY;
                    };
                    if (isPond(c + 1, r)) grow.addRect(QRectF(c * TILE + 8, r * TILE + 8, TILE, TILE - 16));
                    if (isPond(c, r + 1)) grow.addRect(QRectF(c * TILE + 8, r * TILE + 8, TILE - 16, TILE));
                    if (isPond(c + 1, r) && isPond(c, r + 1) && isPond(c + 1, r + 1))
                        grow.addRect(QRectF(c * TILE + 8, r * TILE + 8, TILE, TILE));
                    pond = pond.united(tile).united(grow);
                    if (k == data::TERRAIN_LILY) lilies.push_back(ctr);
                } else if (k == data::TERRAIN_ROCK) {
                    rocks.push_back(ctr);
                }
            }
        if (!pond.isEmpty()) {
            pond = pond.simplified();
            const QRectF b = pond.boundingRect().adjusted(-12, -12, 12, 12);
            QLinearGradient water(b.topLeft(), b.bottomRight());
            water.setColorAt(0, icy ? QColor("#e4f2fa") : QColor("#5fb3d9"));
            water.setColorAt(1, icy ? QColor("#bcd9ea") : QColor("#3b8fbf"));
            // Bank, then water: thick round-join strokes grow the inset blob by ~8 px with rounded corners.
            g.setBrush(Qt::NoBrush);
            g.setPen(QPen(icy ? QColor("#8fb4c8") : QColor("#3d7f6a"), 22, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            g.drawPath(pond);
            g.setPen(QPen(QBrush(water), 16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            g.setBrush(water);
            g.drawPath(pond);
            QPainterPathStroker grow;
            grow.setWidth(16);
            grow.setJoinStyle(Qt::RoundJoin);
            const QPainterPath full = pond.united(grow.createStroke(pond));
            g.save();
            g.setClipPath(full);
            for (int i = 0; i < int(b.width() * b.height() / 1400); i++) {
                const double x = b.left() + trand() * b.width(), y = b.top() + trand() * b.height();
                if (icy) {   // white streaks and a crack or two
                    g.setPen(roundPen(rgba(255, 255, 255, 0.7), 1.5));
                    g.drawLine(QPointF(x, y), QPointF(x + 10 + trand() * 12, y - 4 - trand() * 4));
                } else {     // gentle ripples
                    g.setPen(roundPen(rgba(220, 245, 255, 0.55), 1.4));
                    g.drawArc(QRectF(x - 9, y - 3, 18, 7), 20 * 16, 140 * 16);
                }
            }
            g.restore();
        }
        for (const QPointF &c : lilies) {
            // A lily pad: green disc with a notch, a darker vein, sometimes a pink flower.
            const double R = TILE * 0.36, rot = trand() * 360;
            g.setPen(QPen(QColor("#2f7a2f"), 1.5));
            g.setBrush(icy ? QColor("#7fae7a") : QColor("#58b04a"));
            g.drawPie(QRectF(c.x() - R, c.y() - R, 2 * R, 2 * R), int(rot * 16), int(320 * 16));
            g.setPen(roundPen(rgba(30, 90, 30, 0.5), 1.2));
            const double a = (rot + 160) * M_PI / 180;
            g.drawLine(c, QPointF(c.x() + std::cos(a) * R * 0.8, c.y() - std::sin(a) * R * 0.8));
            if (!icy && trand() < 0.5) {
                const QPointF f(c.x() + R * 0.35, c.y() - R * 0.3);
                for (int k = 0; k < 5; k++) {
                    double pa = k * 2 * M_PI / 5;
                    fillCircle(g, f.x() + std::cos(pa) * 3.2, f.y() + std::sin(pa) * 3.2, 2.8, QColor("#f7a8c8"));
                }
                fillCircle(g, f.x(), f.y(), 2, QColor("#ffe066"));
            }
        }
        for (const QPointF &c : rocks) {
            // A boulder: shadow, two or three grey stones with a highlight.
            fillEllipse(g, c.x() + 3, c.y() + 12, TILE * 0.38, TILE * 0.14, 0, rgba(0, 0, 0, 0.22));
            const int n = 2 + int(trand() * 2);
            for (int k = 0; k < n; k++) {
                const double ox = (k - (n - 1) / 2.0) * 13 + (trand() - 0.5) * 6;
                const double oy = (trand() - 0.5) * 8 + (k == 1 ? -4 : 2);
                const double rx = 13 + trand() * 6 - k * 1.5, ry = 10 + trand() * 4 - k;
                const int shade = 120 + int(trand() * 40);
                g.setPen(QPen(QColor(70, 70, 75), 1.4));
                g.setBrush(icy ? QColor(shade + 50, shade + 58, shade + 68) : QColor(shade, shade, shade + 6));
                g.drawEllipse(QPointF(c.x() + ox, c.y() + oy), rx, ry);
                fillEllipse(g, c.x() + ox - rx * 0.3, c.y() + oy - ry * 0.4, rx * 0.35, ry * 0.25, 0,
                            rgba(255, 255, 255, icy ? 0.75 : 0.4));
            }
        }
    }

    // Dirt path, plus the optional second entrance (claude2-0c) drawn the same way.
    const std::vector<QPointF> &pathPts = game.path;
    QPainterPath route(pathPts[0]);
    for (size_t i = 1; i < pathPts.size(); i++) route.lineTo(pathPts[i]);
    if (game.path2.size() >= 2) {
        route.moveTo(game.path2[0]);
        for (size_t i = 1; i < game.path2.size(); i++) route.lineTo(game.path2[i]);
    }
    g.setBrush(Qt::NoBrush);
    if (season == data::Season::Winter) {   // snowbank along the path edges
        g.setPen(roundPen(rgba(250, 252, 255, 0.75), TILE * 1.0));
        g.drawPath(route);
    }
    for (auto [width, color] : {std::pair{0.92, rgba(50, 35, 15, 0.22)},
                                std::pair{0.84, QColor("#93653b")},
                                std::pair{0.74, QColor("#c99a62")},
                                std::pair{0.40, rgba(228, 190, 135, 0.45)}}) {
        g.setPen(roundPen(color, TILE * width));
        g.drawPath(route);
    }

    // Dirt specks and pebbles along the path (each entrance).
    for (const std::vector<QPointF> *routePts : {&game.path, &game.path2}) {
    const std::vector<QPointF> &pathPts = *routePts;
    if (pathPts.size() < 2) continue;
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
    }

    // Decorations on grass tiles.
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            double roll = rand();
            double cx = c * TILE + 12 + rand() * (TILE - 24);
            double cy = r * TILE + 12 + rand() * (TILE - 24);
            if (game.isPathTile(c, r) || game.terrainAt(c, r) >= 0) continue;   // no flowers on ponds/rocks

            if (roll < 0.17 && season == data::Season::Winter) {   // snow drift instead of flowers
                drawSnowPatch(g, cx, cy, TILE * (0.18 + rand() * 0.14), TILE * (0.1 + rand() * 0.06));
            } else if (roll < 0.17) {   // flowers
                const char *const *palette = season == data::Season::Spring   ? SPRING_FLOWERS
                                             : season == data::Season::Summer ? SUMMER_FLOWERS
                                             : season == data::Season::Autumn ? AUTUMN_FLOWERS
                                                                              : FLOWER_COLORS;
                int n = 1 + int(rand() * 3);
                for (int i = 0; i < n; i++) {
                    double fx = cx + (rand() - 0.5) * 14, fy = cy + (rand() - 0.5) * 14;
                    QColor color(palette[int(rand() * 5)]);
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
            } else if (roll < 0.45) {   // grass tuft (frosted in winter, golden in autumn)
                g.setPen(roundPen(QColor(season == data::Season::Winter   ? "#a9c9b4"
                                         : season == data::Season::Autumn ? "#a58a3a"
                                                                          : "#4f9a3b"),
                                  1.6));
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

    // Season scatter: blossoms, fallen leaves or snow drifts (grass only, small and flat so
    // plants and bugs stay readable on top).
    if (season != data::Season::Summer) {
        for (int r = 0; r < GRID_ROWS; r++) {
            for (int c = 0; c < GRID_COLS; c++) {
                const double roll = srand();
                const double cx = c * TILE + TILE * (0.15 + srand() * 0.7);
                const double cy = r * TILE + TILE * (0.15 + srand() * 0.7);
                const bool onPath = game.isPathTile(c, r) || game.terrainAt(c, r) >= 0;
                if (season == data::Season::Spring) {
                    if (onPath || roll > 0.16) continue;
                    // A drift of fallen blossom petals.
                    int n = 3 + int(srand() * 4);
                    for (int i = 0; i < n; i++)
                        drawBlossom(g, cx + (srand() - 0.5) * TILE * 0.4, cy + (srand() - 0.5) * TILE * 0.3,
                                    TILE * 0.03, srand() * TAU,
                                    srand() < 0.6 ? QColor("#ffc2d6") : QColor("#fff4f8"));
                } else if (season == data::Season::Autumn) {
                    // Leaf piles on grass; the odd stray leaf blown onto the path.
                    if (onPath ? roll > 0.08 : roll > 0.2) continue;
                    int n = onPath ? 1 : 2 + int(srand() * (roll < 0.12 ? 6 : 2));
                    for (int i = 0; i < n; i++)
                        drawLeaf(g, cx + (srand() - 0.5) * TILE * 0.35, cy + (srand() - 0.5) * TILE * 0.3,
                                 TILE * (0.06 + srand() * 0.03), srand() * TAU,
                                 QColor(LEAF_COLORS[int(srand() * 5)]));
                } else {   // winter
                    if (onPath || roll > 0.22) continue;
                    drawSnowPatch(g, cx, cy, TILE * (0.22 + srand() * 0.2), TILE * (0.12 + srand() * 0.08));
                }
            }
        }
    }

    // Bug nest just inside the edge where each entrance's path comes in.
    for (const std::vector<QPointF> *routePts : {&game.path, &game.path2}) {
        if (routePts->size() < 2) continue;
        const QPointF &first = (*routePts)[0], &second = (*routePts)[1];
        double dir = second.x() > first.x() ? 1 : (second.x() < first.x() ? -1 : 0);
        double dirY = dir != 0 ? 0 : second.y() > first.y() ? 1 : (second.y() < first.y() ? -1 : 0);
        double nx = std::clamp(first.x() + dir * TILE * 1.3, TILE * 0.32, W - TILE * 0.32);
        double ny = std::clamp(first.y() + dirY * TILE * 1.3, TILE * 0.32, H - TILE * 0.32);
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

    // (Immersive: the vignette is drawn over the whole widget in draw(), not baked in here.)
    g.end();
    return pm;
}

// ---------- outer garden (immersive: the garden past the board, claude2-3e) ----------
// Same look and layout as the web build (js/renderer.js buildOuter): drawn per tile with a
// tile-seeded Rng so the layout doesn't depend on the window size, cached as one pixmap.

namespace {

Rng tileRng(uint32_t seed, int c, int r) {
    uint32_t h = seed ^ (uint32_t(c + 4096) * 0x9E3779B1u) ^ (uint32_t(r + 4096) * 0x85EBCA77u);
    h = (h ^ (h >> 16)) * 0x7FEB352Du;
    return Rng(h ^ (h >> 15));
}

struct ExtRoute { QPointF head0, head1, tail0, tail1; const std::vector<QPointF> *pts; };

std::vector<ExtRoute> extendedRoutes(const Game &game) {
    constexpr double FAR = 8000;
    std::vector<ExtRoute> out;
    for (const std::vector<QPointF> *pts : {&game.path, &game.path2}) {
        if (pts->size() < 2) continue;
        auto away = [](const QPointF &a, const QPointF &b) {
            double dx = a.x() - b.x(), dy = a.y() - b.y(), l = std::hypot(dx, dy);
            if (l <= 0) l = 1;
            return QPointF(a.x() + dx / l * FAR, a.y() + dy / l * FAR);
        };
        const size_t n = pts->size();
        out.push_back({away((*pts)[0], (*pts)[1]), (*pts)[0], (*pts)[n - 1], away((*pts)[n - 1], (*pts)[n - 2]), pts});
    }
    return out;
}

double distToSeg(double px, double py, const QPointF &a, const QPointF &b) {
    double dx = b.x() - a.x(), dy = b.y() - a.y(), l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? clamp01(((px - a.x()) * dx + (py - a.y()) * dy) / l2) : 0;
    return std::hypot(px - (a.x() + dx * t), py - (a.y() + dy * t));
}

bool nearRoute(const std::vector<ExtRoute> &routes, double x, double y, double d) {
    for (const ExtRoute &R : routes) {
        if (distToSeg(x, y, R.head0, R.head1) < d || distToSeg(x, y, R.tail0, R.tail1) < d) return true;
        for (size_t k = 0; k + 1 < R.pts->size(); k++)
            if (distToSeg(x, y, (*R.pts)[k], (*R.pts)[k + 1]) < d) return true;
    }
    return false;
}

void drawBush(QPainter &g, double x, double y, double s, data::Season season, Rng &rand) {
    static const char *const AUT[] = {"#b5532a", "#d9822b", "#c9a23a", "#8f4a22"};
    static const char *const WIN[] = {"#4f7a5e", "#5f8a6c", "#6f9a7a", "#3f6a4e"};
    static const char *const SUM[] = {"#3f8f2f", "#4fa23a", "#2f7a2a", "#5bb04a"};
    static const char *const SPR[] = {"#4fa547", "#5cb853", "#3f9240", "#6cc25e"};
    const char *const *greens = season == data::Season::Autumn ? AUT : season == data::Season::Winter ? WIN
                              : season == data::Season::Summer ? SUM : SPR;
    fillEllipse(g, x + 2, y + s * 0.55, s * 1.25, s * 0.42, 0, rgba(0, 0, 0, 0.18));
    int n = 5 + int(rand() * 3);
    for (int k = 0; k < n; k++) {
        double a = double(k) / n * TAU + rand() * 0.5;
        double d = s * (0.35 + rand() * 0.3);
        double rr = s * (0.48 + rand() * 0.2);
        fillCircle(g, x + std::cos(a) * d * 1.2, y + std::sin(a) * d * 0.6 - s * 0.15, rr, QColor(greens[k % 4]));
    }
    fillCircle(g, x, y - s * 0.3, s * 0.55, QColor(greens[1]));
    fillCircle(g, x - s * 0.25, y - s * 0.5, s * 0.22, rgba(255, 255, 255, 0.16));
    if (season == data::Season::Winter) {
        drawSnowPatch(g, x, y - s * 0.55, s * 0.75, s * 0.3);
    } else if (season == data::Season::Spring && rand() < 0.6) {
        for (int k = 0; k < 4; k++) {
            double bx = x + (rand() - 0.5) * s * 1.4;
            double by = y - s * 0.2 + (rand() - 0.5) * s * 0.6;
            double rot = rand() * TAU;
            drawBlossom(g, bx, by, s * 0.07, rot, QColor(rand() < 0.5 ? "#ffc2d6" : "#ffffff"));
        }
    } else if (season == data::Season::Summer && rand() < 0.4) {
        for (int k = 0; k < 5; k++) {
            double bx = x + (rand() - 0.5) * s * 1.4;
            double by = y - s * 0.2 + (rand() - 0.5) * s * 0.6;
            fillCircle(g, bx, by, s * 0.08, QColor("#e8342c"));
        }
    }
}

// Whole garden rect the particles (weather, fireflies, wind) live in; the board when not immersive.
double DOM_X = 0, DOM_Y = 0, DOM_W = 960, DOM_H = 640;

}  // namespace

QPixmap Renderer::buildOuter(const Game &game, int c0, int r0, int c1, int r1) {
    const data::MapDef &map = game.map();
    const data::Season season = map.season;
    const QRectF O(c0 * TILE, r0 * TILE, (c1 - c0) * TILE, (r1 - r0) * TILE);
    // Resolution: the board's, within ~8 Mpx.
    const double res = std::min(dpr_, std::sqrt(8e6 / (O.width() * O.height())));
    QImage img(int(std::ceil(O.width() * res)), int(std::ceil(O.height() * res)), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter g(&img);
    g.setRenderHint(QPainter::Antialiasing);
    g.scale(res, res);
    g.translate(-O.left(), -O.top());
    const uint32_t seed = map.seed;
    const auto routes = extendedRoutes(game);
    auto onBoard = [](int c, int r) { return c >= 0 && r >= 0 && c < GRID_COLS && r < GRID_ROWS; };
    QPainterPath outside;
    outside.setFillRule(Qt::OddEvenFill);
    outside.addRect(O);
    outside.addRect(QRectF(0, 0, W, H));
    const QColor grassA(map.grassA), grassB(map.grassB);

    // Grass tiles (the board's checker + variation, continued outward).
    for (int r = r0; r < r1; r++)
        for (int c = c0; c < c1; c++) {
            if (onBoard(c, r)) continue;
            Rng tr = tileRng(seed, c, r);
            QRectF tile(c * TILE, r * TILE, TILE, TILE);
            g.fillRect(tile, ((r + c) % 2 + 2) % 2 ? grassA : grassB);
            double v = tr();
            g.fillRect(tile, v < 0.5 ? rgba(255, 255, 220, (0.5 - v) * 0.12) : rgba(0, 50, 0, (v - 0.5) * 0.1));
        }
    // Blades (~7 per tile, like the board's 1100 per 150 tiles).
    {
        QPainterPath dark, light;
        for (int r = r0; r < r1; r++)
            for (int c = c0; c < c1; c++) {
                if (onBoard(c, r)) continue;
                Rng br = tileRng(seed ^ 0xB1ADEu, c, r);
                for (int i = 0; i < 7; i++) {
                    double x = c * TILE + br() * TILE;
                    double y = r * TILE + br() * TILE;
                    QPainterPath &bp = br() < 0.5 ? dark : light;
                    double lean = (br() - 0.5) * 3;
                    double len = 3 + br() * 4;
                    bp.moveTo(x, y);
                    bp.lineTo(x + lean, y - len);
                }
            }
        g.setBrush(Qt::NoBrush);
        g.setPen(roundPen(rgba(40, 110, 40, 0.35), 1.2));
        g.drawPath(dark);
        g.setPen(roundPen(rgba(190, 240, 140, 0.35), 1.2));
        g.drawPath(light);
    }

    // Season wash + winter frost.
    g.save();
    g.setClipPath(outside);
    if (season == data::Season::Summer) g.fillRect(O, rgba(255, 225, 90, 0.07));
    else if (season == data::Season::Autumn) g.fillRect(O, rgba(225, 130, 35, 0.08));
    else if (season == data::Season::Winter) {
        g.fillRect(O, rgba(215, 228, 245, 0.22));
        QPainterPath frost[3];
        for (int r = r0; r < r1; r++)
            for (int c = c0; c < c1; c++) {
                if (onBoard(c, r)) continue;
                Rng fr = tileRng(seed ^ 0xF205u, c, r);
                for (int i = 0; i < 5; i++) {
                    double fx = c * TILE + fr() * TILE;
                    double fy = r * TILE + fr() * TILE;
                    double rad = 0.6 + fr() * 0.9;
                    frost[std::min(2, int(fr() * 3))].addEllipse(QPointF(fx, fy), rad, rad);
                }
            }
        for (int k = 0; k < 3; k++) g.fillPath(frost[k], rgba(255, 255, 255, 0.4 + k * 0.1));
    }
    g.restore();

    // The path, continued off-screen from the entrance(s) and past the house (the whole route;
    // the board covers its inner part, so it meets the board's path seamlessly).
    {
        QPainterPath ext;
        for (const ExtRoute &R : routes) {
            ext.moveTo(R.head0);
            for (const QPointF &q : *R.pts) ext.lineTo(q);
            ext.lineTo(R.tail1);
        }
        g.setBrush(Qt::NoBrush);
        if (season == data::Season::Winter) {
            g.setPen(roundPen(rgba(250, 252, 255, 0.75), TILE * 1.0));
            g.drawPath(ext);
        }
        for (auto [width, color] : {std::pair{0.92, rgba(50, 35, 15, 0.22)}, std::pair{0.84, QColor("#93653b")},
                                    std::pair{0.74, QColor("#c99a62")}, std::pair{0.40, rgba(228, 190, 135, 0.45)}}) {
            g.setPen(roundPen(color, TILE * width));
            g.drawPath(ext);
        }
        // Specks and pebbles along the visible part of each extension.
        QPainterPath pebbles, darkSpecks, lightSpecks;
        Rng prand(seed ^ 0x9A7E5u);
        for (const ExtRoute &R : routes)
            for (auto [a, b] : {std::pair{R.head0, R.head1}, std::pair{R.tail0, R.tail1}}) {
                double L = std::hypot(b.x() - a.x(), b.y() - a.y());
                if (L <= 0) L = 1;
                const double ux = (b.x() - a.x()) / L, uy = (b.y() - a.y()) / L;
                for (double d = 0; d < L; d += 9) {
                    const double x0 = a.x() + ux * d, y0 = a.y() + uy * d;
                    if (x0 < O.left() - TILE || y0 < O.top() - TILE || x0 > O.right() + TILE || y0 > O.bottom() + TILE) continue;
                    const double off = (prand() - 0.5) * TILE * 0.6;
                    const double x = x0 - uy * off, y = y0 + ux * off;
                    const double kind = prand();
                    if (kind < 0.08) {
                        double rx = 2.5 + prand() * 2;
                        double ry = 1.8 + prand() * 1.2;
                        double rot = prand() * 3;
                        QTransform t;
                        t.translate(x, y);
                        t.rotateRadians(rot);
                        QPainterPath e;
                        e.addEllipse(QPointF(0, 0), rx, ry);
                        pebbles.addPath(t.map(e));
                    } else {
                        double sr = 0.7 + prand() * 1.5;
                        (kind < 0.55 ? darkSpecks : lightSpecks).addEllipse(QPointF(x, y), sr, sr);
                    }
                }
            }
        g.fillPath(pebbles, QColor("#a49c90"));
        g.fillPath(darkSpecks, rgba(120, 80, 40, 0.45));
        g.fillPath(lightSpecks, rgba(235, 205, 160, 0.5));
    }

    // Decorations: the board's flowers/pebbles/tufts and season scatter, plus bushes and trees
    // further out (never on the path).
    const char *const *palette = season == data::Season::Spring ? SPRING_FLOWERS : season == data::Season::Summer ? SUMMER_FLOWERS
                               : season == data::Season::Autumn ? AUTUMN_FLOWERS : FLOWER_COLORS;
    struct Prop { bool tree; double x, y, s; uint32_t seed; };
    std::vector<Prop> props;
    for (int r = r0; r < r1; r++)
        for (int c = c0; c < c1; c++) {
            if (onBoard(c, r)) continue;
            Rng dr = tileRng(seed ^ 0xDEC0u, c, r);
            const double roll = dr();
            const double cx = c * TILE + 12 + dr() * (TILE - 24);
            const double cy = r * TILE + 12 + dr() * (TILE - 24);
            if (nearRoute(routes, c * TILE + TILE / 2, r * TILE + TILE / 2, TILE * 0.75)) continue;
            const int dist = std::max(c < 0 ? -c : c >= GRID_COLS ? c - GRID_COLS + 1 : 0,
                                      r < 0 ? -r : r >= GRID_ROWS ? r - GRID_ROWS + 1 : 0);
            const double big = dr();
            if (dist >= 2 && big < 0.07) {
                double s = 0.85 + dr() * 0.35;
                props.push_back({true, cx, cy + 8, s, 0});
                continue;
            }
            const double edgeGap = std::max({-cx, cx - W, -cy, cy - H});
            if (dist >= 1 && big < 0.15 && edgeGap > TILE * 0.6) {
                double s = TILE * (0.22 + dr() * 0.12);
                uint32_t bs = uint32_t(dr() * 4294967296.0);
                props.push_back({false, cx, cy, s, bs});
                continue;
            }
            if (roll < 0.17 && season == data::Season::Winter) {
                double rx = TILE * (0.18 + dr() * 0.14);
                double ry = TILE * (0.1 + dr() * 0.06);
                drawSnowPatch(g, cx, cy, rx, ry);
            } else if (roll < 0.17) {
                int n = 1 + int(dr() * 3);
                for (int i = 0; i < n; i++) {
                    double px = cx + (dr() - 0.5) * 14;
                    double py = cy + (dr() - 0.5) * 14;
                    QColor color(palette[int(dr() * 5)]);
                    double frot = dr() * TAU;
                    g.setPen(roundPen(rgba(40, 110, 40, 0.6), 1.2));
                    g.drawLine(QPointF(px, py), QPointF(px, py + 5));
                    for (int k = 0; k < 5; k++) {
                        double a = frot + k * TAU / 5;
                        fillCircle(g, px + std::cos(a) * 2.8, py + std::sin(a) * 2.8, 2.3, color);
                    }
                    fillCircle(g, px, py, 1.7, QColor("#ffb627"));
                }
            } else if (roll < 0.27) {
                int np = 2 + int(dr() * 2);
                for (int i = 0; i < np; i++) {
                    double qx = cx + (dr() - 0.5) * 14;
                    double qy = cy + (dr() - 0.5) * 10;
                    double prx = 2.5 + dr() * 2.5;
                    double prot = dr() * 3;
                    fillEllipse(g, qx, qy, prx, prx * 0.7, prot, QColor("#b3ada3"));
                    g.save();
                    g.translate(qx, qy);
                    g.rotate(deg(prot));
                    g.setPen(QPen(QColor("#7d776f"), 0.8));
                    g.setBrush(Qt::NoBrush);
                    g.drawEllipse(QPointF(0, 0), prx, prx * 0.7);
                    g.restore();
                    fillCircle(g, qx - prx * 0.3, qy - prx * 0.25, prx * 0.3, rgba(255, 255, 255, 0.35));
                }
            } else if (roll < 0.45) {
                g.setPen(roundPen(QColor(season == data::Season::Winter ? "#a9c9b4" : season == data::Season::Autumn ? "#a58a3a" : "#4f9a3b"), 1.6));
                g.setBrush(Qt::NoBrush);
                for (int k = 0; k < 5; k++) {
                    double ba = -M_PI / 2 + (k - 2) * 0.32 + (dr() - 0.5) * 0.2;
                    double bl = 6 + dr() * 4;
                    QPainterPath blade(QPointF(cx, cy + 4));
                    blade.quadTo(QPointF(cx + std::cos(ba) * bl * 0.5, cy + 4 + std::sin(ba) * bl * 0.6),
                                 QPointF(cx + std::cos(ba) * bl, cy + 4 + std::sin(ba) * bl));
                    g.drawPath(blade);
                }
            }
            // Season scatter.
            const double sr2 = dr();
            const double scx = c * TILE + TILE * (0.15 + dr() * 0.7);
            const double scy = r * TILE + TILE * (0.15 + dr() * 0.7);
            if (season == data::Season::Spring && sr2 < 0.16) {
                int nb = 3 + int(dr() * 4);
                for (int i = 0; i < nb; i++) {
                    double bx = scx + (dr() - 0.5) * TILE * 0.4;
                    double by = scy + (dr() - 0.5) * TILE * 0.3;
                    double brot = dr() * TAU;
                    drawBlossom(g, bx, by, TILE * 0.03, brot, QColor(dr() < 0.6 ? "#ffc2d6" : "#fff4f8"));
                }
            } else if (season == data::Season::Autumn && sr2 < 0.2) {
                int nl = 2 + int(dr() * (sr2 < 0.12 ? 6 : 2));
                for (int i = 0; i < nl; i++) {
                    double lx = scx + (dr() - 0.5) * TILE * 0.35;
                    double ly = scy + (dr() - 0.5) * TILE * 0.3;
                    double lsz = TILE * (0.06 + dr() * 0.03);
                    double lrot = dr() * TAU;
                    drawLeaf(g, lx, ly, lsz, lrot, QColor(LEAF_COLORS[int(dr() * 5)]));
                }
            } else if (season == data::Season::Winter && sr2 < 0.22) {
                double rx = TILE * (0.22 + dr() * 0.2);
                double ry = TILE * (0.12 + dr() * 0.08);
                drawSnowPatch(g, scx, scy, rx, ry);
            }
        }
    std::stable_sort(props.begin(), props.end(), [](const Prop &a, const Prop &b) { return a.y < b.y; });
    for (const Prop &p : props) {
        if (!p.tree) {
            Rng br(p.seed);
            drawBush(g, p.x, p.y, p.s, season, br);
        } else {
            const bool pine = season == data::Season::Winter || (season == data::Season::Autumn && int(p.x * 7 + p.y) % 3 == 0);
            fillEllipse(g, p.x + 3, p.y + 26 * p.s, 26 * p.s, 8 * p.s, 0, rgba(0, 0, 0, 0.2));
            drawGlyph(g, pine ? QStringLiteral("🌲") : QStringLiteral("🌳"), 58 * p.s, p.x, p.y);
            if (season == data::Season::Winter) drawSnowPatch(g, p.x, p.y - 4 * p.s, 16 * p.s, 6 * p.s);
        }
    }

    // Night: the board's wash and moon glow, continued.
    if (map.night) {
        g.save();
        g.setClipPath(outside);
        g.fillRect(O, rgba(10, 20, 60, 0.38));
        QRadialGradient glow = radial(W - 70, 60, 0, W - 70, 60, 260);
        glow.setColorAt(0, rgba(220, 230, 255, 0.28));
        glow.setColorAt(1, rgba(220, 230, 255, 0));
        g.fillRect(O, glow);
        g.restore();
    }
    g.end();

    // Plantable cue: the garden past the grid a little duller (like the web's canvas
    // 'saturation' blend with 22% gray: mix toward luminance) and darker, plus a soft shade
    // falling off the board's edge (like a slightly raised bed).
    {
        const QRect board = QRectF(-O.left() * res, -O.top() * res, W * res, H * res).toAlignedRect();
        for (int y = 0; y < img.height(); y++) {
            QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            const bool inRows = y >= board.top() && y <= board.bottom();
            for (int x = 0; x < img.width(); x++) {
                if (inRows && x >= board.left() && x <= board.right()) continue;
                const QRgb px = row[x];
                const int r = qRed(px), gg = qGreen(px), b = qBlue(px);
                const double lum = 0.3 * r + 0.59 * gg + 0.11 * b;
                row[x] = qRgba(int(r + (lum - r) * 0.22), int(gg + (lum - gg) * 0.22), int(b + (lum - b) * 0.22), qAlpha(px));
            }
        }
    }
    QPainter h(&img);
    h.setRenderHint(QPainter::Antialiasing);
    h.scale(res, res);
    h.translate(-O.left(), -O.top());
    h.fillPath(outside, map.night ? rgba(0, 0, 20, 0.12) : rgba(20, 35, 10, 0.12));
    const double E = 18;
    struct Edge { QRectF rect; QPointF a, b; };
    for (const Edge &q : {Edge{QRectF(0, -E, W, E), QPointF(0, 0), QPointF(0, -E)},
                          Edge{QRectF(0, H, W, E), QPointF(0, H), QPointF(0, H + E)},
                          Edge{QRectF(-E, 0, E, H), QPointF(0, 0), QPointF(-E, 0)},
                          Edge{QRectF(W, 0, E, H), QPointF(W, 0), QPointF(W + E, 0)}}) {
        QLinearGradient sg(q.a, q.b);
        sg.setColorAt(0, rgba(20, 35, 10, 0.2));
        sg.setColorAt(1, rgba(20, 35, 10, 0));
        h.fillRect(q.rect, sg);
    }
    h.end();
    return QPixmap::fromImage(img);
}

void Renderer::setupOuter(const Game &game, const QRectF &ow) {
    // Tiles covering the widget (+1 tile), and always at least one ring around the board.
    const int c0 = std::min(-1, int(std::floor(ow.left() / TILE)) - 1);
    const int r0 = std::min(-1, int(std::floor(ow.top() / TILE)) - 1);
    const int c1 = std::max(GRID_COLS + 1, int(std::ceil(ow.right() / TILE)) + 1);
    const int r1 = std::max(GRID_ROWS + 1, int(std::ceil(ow.bottom() / TILE)) + 1);
    const bool same = !outer_.isNull() && outerMap_ == game.mapIndex && outerDpr_ == dpr_;
    // Keep the current one while it still covers the widget (e.g. a window made smaller).
    if (same && outerC0_ <= c0 && outerR0_ <= r0 && outerC1_ >= c1 && outerR1_ >= r1) return;
    outer_ = buildOuter(game, c0, r0, c1, r1);
    outerRect_ = QRectF(c0 * TILE, r0 * TILE, (c1 - c0) * TILE, (r1 - r0) * TILE);
    outerC0_ = c0; outerR0_ = r0; outerC1_ = c1; outerR1_ = r1;
    outerMap_ = game.mapIndex;
    outerDpr_ = dpr_;
}

// Vignette over the whole garden (it was baked into the board before).
void Renderer::drawVignette(QPainter &p, const Game &game, const QRectF &a) {
    const data::MapDef &map = game.map();
    const double mn = std::min(a.width(), a.height()), mx = std::max(a.width(), a.height());
    QRadialGradient vg = radial(a.center().x(), a.center().y(), mn * 0.45, a.center().x(), a.center().y(), mx * 0.72);
    vg.setColorAt(0, rgba(20, 40, 10, 0));
    vg.setColorAt(1, map.night                               ? rgba(0, 0, 20, 0.45)
                     : map.season == data::Season::Winter ? rgba(40, 60, 90, 0.24)
                     : map.season == data::Season::Autumn ? rgba(70, 35, 5, 0.26)
                                                          : rgba(20, 40, 10, 0.24));
    p.fillRect(a, vg);
}

// Fireflies drifting over night maps: each follows its own slow looping path and blinks.
void Renderer::drawFireflies(QPainter &p, const Game &g) {
    if (!g.map().night) return;
    const double t = g.time;
    const int nf = int(std::lround(28 * std::max(1.0, (DOM_W * DOM_H) / (W * H))));   // over the whole garden
    for (int i = 0; i < nf; i++) {
        double phase = i * 2.39996;   // golden angle spreads them out
        double x = std::fmod(80 + i * 137.0 + 40 * std::sin(t * 0.31 + phase) + t * (6 + i % 5), DOM_W);
        double y = std::fmod(40 + i * 89.0 + 30 * std::cos(t * 0.27 + phase * 1.3), DOM_H);
        if (x < 0) x += DOM_W;
        if (y < 0) y += DOM_H;
        x += DOM_X; y += DOM_Y;
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

// Seasonal weather drifting over the board: snowfall in winter, tumbling leaves in autumn,
// a few petals in spring. Counts scale with the board; everything is small and translucent
// so it never hides bugs or plants.
void Renderer::drawWeather(QPainter &p, const Game &g) {
    const data::Season season = g.map().season;
    if (season == data::Season::Summer) return;
    const double t = g.time;
    // Immersive: over the whole garden (DOM_*), count scaled by its area (1 = default 15x10 board).
    const double area = (DOM_W * DOM_H) / (960.0 * 640.0);
    p.save();
    p.setPen(Qt::NoPen);
    if (season == data::Season::Winter) {
        const int n = int(60 * area);
        for (int i = 0; i < n; i++) {
            double phase = i * 2.39996;
            double fall = TILE * (0.3 + (i % 5) * 0.08);   // px/s, varied per flake
            double x = std::fmod(i * 97.0 + TILE * 0.25 * std::sin(t * 0.7 + phase) + t * TILE * 0.1, DOM_W);
            double y = std::fmod(i * 53.0 + t * fall, DOM_H + 10) - 5;
            if (x < 0) x += DOM_W;
            x += DOM_X; y += DOM_Y;
            double r = TILE * (0.025 + (i % 3) * 0.012);
            p.setBrush(rgba(255, 255, 255, 0.5 + 0.12 * (i % 3)));
            p.drawEllipse(QPointF(x, y), r, r);
        }
    } else {
        const bool autumn = season == data::Season::Autumn;
        const int n = int((autumn ? 14 : 8) * area);
        const double pad = TILE * 0.5;
        p.setOpacity(0.85);
        for (int i = 0; i < n; i++) {
            double phase = i * 2.39996;
            double x = std::fmod(i * 173.0 + t * TILE * (0.3 + (i % 4) * 0.07) +
                                     TILE * 0.5 * std::sin(t * 0.9 + phase), DOM_W + 2 * pad) - pad;
            double y = std::fmod(i * 71.0 + t * TILE * (0.2 + (i % 3) * 0.06), DOM_H + 2 * pad) - pad;
            if (x < -pad) x += DOM_W + 2 * pad;
            x += DOM_X; y += DOM_Y;
            double rot = t * (1.2 + (i % 5) * 0.3) + phase;
            if (autumn) {
                drawLeaf(p, x, y, TILE * (0.08 + (i % 3) * 0.015), rot, QColor(LEAF_COLORS[i % 5]));
            } else {
                // Petals flutter: squash the ellipse as they spin.
                double flip = 0.35 + 0.65 * std::abs(std::sin(rot * 1.3));
                fillEllipse(p, x, y, TILE * 0.05, TILE * 0.032 * flip, rot,
                            QColor(i % 2 ? "#ffc2d6" : "#fff0f5"));
            }
        }
    }
    p.restore();
}

// ---------- layers ----------

void Renderer::drawGrid(QPainter &p) {
    p.setPen(QPen(rgba(255, 255, 255, 0.16), 1));
    for (int c = 1; c < GRID_COLS; c++) p.drawLine(QPointF(c * TILE, 0), QPointF(c * TILE, H));
    for (int r = 1; r < GRID_ROWS; r++) p.drawLine(QPointF(0, r * TILE), QPointF(W, r * TILE));
}

void Renderer::drawPreview(QPainter &p, const View &view) {
    const auto &h = *view.hover;
    if (h.col < 0 || h.col >= GRID_COLS || h.row < 0 || h.row >= GRID_ROWS) return;
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
        // Grow the whole plant around its center on big boards (restored at the loop end).
        p.save();
        p.translate(cx, cy);
        p.scale(FX_PLANT, FX_PLANT);
        p.translate(-cx, -cy);

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
            const double R = def.levels[t.level].range * TILE / FX_PLANT;   // true range despite the plant scale
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
        p.restore();
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
        const double size = (def.boss ? BOSS_SIZE : ENEMY_SIZE) * def.scale * FX_BUG;   // bigger on big boards
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

        // Wild bat: a faint fading trail of where it has been (drawn at flight height).
        if (def.wild && e.trail.size() > 1) {
            for (size_t i = 1; i < e.trail.size(); i++) {
                double f = double(i) / e.trail.size();
                p.setPen(roundPen(rgba(190, 120, 255, 0.28 * f), 1 + 3 * f));
                p.drawLine(QPointF(e.trail[i - 1].x(), e.trail[i - 1].y() - alt),
                           QPointF(e.trail[i].x(), e.trail[i].y() - alt));
            }
        }
        // Grape vine tether: a wavy vine from the plant down to the lassoed flyer.
        if (e.grounded(time) && e.vineBy >= 0) {
            for (const auto &t : g.towers) {
                if (t.id != e.vineBy) continue;
                QPainterPath vine(QPointF(t.x, t.y - 6));
                const double vx = e.x - t.x, vy = e.y - (t.y - 6), len = std::hypot(vx, vy);
                const double nx = len > 0 ? -vy / len : 0, ny = len > 0 ? vx / len : 0;
                for (int k = 1; k <= 12; k++) {
                    double q = k / 12.0, wob = std::sin(q * 3 * M_PI + time * 6) * 4 * (1 - q * 0.5);
                    vine.lineTo(t.x + vx * q + nx * wob, t.y - 6 + vy * q + ny * wob);
                }
                p.setBrush(Qt::NoBrush);
                p.setPen(roundPen(QColor("#2f6b1f"), 3.5));
                p.drawPath(vine);
                p.setPen(roundPen(QColor("#6cbf3a"), 1.8));
                p.drawPath(vine);
                fillEllipse(p, t.x + vx * 0.5 + nx * 5, t.y - 6 + vy * 0.5 + ny * 5, 4, 2.2,
                            std::atan2(vy, vx), QColor("#5fae35"));   // a leaf
                p.setPen(roundPen(QColor("#2f6b1f"), 2));
                p.drawEllipse(QPointF(e.x, e.y), size * 0.32, size * 0.16);   // loop around the bug
                break;
            }
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
        if (def.wild) {
            // Fairness tell: glowing red eyes.
            p.setOpacity(1);
            for (int s = -1; s <= 1; s += 2) {
                fillCircle(p, s * size * 0.075, -size * 0.13, size * 0.07, rgba(255, 40, 40, 0.35));
                fillCircle(p, s * size * 0.075, -size * 0.13, size * 0.032, QColor("#ff3b3b"));
            }
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
            double bw = size * 0.85, bh = (def.boss ? 5 : 4) * FX_UI;
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
        case data::Projectile::Vine:    // the lasso is drawn as a tether, not a projectile
            break;
        }
    }
}

void Renderer::drawEffects(QPainter &p, const Game &g) {
    // Wind gust: white streaks racing across the board in the push direction (faint while warning).
    if (const double ws = g.windStrength(), ww = g.windWarning(); ws > 0.01 || ww > 0) {
        const int dir = g.windDir();
        p.setOpacity(std::min(1.0, 0.3 * ww + 0.9 * ws));
        // (claude2-3e) Few, long, fast, tapered streaks with a gentle S-curve and a slight slant,
        // so they read as gusts of wind rather than falling rain. Scaled for big boards.
        const int n = std::max(8, int(15 * std::sqrt(g.W * g.H / (960.0 * 640.0)) *
                                      std::sqrt(std::max(1.0, DOM_W * DOM_H / (g.W * g.H)))));   // whole garden
        const double slant = 0.22;   // sideways drift per unit of travel
        for (int i = 0; i < n; i++) {
            const double speed = (1000 + (i * 37 % 400)) * FX_UI;
            const double len = (110 + (i * 53 % 70)) * FX_UI;
            const double travel = std::fmod(g.time * speed + i * 397.3, DOM_H + len * 2) - len;
            const double y = DOM_Y + (dir > 0 ? travel : DOM_H - travel);          // head position
            const double x = DOM_X + std::fmod(i * 157.7 + 40 + travel * slant, DOM_W);
            const double sway = std::sin(g.time * 2.2 + i * 1.7) * 10 * FX_UI;
            const QPointF head(x, y), tail(x - len * slant, y - dir * len);
            QPainterPath s(tail);
            s.cubicTo(QPointF(tail.x() + sway, y - dir * len * 0.66), QPointF(head.x() - sway, y - dir * len * 0.33), head);
            // Tapered: transparent tail fading into a bright head.
            QLinearGradient fade(tail, head);
            fade.setColorAt(0, rgba(255, 255, 255, 0));
            fade.setColorAt(0.6, rgba(255, 255, 255, 0.55));
            fade.setColorAt(1, rgba(255, 255, 255, 0.95));
            QLinearGradient shade(tail, head);
            shade.setColorAt(0, rgba(20, 60, 30, 0));
            shade.setColorAt(1, rgba(20, 60, 30, 0.2));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QBrush(shade), 3.4 * FX_UI, Qt::SolidLine, Qt::RoundCap));   // soft shadow for pale ground
            p.drawPath(s.translated(1.2, 1.2));
            p.setPen(QPen(QBrush(fade), 2.4 * FX_UI, Qt::SolidLine, Qt::RoundCap));
            p.drawPath(s);
        }
        p.setOpacity(1);
    }
    for (const auto &fx : g.effects) {
        const double u = fx.ttl > 0 ? clamp01(fx.t / fx.ttl) : 1;
        const double a = 1 - u;
        const double eu = easeOut(u);
        // Small markers (hit sparks, kill puffs, coin text...) grow on big boards. Area effects
        // (splash, flame, leak) keep their true gameplay size.
        p.save();
        const bool marker = fx.kind == EffectKind::Hit || fx.kind == EffectKind::Kill ||
                            fx.kind == EffectKind::Coin || fx.kind == EffectKind::Jackpot ||
                            fx.kind == EffectKind::Split;
        if (marker && FX_UI > 1) {
            p.translate(fx.x, fx.y);
            p.scale(FX_UI, FX_UI);
            p.translate(-fx.x, -fx.y);
        }
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

        case EffectKind::Lasso: {
            // Vine lasso: a green loop tightening around the flyer.
            p.setOpacity(a);
            p.setBrush(Qt::NoBrush);
            p.setPen(roundPen(QColor("#5fae35"), 3));
            const double R = fx.radius * (1.6 - 0.9 * eu);
            p.drawEllipse(QPointF(fx.x, fx.y), R, R * 0.6);
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
        p.restore();
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
        QPen glow = roundPen(rgba(180, 225, 255, 0.18), 14 * FX_UI);
        g.setBrush(Qt::NoBrush);
        g.setPen(glow);
        g.drawPath(trail);
        QPen dash = roundPen(game.map().season == data::Season::Winter ? rgba(60, 100, 160, 0.6) : rgba(255, 255, 255, 0.55), 2.5 * FX_UI);   // blue on snow
        dash.setDashPattern({3, 3});
        g.setPen(dash);
        g.drawPath(trail);
        // Chevrons every ~1.5 tiles along the lane.
        const double len = trail.length();
        g.setPen(roundPen(game.map().season == data::Season::Winter ? rgba(50, 90, 150, 0.75) : rgba(255, 255, 255, 0.7), 2.2 * FX_UI));
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
        // The zone ring keeps its gameplay radius; the crosshair grows on big boards (FX_UI).
        QPen ring(col, 2 * FX_UI);
        ring.setDashPattern({3, 2});
        p.setBrush(Qt::NoBrush);
        p.setPen(ring);
        p.drawEllipse(QPointF(x, y), FIRE_POINT_RADIUS, FIRE_POINT_RADIUS);
        p.setPen(QPen(col, 2 * FX_UI));
        p.drawEllipse(QPointF(x, y), 7 * FX_UI, 7 * FX_UI);
        for (int k = 0; k < 4; k++) {
            double a = k * M_PI / 2;
            p.drawLine(QPointF(x + std::cos(a) * 10 * FX_UI, y + std::sin(a) * 10 * FX_UI),
                       QPointF(x + std::cos(a) * 16 * FX_UI, y + std::sin(a) * 16 * FX_UI));
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
    // Multi-select: a soft ring on every plant in the group.
    for (const Tower &t : g.towers) {
        if (!view.inGroup(t.id) || t.id == view.selectedId) continue;
        const double gr = (TILE * 0.34 + 6) * FX_PLANT;   // matches the scaled plant
        strokeCircle(p, t.x, t.y, gr, rgba(255, 240, 140, 0.35), 6 * FX_PLANT);
        strokeCircle(p, t.x, t.y, gr, QColor("#fff1a0"), 2 * FX_PLANT);
    }
    // Focus fire: aim lines from each focusing plant and a pulsing marker on the bug.
    std::vector<int> marked;
    for (const Tower &t : g.towers) {
        if (t.focusId < 0) continue;
        for (const Enemy &e : g.enemies) {
            if (e.id != t.focusId || !e.alive) continue;
            const double ey = e.y - g.visualY(e);
            QPen line(rgba(255, 70, 50, 0.45), 1.5);
            line.setDashPattern({3, 3});
            p.setPen(line);
            p.drawLine(QPointF(t.x, t.y), QPointF(e.x, ey));
            if (std::find(marked.begin(), marked.end(), e.id) == marked.end()) {
                marked.push_back(e.id);
                const double pulse = 1 + 0.15 * std::sin(g.time * 10);
                const double r = TILE * 0.42 * pulse * FX_BUG;   // wraps the (scaled) bug
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(rgba(255, 60, 40, 0.95), 2.5 * FX_UI));
                p.drawEllipse(QPointF(e.x, ey), r, r);
                for (int k = 0; k < 4; k++) {   // corner ticks of a target reticle
                    double a = k * M_PI / 2 + M_PI / 4;
                    p.drawLine(QPointF(e.x + std::cos(a) * r * 0.7, ey + std::sin(a) * r * 0.7),
                               QPointF(e.x + std::cos(a) * r * 1.25, ey + std::sin(a) * r * 1.25));
                }
            }
        }
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

void Renderer::draw(QPainter &p, const Game &game, const View &view, double scale, const QRectF &outerWorld) {
    setupScale(scale, game);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(1);
    if (!outerWorld.isEmpty()) {
        // Immersive: the garden past the board, then the board, then a vignette over all of it.
        setupOuter(game, outerWorld);
        const double k = outer_.width() / outerRect_.width();
        const QRectF vis = outerWorld.intersected(outerRect_);
        p.drawPixmap(vis, outer_, QRectF((vis.left() - outerRect_.left()) * k, (vis.top() - outerRect_.top()) * k,
                                         vis.width() * k, vis.height() * k));
        DOM_X = outerRect_.left(); DOM_Y = outerRect_.top(); DOM_W = outerRect_.width(); DOM_H = outerRect_.height();
    } else {
        DOM_X = 0; DOM_Y = 0; DOM_W = W; DOM_H = H;
    }
    p.drawPixmap(QRectF(0, 0, W, H), bg_, bg_.rect());
    drawVignette(p, game, outerWorld.isEmpty() ? QRectF(0, 0, W, H) : outerWorld);

    if (view.placing >= 0) drawGrid(p);
    if (view.placing >= 0 && view.hover) drawPreview(p, view);
    drawRanges(p, game, view);
    drawFirePoints(p, game, view);
    drawTowers(p, game, view);
    drawEnemies(p, game);
    drawShots(p, game);
    drawEffects(p, game);
    drawWeather(p, game);
    drawFireflies(p, game);
    drawHouseBar(p, game);
    drawBossBar(p, game);
}

// House health bar: lives left out of START_LIVES, always shown above the house, styled like the
// bugs' HP bars (dark backing; green > 50%, yellow > 25%, red below). Lives carry over between maps.
void Renderer::drawHouseBar(QPainter &p, const Game &g) {
    if (g.path.empty()) return;
    const QPointF &last = g.path.back();
    const double hx = std::min(W - TILE * 0.72, last.x()), hy = last.y() - 6;   // same spot as the house
    const double ratio = clamp01(double(g.lives) / std::max(1, data::START_LIVES));
    const double bw = 46 * FX_UI, bh = 6 * FX_UI;
    const double bx = hx - bw / 2, by = std::max(2.0, hy - 30 - bh);
    p.fillRect(QRectF(bx - 1, by - 1, bw + 2, bh + 2), rgba(20, 20, 20, 0.65));
    p.fillRect(QRectF(bx, by, bw * ratio, bh),
               QColor(ratio > 0.5 ? "#5ee35e" : ratio > 0.25 ? "#ffd24d" : "#ff5a4d"));
}

// Boss HP bar across the top of the board: name, then one segment per living boss.
void Renderer::drawBossBar(QPainter &p, const Game &g) {
    std::vector<const Enemy *> bosses;
    for (const auto &e : g.enemies)
        if (e.alive && data::ENEMIES[e.type].boss) bosses.push_back(&e);
    if (bosses.empty()) return;
    std::sort(bosses.begin(), bosses.end(), [](const Enemy *a, const Enemy *b) { return a->id < b->id; });

    // Bigger boards are drawn smaller on screen: scale the bar up (around the bottom center)
    // so it stays the same on-screen size as on the default 15-column board.
    p.save();
    const double grow = std::max(1.0, W / 960.0);
    p.translate(W / 2, H);
    p.scale(grow, grow);
    p.translate(-W / 2, -H);
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
    p.restore();
}
