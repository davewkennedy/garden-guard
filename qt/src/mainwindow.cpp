#include "mainwindow.h"

#include "boardwidget.h"
#include "data.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QMenu>
#include <QSlider>
#include <QWidgetAction>
#include <map>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
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
#shopItem[fresh="true"] { border-color: #f2b705; background: #fff8dc; }
#shopNew {
  background: #f2b705; color: #ffffff; border-radius: 6px;
  font-size: 9px; font-weight: 800; padding: 0 3px;
}
#shopIcon { font-size: 24px; }
#shopName { font-weight: 700; font-size: 12px; }
#shopCost { font-weight: 700; font-size: 12px; color: #a0683c; }
#shopCost:disabled { color: #d9480f; }
#shopDesc { font-size: 11px; color: #64735a; }
#nextWave { color: #24331c; font-size: 14px; font-weight: 600; }
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
    setWindowTitle("Garden Guard++");
    setStyleSheet(STYLE);

    auto *central = new QWidget;
    central->setObjectName("central");
    central->setAttribute(Qt::WA_StyledBackground);
    central->setFocusPolicy(Qt::StrongFocus);
    setCentralWidget(central);

    board_ = new BoardWidget(&game_);
    connect(board_, &BoardWidget::tileHovered, this, &MainWindow::onHover);
    connect(board_, &BoardWidget::pointClicked, this, [this](double x, double y) { lastClick_ = QPointF(x, y); });
    connect(board_, &BoardWidget::tileClicked, this, &MainWindow::onClick);
    connect(board_, &BoardWidget::tileDoubleClicked, this, &MainWindow::onDoubleClick);
    connect(&game_, &Game::gust, this, [this] { board_->showToast(QStringLiteral("🌬️ A gust of wind! Flyers are blown off course")); });
    connect(&game_, &Game::wildcard, this, [this] { board_->showToast(QStringLiteral("🦇 A wild bat appears! Catch it for 30 coins!")); });
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
    introTitle->setText("Garden Guard++");
    introText->setText(QString("Bugs are marching on your house! Plant defenders beside the path to "
                               "stop them, and upgrade them with the coins you earn. Survive %1 waves "
                               "in each of %2 gardens. New bugs (even flying ones!) appear as you go, "
                               "and your lives carry over between gardens.")
                           .arg(data::MAPS[0].waves.empty() ? data::WAVES.size() : data::MAPS[0].waves.size())
                           .arg(data::MAPS.size()));
    {
        // Personal best, saved between runs as map * 100 + wave.
        const int best = QSettings().value("best", 0).toInt();
        if (best > 0) {
            const int m = std::min(best / 100, int(data::MAPS.size()) - 1);
            introText->setText(introText->text() +
                               QString("\n\n🏆 Your best: %1, wave %2").arg(data::MAPS[m].name).arg(best % 100));
        }
    }
    startGame->setText("Let's grow!");
    connect(startGame, &QPushButton::clicked, this, [this] {
        sfx("click");
        intro_->hide();
        centralWidget()->setFocus();
    });
    {
        // Saved stars, and a shortcut to the furthest map reached (with fresh lives).
        QSettings settings;
        int stars = 0;
        for (int i = 0; i < int(data::MAPS.size()); i++) stars += settings.value(QString("stars/%1").arg(i), 0).toInt();
        if (stars > 0)
            introText->setText(introText->text() + QString("\n⭐ %1 of %2 stars earned").arg(stars).arg(3 * data::MAPS.size()));
        const int furthest = std::min(settings.value("best", 0).toInt() / 100, int(data::MAPS.size()) - 1);
        if (furthest > 0) {
            auto *cont = button(QString("Continue from %1 (map %2)").arg(data::MAPS[furthest].name).arg(furthest + 1));
            cont->setMinimumHeight(40);
            cont->setFocusPolicy(Qt::StrongFocus);
            if (auto *card = intro_->findChild<QFrame *>("card")) card->layout()->addWidget(cont);
            connect(cont, &QPushButton::clicked, this, [this, furthest] { startRunAt(furthest); });
            auto *choose = button("🗺️ Choose map");
            choose->setMinimumHeight(40);
            choose->setFocusPolicy(Qt::StrongFocus);
            if (auto *card = intro_->findChild<QFrame *>("card")) card->layout()->addWidget(choose);
            connect(choose, &QPushButton::clicked, this, [this, furthest] {
                sfx("click");
                showMapSelect(furthest);
            });
        }
    }
    buildMapSelect();
    buildJournal();
    buildDifficultyPicker();
    end_ = buildOverlay(endTitle_, endText_, again);
    again->setText("Play again");
    connect(again, &QPushButton::clicked, this, &MainWindow::playAgain);
    end_->hide();
    next_ = buildOverlay(nextTitle_, nextText_, nextBtn_);
    connect(nextBtn_, &QPushButton::clicked, this, &MainWindow::goToNextMap);
    next_->hide();
    bugCard_ = buildOverlay(bugCardTitle_, bugCardText_, bugCardBtn_);
    bugCardBtn_->setText("Got it, bring them on!");
    connect(bugCardBtn_, &QPushButton::clicked, this, [this] {
        sfx("click");
        bugCard_->hide();
        centralWidget()->setFocus();
    });
    bugCard_->hide();

    // Game events -> sounds and messages.
    connect(&game_, &Game::shot, this, [this](int type) { runShots_++; sfx("shoot_" + data::TOWERS[type].id); });
    connect(&game_, &Game::killed, this, [this](int reward) { runKills_++; runCoins_ += reward; sfx("kill"); });
    connect(&game_, &Game::placed, this, [this] { runPlanted_++; });
    connect(&game_, &Game::bugSound, this, [this](const QString &name) { sfx(name); });   // cricket hops, splits...
    connect(&game_, &Game::waveEnded, this, [this](int, int bonus, int harvest) { runCoins_ += bonus + harvest; });
    connect(&game_, &Game::leaked, this, [this] { sfx("leak"); });
    connect(&game_, &Game::placed, this, [this] { sfx("place"); });
    connect(&game_, &Game::upgraded, this, [this] { sfx("upgrade"); });
    connect(&game_, &Game::sold, this, [this] { sfx("sell"); });
    connect(&game_, &Game::waveStarted, this, [this](int wave) {
        sfx("waveStart");
        for (const auto &grp : game_.waves()[wave - 1].groups)
            if (data::ENEMIES[grp.enemy].boss) {
                board_->showToast(QString("⚠️ Boss incoming: %1 %2!")
                                      .arg(data::ENEMIES[grp.enemy].icon, data::ENEMIES[grp.enemy].name), true);
                break;
            }
        QSettings settings;
        const int progress = game_.mapIndex * 100 + wave;
        if (progress > settings.value("best", 0).toInt()) settings.setValue("best", progress);
    });
    connect(&game_, &Game::waveEnded, this, [this](int wave, int bonus, int harvest) {
        sfx("waveEnd");
        QString msg = QString("Wave %1 cleared! +%2 %3").arg(wave).arg(bonus).arg(COIN);
        if (harvest > 0) {
            sfx("harvest");
            msg += QString("  ·  Sunflowers +%1 %2").arg(harvest).arg(COIN);
        }
        board_->showToast(msg);
    });
    // Celebrate first, then show the card once the confetti has had its moment.
    connect(&game_, &Game::mapCleared, this, [this] {
        sfx("win");
        board_->celebrate();
        QTimer::singleShot(1500, this, [this] { if (game_.phase == Phase::MapCleared) showMapCleared(); });
    });
    connect(&game_, &Game::mapStarted, this, [this](int index) {
        mapStartLives_ = game_.lives;
        board_->showToast(QString("Map %1 of %2: %3").arg(index + 1).arg(game_.totalMaps)
                              .arg(data::MAPS[index].name));
    });
    // Stars first, so the cards shown afterwards can display them.
    connect(&game_, &Game::mapCleared, this, &MainWindow::awardStars);
    connect(&game_, &Game::won, this, &MainWindow::awardStars);
    connect(&game_, &Game::won, this, [this] {
        sfx("win");
        board_->celebrate();
        QTimer::singleShot(900, this, [this] { board_->celebrate(); });   // encore!
        QTimer::singleShot(1800, this, [this] { if (game_.phase == Phase::Won) showEnd(true); });
    });
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
    h->addWidget(label("Garden Guard++", "title"));
    h->addStretch(1);
    map_ = label({}, "stat");
    coins_ = label({}, "stat");
    lives_ = label({}, "stat");
    wave_ = label({}, "stat");
    map_->setToolTip("Campaign map");
    for (QLabel *l : {map_, coins_, lives_, wave_}) h->addWidget(l);
    h->addStretch(1);

    auto *journalBtn = button(QStringLiteral("📖"), {}, "iconBtn");
    journalBtn->setToolTip("Garden Journal (J)");
    connect(journalBtn, &QPushButton::clicked, this, [this] { toggleJournal(); });
    h->addWidget(journalBtn);

    autoBtn_ = button(QStringLiteral("🤖"), {}, "iconBtn");
    autoBtn_->setToolTip("Autopilot: the AI plays the whole campaign at 2× (A)");
    connect(autoBtn_, &QPushButton::clicked, this, [this] { sfx("click"); setAuto(!auto_); });
    h->addWidget(autoBtn_);

    speedBtn_ = button({}, {}, "iconBtn");
    pauseBtn_ = button({}, {}, "iconBtn");
    soundBtn_ = button({}, {}, "iconBtn");
    speedBtn_->setToolTip("Fast forward (F)");
    pauseBtn_->setToolTip("Pause (Space)");
    connect(speedBtn_, &QPushButton::clicked, this, [this] { sfx("click"); setSpeed(speed_ % 3 + 1); });
    connect(pauseBtn_, &QPushButton::clicked, this, [this] { sfx("click"); setUserPaused(!userPaused_); });
    connect(soundBtn_, &QPushButton::clicked, this, [this] {
        audio_.setEnabled(!audio_.enabled());
        syncSoundButton();
        sfx("click");   // only audible when turning sound on
    });
    musicBtn_ = button(QStringLiteral("🎵"), {}, "iconBtn");
    musicBtn_->setToolTip("Music settings (M turns music on or off)");
    connect(musicBtn_, &QPushButton::clicked, this, [this] { sfx("click"); showMusicMenu(); });
    for (QPushButton *b : {speedBtn_, pauseBtn_, soundBtn_, musicBtn_}) h->addWidget(b);
    return header;
}

