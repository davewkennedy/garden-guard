// Headless simulation test: plays the campaign with automatically placed plants
// and checks the results. No window, no sound: just the Game class.
//   ./build/debug/garden_simtest
#include "../src/game.h"

#include <QCoreApplication>
#include <cstdio>
#include <map>
#include <vector>

struct Stats {
    std::map<int, int> shots;   // tower type -> shots fired
    int kills = 0, leaks = 0, harvest = 0;
};

static int typeOf(const char *id) {
    for (int i = 0; i < int(data::TOWERS.size()); i++)
        if (data::TOWERS[i].id == id) return i;
    std::fprintf(stderr, "unknown tower %s\n", id);
    std::exit(2);
}

static bool check(bool cond, const char *what) {
    std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what);
    return cond;
}

// Plants each type on the free grass tile that sees the most path tiles, fully upgraded.
static void autoPlant(Game &g, const std::vector<int> &types) {
    for (int type : types) {
        int bestCol = -1, bestRow = -1, bestScore = -1;
        for (int r = 0; r < data::ROWS; r++)
            for (int c = 0; c < data::COLS; c++) {
                if (!g.canPlace(c, r)) continue;
                int score = 0;
                for (int pr = 0; pr < data::ROWS; pr++)
                    for (int pc = 0; pc < data::COLS; pc++)
                        if (g.isPathTile(pc, pr) && (pc - c) * (pc - c) + (pr - r) * (pr - r) <= 5) score++;
                if (score > bestScore) { bestScore = score; bestCol = c; bestRow = r; }
            }
        Tower *t = g.placeTower(type, bestCol, bestRow);
        if (!t) { std::fprintf(stderr, "could not place type %d\n", type); std::exit(2); }
        int id = t->id;
        while (g.upgradeTower(id)) {}
    }
}

// Plays waves until the map is cleared or lost.
static void playMap(Game &g) {
    while (g.phase == Phase::Build && g.startWave())
        for (int i = 0; i < 60 * 600 && g.phase == Phase::Wave; i++) g.update(1.0 / 60);
}

static void watch(Game &g, Stats &s) {
    QObject::connect(&g, &Game::shot, [&s](int type) { s.shots[type]++; });
    QObject::connect(&g, &Game::killed, [&s] { s.kills++; });
    QObject::connect(&g, &Game::leaked, [&s] { s.leaks++; });
    QObject::connect(&g, &Game::waveEnded, [&s](int, int, int h) { s.harvest += h; });
}

static void printShots(const Stats &s) {
    for (auto [type, n] : s.shots)
        std::printf("      %-15s %5d shots\n", qPrintable(data::TOWERS[type].name), n);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    const int sprout = typeOf("sprout"), frost = typeOf("frost"), mortar = typeOf("mortar");
    const int cactus = typeOf("cactus"), mushroom = typeOf("mushroom"), chili = typeOf("chili");
    const int sunflower = typeOf("sunflower");
    bool ok = true;

    // 1) New plants only on map 1: do they fire, poison, burn and pay out?
    {
        std::printf("Test 1: new plants only (Sunny Meadow)\n");
        Game g;
        Stats s;
        watch(g, s);
        g.coins = 100000;
        autoPlant(g, {mushroom, mushroom, chili, chili, chili, chili, sunflower, sunflower});
        playMap(g);
        const int cleared = g.phase == Phase::Wave || g.phase == Phase::Lost ? g.wave - 1 : g.wave;
        std::printf("    reached wave %d, lives %d, kills %d, harvest %d\n", g.wave, g.lives, s.kills, s.harvest);
        printShots(s);
        ok &= check(s.shots[mushroom] > 0 && s.shots[chili] > 0, "mushrooms and chilis fire");
        ok &= check(s.shots.count(sunflower) == 0, "sunflowers never fire");
        ok &= check(s.harvest == 2 * 45 * cleared, "two max sunflowers pay 45 each per cleared wave");
    }

    // 2) Full campaign with a strong mixed defence on every map.
    {
        std::printf("Test 2: full campaign\n");
        Game g;
        Stats s;
        watch(g, s);
        int cleared = 0, started = 0;
        QObject::connect(&g, &Game::mapCleared, [&] { cleared++; });
        QObject::connect(&g, &Game::mapStarted, [&] { started++; });
        const std::vector<int> defence = {sprout, sprout, frost, mortar, cactus, cactus,
                                          mushroom, mushroom, chili, chili, sunflower};

        for (int m = 0; m < g.totalMaps; m++) {
            g.coins = 100000;
            autoPlant(g, defence);
            playMap(g);
            std::printf("    map %d %-14s -> %s, lives %d\n", m + 1, qPrintable(g.map().name),
                        g.phase == Phase::MapCleared ? "cleared" : g.phase == Phase::Won ? "WON" : "LOST",
                        g.lives);
            if (g.phase != Phase::MapCleared) break;
            const int livesBefore = g.lives;
            ok &= check(!g.startWave(), "can't start a wave between maps");
            ok &= check(g.nextMap(), "nextMap() moves on");
            ok &= check(g.mapIndex == m + 1 && g.wave == 0 && g.phase == Phase::Build, "new map starts at wave 0");
            ok &= check(g.towers.empty() && g.coins == g.map().startCoins, "plants and coins reset");
            ok &= check(g.lives == livesBefore, "lives carry over");
        }
        printShots(s);
        ok &= check(g.phase == Phase::Won, "campaign won");
        ok &= check(cleared == g.totalMaps - 1 && started == g.totalMaps - 1, "mapCleared/mapStarted signals");
        ok &= check(!g.nextMap(), "no map after the last one");

        g.reset();
        ok &= check(g.mapIndex == 0 && g.lives == data::START_LIVES && g.coins == data::MAPS[0].startCoins,
                    "reset() returns to map 1 with full lives");
    }

    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
