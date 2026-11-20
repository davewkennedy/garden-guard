#include "autopilot.h"

#include "data.h"
#include "game.h"

#include <QString>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int typeOf(const char *id) {
    for (int i = 0; i < int(data::TOWERS.size()); i++)
        if (data::TOWERS[i].id == id) return i;
    std::fprintf(stderr, "autopilot: unknown plant %s\n", id);
    std::abort();
}

// Plants that hit bugs (as opposed to the sunflower and clover).
bool attacks(int type) {
    auto p = data::TOWERS[type].projectile;
    return p != data::Projectile::None && p != data::Projectile::Aura;
}

constexpr int EARLY_PLANTS = 9;   // build this many before upgrades get priority

}  // namespace

Autopilot::Autopilot(Game &game) : g_(game) {
    // A balanced build: cheap damage first, an early sunflower for income, then
    // slows, splash, poison (good against armor) and the specialists.
    const char *env = std::getenv("AUTOPILOT_PLAN");   // for tuning: comma-separated ids
    if (env) {
        for (const QString &id : QString(env).split(','))
            plan_.push_back(typeOf(qPrintable(id.trimmed())));
        return;
    }
    // Tuned with garden_simtest: frost + splash early is far stronger than spreading out.
    for (const char *id : {"sprout", "sprout", "frost", "mortar", "mushroom", "mortar", "chili",
                           "mushroom", "cactus", "rose", "clover", "mortar", "chili", "cactus",
                           "rose", "clover", "corn", "corn", "frost", "sprout"})
        plan_.push_back(typeOf(id));
    // Maps with flyers: lead with plants that can hit the air (mortars and chilis can't).
    for (const char *id : {"sprout", "corn", "sprout", "frost", "corn", "mushroom", "mortar",
                           "rose", "cactus", "corn", "clover", "chili", "mortar", "rose",
                           "cactus", "mushroom", "clover", "frost", "chili", "sprout"})
        airPlan_.push_back(typeOf(id));
}

// How good a tile is for a plant of this type (higher is better; < 0 = don't).
double Autopilot::tileScore(int type, int col, int row) const {
    const data::TowerDef &def = data::TOWERS[type];
    if (def.projectile == data::Projectile::None) {
        // Sunflower: anywhere off the path, preferring spots no attacker would want.
        int near = 0;
        for (int r = row - 2; r <= row + 2; r++)
            for (int c = col - 2; c <= col + 2; c++) near += g_.isPathTile(c, r);
        return 100 - near;
    }
    const double range = def.levels[0].range;
    const double r2 = range * range;
    if (def.projectile == data::Projectile::Aura) {
        // Clover: as many attacking plants inside its aura as possible.
        int n = 0;
        for (const Tower &t : g_.towers) {
            double dc = t.col - col, dr = t.row - row;
            if (attacks(t.type) && dc * dc + dr * dr <= r2) n++;
        }
        return n > 0 ? n : -1;
    }
    // Attackers: how much path they cover, with a bonus for path near the
    // entrance (bugs spend longer in range of plants they meet early).
    double score = 0;
    for (int r = 0; r < data::ROWS; r++)
        for (int c = 0; c < data::COLS; c++) {
            if (!g_.isPathTile(c, r)) continue;
            double dc = c - col, dr = r - row;
            if (dc * dc + dr * dr <= r2) score += 1.0 + 0.15 * (data::COLS - c) / data::COLS;
        }
    // Plants that can hit flyers also value covering the sky lanes (sampled every
    // half tile). Ground attacks (mortar splash, chili flame) can't, so they skip this.
    const bool ground = def.projectile == data::Projectile::Mortar || def.projectile == data::Projectile::Flame;
    if (!ground) {
        for (const auto &lane : g_.map().skyLanes)
            for (size_t i = 0; i + 1 < lane.size(); i++) {
                double ax = lane[i][0], ay = lane[i][1], bx = lane[i + 1][0], by = lane[i + 1][1];
                int steps = std::max(1, int(std::hypot(bx - ax, by - ay) * 2));
                for (int s = 0; s < steps; s++) {
                    double x = ax + (bx - ax) * s / steps, y = ay + (by - ay) * s / steps;
                    double dc = x - col, dr = y - row;
                    if (dc * dc + dr * dr <= r2) score += 0.5;
                }
            }
    }
    return score > 0 ? score : -1;
}

bool Autopilot::placeNext() {
    const auto &plan = g_.map().skyLanes.empty() || std::getenv("AUTOPILOT_PLAN") ? plan_ : airPlan_;
    const int type = plan[g_.towers.size() % plan.size()];
    if (g_.coins < data::TOWERS[type].levels[0].cost) return false;
    int bestCol = -1, bestRow = -1;
    double best = -1;
    for (int r = 0; r < data::ROWS; r++)
        for (int c = 0; c < data::COLS; c++) {
            if (!g_.canPlace(c, r)) continue;
            double s = tileScore(type, c, r);
            if (s > best) { best = s; bestCol = c; bestRow = r; }
        }
    if (best < 0) return false;
    return g_.placeTower(type, bestCol, bestRow) != nullptr;
}

bool Autopilot::upgradeOne() {
    // Cheapest affordable upgrade, preferring attackers (they win waves).
    const Tower *pick = nullptr;
    int pickCost = 0;
    for (const Tower &t : g_.towers) {
        int cost = g_.upgradeCost(t);
        if (cost < 0 || cost > g_.coins) continue;
        int weighted = attacks(t.type) ? cost : cost * 2;
        if (!pick || weighted < pickCost) { pick = &t; pickCost = weighted; }
    }
    return pick && g_.upgradeTower(pick->id);
}

bool Autopilot::think() {
    if (g_.isOver()) return false;
    // Early on, prefer new plants but never sit on coins; later, prefer upgrades.
    const char *mode = std::getenv("AUTOPILOT_MODE");   // for tuning: "save" or "spend"
    const bool save = mode && QString(mode) == "save";
    // Keep buying until nothing is affordable, so coins never pile up between
    // thinks (it only gets ~2 thinks between waves). The cap is just a safety net.
    // Early on it saves for the next planned plant, but only up to a point: above
    // HOARD coins, or going into the map's last wave, it upgrades with the surplus.
    constexpr int HOARD = 150;
    bool acted = false;
    for (int i = 0; i < 32; i++) {
        const bool lastWave = g_.wave >= g_.totalWaves - 1;
        const bool surplus = !save && (g_.coins > HOARD || lastWave);
        bool did = int(g_.towers.size()) < EARLY_PLANTS ? placeNext() || (surplus && upgradeOne())
                                                        : upgradeOne() || placeNext();
        if (!did) break;
        acted = true;
    }
    return acted;
}