// ---------- music (music.h; same rows and wording as the web's music panel) ----------

void MainWindow::toggleMusic() {
    music_.setEnabled(!music_.enabled());
    setProp(musicBtn_, "on", music_.enabled());
    board_->showToast(music_.enabled() ? QStringLiteral("🎵 Music on") : QStringLiteral("🎵 Music off"));
}

void MainWindow::showMusicMenu() {
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *box = new QWidget(menu);
    auto *form = new QFormLayout(box);
    auto *musicOn = new QCheckBox(box);
    musicOn->setChecked(music_.enabled());
    auto *vol = new QSlider(Qt::Horizontal, box);
    vol->setRange(0, 100);
    vol->setSingleStep(5);
    vol->setPageStep(5);
    vol->setValue(int(std::lround(music_.volume() * 100)));
    vol->setMinimumWidth(140);
    auto *sfxOn = new QCheckBox(box);
    sfxOn->setChecked(audio_.enabled());
    form->addRow("Music", musicOn);
    form->addRow("Music volume", vol);
    form->addRow("Sound effects", sfxOn);
    connect(musicOn, &QCheckBox::toggled, this, [this](bool on) { if (on != music_.enabled()) toggleMusic(); });
    connect(vol, &QSlider::valueChanged, this, [this, vol](int v) {
        const int snapped = (v + 2) / 5 * 5;   // step 5, as on the web
        if (snapped != v) { vol->setValue(snapped); return; }
        music_.setVolume(snapped / 100.f);
    });
    connect(sfxOn, &QCheckBox::toggled, this, [this](bool on) {
        audio_.setEnabled(on);
        syncSoundButton();
        sfx("click");
    });
    auto *act = new QWidgetAction(menu);
    act->setDefaultWidget(box);
    menu->addAction(act);
    menu->popup(musicBtn_->mapToGlobal(QPoint(0, musicBtn_->height())));
}

