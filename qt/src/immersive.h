// Garden Guard++ immersive look (owner: Hank): the board widget fills the whole window behind
// the header and side panel. An empty "slot" widget stays in the layout and says where the
// board itself should fit (BoardWidget::setSafeRect), like #boardWrap in the web version.
#pragma once

#include <QObject>

class QWidget;
class BoardWidget;

class ImmersiveLayout : public QObject {
public:
    ImmersiveLayout(QWidget *central, BoardWidget *board, QWidget *slot);
    void sync();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    QWidget *central_;
    BoardWidget *board_;
    QWidget *slot_;
};
