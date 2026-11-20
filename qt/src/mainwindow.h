// Garden Guard: the main window. Owns the game, sound and board, builds the
// HUD/shop/tower panel, handles input, and runs the game loop.
#pragma once

#include "audio.h"
#include "autopilot.h"
#include "game.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <vector>

class BoardWidget;
class QFormLayout;
class QFrame;
class QLabel;
class QPushButton;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Hidden screenshot mode (see main.cpp): sets up a scene to capture.
    // Scenes: intro, play, placing, mapclear, win, lose. Returns false if unknown.
    bool setupScene(const QString &scene);
    void startAutopilot(int speed) { setAuto(true); setSpeed(speed); }   // hidden --autopilot option

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QWidget *buildHeader();
    QWidget *buildSide();
    QWidget *buildOverlay(QLabel *&title, QLabel *&text, QPushButton *&button);
    QPushButton *makeShopItem(int type, int index);

    void tick();
    void syncUi();
    bool paused() const;
    bool overlayOpen() const;

    void toggleShop(int type);
    void onHover(int col, int row);
    void onClick(int col, int row);
    bool isValid(int col, int row) const;
    // Focus fire and multi-select (focusfire.cpp).
    void onDoubleClick(int col, int row);
    bool tryFocusClick();                   // a bug was clicked with plants selected
    void focusOn(int enemyId);
    bool cancelFocusOnSelection();          // Esc: true if a focus was cancelled
    bool upgradeAllInGroup();               // true if group mode handled the click
    void syncFocusPanel(const Tower &t, bool over);
    std::vector<int> selection() const;     // the group, or just the selected plant
    QPointF lastClick_;                     // logical board px of the last click
    void fail(const QString &msg);
    void sfx(const QString &name) { audio_.play(name); }

    void setUserPaused(bool on);
    void setSpeed(int s);
    void syncSoundButton();
    void showEnd(bool won);
    void showMapCleared();
    void goToNextMap();
    void clearView();
    void playAgain();
    void fillPanel(const Tower &t);
    void setStat(QLabel *label, const QString &text);
    void setAuto(bool on);
    void runAutopilot();

    Game game_;
    Audio audio_;
    Autopilot autopilot_{game_};   // the 🤖 AI player (declared after game_, which it uses)
    BoardWidget *board_ = nullptr;

    // Header
    QLabel *map_ = nullptr, *coins_ = nullptr, *lives_ = nullptr, *wave_ = nullptr;
    QPushButton *autoBtn_ = nullptr, *speedBtn_ = nullptr, *pauseBtn_ = nullptr, *soundBtn_ = nullptr;

    // Autopilot state
    bool auto_ = false;
    double lastThink_ = -1;      // game time of the AI's last decision
    double buildStart_ = -1;     // game time the current build phase began
    QElapsedTimer overlayClock_; // how long the map-cleared card has been showing
    bool overlayTiming_ = false;

    // Side panel
    // Shop (claude2-0c): only unlocked plants are shown; shopItems_[i] shows type shopOrder_[i]
    // and has key i (1-9, 0, -). Rebuilt by syncUi whenever the map (and so the unlocks) changes.
    std::vector<QPushButton *> shopItems_;
    std::vector<int> shopOrder_;
    class QGridLayout *shopGrid_ = nullptr;
    int shopMap_ = -1;
    bool shopUnlockAll_ = false;
    void rebuildShop();
    QFrame *panel_ = nullptr;
    QLabel *tpIcon_ = nullptr, *tpName_ = nullptr, *tpLevel_ = nullptr;
    QFormLayout *tpStats_ = nullptr;
    QPushButton *tpUpgrade_ = nullptr, *tpSell_ = nullptr;
    QPushButton *tpAim_ = nullptr;   // set/clear the selected plant's fire point
    QPushButton *startWave_ = nullptr;
    QLabel *nextWave_ = nullptr;   // preview of the next wave's bugs

    // Run stats for the end card (whole campaign run).
    int runKills_ = 0, runPlanted_ = 0, runShots_ = 0, runCoins_ = 0;
    // Star ratings: ★★★ = no lives lost on that map, ★★ = 3 or fewer, ★ = cleared.
    int mapStartLives_ = data::START_LIVES;
    std::vector<int> runStars_;    // stars for each map cleared this run
    void awardStars();             // called when a map is cleared (or the campaign won)
    QString panelKey_;   // which tower/level the panel currently shows

    // Overlays
    QWidget *intro_ = nullptr, *end_ = nullptr, *next_ = nullptr, *bugCard_ = nullptr;
    QLabel *bugCardTitle_ = nullptr, *bugCardText_ = nullptr;
    QPushButton *bugCardBtn_ = nullptr;
    void showNewBugs();   // "New bugs!" card when a map introduces bug types
    // Map select (from the intro card): one row per reached map, each with its stars and Play.
    QWidget *mapSelect_ = nullptr;
    QWidget *mapList_ = nullptr;   // rows inside the map select card (rebuilt on open)
    void buildMapSelect();
    void showMapSelect(int furthest);   // list maps 0..furthest
    void startRunAt(int map);           // fresh run (full lives) starting at `map`
    QLabel *endTitle_ = nullptr, *endText_ = nullptr;
    QLabel *nextTitle_ = nullptr, *nextText_ = nullptr;
    QPushButton *nextBtn_ = nullptr;

    // Loop
    QTimer frameTimer_;
    QElapsedTimer clock_;
    bool userPaused_ = false;
    int speed_ = 1;
    bool primed_ = false;   // skip "bump" animations on the first sync
};