QPushButton *MainWindow::makeShopItem(int type, int index) {
    const data::TowerDef &def = data::TOWERS[type];
    auto *b = button({}, {}, "shopItem");
    b->setCheckable(true);
    b->setMinimumHeight(52);
    // Keys 1-9, then 0 and - for the tenth and eleventh plants.
    const QString key = index < 9 ? QString::number(index + 1) : index == 9 ? "0" : index == 10 ? "-" : "";
    const bool groundOnly = def.projectile == data::Projectile::Mortar || def.projectile == data::Projectile::Flame;
    const bool attacker = def.projectile != data::Projectile::None && def.projectile != data::Projectile::Aura;
    b->setToolTip(QString("<b>%1</b> %2<br>%3%4").arg(def.name, key.isEmpty() ? "" : "(key " + key + ")", def.desc,
                  !attacker ? "" : groundOnly ? "<br><i>✗ Ground only: can't hit flyers</i>"
                                              : "<br><i>✓ Hits flyers too</i>"));

    // Compact card (the description is in the tooltip): icon, cost and key on top, name below.
    auto *g = new QGridLayout(b);
    g->setContentsMargins(8, 4, 8, 5);
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(0);
    g->addWidget(label(def.icon, "shopIcon"), 0, 0);
    g->addWidget(label(COIN + " " + QString::number(def.levels[0].cost), "shopCost"), 0, 1);
    if (!key.isEmpty()) g->addWidget(label(key, "shopKey"), 0, 2, Qt::AlignRight | Qt::AlignTop);
    // Plants unlocked on this very map get a NEW ribbon and a golden border (claude2-0c). The
    // ribbon floats over the card's top-left corner, outside the layout, so it takes no width
    // from the cost, key or name.
    const bool fresh = game_.mapIndex > 0 && def.unlockMap == game_.mapIndex && !game_.unlockAll;
    g->addWidget(label(def.name, "shopName"), 1, 0, 1, 3);
    if (fresh) {
        QLabel *ribbon = label("NEW", "shopNew");
        ribbon->setParent(b);
        ribbon->adjustSize();
        ribbon->move(2, 0);
        ribbon->raise();
        b->setProperty("fresh", true);
        b->setToolTip(b->toolTip() + "<br><b>✨ New plant!</b> " + def.counters.toHtmlEscaped());
    }
    g->setColumnStretch(1, 1);
    for (QLabel *l : b->findChildren<QLabel *>()) l->setAttribute(Qt::WA_TransparentForMouseEvents);

    connect(b, &QPushButton::clicked, this, [this, type] { toggleShop(type); });
    return b;
}

// Shows only the plants unlocked on the current map: older unlocks first, so their keys stay
// the same from map to map, and each map's new plants join at the end.
void MainWindow::rebuildShop() {
    shopMap_ = game_.mapIndex;
    shopUnlockAll_ = game_.unlockAll;
    for (QPushButton *b : shopItems_) b->deleteLater();
    shopItems_.clear();
    shopOrder_.clear();
    for (int i = 0; i < int(data::TOWERS.size()); i++)
        if (game_.isUnlocked(i)) shopOrder_.push_back(i);
    std::stable_sort(shopOrder_.begin(), shopOrder_.end(),
                     [](int a, int b) { return data::TOWERS[a].unlockMap < data::TOWERS[b].unlockMap; });
    for (int i = 0; i < int(shopOrder_.size()); i++) {
        QPushButton *item = makeShopItem(shopOrder_[i], i);
        shopItems_.push_back(item);
        shopGrid_->addWidget(item, i / 2, i % 2);
    }
}

