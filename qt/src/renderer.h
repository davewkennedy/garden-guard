// Garden Guard: all board drawing (ported from js/render.js).
// Reads the game state and never changes it.
#pragma once

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QString>
#include <optional>

class Game;
class QPainter;

// What the UI wants shown on the board besides the game itself.
struct View {
    struct Hover { int col, row; bool valid; };
    std::optional<Hover> hover;
    int placing = -1;      // tower type being placed, -1 if none
    int selectedId = -1;   // selected tower id, -1 if none
};

class Renderer {
public:
    // Draws the board in logical 960x640 coordinates. `scale` is how many device
    // pixels one logical pixel covers, so cached images stay sharp at any size.
    void draw(QPainter &p, const Game &game, const View &view, double scale);

private:
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

    double dpr_ = 0;
    int bgMap_ = -1;    // which map the background was built for
    QPixmap bg_;
    QHash<QString, QPixmap> glyphs_;   // key: char + size (+ tint color)
    QHash<int, QPixmap> pots_;
};
