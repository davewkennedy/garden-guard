#include "boardwidget.h"

#include "data.h"
#include "game.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {
constexpr int TOAST_MS = 1800;
constexpr double CORNER = 16;
}

BoardWidget::BoardWidget(Game *game, QWidget *parent) : QWidget(parent), game_(game) {
    setMouseTracking(true);
    setMinimumSize(data::W / 2, data::H / 2);
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

QRectF BoardWidget::boardRect() const {
    // Largest 3:2 rectangle that fits: centered left-to-right, at the top
    // (leaving room below for its shadow).
    double s = std::min(width() / double(data::W), (height() - 6) / double(data::H));
    double w = data::W * s, h = data::H * s;
    return {(width() - w) / 2, 0, w, h};
}

bool BoardWidget::tileAt(QPointF pos, int &col, int &row) const {
    QRectF r = boardRect();
    if (r.isEmpty()) return false;
    col = int(std::floor((pos.x() - r.left()) * data::W / r.width() / data::TILE));
    row = int(std::floor((pos.y() - r.top()) * data::H / r.height() / data::TILE));
    return col >= 0 && row >= 0 && col < data::COLS && row < data::ROWS;
}

void BoardWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = boardRect();
    const double s = r.width() / data::W;

    // Soft shadow, then the board clipped to rounded corners.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 70, 20, 30));
    p.drawRoundedRect(r.translated(0, 4), CORNER, CORNER);
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(r, CORNER, CORNER);
    p.setClipPath(clip);
    p.translate(r.topLeft());
    p.scale(s, s);
    renderer_.draw(p, *game_, view, s * devicePixelRatioF());
    p.restore();

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
        emit tileHovered(col, row);
        emit tileClicked(col, row);
    }
}

void BoardWidget::leaveEvent(QEvent *) {
    emit hoverLeft();
}