QWidget *MainWindow::buildSide() {
    auto *side = new QFrame;
    side->setObjectName("side");
    side->setFixedWidth(306);
    auto *v = new QVBoxLayout(side);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(12);

    v->addWidget(label("Plants", "sideTitle"));
    shopGrid_ = new QGridLayout;   // filled by rebuildShop() on the first syncUi
    shopGrid_->setSpacing(6);
    v->addLayout(shopGrid_);

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

    // Fire point (ambush): the plant only shoots bugs near a spot you pick.
    tpAim_ = button(QStringLiteral("🎯 Set fire point (T)"));
    tpAim_->setToolTip("Ambush: pick a spot in range. The plant waits for bugs near there\n"
                       "instead of shooting the first bug it sees, and hits 30% harder.\n"
                       "Best for splash and flame plants (mortar, chili) at busy corners.\n"
                       "Not great for frost: a slow should reach every bug in range.");
    pv->addWidget(tpAim_);
    connect(tpAim_, &QPushButton::clicked, this, [this] {
        View &view = board_->view;
        Tower *t = game_.getTower(view.selectedId);
        if (!t || game_.isOver()) return;
        sfx("click");
        if (t->hasFirePoint) {
            game_.clearFirePoint(t->id);
            board_->showToast("🎯 Fire point cleared: back to automatic targeting");
        } else if (view.aimingId == t->id) {
            view.aimingId = -1;   // second click cancels
        } else {
            view.aimingId = t->id;
            board_->showToast("🎯 Click a spot inside the range circle (Esc cancels)");
        }
    });
    panel_->hide();
    v->addWidget(panel_);

    connect(tpUpgrade_, &QPushButton::clicked, this, [this] {
        Tower *t = game_.getTower(board_->view.selectedId);
        if (!t || game_.isOver()) return;
        if (upgradeAllInGroup()) return;   // group mode (double-click)
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

    // Preview of the bugs in the next wave, so you can plan your defence.
    nextWave_ = label({}, "nextWave");
    nextWave_->setWordWrap(true);
    nextWave_->setAlignment(Qt::AlignCenter);
    v->addWidget(nextWave_);

    QLabel *hint = label("Tip: hover a plant for details. Keys: number keys pick plants, "
                         "N next wave, T fire point, A autopilot, Space pause, F speed (1×/2×/3×), Esc cancel.", "hint");
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
    for (QWidget *o : {intro_, end_, next_, bugCard_, mapSelect_, journal_})
        if (o) o->setGeometry(centralWidget()->rect());
}

// ---------- loop ----------

bool MainWindow::overlayOpen() const {
    return intro_->isVisible() || end_->isVisible() || next_->isVisible() || bugCard_->isVisible() ||
           (mapSelect_ && mapSelect_->isVisible()) || (journal_ && journal_->isVisible());
}

// Bugs that appear on this map's waves but on no earlier map get introduced.
void MainWindow::showNewBugs() {
    auto typesOn = [](int m) {
        std::vector<bool> seen(data::ENEMIES.size(), false);
        const auto &ws = data::MAPS[m].waves.empty() ? data::WAVES : data::MAPS[m].waves;
        for (const auto &w : ws)
            for (const auto &grp : w.groups) seen[grp.enemy] = true;
        return seen;
    };
    const int m = game_.mapIndex;
    if (m == 0) return;   // the intro card covers the first map
    std::vector<bool> before(data::ENEMIES.size(), false);
    for (int i = 0; i < m; i++) {
        auto s = typesOn(i);
        for (size_t k = 0; k < s.size(); k++) before[k] = before[k] || s[k];
    }
    const auto now = typesOn(m);
    QStringList lines;
    for (size_t k = 0; k < now.size(); k++) {
        if (!now[k] || before[k]) continue;
        const data::EnemyDef &d = data::ENEMIES[k];
        QString line = QString("%1  <b>%2</b>").arg(d.icon, d.name);
        if (d.flying) line += QStringLiteral(" <i>(flying)</i>");
        if (!d.desc.isEmpty()) line += "<br>" + d.desc.toHtmlEscaped();
        lines << line;
    }
    // ...and the plants unlocked on this map, each with the bug it counters (claude2-0c).
    QStringList plants;
    for (const data::TowerDef &def : data::TOWERS) {
        if (def.unlockMap != m) continue;
        QString line = QString("%1  <b>%2</b>").arg(def.icon, def.name);
        if (!def.counters.isEmpty()) line += ": " + def.counters.toHtmlEscaped();
        plants << line;
    }
    // ...and this map's special rules, so nothing invisible surprises the player (claude2-0c).
    // Always listed (not just the ones new since the last map), so Continue/map select runs see them.
    QStringList rules;
    {
        const data::MapDef &md = data::MAPS[m];
        const auto &ws = md.waves.empty() ? data::WAVES : md.waves;
        bool wildBats = false;
        for (const auto &w : ws)
            for (const auto &grp : w.groups) wildBats = wildBats || data::ENEMIES[grp.enemy].wild;
        bool water = false, rocks = false;
        for (const auto &t : md.terrain) {
            water = water || t[2] == data::TERRAIN_WATER || t[2] == data::TERRAIN_LILY;
            rocks = rocks || t[2] == data::TERRAIN_ROCK;
        }
        if (md.night) rules << QStringLiteral("🌙 <b>Night</b>: the garden is dark, so watch closely");
        if (wildBats) rules << QStringLiteral("🦇 <b>Wild bats</b> ignore the sky lanes and swoop straight for your house");
        if (md.windEvery > 0)
            rules << QStringLiteral("🌬️ <b>Wind gusts</b> blow flyers off course; a 🍇 Grape Vine keeps them grounded");
        if (md.bugSpeedMult < 1)
            rules << QStringLiteral("❄️ <b>Deep snow</b> slows ground bugs, but frost is weaker");
        if (water) rules << QStringLiteral("🪷 <b>Ponds</b>: plant on the lily pads, not the water");
        if (rocks) rules << QStringLiteral("🪨 <b>Boulders</b> block planting");
        if (!md.path2.empty()) rules << QStringLiteral("🔀 <b>Two entrances</b>: bugs come from two directions");
    }
    if (lines.isEmpty() && plants.isEmpty() && rules.isEmpty()) return;
    const QString bugsWord = lines.size() == 1 ? "New bug" : "New bugs";
    if (lines.isEmpty() && plants.isEmpty())
        bugCardTitle_->setText(QString("%1: watch out!").arg(data::MAPS[m].name));
    else if (plants.isEmpty() && !lines.isEmpty())
        bugCardTitle_->setText(bugsWord + "!");
    else if (lines.isEmpty())
        bugCardTitle_->setText(plants.size() == 1 ? "New plant!" : "New plants!");
    else
        bugCardTitle_->setText(QString("%1… and new %2 to stop them!")
                                   .arg(bugsWord, plants.size() == 1 ? "plant" : "plants"));
    QString html = lines.join("<br><br>");
    if (!plants.isEmpty())
        html += QString("%1<span style=\"color:#2f8a3a; font-weight:800;\">🌱 Unlocked in your shop:</span><br>%2")
                    .arg(lines.isEmpty() ? "" : "<br><br>", plants.join("<br>"));
    if (!rules.isEmpty())
        html += QString("%1<span style=\"color:#a0683c; font-weight:800;\">🗺️ On this map:</span><br>%2")
                    .arg(html.isEmpty() ? "" : "<br><br>", rules.join("<br>"));
    bugCardText_->setTextFormat(Qt::RichText);
    bugCardText_->setText(html);
    bugCard_->show();
    bugCard_->raise();
    bugCardBtn_->setFocus();
}

bool MainWindow::paused() const {
    return userPaused_ || overlayOpen() || isMinimized();
}

void MainWindow::tick() {
    const double dt = std::min(0.05, clock_.nsecsElapsed() / 1e9);
    clock_.restart();
    if (!paused())
        for (int i = 0; i < speed_; i++) game_.update(dt);
    runAutopilot();
    music_.follow(game_.phase);   // idempotent: acts only when the phase changes
    board_->update();
    syncUi();
}

// ---------- autopilot ----------

void MainWindow::setAuto(bool on) {
    auto_ = on;
    setProp(autoBtn_, "on", on);   // highlighted while it's playing
    lastThink_ = buildStart_ = -1;
    overlayTiming_ = false;
    if (on) {
        setSpeed(2);
        setUserPaused(false);
        board_->showToast(QStringLiteral("🤖 Autopilot on: sit back and watch (A to stop)"));
    } else {
        board_->showToast(QStringLiteral("🤖 Autopilot off: your turn!"));
    }
}

// Plays like the headless test in garden_simtest: decides twice per game second,
// starts each wave after a short pause, and clicks through the map-cleared card.
void MainWindow::runAutopilot() {
    if (!auto_) return;
    if (intro_->isVisible()) intro_->hide();
    if (end_->isVisible()) {   // campaign over: hand control back
        setAuto(false);
        return;
    }
    for (QWidget *card : {next_, bugCard_}) {   // let a card show for a moment, then move on
        if (!card->isVisible()) continue;
        if (!overlayTiming_) { overlayClock_.start(); overlayTiming_ = true; }
        if (overlayClock_.elapsed() > (card == bugCard_ ? 4000 : 2500)) {
            overlayTiming_ = false;
            if (card == next_) goToNextMap();
            else bugCard_->hide();
        }
        return;
    }
    if (game_.isOver() || paused()) return;

    // game_.time restarts on each map, hence the "<" checks.
    if (game_.time < lastThink_ || game_.time - lastThink_ >= 0.5) {
        lastThink_ = game_.time;
        autopilot_.think();
    }
    if (game_.phase == Phase::Build) {
        if (buildStart_ < 0 || game_.time < buildStart_) buildStart_ = game_.time;
        if (game_.time - buildStart_ >= 2.0) {
            game_.startWave();
            buildStart_ = -1;
        }
    } else {
        buildStart_ = -1;
    }
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
    if (shopMap_ != game_.mapIndex || shopUnlockAll_ != game_.unlockAll) rebuildShop();
    if (view.placing >= 0 && !game_.isUnlocked(view.placing)) view.placing = -1;
    for (int i = 0; i < int(shopItems_.size()); i++) {
        QPushButton *b = shopItems_[i];
        const int type = shopOrder_[i];
        bool disabled = over || game_.coins < data::TOWERS[type].levels[0].cost;
        if (b->isEnabled() == disabled) b->setEnabled(!disabled);
        bool selected = view.placing == type;
        if (b->isChecked() != selected) b->setChecked(selected);
    }
    board_->setCursor(view.placing >= 0 || view.aimingId >= 0 ? Qt::CrossCursor : Qt::ArrowCursor);
    if (view.aimingId >= 0 && view.aimingId != view.selectedId) view.aimingId = -1;   // selection changed
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
        // Fire points only make sense for plants that attack.
        const auto proj = data::TOWERS[t->type].projectile;
        const bool canAim = proj != data::Projectile::None && proj != data::Projectile::Aura;
        tpAim_->setVisible(canAim);
        setTextIfChanged(tpAim_, t->hasFirePoint ? QStringLiteral("🎯 Clear fire point")
                                 : view.aimingId == t->id ? QStringLiteral("🎯 Pick a spot… (Esc)")
                                                           : QStringLiteral("🎯 Set fire point (T)"));
        tpAim_->setEnabled(!over);
        syncFocusPanel(*t, over);
    } else {
        panelKey_.clear();
        view.group.clear();
    }

    // Start wave button
    int next = std::min(game_.wave + 1, game_.totalWaves);
    setTextIfChanged(startWave_, game_.phase == Phase::Wave
                                     ? QString("Wave %1…").arg(game_.wave)
                                     : QString("Start wave %1").arg(next));
    startWave_->setEnabled(game_.phase == Phase::Build && game_.wave < game_.totalWaves);

    // Next wave preview: "Next: 🐜×10  🐞×4", with the boss called out.
    QString preview;
    if (game_.phase == Phase::Build && game_.wave < game_.totalWaves) {
        std::vector<int> counts(data::ENEMIES.size(), 0);
        for (const auto &grp : game_.waves()[game_.wave].groups) counts[grp.enemy] += grp.count;
        QStringList parts;
        bool boss = false, air = false;
        for (size_t i = 0; i < counts.size(); i++) {
            if (!counts[i]) continue;
            parts << QString("%1×%2").arg(data::ENEMIES[i].icon).arg(counts[i]);
            boss |= data::ENEMIES[i].boss;
            air |= data::ENEMIES[i].flying;
        }
        preview = "Next: " + parts.join("  ");
        if (air) preview += QStringLiteral("\n✈️ Flyers incoming: mortars and chilis can't hit them");
        if (boss) preview += QStringLiteral("\n⚠️ Boss wave!");
    }
    if (nextWave_->text() != preview) nextWave_->setText(preview);
    nextWave_->setVisible(!preview.isEmpty());

    primed_ = true;
}

void MainWindow::fillPanel(const Tower &t) {
    QString key = QString("%1:%2:%3:%4:%5:%6").arg(t.id).arg(t.type).arg(t.level).arg(t.hasFirePoint)
                      .arg(board_->view.group.size()).arg(t.focusId >= 0);
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
    if (def.projectile == data::Projectile::Aura) {
        row("Range", QString::number(lv.range) + " tiles");
        row("Boost", QString("+%1% fire rate to nearby plants").arg(qRound(lv.boost * 100)));
        return;
    }
    row("Damage", t.hasFirePoint ? QString("%1 (🎯 +%2%)").arg(game_.damageOf(t)).arg(qRound((AMBUSH_BONUS - 1) * 100))
                                 : QString::number(lv.damage));
    const bool groundOnly = def.projectile == data::Projectile::Mortar || def.projectile == data::Projectile::Flame;
    row("Air", groundOnly ? QStringLiteral("✗ ground only") : QStringLiteral("✓ hits flyers"));
    row("Range", QString::number(lv.range) + " tiles");
    row("Fire rate", QString::number(lv.rate) + "/s");
    if (lv.slow > 0) row("Slow", QString("%1% for %2s").arg(qRound(lv.slow * 100)).arg(lv.slowTime));
    if (lv.splash > 0) row("Splash", QString::number(lv.splash) + " tiles");
    if (lv.pierce > 0) row("Pierce", QString("%1 bugs").arg(lv.pierce));
    if (lv.shots > 0) row("Volley", QString("%1 targets").arg(lv.shots));
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
    if (overlayOpen() || game_.isOver() || !game_.isUnlocked(type)) return;
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
    if (view.aimingId >= 0) {
        // Choosing a fire point: the center of the clicked tile, which must be in range.
        const double x = col * data::TILE + data::TILE / 2.0, y = row * data::TILE + data::TILE / 2.0;
        if (!game_.setFirePoint(view.aimingId, x, y)) { fail("Out of range: pick a spot inside the circle"); return; }
        sfx("place");
        board_->showToast("🎯 Ambush set! This plant now waits for bugs at that spot.");
        view.aimingId = -1;
        return;
    }
    if (view.placing >= 0) {
        const int cost = data::TOWERS[view.placing].levels[0].cost;
        if (!game_.canPlace(col, row)) { fail("Can't plant there"); return; }
        if (game_.coins < cost) { fail("Not enough coins"); return; }
        if (!game_.placeTower(view.placing, col, row)) { fail("Can't plant there"); return; }
        if (game_.coins < cost) view.placing = -1;   // keep planting while affordable
        onHover(col, row);
        return;
    }
    if (tryFocusClick()) return;   // a bug was clicked with plants selected
    Tower *t = game_.towerAt(col, row);
    view.selectedId = t ? t->id : -1;
    view.group.clear();   // a single click selects just one plant
}

void MainWindow::keyPressEvent(QKeyEvent *e) {
    // J opens the 📖 journal, or closes it (the one key that works while its card is open).
    if (e->key() == Qt::Key_J && !e->isAutoRepeat() && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        (journal_ && (journal_->isVisible() || !overlayOpen()))) {
        toggleJournal();
        return;
    }
    if (overlayOpen() ||(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        QMainWindow::keyPressEvent(e);
        return;
    }
    View &view = board_->view;
    switch (e->key()) {
    case Qt::Key_Escape:
        if (view.aimingId >= 0) view.aimingId = -1;
        else if (view.placing >= 0) view.placing = -1;
        else if (!cancelFocusOnSelection()) { view.selectedId = -1; view.group.clear(); }
        return;
    case Qt::Key_Space:
        if (!e->isAutoRepeat()) { sfx("click"); setUserPaused(!userPaused_); }
        return;
    case Qt::Key_F:
        if (!e->isAutoRepeat()) { sfx("click"); setSpeed(speed_ % 3 + 1); }
        return;
    case Qt::Key_N:   // start the next wave
        if (!e->isAutoRepeat() && game_.startWave()) sfx("click");
        return;
    case Qt::Key_T:   // fire point for the selected plant (same as the 🎯 button)
        if (!e->isAutoRepeat() && view.selectedId >= 0 && tpAim_->isVisible() && tpAim_->isEnabled())
            tpAim_->click();
        return;
    case Qt::Key_A:   // autopilot
        if (!e->isAutoRepeat()) { sfx("click"); setAuto(!auto_); }
        return;
    case Qt::Key_M:   // music on/off (same key and toast as the web)
        if (!e->isAutoRepeat()) toggleMusic();
        return;
    default:
        break;
    }
    // Keys follow the shop's visible order (only unlocked plants): 1-9, 0, -.
    const int n = e->key() == Qt::Key_0 ? 9 : e->key() == Qt::Key_Minus ? 10 : e->key() - Qt::Key_1;
    if (!e->isAutoRepeat() && n >= 0 && n <= 10) {
        if (n < int(shopOrder_.size())) toggleShop(shopOrder_[n]);
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
    speed_ = std::clamp(s, 1, 3);
    speedBtn_->setText(QString("%1×").arg(speed_));
    setProp(speedBtn_, "on", speed_ > 1);
}

void MainWindow::syncSoundButton() {
    const bool on = audio_.enabled();
    soundBtn_->setText(on ? QStringLiteral("🔊") : QStringLiteral("🔇"));
    soundBtn_->setToolTip(on ? "Sound on" : "Sound off");
    if (musicBtn_) setProp(musicBtn_, "on", music_.enabled());
}

// ---------- overlays ----------

void MainWindow::clearView() {
    View &view = board_->view;
    view.placing = -1;
    view.selectedId = -1;
    view.aimingId = -1;
    view.hover.reset();
}

static QString starsText(int n) {
    return QString(n, QChar(0x2605)) + QString(3 - n, QChar(0x2606));   // ★★☆
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
    QStringList starLines;
    int total = 0;
    for (size_t i = 0; i < runStars_.size(); i++) {
        starLines << QString("%1 %2").arg(data::MAPS[i].name, starsText(runStars_[i]));
        total += runStars_[i];
    }
    if (!starLines.isEmpty())
        endText_->setText(endText_->text() + QString("\n\n%1\n%2 of %3 stars")
                                                 .arg(starLines.join("\n")).arg(total).arg(3 * game_.totalMaps));
    endText_->setText(endText_->text() +
                      QString("\n\n🐜 %1 bugs squashed   🌱 %2 plants planted\n🎯 %3 shots fired   %4 %5 coins earned")
                          .arg(runKills_).arg(runPlanted_).arg(runShots_).arg(COIN).arg(runCoins_));
    endText_->setText(endText_->text() + QString("\nDifficulty: %1").arg(difficultyLabel()));
    end_->show();
    end_->raise();
    end_->findChild<QPushButton *>()->setFocus();
}

static int starsFor(int livesLost) {
    return livesLost <= 0 ? 3 : livesLost <= 3 ? 2 : 1;
}

void MainWindow::awardStars() {
    const int stars = starsFor(mapStartLives_ - game_.lives);
    runStars_.push_back(stars);
    QSettings settings;
    const QString key = QString("stars/%1").arg(game_.mapIndex);
    if (stars > settings.value(key, 0).toInt()) settings.setValue(key, stars);
}

void MainWindow::showMapCleared() {
    clearView();
    if (game_.isLastMap()) { showEnd(true); return; }   // no next map: the campaign is won
    const data::MapDef &next = data::MAPS[game_.mapIndex + 1];
    const int stars = starsFor(mapStartLives_ - game_.lives);
    nextTitle_->setText(QString("%1 cleared!\n%2").arg(game_.map().name, starsText(stars)));
    nextText_->setText(QString("%1\nYou kept %2. Next up: %3. %4\n\n"
                               "Your coins and plants reset for the new garden, but your lives carry over.")
                           .arg(stars == 3 ? QStringLiteral("Flawless: not a single bug got through!")
                                           : QString("%1 lost here (lose none for ★★★).")
                                                 .arg(livesText(mapStartLives_ - game_.lives)))
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
    if (!auto_) setSpeed(1);   // the autopilot keeps its speed
    next_->hide();
    primed_ = false;   // new coins total shouldn't "bump"
    centralWidget()->setFocus();
    showNewBugs();
}

// ---------- map select ----------

// A fresh run (full lives, run stats cleared) starting at `map`. Plant unlocks follow
// mapIndex inside Game, so starting at map N unlocks everything up to N.
void MainWindow::startRunAt(int map) {
    sfx("click");
    runKills_ = runPlanted_ = runShots_ = runCoins_ = 0;
    runStars_.clear();
    applyDifficulty();   // the chosen difficulty applies to the new run
    game_.reset(map);
    clearView();
    setUserPaused(false);
    if (!auto_) setSpeed(1);
    primed_ = false;
    intro_->hide();
    if (mapSelect_) mapSelect_->hide();
    centralWidget()->setFocus();
    showNewBugs();
}

void MainWindow::buildMapSelect() {
    QLabel *title, *text;
    QPushButton *back;
    mapSelect_ = buildOverlay(title, text, back);
    title->setText("Choose a garden");
    text->setText("Start a fresh run with full lives at any garden you've reached.");
    back->setText("Back");
    back->setDefault(false);
    connect(back, &QPushButton::clicked, this, [this] {
        sfx("click");
        mapSelect_->hide();
        intro_->show();
        intro_->raise();
    });
    mapList_ = new QWidget;
    mapList_->setFixedWidth(396);   // card width minus padding (see buildOverlay)
    if (auto *card = mapSelect_->findChild<QFrame *>("card"))
        static_cast<QVBoxLayout *>(card->layout())->insertWidget(2, mapList_);   // above Back
    mapSelect_->hide();
}

void MainWindow::showMapSelect(int furthest) {
    // Rebuild the rows so stars are current.
    delete mapList_->layout();
    for (QWidget *w : mapList_->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) delete w;
    auto *grid = new QGridLayout(mapList_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(0, 1);
    QSettings settings;
    furthest = std::clamp(furthest, 0, int(data::MAPS.size()) - 1);
    static const char *const SEASON_ICON[] = {"🌸", "☀️", "🍂", "❄️"};
    QPushButton *first = nullptr;
    for (int i = 0; i <= furthest; i++) {
        const data::MapDef &m = data::MAPS[i];
        const int stars = std::clamp(settings.value(QString("stars/%1").arg(i), 0).toInt(), 0, 3);
        auto *name = new QLabel(QString("%1. %2 %3").arg(i + 1).arg(m.name, SEASON_ICON[int(m.season)]));
        name->setObjectName("mapRowName");
        name->setStyleSheet("font-size: 14px; font-weight: 600;");
        auto *starLabel = new QLabel(starsText(stars));
        starLabel->setStyleSheet(QString("font-size: 15px; color: %1;").arg(stars > 0 ? "#f2b705" : "#9a9a9a"));
        auto *play = button("Play");
        play->setMinimumWidth(70);
        play->setFocusPolicy(Qt::StrongFocus);
        connect(play, &QPushButton::clicked, this, [this, i] { startRunAt(i); });
        grid->addWidget(name, i, 0);
        grid->addWidget(starLabel, i, 1);
        grid->addWidget(play, i, 2);
        if (!first) first = play;
    }
    intro_->hide();
    mapSelect_->setGeometry(centralWidget()->rect());
    mapSelect_->show();
    mapSelect_->raise();
    if (first) first->setFocus();
}

// ---------- screenshot scenes ----------

bool MainWindow::setupScene(const QString &scene_) {
    if (scene_ == "intro") return true;
    // Trailing digits pick the map: "bugs3" = the bugs scene on map 3, "play10" = map 10.
    QString scene = scene_;
    int targetMap = 0;
    int digits = 0;
    while (digits < scene.size() && scene[scene.size() - 1 - digits].isDigit()) digits++;
    if (digits > 0) {
        targetMap = std::clamp(scene.right(digits).toInt() - 1, 0, game_.totalMaps - 1);
        scene.chop(digits);
    }
    if (scene == "mapselect") {   // the map select card, listing every map
        intro_->hide();
        showMapSelect(game_.totalMaps - 1);
        return true;
    }
    if (scene == "journal") {   // the 📖 journal as of the target map ("journal4")
        intro_->hide();
        {
            QSignalBlocker quiet(&game_);
            while (game_.mapIndex < targetMap) {
                game_.phase = Phase::MapCleared;
                game_.nextMap();
            }
        }
        syncUi();
        toggleJournal();
        return true;
    }
    const QStringList known = {"play", "placing", "mapclear", "win", "lose", "night", "celebrate", "bugs",
                               "aim", "newbugs", "focus", "vine"};
    if (!known.contains(scene)) return false;
    if (scene == "vine" && targetMap == 0) targetMap = 1;   // grape vines and butterflies start on map 2
    intro_->hide();

    // A busy mid-game board: one of every plant, bugs on the path, a tower selected.
    // Signals are blocked so setting it up makes no sounds or toasts.
    QSignalBlocker quiet(&game_);
    if (scene == "night")
        for (int i = 0; i < int(data::MAPS.size()); i++)
            if (data::MAPS[i].night) targetMap = i;
    if (scene == "newbugs" && targetMap == 0) targetMap = 1;
    while (game_.mapIndex < targetMap) {
        game_.phase = Phase::MapCleared;
        game_.nextMap();
    }
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
    const int frost = plant("frost", 3, 3, 1);
    plant("mortar", 4, 4, 0);
    // Locked plants are skipped (placeTower refuses them), so early maps show fewer.
    int cactus = plant("cactus", 8, 4, 2);
    if (cactus < 0) cactus = frost;   // aim scene before the cactus unlocks: the fern aims instead
    if (scene == "aim") game_.setFirePoint(selected, 2 * 64 + 32, 4 * 64 + 32);   // sprout ambush
    plant("mushroom", 3, 5, 1);
    plant("chili", 5, 5, 2);
    plant("sunflower", 0, 8, 1);
    int grape = -1;
    if (scene == "vine" && !game_.map().skyLanes.empty()) {
        // A grape vine on the free tile nearest the middle of the first sky lane.
        const auto &lane = game_.map().skyLanes.front();
        const auto &a = lane[std::min<size_t>(1, lane.size() - 1)], &b = lane[std::min<size_t>(2, lane.size() - 1)];
        const double lx = (a[0] + b[0]) / 2, ly = (a[1] + b[1]) / 2;
        std::vector<std::pair<double, std::pair<int, int>>> tiles;
        for (int r = 0; r < game_.rows; r++)
            for (int c = 0; c < game_.cols; c++) tiles.push_back({std::hypot(c - lx, r - ly), {c, r}});
        std::sort(tiles.begin(), tiles.end());
        for (const auto &tile : tiles)
            if ((grape = plant("grape", tile.second.first, tile.second.second, 2)) >= 0) break;
    }
    game_.coins = 1234;
    // "bugs" stages the final wave, where every new bug type shows up.
    game_.wave = scene == "bugs" ? game_.totalWaves - 1 : std::min(3, game_.totalWaves - 1);    game_.startWave();
    for (int i = 0; i < 60 * (scene == "bugs" ? 24 : 9); i++) game_.update(1.0 / 60);
    if (scene == "vine") {
        // Keep playing until the vine has a flyer lassoed, so the tether shows.
        auto lassoed = [this] {
            for (const Enemy &e : game_.enemies)
                if (e.alive && e.grounded(game_.time) && e.vineBy >= 0) return true;
            return false;
        };
        game_.lives = 999;   // staging only: leaks must not end the run before a lasso
        for (int i = 0; i < 60 * 60 && !lassoed(); i++) {
            if (game_.phase == Phase::Build) game_.startWave();
            else if (game_.phase != Phase::Wave) break;
            game_.update(1.0 / 60);
        }
        for (int i = 0; i < 20 && lassoed(); i++) game_.update(1.0 / 60);   // a moment into the pull
        board_->view.selectedId = grape;
        game_.lives = data::START_LIVES;   // the lasso lasts 3.5 s, longer than the screenshot delay
    }

    if (scene == "celebrate") {
        // Fire the confetti shortly before the screenshot (taken at 800ms) so it's mid-air.
        QTimer::singleShot(300, this, [this] { board_->celebrate(); });
    } else if (scene == "focus") {
        // Double-click selection of every sprout, focus-firing the bug furthest along.
        // Select the most common attacking plant type (the staged sprout may not exist on every map).
        std::map<int, int> count;
        for (const Tower &o : game_.towers) {
            auto k = data::TOWERS[o.type].projectile;
            if (k != data::Projectile::None && k != data::Projectile::Aura) count[o.type]++;
        }
        int type = -1, best = 0;
        for (auto [ty, n] : count) if (n > best) { best = n; type = ty; }
        for (const Tower &o : game_.towers)
            if (o.type == type) {
                board_->view.group.push_back(o.id);
                board_->view.selectedId = o.id;
            }
        // Target the bug closest to the selected plant (it will stay on screen for the shot).
        const Tower *sel = game_.getTower(board_->view.selectedId);
        const Enemy *lead = nullptr;
        for (const Enemy &e : game_.enemies)
            if (sel && (!lead || std::hypot(e.x - sel->x, e.y - sel->y) < std::hypot(lead->x - sel->x, lead->y - sel->y)))
                lead = &e;
        setUserPaused(true);   // freeze the moment for the screenshot
        if (lead) game_.focusFire(board_->view.group, lead->id);
    } else if (scene == "aim") {
        // The cactus is choosing a fire point; the sprout already has one.
        board_->view.selectedId = cactus;
        board_->view.aimingId = cactus;
        board_->view.hover = View::Hover{10, 5, true};
    } else if (scene == "vine") {
        // Staged above: the grape vine is selected and the lasso frozen mid-pull.
    } else if (scene == "play" || scene == "night" || scene == "bugs") {
        board_->view.selectedId = selected;
        board_->showToast(QString("Wave %1 cleared! +40 %2  ·  Sunflowers +28 %2").arg(game_.wave - 1).arg(COIN));
    } else if (scene == "placing") {
        board_->view.placing = game_.isUnlocked(4) ? 4 : 1;   // mushroom, or frost before it unlocks
        onHover(7, 4);
    } else if (scene == "mapclear") {
        showMapCleared();
    } else if (scene == "newbugs") {
        showNewBugs();
    } else {
        showEnd(scene == "win");
    }
    syncUi();
    return true;
}

void MainWindow::playAgain() {
    runKills_ = runPlanted_ = runShots_ = runCoins_ = 0;
    runStars_.clear();
    applyDifficulty();   // the difficulty persists anyway; re-applied so it matches the saved choice
    game_.reset();
    clearView();
    setUserPaused(false);
    setSpeed(1);
    end_->hide();
    primed_ = false;   // resync the HUD without bump animations
    sfx("click");
    centralWidget()->setFocus();
}
