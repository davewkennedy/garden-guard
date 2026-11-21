#include "immersive.h"

#include "boardwidget.h"

#include <QEvent>
#include <QWidget>

ImmersiveLayout::ImmersiveLayout(QWidget *central, BoardWidget *board, QWidget *slot)
    : QObject(central), central_(central), board_(board), slot_(slot) {
    board_->setParent(central_);
    board_->lower();   // behind the header, side panel and overlays
    slot_->setAttribute(Qt::WA_TransparentForMouseEvents);   // clicks reach the board below
    central_->installEventFilter(this);
    slot_->installEventFilter(this);
    sync();
}

void ImmersiveLayout::sync() {
    board_->setGeometry(central_->rect());
    board_->lower();
    board_->setSafeRect(QRectF(slot_->mapTo(central_, QPoint(0, 0)), slot_->size()));
}

bool ImmersiveLayout::eventFilter(QObject *obj, QEvent *e) {
    if ((obj == central_ || obj == slot_) && (e->type() == QEvent::Resize || e->type() == QEvent::Move ||
                                              e->type() == QEvent::Show || e->type() == QEvent::LayoutRequest))
        sync();
    return QObject::eventFilter(obj, e);
}
