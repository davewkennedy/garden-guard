// Garden Guard: all board drawing (ported from js/render.js).
// Reads the game state and never changes it.
#pragma once

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <algorithm>
#include <optional>
#include <vector>

class Game;
class QPainter;

// What the UI wants shown on the board besides the game itself.
struct View {
    struct Hover { int col, row; bool valid; };
    std::optional<Hover> hover;
    int placing = -1;      // tower type being placed, -1 if none
    int selectedId = -1;   // selected tower id, -1 if none
    int aimingId = -1;     // tower whose fire point is being chosen, -1 if none
    std::vector<int> group;   // multi-select (double-click): all selected tower ids
    bool inGroup(int id) const { return std::find(group.begin(), group.end(), id) != group.end(); }
};

class Renderer {
public:
    // Draws the board in logical 960x640 coordinates. `scale` is how many device
    // pixels one logical pixel covers, so cached images stay sharp at any size.
    // Immersive: `outerWorld` is the whole widget in board coordinates; the garden (grass,
    // decorations, path, season, weather) is drawn over all of it, not just the board.
    // An empty rect draws the board only.
    void draw(QPainter &p, const Game &game, const View &view, double scale, const QRectF &outerWorld = QRectF());

private:
    // (claude2-3e) The garden past the board: cached per map, area and scale.
    QPixmap buildOuter(const Game &game, int c0, int r0, int c1, int r1);
    void setupOuter(const Game &game, const QRectF &outerWorld);
    void drawVignette(QPainter &p, const Game &game, const QRectF &area);
    QPixmap outer_;
    QRectF outerRect_;        // world rect the outer pixmap covers
    int outerC0_ = 0, outerR0_ = 0, outerC1_ = 0, outerR1_ = 0, outerMap_ = -1;
    double outerDpr_ = 0;

    void setupScale(double scale, const Game &game);
    // Cached images. Returned by value: QPixmap copies are cheap (shared data)
    // and stay valid even when the cache grows.
    QPixmap glyph(const QString &ch, double size);
    QPixmap tint(const QString &ch, double size, const QColor &color);
    void drawGlyph(QPainter &p, const QString &ch, double size, double x, double y, double s = 1);
    QPixmap pot(int type);
    QPixmap buildBackground(const Game &game);

    void drawGrid(QPainter &p);
    void drawPreview(QPainter &p, const View &view);
    void drawRanges(QPainter &p, const Game &g, const View &view);
    void drawTowers(QPainter &p, const Game &g, const View &view);
    void drawEnemies(QPainter &p, const Game &g);
    void drawShots(QPainter &p, const Game &g);
    void drawEffects(QPainter &p, const Game &g);
    void drawBossBar(QPainter &p, const Game &g);   // big HP bar while a boss is alive
    void drawHouseBar(QPainter &p, const Game &g);  // lives left, above the house
    void drawFireflies(QPainter &p, const Game &g);
    void drawWeather(QPainter &p, const Game &g);   // season snow / leaves / petals
    void drawFirePoints(QPainter &p, const Game &g, const View &view);
    void drawSkyLanes(QPainter &g, const Game &game);   // into the background

    double dpr_ = 0;
    int bgMap_ = -1;    // which map the background was built for
    QPixmap bg_;
    QHash<QString, QPixmap> glyphs_;   // key: char + size (+ tint color)
    QHash<int, QPixmap> pots_;
};
