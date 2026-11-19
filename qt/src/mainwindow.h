// Garden Guard: the main window. Owns the game, sound and board, builds the
// HUD/shop/tower panel, handles input, and runs the game loop.
#pragma once

#include "audio.h"
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

    Game game_;
    Audio audio_;
    BoardWidget *board_ = nullptr;

    // Header
    QLabel *map_ = nullptr, *coins_ = nullptr, *lives_ = nullptr, *wave_ = nullptr;
    QPushButton *speedBtn_ = nullptr, *pauseBtn_ = nullptr, *soundBtn_ = nullptr;

    // Side panel
    std::vector<QPushButton *> shopItems_;
    QFrame *panel_ = nullptr;
    QLabel *tpIcon_ = nullptr, *tpName_ = nullptr, *tpLevel_ = nullptr;
    QFormLayout *tpStats_ = nullptr;
    QPushButton *tpUpgrade_ = nullptr, *tpSell_ = nullptr;
    QPushButton *startWave_ = nullptr;
    QString panelKey_;   // which tower/level the panel currently shows

    // Overlays
    QWidget *intro_ = nullptr, *end_ = nullptr, *next_ = nullptr;
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
