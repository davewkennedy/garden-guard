// Garden Guard++: the 📖 Garden Journal (MainWindow methods, kept in their own file so
// they don't collide with other edits to mainwindow.cpp).
//
// An encyclopedia overlay: every bug met so far (bugs in this map's and earlier maps'
// waves) and every plant unlocked so far. Things not met yet are teased as "???" rows.
// Opened with the header 📖 button or J; the game pauses while it's open (overlayOpen).
#include "data.h"
#include "mainwindow.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

constexpr int JOURNAL_W = 860;   // the card is wider than the standard 460 px: two columns
constexpr int COL_W = 380;

// One journal entry: a big icon, then a bold name and smaller detail lines.
QWidget *entry(const QString &icon, const QString &name, const QString &detail, bool known) {
    auto *row = new QWidget;
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 2, 0, 2);
    h->setSpacing(10);
    auto *ic = new QLabel(known ? icon : QStringLiteral("❔"));
    ic->setFixedWidth(34);
    ic->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    ic->setStyleSheet(QString("font-size: 24px;%1").arg(known ? "" : " color: #9a9a9a;"));
    auto *text = new QLabel;
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    text->setFixedWidth(COL_W - 34 - 10);
    text->setStyleSheet("font-size: 13px;");
    text->setText(known ? QString("<b style='font-size:14px'>%1</b><br>%2").arg(name.toHtmlEscaped(), detail)
                        : QStringLiteral("<b style='color:#8a8a8a'>?\?\?</b><br>"   // \? avoids the ??< trigraph
                                         "<span style='color:#9a9a9a'>Unlocks on a later map.</span>"));
    h->addWidget(ic);
    h->addWidget(text, 1);
    return row;
}

QLabel *heading(const QString &text) {
    auto *l = new QLabel(text);
    l->setStyleSheet("font-size: 17px; font-weight: 700; color: #2e7d32; padding-bottom: 4px;");
    return l;
}

QString speedWord(double s) {
    return s < 0.6 ? "very slow" : s < 1.0 ? "slow" : s < 1.4 ? "medium" : s < 2.0 ? "fast" : "very fast";
}

}  // namespace

void MainWindow::buildJournal() {
    QLabel *title, *text;
    QPushButton *close;
    journal_ = buildOverlay(title, text, close);
    title->setText(QStringLiteral("📖 Garden Journal"));
    text->setText("Every bug you've met and every plant you've grown. New pages fill in as you travel.");
    close->setText("Close (J)");
    connect(close, &QPushButton::clicked, this, [this] { toggleJournal(); });
    if (auto *card = journal_->findChild<QFrame *>("card")) {
        card->setFixedWidth(JOURNAL_W);
        title->setFixedWidth(JOURNAL_W - 64);
        text->setFixedWidth(JOURNAL_W - 64);
        auto *scroll = new QScrollArea;
        scroll->setObjectName("journalScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFixedWidth(JOURNAL_W - 64);
        scroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
        static_cast<QVBoxLayout *>(card->layout())->insertWidget(2, scroll);   // above Close
        journalScroll_ = scroll;
    }
    journal_->hide();
}

void MainWindow::fillJournal() {
    // Bugs met: anything in the waves of this map or an earlier one (grubs come with caterpillars).
    std::vector<bool> met(data::ENEMIES.size(), false);
    for (int m = 0; m <= game_.mapIndex && m < int(data::MAPS.size()); m++) {
        const auto &ws = data::MAPS[m].waves.empty() ? data::WAVES : data::MAPS[m].waves;
        for (const auto &w : ws)
            for (const auto &grp : w.groups) met[grp.enemy] = true;
    }
    for (size_t k = 0; k < data::ENEMIES.size(); k++)
        if (met[k] && data::ENEMIES[k].splitInto >= 0) met[data::ENEMIES[k].splitInto] = true;

    auto *page = new QWidget;
    auto *cols = new QHBoxLayout(page);
    cols->setContentsMargins(0, 0, 8, 0);
    cols->setSpacing(24);

    auto *bugs = new QVBoxLayout;
    bugs->setSpacing(6);
    int nMet = 0;
    for (bool b : met) nMet += b;
    bugs->addWidget(heading(QString("🐞 Bugs met (%1 of %2)").arg(nMet).arg(data::ENEMIES.size())));
    // Known entries first, then the "???" teasers.
    for (int pass = 0; pass < 2; pass++)
    for (size_t k = 0; k < data::ENEMIES.size(); k++) {
        if (met[k] != (pass == 0)) continue;
        const data::EnemyDef &d = data::ENEMIES[k];
        QStringList tags;
        if (d.boss) tags << "<b style='color:#c62828'>boss</b>";
        if (d.wild) tags << "<b style='color:#7b3fa0'>wildcard flyer</b>";
        else if (d.flying) tags << "<b style='color:#1565c0'>flying</b>";
        if (d.armor > 0) tags << QString("armor %1").arg(d.armor);
        QString stats = QString("HP %1 · %2").arg(d.hp).arg(speedWord(d.speed));
        if (!tags.isEmpty()) stats += " · " + tags.join(" · ");
        stats += QString(" · %1 💰").arg(d.reward);
        const QString detail = d.desc.toHtmlEscaped() + "<br><span style='color:#6b6b6b'>" + stats + "</span>";
        const QString name = d.golden ? QStringLiteral("✨ ") + d.name : d.name;
        bugs->addWidget(entry(d.icon, name, detail, met[k]));
    }
    bugs->addStretch(1);

    auto *plants = new QVBoxLayout;
    plants->setSpacing(6);
    int nUnlocked = 0;
    for (int i = 0; i < int(data::TOWERS.size()); i++) nUnlocked += game_.isUnlocked(i);
    plants->addWidget(heading(QString("🌱 Plants unlocked (%1 of %2)").arg(nUnlocked).arg(data::TOWERS.size())));
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < int(data::TOWERS.size()); i++) {
        if (game_.isUnlocked(i) != (pass == 0)) continue;
        const data::TowerDef &d = data::TOWERS[i];
        QString detail = d.desc.toHtmlEscaped();
        if (!d.counters.isEmpty()) detail += "<br><span style='color:#2e7d32'>Tip: " + d.counters.toHtmlEscaped() + "</span>";
        detail += QString("<br><span style='color:#6b6b6b'>Cost %1 💰").arg(d.levels[0].cost);
        if (d.levels[0].range > 0) detail += QString(" · range %1 tiles").arg(d.levels[0].range);
        detail += "</span>";
        plants->addWidget(entry(d.icon, d.name, detail, game_.isUnlocked(i)));
    }
    plants->addStretch(1);

    cols->addLayout(bugs, 1);
    cols->addLayout(plants, 1);
    journalScroll_->setWidget(page);   // deletes the previous page
}

void MainWindow::toggleJournal() {
    if (!journal_) return;
    sfx("click");
    if (journal_->isVisible()) {
        journal_->hide();
        centralWidget()->setFocus();
        return;
    }
    if (overlayOpen()) return;   // don't stack on top of other cards
    fillJournal();
    // Fit the scroll area to the window, leaving room for the title, text and Close button.
    journalScroll_->setFixedHeight(std::max(220, centralWidget()->height() - 300));
    journal_->setGeometry(centralWidget()->rect());
    journal_->show();
    journal_->raise();
    if (auto *btn = journal_->findChild<QPushButton *>()) btn->setFocus();
}
