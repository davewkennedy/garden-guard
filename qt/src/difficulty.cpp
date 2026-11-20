// Garden Guard++: difficulty picker (MainWindow methods, kept in their own file so they
// don't collide with other edits to mainwindow.cpp).
//
// Three toggle buttons on the intro card: 🌱 Easy · 🌿 Normal · 🌳 Hard (Normal by default),
// saved in QSettings "difficulty" (0/1/2) and applied before every run starts: Let's grow,
// Continue, Choose map and Play again. Shown in the window title and on the end card.
#include "mainwindow.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
const char *const DIFF_NAMES[] = {"🌱 Easy", "🌿 Normal", "🌳 Hard"};
}

QString MainWindow::difficultyLabel() const {
    return QString::fromUtf8(DIFF_NAMES[std::clamp(difficulty_, 0, 2)]);
}

void MainWindow::buildDifficultyPicker() {
    difficulty_ = std::clamp(QSettings().value("difficulty", 1).toInt(), 0, 2);
    auto *card = intro_->findChild<QFrame *>("card");
    if (!card) return;
    auto *row = new QWidget;
    row->setFixedWidth(396);   // card width minus padding (see buildOverlay), or the card stretches
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto *group = new QButtonGroup(row);
    group->setExclusive(true);
    for (int i = 0; i < 3; i++) {
        auto *b = new QPushButton(QString::fromUtf8(DIFF_NAMES[i]));
        b->setCheckable(true);
        b->setChecked(i == difficulty_);
        b->setFocusPolicy(Qt::StrongFocus);
        b->setMinimumHeight(32);
        b->setToolTip(i == 0 ? "Relaxed: bugs -25% HP, start coins +25%"
                      : i == 1 ? "The garden as designed"
                               : "Seasoned gardeners: bugs +30% HP, start coins -10%");
        // Checked = filled green pill, so the choice is obvious.
        b->setStyleSheet("QPushButton { border: 1px solid #b7d8a8; border-radius: 14px; padding: 4px 10px;"
                         " background: #f4faef; font-weight: 600; }"
                         "QPushButton:checked { background: #4caf50; color: white; border-color: #3d8b40; }");
        group->addButton(b, i);
        h->addWidget(b, 1);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        sfx("click");
        difficulty_ = id;
        QSettings().setValue("difficulty", id);
        applyDifficulty();   // also updates the current (not yet started) run
    });
    static_cast<QVBoxLayout *>(card->layout())->insertWidget(2, row);   // under the intro text
    applyDifficulty();
}

// Applies the chosen difficulty to the game and shows it in the window title.
// Called before each run starts (startRunAt / playAgain) and when the choice changes.
void MainWindow::applyDifficulty() {
    game_.setDifficulty(static_cast<Difficulty>(std::clamp(difficulty_, 0, 2)));
    setWindowTitle(QString("Garden Guard++ · %1").arg(difficultyLabel()));
}
