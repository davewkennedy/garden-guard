// Garden Guard++: focus fire and multi-select (MainWindow methods, kept in their own
// file so they don't collide with other edits to mainwindow.cpp).
//
// Double-click a plant: select every plant of that type. With one or more plants
// selected, click a bug: they all shoot it whenever it's in range (even off a fire
// point, but without the ambush bonus) until it dies, escapes or splits; then each
// plant goes back to what it was doing. Esc cancels a focus first, then deselects.
#include "boardwidget.h"
#include "data.h"
#include "mainwindow.h"

#include <QLabel>
#include <QPushButton>
#include <algorithm>

namespace {
const QString COIN = QStringLiteral("💰");
}

std::vector<int> MainWindow::selection() const {
    const View &view = board_->view;
    if (view.group.size() > 1) return view.group;
    if (view.selectedId >= 0) return {view.selectedId};
    return {};
}

void MainWindow::onDoubleClick(int col, int row) {
    View &view = board_->view;
    if (overlayOpen() || game_.isOver() || view.placing >= 0 || view.aimingId >= 0) return;
    Tower *t = game_.towerAt(col, row);
    if (!t) return;
    view.selectedId = t->id;
    view.group.clear();
    for (const Tower &o : game_.towers)
        if (o.type == t->type) view.group.push_back(o.id);
    const data::TowerDef &def = data::TOWERS[t->type];
    board_->showToast(QString("%1 %2 × %3 selected: click a bug to focus fire")
                          .arg(def.icon, def.name).arg(view.group.size()));
}

bool MainWindow::tryFocusClick() {
    if (board_->view.selectedId < 0) return false;
    const int bug = game_.enemyNear(lastClick_.x(), lastClick_.y(), data::TILE * 0.6);
    if (bug < 0) return false;
    focusOn(bug);
    return true;
}

void MainWindow::focusOn(int enemyId) {
    const std::vector<int> ids = selection();
    const Enemy *target = nullptr;
    for (const Enemy &e : game_.enemies)
        if (e.id == enemyId) target = &e;
    if (!target || ids.empty()) return;
    // Ground-only plants can't hit an airborne bug: say why instead of silently failing.
    bool anyCanHit = false;
    for (int id : ids)
        if (const Tower *t = game_.getTower(id)) anyCanHit |= game_.canHit(*t, *target);
    const data::EnemyDef &bug = data::ENEMIES[target->type];
    if (!anyCanHit) {
        const Tower *first = game_.getTower(ids[0]);
        fail(QString("%1 can't hit flyers. Ground it with a 🍇 Grape Vine first!")
                 .arg(first ? data::TOWERS[first->type].name : QStringLiteral("That plant")));
        return;
    }
    if (!game_.focusFire(ids, enemyId)) { fail("Support plants can't focus fire"); return; }
    sfx("click");
    board_->showToast(QString("🎯 Focus fire on the %1 %2! (Esc cancels)").arg(bug.icon, bug.name));
}

bool MainWindow::cancelFocusOnSelection() {
    bool cancelled = false;
    for (int id : selection()) {
        Tower *t = game_.getTower(id);
        if (t && t->focusId >= 0) { game_.clearFocus(id); cancelled = true; }
    }
    if (cancelled) board_->showToast("Focus fire cancelled");
    return cancelled;
}

bool MainWindow::upgradeAllInGroup() {
    if (board_->view.group.size() <= 1) return false;
    int done = 0;
    while (true) {   // cheapest upgrades first, as many as the coins allow
        Tower *pick = nullptr;
        int best = 0;
        for (int id : board_->view.group) {
            Tower *o = game_.getTower(id);
            int c = o ? game_.upgradeCost(*o) : -1;
            if (c >= 0 && c <= game_.coins && (!pick || c < best)) { pick = o; best = c; }
        }
        if (!pick || !game_.upgradeTower(pick->id)) break;
        done++;
    }
    if (done == 0) fail("Not enough coins to upgrade any of them");
    else { sfx("click"); board_->showToast(QString("Upgraded %1 plant%2").arg(done).arg(done == 1 ? "" : "s")); }
    return true;
}

// Called from syncUi every frame while a plant is selected.
void MainWindow::syncFocusPanel(const Tower &t, bool over) {
    View &view = board_->view;
    auto setText = [](QLabel *l, const QString &s) { if (l->text() != s) l->setText(s); };
    auto setBtn = [](QPushButton *b, const QString &s) { if (b->text() != s) b->setText(s); };
    std::vector<int> &group = view.group;
    group.erase(std::remove_if(group.begin(), group.end(), [this](int id) { return !game_.getTower(id); }),
                group.end());
    if (group.size() > 1) {
        // Group mode: name × count, focus hint, "Upgrade all"; no sell or fire point.
        const data::TowerDef &def = data::TOWERS[t.type];
        int total = 0, focusing = 0;
        for (int id : group) {
            const Tower *o = game_.getTower(id);
            int c = game_.upgradeCost(*o);
            if (c > 0) total += c;
            if (o->focusId >= 0) focusing++;
        }
        setText(tpName_, QString("%1 × %2").arg(def.name).arg(group.size()));
        setText(tpLevel_, focusing ? QString("🎯 %1 focusing (Esc cancels)").arg(focusing)
                                   : QStringLiteral("Click a bug to focus fire"));
        setBtn(tpUpgrade_, total > 0 ? QString("Upgrade all · %1 %2").arg(COIN).arg(total)
                                     : QStringLiteral("All at max level"));
        tpUpgrade_->setEnabled(!over && total > 0);
        tpSell_->setVisible(false);
        tpAim_->setVisible(false);
        return;
    }
    tpSell_->setVisible(true);
    if (t.focusId >= 0) {
        for (const Enemy &e : game_.enemies)
            if (e.id == t.focusId)
                setText(tpLevel_, QString("🎯 Focusing the %1 %2 (Esc cancels)")
                                      .arg(data::ENEMIES[e.type].icon, data::ENEMIES[e.type].name));
    }
}
