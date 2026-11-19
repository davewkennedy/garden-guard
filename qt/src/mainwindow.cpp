#include "mainwindow.h"

#include "boardwidget.h"
#include "data.h"

#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

namespace {

constexpr int BUMP_MS = 300;
const QString COIN = QStringLiteral("💰");

// Qt style sheets are Qt's take on CSS. Colors match the web version's light theme.
const char *const STYLE = R"(
#central { background: #eef6e4; }
QLabel { color: #24331c; }
#title { font-size: 24px; font-weight: 800; color: #2f8a3a; }
#stat {
  background: #ffffff; border: 1px solid #d6e3c6; border-radius: 14px;
  padding: 4px 14px; font-size: 16px; font-weight: 700;
}
#stat[low="true"] { color: #e0533d; border-color: #e0533d; }
#stat[bump="true"] { background: #fff3c4; border-color: #ffc93c; }

QPushButton {
  background: #ffffff; color: #24331c; border: 1px solid #d6e3c6;
  border-radius: 10px; padding: 6px 12px; font-weight: 600;
}
QPushButton:hover { background: #f4f8ec; }
QPushButton:pressed { background: #e6efda; }
QPushButton:disabled { color: #a3ad99; background: #f3f6ee; }
QPushButton[kind="primary"] { background: #4caf50; color: #ffffff; border: 1px solid #2f8a3a; }
QPushButton[kind="primary"]:hover { background: #43a047; }
QPushButton[kind="primary"]:disabled { background: #b5d8b6; color: #f4f8ec; border-color: #a3cba5; }
QPushButton[kind="danger"] { background: #e0533d; color: #ffffff; border: 1px solid #b83a27; }
QPushButton[kind="danger"]:hover { background: #cf4732; }
QPushButton[kind="danger"]:disabled { background: #efb3a9; color: #fbeeeb; }
QPushButton[kind="big"] { font-size: 16px; font-weight: 700; min-height: 40px; }
#iconBtn { font-size: 18px; min-width: 40px; max-width: 40px; min-height: 40px; max-height: 40px; padding: 0; }
#iconBtn[on="true"] { background: #fff3c4; border-color: #ffc93c; }

#side { background: #ffffff; border: 1px solid #d6e3c6; border-radius: 16px; }
#sideTitle { font-size: 18px; font-weight: 800; }
#hint { color: #64735a; font-size: 12px; }

#shopItem { background: #f4f8ec; border: 2px solid #d6e3c6; border-radius: 12px; padding: 0; }
#shopItem:hover { border-color: #4caf50; }
#shopItem:checked { background: rgba(76, 175, 80, 0.18); border: 2px solid #2f8a3a; }
#shopItem:disabled { background: #f6f7f3; border-color: #e3e8dc; }
#shopIcon { font-size: 24px; }
#shopName { font-weight: 700; font-size: 12px; }
#shopCost { font-weight: 700; font-size: 12px; color: #a0683c; }
#shopCost:disabled { color: #d9480f; }
#shopDesc { font-size: 11px; color: #64735a; }
#shopName:disabled, #shopDesc:disabled { color: #a3ad99; }
#shopKey {
  background: #ffffff; border: 1px solid #d6e3c6; border-radius: 6px;
  font-size: 11px; font-weight: 700; color: #64735a; padding: 0 5px;
}

#towerPanel { background: #f4f8ec; border: 1px solid #d6e3c6; border-radius: 12px; }
#tpIcon { font-size: 26px; }
#tpName { font-size: 16px; font-weight: 800; }
#tpLevel { color: #64735a; }

#overlay { background: rgba(20, 32, 14, 140); }
#card { background: #ffffff; border-radius: 18px; }
#overlayTitle { font-size: 26px; font-weight: 800; color: #2f8a3a; }
#overlayText { font-size: 15px; }
)";

QLabel *label(const QString &text, const QString &name, QWidget *parent = nullptr) {
    auto *l = new QLabel(text, parent);
    l->setObjectName(name);
    return l;
}

QPushButton *button(const QString &text, const QString &kind = {}, const QString &name = {}) {
    auto *b = new QPushButton(text);
    if (!kind.isEmpty()) b->setProperty("kind", kind);
    if (!name.isEmpty()) b->setObjectName(name);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);   // keep keyboard shortcuts working after clicks
    return b;
}

// Re-applies the style sheet after a dynamic property changes.
void repolish(QWidget *w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}

void setProp(QWidget *w, const char *name, const QVariant &value) {
    if (w->property(name) == value) return;
    w->setProperty(name, value);
    repolish(w);
}

void setTextIfChanged(QAbstractButton *b, const QString &text) {
    if (b->text() != text) b->setText(text);
}

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Garden Guard");
    setStyleSheet(STYLE);

    auto *central = new QWidget;
    central->setObjectName("central");
    central->setAttribute(Qt::WA_StyledBackground);
    central->setFocusPolicy(Qt::StrongFocus);
    setCentralWidget(central);

    board_ = new BoardWidget(&game_);
    connect(board_, &BoardWidget::tileHovered, this, &MainWindow::onHover);
    connect(board_, &BoardWidget::tileClicked, this, &MainWindow::onClick);
    connect(board_, &BoardWidget::hoverLeft, this, [this] { board_->view.hover.reset(); });

    auto *play = new QHBoxLayout;
    play->setSpacing(16);
    play->addWidget(board_, 1);
    play->addWidget(buildSide());

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(18, 12, 18, 18);
    root->setSpacing(12);
    root->addWidget(buildHeader());
    root->addLayout(play, 1);

    // Overlays sit on top of everything inside the central widget.
    QLabel *introTitle, *introText;
    QPushButton *startGame, *again;
    intro_ = buildOverlay(introTitle, introText, startGame);
    introTitle->setText("Garden Guard");
    introText->setText(QString("Bugs are marching on your house! Plant defenders beside the path to "
                               "stop them. Earn coins for every bug and upgrade your plants. Survive "
                               "10 waves on each of %1 gardens. Your lives carry over between them!")
                           .arg(data::MAPS.size()));
    startGame->setText("Let's grow!");
    connect(startGame, &QPushButton::clicked, this, [this] {
        sfx("click");
        intro_->hide();
        centralWidget()->setFocus();
    });
    end_ = buildOverlay(endTitle_, endText_, again);
    again->setText("Play again");
    connect(again, &QPushButton::clicked, this, &MainWindow::playAgain);
    end_->hide();
    next_ = buildOverlay(nextTitle_, nextText_, nextBtn_);
    connect(nextBtn_, &QPushButton::clicked, this, &MainWindow::goToNextMap);
    next_->hide();

    // Game events -> sounds and messages.
    connect(&game_, &Game::shot, this, [this](int type) { sfx("shoot_" + data::TOWERS[type].id); });
    connect(&game_, &Game::killed, this, [this] { sfx("kill"); });
    connect(&game_, &Game::leaked, this, [this] { sfx("leak"); });
    connect(&game_, &Game::placed, this, [this] { sfx("place"); });
    connect(&game_, &Game::upgraded, this, [this] { sfx("upgrade"); });
    connect(&game_, &Game::sold, this, [this] { sfx("sell"); });
    connect(&game_, &Game::waveStarted, this, [this] { sfx("waveStart"); });
    connect(&game_, &Game::waveEnded, this, [this](int wave, int bonus, int harvest) {
        sfx("waveEnd");
        QString msg = QString("Wave %1 cleared! +%2 %3").arg(wave).arg(bonus).arg(COIN);
        if (harvest > 0) {
            sfx("harvest");
            msg += QString("  ·  Sunflowers +%1 %2").arg(harvest).arg(COIN);
        }
        board_->showToast(msg);
    });
    connect(&game_, &Game::mapCleared, this, [this] { sfx("win"); showMapCleared(); });
    connect(&game_, &Game::mapStarted, this, [this](int index) {
        board_->showToast(QString("Map %1 of %2: %3").arg(index + 1).arg(game_.totalMaps)
                              .arg(data::MAPS[index].name));
    });
    connect(&game_, &Game::won, this, [this] { sfx("win"); showEnd(true); });
    connect(&game_, &Game::lost, this, [this] { sfx("lose"); showEnd(false); });

    // Game loop: about 60 frames per second.
    frameTimer_.setTimerType(Qt::PreciseTimer);
    connect(&frameTimer_, &QTimer::timeout, this, &MainWindow::tick);
    frameTimer_.start(16);
    clock_.start();

    setUserPaused(false);
    setSpeed(1);
    syncSoundButton();
    syncUi();
    resize(1320, 860);
    startGame->setFocus();
}

// ---------- building the UI ----------

QWidget *MainWindow::buildHeader() {
    auto *header = new QWidget;
    auto *h = new QHBoxLayout(header);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    h->addWidget(label("Garden Guard", "title"));
    h->addStretch(1);
    map_ = label({}, "stat");
    coins_ = label({}, "stat");
    lives_ = label({}, "stat");
    wave_ = label({}, "stat");
    map_->setToolTip("Campaign map");
    for (QLabel *l : {map_, coins_, lives_, wave_}) h->addWidget(l);
    h->addStretch(1);

    speedBtn_ = button({}, {}, "iconBtn");
    pauseBtn_ = button({}, {}, "iconBtn");
    soundBtn_ = button({}, {}, "iconBtn");
    speedBtn_->setToolTip("Fast forward (F)");
    pauseBtn_->setToolTip("Pause (Space)");
    connect(speedBtn_, &QPushButton::clicked, this, [this] { sfx("click"); setSpeed(speed_ == 2 ? 1 : 2); });
    connect(pauseBtn_, &QPushButton::clicked, this, [this] { sfx("click"); setUserPaused(!userPaused_); });
    connect(soundBtn_, &QPushButton::clicked, this, [this] {
        audio_.setEnabled(!audio_.enabled());
        syncSoundButton();
        sfx("click");   // only audible when turning sound on
    });
    for (QPushButton *b : {speedBtn_, pauseBtn_, soundBtn_}) h->addWidget(b);
    return header;
}

QPushButton *MainWindow::makeShopItem(int type, int index) {
    const data::TowerDef &def = data::TOWERS[type];
    auto *b = button({}, {}, "shopItem");
    b->setCheckable(true);
    b->setMinimumHeight(86);
    b->setToolTip(QString("%1 (key %2)").arg(def.name).arg(index + 1));

    // Compact card: icon, cost and key badge on top; then the full-width name and description.
    auto *g = new QGridLayout(b);
    g->setContentsMargins(8, 5, 8, 6);
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(1);
    QLabel *desc = label(def.desc, "shopDesc");
    desc->setWordWrap(true);
    g->addWidget(label(def.icon, "shopIcon"), 0, 0);
    g->addWidget(label(COIN + " " + QString::number(def.levels[0].cost), "shopCost"), 0, 1);
    g->addWidget(label(QString::number(index + 1), "shopKey"), 0, 2, Qt::AlignRight | Qt::AlignTop);
    g->addWidget(label(def.name, "shopName"), 1, 0, 1, 3);
    g->addWidget(desc, 2, 0, 1, 3);
    g->setColumnStretch(1, 1);
    g->setRowStretch(2, 1);
    for (QLabel *l : b->findChildren<QLabel *>()) l->setAttribute(Qt::WA_TransparentForMouseEvents);

    connect(b, &QPushButton::clicked, this, [this, type] { toggleShop(type); });
    return b;
}

QWidget *MainWindow::buildSide() {
    auto *side = new QFrame;
    side->setObjectName("side");
    side->setFixedWidth(306);
    auto *v = new QVBoxLayout(side);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(12);

    v->addWidget(label("Plants", "sideTitle"));
    auto *shop = new QGridLayout;
    shop->setSpacing(8);
    for (int i = 0; i < int(data::TOWERS.size()); i++) {
        QPushButton *item = makeShopItem(i, i);
        shopItems_.push_back(item);
        shop->addWidget(item, i / 2, i % 2);
    }
    v->addLayout(shop);

    // Tower panel (shown while a planted tower is selected).
    panel_ = new QFrame;
    panel_->setObjectName("towerPanel");
    auto *pv = new QVBoxLayout(panel_);
    pv->setContentsMargins(12, 10, 12, 12);
    pv->setSpacing(6);
    auto *head = new QHBoxLayout;
    tpIcon_ = label({}, "tpIcon");
    tpName_ = label({}, "tpName");
    QPushButton *close = button(QStringLiteral("✕"), {}, "iconBtn");
    close->setToolTip("Close (Esc)");
    connect(close, &QPushButton::clicked, this, [this] { sfx("click"); board_->view.selectedId = -1; });
    head->addWidget(tpIcon_);
    head->addWidget(tpName_, 1);
    head->addWidget(close);
    pv->addLayout(head);
    tpLevel_ = label({}, "tpLevel");
    pv->addWidget(tpLevel_);
    tpStats_ = new QFormLayout;
    tpStats_->setLabelAlignment(Qt::AlignLeft);
    tpStats_->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    pv->addLayout(tpStats_);
    auto *actions = new QHBoxLayout;
    tpUpgrade_ = button("Upgrade", "primary");
    tpSell_ = button("Sell", "danger");
    actions->addWidget(tpUpgrade_, 3);
    actions->addWidget(tpSell_, 2);
    pv->addLayout(actions);
    panel_->hide();
    v->addWidget(panel_);

    connect(tpUpgrade_, &QPushButton::clicked, this, [this] {
        Tower *t = game_.getTower(board_->view.selectedId);
        if (!t || game_.isOver()) return;
        int cost = game_.upgradeCost(*t);
        if (cost < 0) { fail("Already at max level"); return; }
        if (game_.coins < cost) { fail("Not enough coins"); return; }
        sfx("click");
        if (!game_.upgradeTower(t->id)) fail("Not enough coins");
    });
    connect(tpSell_, &QPushButton::clicked, this, [this] {
        Tower *t = game_.getTower(board_->view.selectedId);
        if (!t || game_.isOver()) return;
        sfx("click");
        game_.sellTower(t->id);
        board_->view.selectedId = -1;
    });

    v->addStretch(1);
    startWave_ = button("Start wave 1", "primary");
    startWave_->setMinimumHeight(48);
    startWave_->setStyleSheet("font-size: 16px; font-weight: 700;");
    connect(startWave_, &QPushButton::clicked, this, [this] {
        if (game_.startWave()) sfx("click");
        else fail("Can't start a wave now");
    });
    v->addWidget(startWave_);

    QLabel *hint = label(QString("Tip: pick a plant, then click a grass tile. Keys: 1–%1 plants, "
                                 "Space pause, F speed, Esc cancel.").arg(data::TOWERS.size()), "hint");
    hint->setWordWrap(true);
    v->addWidget(hint);
    return side;
}

QWidget *MainWindow::buildOverlay(QLabel *&title, QLabel *&text, QPushButton *&btn) {
    auto *overlay = new QWidget(centralWidget());
    overlay->setObjectName("overlay");
    overlay->setAttribute(Qt::WA_StyledBackground);
    auto *outer = new QVBoxLayout(overlay);
    outer->setAlignment(Qt::AlignCenter);

    // Fixed card and text widths, so wrapped text always gets the height it needs
    // (a centered layout otherwise sizes word-wrapped labels for the wrong width).
    constexpr int CARD_W = 460, PAD = 32;
    auto *card = new QFrame;
    card->setObjectName("card");
    card->setFixedWidth(CARD_W);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(PAD, 28, PAD, 28);
    v->setSpacing(16);
    v->setSizeConstraint(QLayout::SetMinAndMaxSize);
    title = label({}, "overlayTitle");
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    title->setFixedWidth(CARD_W - 2 * PAD);
    text = label({}, "overlayText");
    text->setAlignment(Qt::AlignCenter);
    text->setWordWrap(true);
    text->setFixedWidth(CARD_W - 2 * PAD);
    btn = button({}, "primary");
    btn->setMinimumHeight(48);
    btn->setStyleSheet("font-size: 17px; font-weight: 700;");
    btn->setFocusPolicy(Qt::StrongFocus);   // Enter/Space press it
    btn->setDefault(true);
    v->addWidget(title);
    v->addWidget(text);
    v->addWidget(btn);
    outer->addWidget(card);

    overlay->setGeometry(centralWidget()->rect());
    overlay->raise();
    return overlay;
}

void MainWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    for (QWidget *o : {intro_, end_, next_})
        if (o) o->setGeometry(centralWidget()->rect());
}

// ---------- loop ----------

bool MainWindow::overlayOpen() const {
    return intro_->isVisible() || end_->isVisible() || next_->isVisible();
}

bool MainWindow::paused() const {
    return userPaused_ || overlayOpen() || isMinimized();
}

void MainWindow::tick() {
    const double dt = std::min(0.05, clock_.nsecsElapsed() / 1e9);
    clock_.restart();
    if (!paused())
        for (int i = 0; i < speed_; i++) game_.update(dt);
    board_->update();
    syncUi();
}

void MainWindow::setStat(QLabel *l, const QString &text) {
    if (l->text() == text) return;
    l->setText(text);
    if (!primed_) return;
    setProp(l, "bump", true);
    QTimer::singleShot(BUMP_MS, l, [l] { setProp(l, "bump", false); });
}

void MainWindow::syncUi() {
    View &view = board_->view;
    const bool over = game_.isOver();
    if (over) { view.placing = -1; view.selectedId = -1; }

    // HUD
    setStat(map_, QStringLiteral("🗺️ %1 (%2/%3)").arg(game_.map().name).arg(game_.mapIndex + 1)
                      .arg(game_.totalMaps));
    setStat(coins_, COIN + " " + QString::number(game_.coins));
    setStat(lives_, QStringLiteral("❤️ ") + QString::number(game_.lives));
    setStat(wave_, QStringLiteral("🌊 %1 / %2").arg(game_.wave).arg(game_.totalWaves));
    setProp(lives_, "low", game_.lives <= 5);

    // Shop
    for (int i = 0; i < int(shopItems_.size()); i++) {
        QPushButton *b = shopItems_[i];
        bool disabled = over || game_.coins < data::TOWERS[i].levels[0].cost;
        if (b->isEnabled() == disabled) b->setEnabled(!disabled);
        bool selected = view.placing == i;
        if (b->isChecked() != selected) b->setChecked(selected);
    }
    board_->setCursor(view.placing >= 0 ? Qt::CrossCursor : Qt::ArrowCursor);
    if (view.hover) view.hover->valid = isValid(view.hover->col, view.hover->row);

    // Tower panel
    Tower *t = view.selectedId >= 0 ? game_.getTower(view.selectedId) : nullptr;
    if (view.selectedId >= 0 && !t) view.selectedId = -1;
    if (panel_->isVisible() != (t != nullptr)) panel_->setVisible(t != nullptr);
    if (t) {
        fillPanel(*t);
        int cost = game_.upgradeCost(*t);
        setTextIfChanged(tpUpgrade_, cost < 0 ? "Max level" : QString("Upgrade · %1 %2").arg(COIN).arg(cost));
        tpUpgrade_->setEnabled(!over && cost >= 0 && game_.coins >= cost);
        setTextIfChanged(tpSell_, QString("Sell · %1 %2").arg(COIN).arg(game_.sellValue(*t)));
        tpSell_->setEnabled(!over);
    } else {
        panelKey_.clear();
    }

    // Start wave button
    int next = std::min(game_.wave + 1, game_.totalWaves);
    setTextIfChanged(startWave_, game_.phase == Phase::Wave
                                     ? QString("Wave %1…").arg(game_.wave)
                                     : QString("Start wave %1").arg(next));
    startWave_->setEnabled(game_.phase == Phase::Build && game_.wave < game_.totalWaves);

    primed_ = true;
}

void MainWindow::fillPanel(const Tower &t) {
    QString key = QString("%1:%2:%3").arg(t.id).arg(t.type).arg(t.level);
    if (key == panelKey_) return;
    panelKey_ = key;
    const data::TowerDef &def = data::TOWERS[t.type];
    const data::TowerLevel &lv = def.levels[t.level];
    tpIcon_->setText(def.icon);
    tpName_->setText(def.name);
    tpLevel_->setText(QString("Level %1 / %2").arg(t.level + 1).arg(def.levels.size()));
    while (tpStats_->rowCount() > 0) tpStats_->removeRow(0);
    auto row = [this](const QString &name, const QString &value) {
        QLabel *v = new QLabel(value);
        v->setAlignment(Qt::AlignRight);
        v->setStyleSheet("font-weight: 700;");
        tpStats_->addRow(name, v);
    };
    if (lv.income > 0) {
        row("Income", QString("+%1 %2 per wave").arg(lv.income).arg(COIN));
        return;
    }
    row("Damage", QString::number(lv.damage));
    row("Range", QString::number(lv.range) + " tiles");
    row("Fire rate", QString::number(lv.rate) + "/s");
    if (lv.slow > 0) row("Slow", QString("%1% for %2s").arg(qRound(lv.slow * 100)).arg(lv.slowTime));
    if (lv.splash > 0) row("Splash", QString::number(lv.splash) + " tiles");
    if (lv.poisonDps > 0) row("Poison", QString("%1/s for %2s").arg(lv.poisonDps).arg(lv.poisonTime));
    if (def.projectile == data::Projectile::Flame)
        row("Spread", QString("%1° cone").arg(qRound(data::FLAME_HALF_ANGLE * 2 * 180 / M_PI)));
}

// ---------- input ----------

bool MainWindow::isValid(int col, int row) const {
    if (!game_.canPlace(col, row)) return false;
    const int placing = board_->view.placing;
    return placing < 0 || game_.coins >= data::TOWERS[placing].levels[0].cost;
}

void MainWindow::fail(const QString &msg) {
    sfx("error");
    board_->showToast(msg, true);
}

void MainWindow::toggleShop(int type) {
    View &view = board_->view;
    if (overlayOpen() || game_.isOver()) return;
    if (view.placing == type) {
        view.placing = -1;
        sfx("click");
        return;
    }
    if (game_.coins < data::TOWERS[type].levels[0].cost) { fail("Not enough coins"); return; }
    view.placing = type;
    view.selectedId = -1;
    sfx("click");
}

void MainWindow::onHover(int col, int row) {
    board_->view.hover = View::Hover{col, row, isValid(col, row)};
}

void MainWindow::onClick(int col, int row) {
    View &view = board_->view;
    if (overlayOpen() || game_.isOver()) return;
    if (view.placing >= 0) {
        const int cost = data::TOWERS[view.placing].levels[0].cost;
        if (!game_.canPlace(col, row)) { fail("Can't plant there"); return; }
        if (game_.coins < cost) { fail("Not enough coins"); return; }
        if (!game_.placeTower(view.placing, col, row)) { fail("Can't plant there"); return; }
        if (game_.coins < cost) view.placing = -1;   // keep planting while affordable
        onHover(col, row);
        return;
    }
    Tower *t = game_.towerAt(col, row);
    view.selectedId = t ? t->id : -1;
}

void MainWindow::keyPressEvent(QKeyEvent *e) {
    if (overlayOpen() || (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        QMainWindow::keyPressEvent(e);
        return;
    }
    View &view = board_->view;
    switch (e->key()) {
    case Qt::Key_Escape:
        if (view.placing >= 0) view.placing = -1;
        else view.selectedId = -1;
        return;
    case Qt::Key_Space:
        if (!e->isAutoRepeat()) { sfx("click"); setUserPaused(!userPaused_); }
        return;
    case Qt::Key_F:
        if (!e->isAutoRepeat()) { sfx("click"); setSpeed(speed_ == 2 ? 1 : 2); }
        return;
    default:
        break;
    }
    const int n = e->key() - Qt::Key_1;
    if (!e->isAutoRepeat() && n >= 0 && n < int(data::TOWERS.size())) {
        toggleShop(n);
        return;
    }
    QMainWindow::keyPressEvent(e);
}

// ---------- header state ----------

void MainWindow::setUserPaused(bool on) {
    userPaused_ = on;
    pauseBtn_->setText(on ? QStringLiteral("▶") : QStringLiteral("⏸"));
    pauseBtn_->setToolTip(on ? "Resume (Space)" : "Pause (Space)");
    setProp(pauseBtn_, "on", on);
    board_->setPausedBanner(on);
}

void MainWindow::setSpeed(int s) {
    speed_ = s == 2 ? 2 : 1;
    speedBtn_->setText(speed_ == 2 ? QStringLiteral("2×") : QStringLiteral("1×"));
    setProp(speedBtn_, "on", speed_ == 2);
}

void MainWindow::syncSoundButton() {
    const bool on = audio_.enabled();
    soundBtn_->setText(on ? QStringLiteral("🔊") : QStringLiteral("🔇"));
    soundBtn_->setToolTip(on ? "Sound on" : "Sound off");
}

// ---------- overlays ----------

void MainWindow::clearView() {
    View &view = board_->view;
    view.placing = -1;
    view.selectedId = -1;
    view.hover.reset();
}

static QString livesText(int lives) {
    return QString("%1 %2").arg(lives).arg(lives == 1 ? "life" : "lives");
}

void MainWindow::showEnd(bool won) {
    clearView();
    endTitle_->setText(won ? "You saved every garden!" : "The bugs got through!");
    endText_->setText(won ? QString("You cleared all %1 maps with %2 left.")
                                .arg(game_.totalMaps).arg(livesText(game_.lives))
                          : QString("You reached wave %1 of %2 on %3 (map %4 of %5).")
                                .arg(game_.wave).arg(game_.totalWaves).arg(game_.map().name)
                                .arg(game_.mapIndex + 1).arg(game_.totalMaps));
    end_->show();
    end_->raise();
    end_->findChild<QPushButton *>()->setFocus();
}

void MainWindow::showMapCleared() {
    clearView();
    const data::MapDef &next = data::MAPS[game_.mapIndex + 1];
    nextTitle_->setText(QString("%1 cleared!").arg(game_.map().name));
    nextText_->setText(QString("You kept %1. Next up: %2. %3\n\n"
                               "Your coins and plants reset for the new garden, but your lives carry over.")
                           .arg(livesText(game_.lives)).arg(next.name).arg(next.blurb));
    nextBtn_->setText(QString("On to %1").arg(next.name));
    next_->show();
    next_->raise();
    nextBtn_->setFocus();
}

void MainWindow::goToNextMap() {
    sfx("click");
    game_.nextMap();
    clearView();
    setUserPaused(false);
    setSpeed(1);
    next_->hide();
    primed_ = false;   // new coins total shouldn't "bump"
    centralWidget()->setFocus();
}

// ---------- screenshot scenes ----------

bool MainWindow::setupScene(const QString &scene) {
    if (scene == "intro") return true;
    const QStringList known = {"play", "placing", "mapclear", "win", "lose"};
    if (!known.contains(scene)) return false;
    intro_->hide();

    // A busy mid-game board: one of every plant, bugs on the path, a tower selected.
    // Signals are blocked so setting it up makes no sounds or toasts.
    QSignalBlocker quiet(&game_);
    game_.coins = 5000;
    auto plant = [this](const char *id, int col, int row, int upgrades) {
        for (int i = 0; i < int(data::TOWERS.size()); i++) {
            if (data::TOWERS[i].id != id) continue;
            if (Tower *t = game_.placeTower(i, col, row)) {
                int tid = t->id;
                for (int u = 0; u < upgrades; u++) game_.upgradeTower(tid);
                return tid;
            }
        }
        return -1;
    };
    const int selected = plant("sprout", 1, 2, 2);
    plant("frost", 3, 3, 1);
    plant("mortar", 4, 4, 0);
    plant("cactus", 8, 4, 2);
    plant("mushroom", 3, 5, 1);
    plant("chili", 5, 5, 2);
    plant("sunflower", 0, 8, 1);
    game_.coins = 1234;
    game_.wave = 6;
    game_.startWave();
    for (int i = 0; i < 60 * 9; i++) game_.update(1.0 / 60);

    if (scene == "play") {
        board_->view.selectedId = selected;
        board_->showToast(QString("Wave 6 cleared! +50 %1  ·  Sunflowers +28 %1").arg(COIN));
    } else if (scene == "placing") {
        board_->view.placing = 4;   // mushroom
        onHover(7, 4);
    } else if (scene == "mapclear") {
        showMapCleared();
    } else {
        showEnd(scene == "win");
    }
    syncUi();
    return true;
}

void MainWindow::playAgain() {
    game_.reset();
    clearView();
    setUserPaused(false);
    setSpeed(1);
    end_->hide();
    primed_ = false;   // resync the HUD without bump animations
    sfx("click");
    centralWidget()->setFocus();
}
