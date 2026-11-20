// Garden Guard+: a simple AI player. It only uses the public Game API, so it
// works both in the app (the 🤖 button) and headless in garden_simtest.
#pragma once

#include <vector>

class Game;

class Autopilot {
public:
    explicit Autopilot(Game &game);

    // Spends coins: plants the next plant in its build plan on the best tile,
    // or upgrades a plant. Call it a few times a second. Returns true if it bought something.
    bool think();

private:
    bool placeNext();
    bool upgradeOne();
    void focus();                // focus fire on high-value bugs (bosses, jackpots, healers)
    double tileScore(int type, int col, int row) const;

    Game &g_;
    std::vector<int> plan_;      // build order (tower type indices), repeats
    std::vector<int> airPlan_;   // build order on maps with flying bugs
};
