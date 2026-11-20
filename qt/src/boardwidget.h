// Garden Guard: the game board widget. Scales the 960x640 board to fit,
// turns mouse positions into tiles, and shows the toast and pause banner.
#pragma once

#include "renderer.h"

#include <QColor>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <vector>

class Game;

class BoardWidget : public QWidget {
    Q_OBJECT
public:
    BoardWidget(Game *game, QWidget *parent = nullptr);

    View view;   // the UI changes this; the renderer reads it

    void showToast(const QString &text, bool error = false);
    void hideToast();
    void setPausedBanner(bool on);
    void celebrate();   // burst of confetti over the board
    QSize sizeHint() const override { return {960, 640}; }

signals:
    void tileHovered(int col, int row);
    void hoverLeft();
    void tileClicked(int col, int row);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF boardRect() const;            // where the board sits inside the widget
    bool tileAt(QPointF pos, int &col, int &row) const;

    Game *game_;
    Renderer renderer_;
    QString toast_;
    bool toastError_ = false;
    bool paused_ = false;
    QTimer toastTimer_;

    // Confetti, in widget coordinates; animated in real time (even while paused).
    struct Confetto { double x, y, vx, vy, rot, spin, w, h; QColor color; };
    std::vector<Confetto> confetti_;
    QElapsedTimer confettiClock_;
    void drawConfetti(QPainter &p);
};
