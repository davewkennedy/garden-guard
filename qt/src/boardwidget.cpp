#include "boardwidget.h"

#include "data.h"
#include "game.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
constexpr int TOAST_MS = 1800;
}

BoardWidget::BoardWidget(Game *game, QWidget *parent) : QWidget(parent), game_(game) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toastTimer_.setSingleShot(true);
    connect(&toastTimer_, &QTimer::timeout, this, &BoardWidget::hideToast);
}

void BoardWidget::showToast(const QString &text, bool error) {
    toast_ = text;
    toastError_ = error;
    toastTimer_.start(TOAST_MS);
    update();
}

void BoardWidget::hideToast() {
    toast_.clear();
    update();
}

void BoardWidget::setPausedBanner(bool on) {
    paused_ = on;
    update();
}

void BoardWidget::celebrate() {
    static const char *const COLORS[] = {"#ff5a5f", "#ffc93c", "#4caf50", "#5bc0de", "#b06ad9", "#ff9f5a", "#ffffff"};
    const QRectF r = boardRect();
    for (int i = 0; i < 180; i++) {
        // Two cannons at the bottom corners, aimed up and inward.
        const bool left = i % 2 == 0;
        const double angle = (left ? -60 : -120) + (std::rand() % 40 - 20);
        const double speed = 500 + std::rand() % 450;
        const double rad = angle * M_PI / 180;
        confetti_.push_back({left ? r.left() + 20 : r.right() - 20, r.bottom() - 10,
                             std::cos(rad) * speed, std::sin(rad) * speed,
                             double(std::rand() % 360), double(std::rand() % 720 - 360),
                             5.0 + std::rand() % 5, 8.0 + std::rand() % 6,
                             QColor(COLORS[std::rand() % 7])});
    }
    confettiClock_.start();
    update();
}

void BoardWidget::drawConfetti(QPainter &p) {
    if (confetti_.empty()) return;
    const double dt = std::min(0.05, confettiClock_.restart() / 1000.0);
    for (auto &c : confetti_) {
        c.vy += 900 * dt;          // gravity
        c.vx *= 1 - 1.2 * dt;      // air drag
        c.vy *= 1 - 0.8 * dt;
        c.x += c.vx * dt;
        c.y += c.vy * dt;
        c.rot += c.spin * dt;
        p.save();
        p.translate(c.x, c.y);
        p.rotate(c.rot);
        // Squash the width with the rotation so pieces look like they flutter.
        p.scale(std::cos(c.rot * M_PI / 90), 1);
        p.fillRect(QRectF(-c.w / 2, -c.h / 2, c.w, c.h), c.color);
        p.restore();
    }
    const double bottom = height() + 40;
    confetti_.erase(std::remove_if(confetti_.begin(), confetti_.end(),
                                   [bottom](const Confetto &c) { return c.y > bottom && c.vy > 0; }),
                    confetti_.end());
    update();   // keep animating until every piece has fallen
}

void BoardWidget::setSafeRect(const QRectF &r) {
    if (r == safeRect_) return;
    safeRect_ = r;
    update();
}

QRectF BoardWidget::safeRect() const {
    const QRectF all(0, 0, width(), height());
    return safeRect_.isEmpty() ? all : safeRect_.intersected(all);
}

QRectF BoardWidget::boardRect() const {
    // Immersive: the largest board-shaped rectangle that fits the safe rect, centered in it.
    const QRectF a = safeRect();
    if (a.width() < 2 || a.height() < 2) return {};
    double s = std::min(a.width() / double(game_->W), a.height() / double(game_->H));
    double w = game_->W * s, h = game_->H * s;
    return {a.left() + (a.width() - w) / 2, a.top() + (a.height() - h) / 2, w, h};
}

bool BoardWidget::tileAt(QPointF pos, int &col, int &row) const {
    QRectF r = boardRect();
    if (r.isEmpty()) return false;
    col = int(std::floor((pos.x() - r.left()) * game_->W / r.width() / data::TILE));
    row = int(std::floor((pos.y() - r.top()) * game_->H / r.height() / data::TILE));
    return col >= 0 && row >= 0 && col < game_->cols && row < game_->rows;
}

void BoardWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = boardRect();
    const double s = r.width() / game_->W;

    // Immersive (claude2-3e): the garden fills the whole widget; no board shadow or rounded clip.
    // The renderer draws in board coordinates; outerWorld is the whole widget in those coordinates.
    if (r.isEmpty() || s <= 0) return;
    p.save();
    p.translate(r.topLeft());
    p.scale(s, s);
    const QRectF outerWorld(-r.left() / s, -r.top() / s, width() / s, height() / s);
    renderer_.draw(p, *game_, view, s * devicePixelRatioF(), outerWorld);
    p.restore();
    drawConfetti(p);

    // Pause banner, centered on the board.
    if (paused_) {
        QFont f = font();
        f.setPixelSize(30);
        f.setBold(true);
        p.setFont(f);
        QFontMetricsF fm(f);
        QRectF box(0, 0, fm.horizontalAdvance("Paused") + 56, fm.height() + 24);
        box.moveCenter(r.center());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 32, 14, 190));
        p.drawRoundedRect(box, 14, 14);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, "Paused");
    }

    // Toast, top-center over the board.
    if (!toast_.isEmpty()) {
        QFont f = font();
        f.setPixelSize(15);
        f.setBold(true);
        p.setFont(f);
        QFontMetricsF fm(f);
        QRectF box(0, 0, fm.horizontalAdvance(toast_) + 36, fm.height() + 18);
        box.moveCenter(QPointF(r.center().x(), r.top() + 16 + box.height() / 2));
        p.setPen(Qt::NoPen);
        p.setBrush(toastError_ ? QColor("#e0533d") : QColor(36, 51, 28, 230));
        p.drawRoundedRect(box, box.height() / 2, box.height() / 2);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, toast_);
    }
}

void BoardWidget::mouseMoveEvent(QMouseEvent *e) {
    int col, row;
    if (tileAt(e->position(), col, row)) emit tileHovered(col, row);
    else emit hoverLeft();
}

void BoardWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    int col, row;
    if (tileAt(e->position(), col, row)) {
        const QRectF r = boardRect();
        emit pointClicked((e->position().x() - r.left()) * game_->W / r.width(),
                          (e->position().y() - r.top()) * game_->H / r.height());
        emit tileHovered(col, row);
        emit tileClicked(col, row);
    }
}

void BoardWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    int col, row;
    if (tileAt(e->position(), col, row)) emit tileDoubleClicked(col, row);
}

void BoardWidget::leaveEvent(QEvent *) {
    emit hoverLeft();
}
