// Headless simulation test: plays the campaign with automatically placed plants
// and checks the results. No window, no sound: just the Game class.
//   ./build/debug/garden_simtest
#include "../src/autopilot.h"
#include "../src/game.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
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

// Points every half tile along the map's sky lanes, in tile coordinates.
static std::vector<std::array<double, 2>> lanePoints(const Game &g) {
    std::vector<std::array<double, 2>> pts;
    for (const auto &lane : g.map().skyLanes)
        for (size_t i = 0; i + 1 < lane.size(); i++) {
            double dx = lane[i + 1][0] - lane[i][0], dy = lane[i + 1][1] - lane[i][1];
            int n = std::max(1, int(std::hypot(dx, dy) * 2));
            for (int k = 0; k < n; k++)
                pts.push_back({lane[i][0] + dx * k / n, lane[i][1] + dy * k / n});
        }
    return pts;
}

// Plants each type on the free grass tile that sees the most path tiles (and, for
// plants that can hit flyers, sky-lane points), fully upgraded.
static void autoPlant(Game &g, const std::vector<int> &types) {
    const auto lane = lanePoints(g);
    for (int type : types) {
        if (!g.isUnlocked(type)) continue;   // campaign tests respect plant unlocks
        const auto proj = data::TOWERS[type].projectile;
        const bool hitsAir = proj != data::Projectile::Mortar && proj != data::Projectile::Flame;
        int bestCol = -1, bestRow = -1, bestScore = -1;
        for (int r = 0; r < g.rows; r++)
            for (int c = 0; c < g.cols; c++) {
                if (!g.canPlace(c, r)) continue;
                int score = 0;
                for (int pr = 0; pr < g.rows; pr++)
                    for (int pc = 0; pc < g.cols; pc++)
                        if (g.isPathTile(pc, pr) && (pc - c) * (pc - c) + (pr - r) * (pr - r) <= 5) score++;
                if (hitsAir)
                    for (const auto &p : lane)
                        if ((p[0] - c) * (p[0] - c) + (p[1] - r) * (p[1] - r) <= 5) score++;
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
    const int rose = typeOf("rose"), corn = typeOf("corn"), clover = typeOf("clover");
    bool ok = true;

    // 0) Rose, corn and clover on map 1: thorns pierce, corn volleys, clover never fires.
    {
        std::printf("Test 0: rose, corn and clover (Sunny Meadow)\n");
        Game g;
        g.unlockAll = true;   // mechanics test: every plant on map 1
        Stats s;
        watch(g, s);
        g.coins = 100000;
        autoPlant(g, {rose, rose, corn, corn, clover});
        playMap(g);
        std::printf("    reached wave %d, lives %d, kills %d\n", g.wave, g.lives, s.kills);
        printShots(s);
        ok &= check(s.shots[rose] > 0 && s.shots[corn] > 0, "roses and corn fire");
        ok &= check(s.shots.count(clover) == 0, "clovers never fire");
        ok &= check(s.kills > 0, "new plants kill bugs");
    }

    // 0b) A clover makes a neighbouring sprout fire faster over the same wave.
    {
        auto sproutShots = [&](bool withClover) {
            Game g;
            g.unlockAll = true;
            Stats s;
            watch(g, s);
            g.coins = 100000;
            autoPlant(g, {sprout});
            if (withClover) {
                const Tower &sp = g.towers.front();
                for (int dc = -1; dc <= 1 && g.towers.size() < 2; dc++)
                    for (int dr = -1; dr <= 1 && g.towers.size() < 2; dr++)
                        if (Tower *c = g.placeTower(clover, sp.col + dc, sp.row + dr)) {
                            int id = c->id;
                            while (g.upgradeTower(id)) {}
                        }
            }
            g.startWave();
            for (int i = 0; i < 60 * 600 && g.phase == Phase::Wave; i++) g.update(1.0 / 60);
            return s.shots[sprout];
        };
        int plain = sproutShots(false), boosted = sproutShots(true);
        std::printf("Test 0b: sprout shots in wave 1: alone %d, next to a clover %d\n", plain, boosted);
        ok &= check(boosted > plain, "clover aura speeds up nearby plants");
    }

    // 0c) Campaign data: each map has 5 waves, only uses bugs unlocked by then,
    //     introduces its own new bugs, and has the right number of sky lanes.
    {
        std::printf("Test 0c: per-map waves, bug unlocks and sky lanes\n");
        using namespace data;
        // Bugs first unlocked on each map (Grubs come from caterpillars, never from waves).
        // Bats are rare wildcards: none before map 5, then 1-2 per map in total.
        const std::vector<std::vector<EnemyType>> unlocks = {
            {Ant, Beetle, Wasp},
            {Caterpillar, Butterfly},
            {Cricket, Scorpion, Snail},
            {Mosquito, GoldenBeetle},
            {Bat}, {}, {}, {}, {}, {},
        };
        const std::vector<int> laneCount = {0, 1, 1, 2, 2, 2, 2, 2, 3, 3};
        const std::vector<std::array<int, 2>> grids = {{15, 10}, {16, 11}, {17, 11}, {18, 12}, {20, 13},
                                                       {22, 15}, {24, 16}, {25, 17}, {27, 18}, {30, 20}};
        bool sizes = MAPS.size() == unlocks.size(), waves5 = true, unlockedOnly = true, introduces = true;
        bool lanes = true, noFlyersOnMap1 = true, finale = false, gridOk = true, batsOk = true, pathOk = true;
        std::vector<EnemyType> allowed;
        for (size_t m = 0; m < MAPS.size() && m < unlocks.size(); m++) {
            const MapDef &md = MAPS[m];
            allowed.insert(allowed.end(), unlocks[m].begin(), unlocks[m].end());
            waves5 &= md.waves.size() == 5;
            gridOk &= md.cols == grids[m][0] && md.rows == grids[m][1];
            pathOk &= md.path.front()[0] == -1 && md.path.back()[0] == md.cols;
            int bats = 0;
            std::vector<EnemyType> seen;
            for (const Wave &w : md.waves)
                for (const WaveGroup &grp : w.groups) {
                    if (grp.enemy == Bat) bats += grp.count;
                    seen.push_back(grp.enemy);
                    bool ok1 = std::find(allowed.begin(), allowed.end(), grp.enemy) != allowed.end();
                    if (!ok1) std::printf("    map %zu uses locked bug %s\n", m + 1, qPrintable(ENEMIES[grp.enemy].name));
                    unlockedOnly &= ok1;
                    if (m == 0 && ENEMIES[grp.enemy].flying) noFlyersOnMap1 = false;
                }
            for (EnemyType e : unlocks[m])
                if (std::find(seen.begin(), seen.end(), e) == seen.end()) {
                    std::printf("    map %zu never shows its new bug %s\n", m + 1, qPrintable(ENEMIES[e].name));
                    introduces = false;
                }
            batsOk &= m < 4 ? bats == 0 : bats >= 1 && bats <= 2;
            lanes &= int(md.skyLanes.size()) == laneCount[m];
            for (const Lane &l : md.skyLanes)
                lanes &= l.size() >= 2 && l.front()[0] == -1 && l.back()[0] == md.cols;
            std::printf("    map %2zu %-17s %dx%d, %zu waves, %zu sky lanes, %d bats\n", m + 1, qPrintable(md.name),
                        md.cols, md.rows, md.waves.size(), md.skyLanes.size(), bats);
        }
        if (!MAPS.empty() && !MAPS.back().waves.empty())
            for (const WaveGroup &grp : MAPS.back().waves.back().groups)
                if (grp.enemy == Snail && grp.count >= 2) finale = true;
        ok &= check(sizes, "10 campaign maps");
        ok &= check(gridOk, "grids grow 15x10 -> 30x20");
        ok &= check(pathOk, "paths run from x = -1 to the house at x = cols");
        ok &= check(waves5, "every map has 5 waves");
        ok &= check(unlockedOnly, "maps only use bugs unlocked so far");
        ok &= check(introduces, "every map shows off its newly unlocked bugs");
        ok &= check(noFlyersOnMap1, "no flyers on map 1");
        ok &= check(batsOk, "wild bats: none on maps 1-4, 1-2 per map on maps 5-10");
        ok &= check(lanes, "sky lanes 0/1/1/2/2/2/2/2/3/3, left edge to house");
        ok &= check(finale, "the finale's last wave brings at least 2 snails");
    }

    // S) Winter snow: on winter maps ground bugs walk at bugSpeedMult of their speed.
    {
        std::printf("Test S: winter snow slows ground bugs\n");
        bool winterOk = true;
        for (int m = 0; m < int(data::MAPS.size()); m++) {
            const bool winter = data::MAPS[m].season == data::Season::Winter;
            winterOk &= winter ? data::MAPS[m].bugSpeedMult < 1 && data::MAPS[m].frostMult < 1
                               : data::MAPS[m].bugSpeedMult == 1 && data::MAPS[m].frostMult == 1;
        }
        // Measure the first ground bug's pace on the first winter map (no plants, no slows).
        double measured = 0, expected = 0;
        for (int m = 0; m < int(data::MAPS.size()) && expected == 0; m++) {
            if (data::MAPS[m].bugSpeedMult >= 1) continue;
            Game g;
            g.reset(m);
            g.startWave();
            for (int i = 0; i < 60 * 30 && g.enemies.empty(); i++) g.update(1.0 / 60);
            if (g.enemies.empty()) break;
            const int id = g.enemies.front().id;
            const auto &def = data::ENEMIES[g.enemies.front().type];
            if (def.flying || def.hopEvery > 0) break;
            const double d0 = g.enemies.front().dist;
            for (int i = 0; i < 60; i++) g.update(1.0 / 60);   // one second
            for (const Enemy &e : g.enemies)
                if (e.id == id) measured = e.dist - d0;
            expected = def.speed * data::TILE * data::MAPS[m].bugSpeedMult;
        }
        std::printf("    first winter map: walked %.1f px in 1 s, expected %.1f\n", measured, expected);
        ok &= check(winterOk, "winter maps (and only they) have snow and weaker frost");
        ok &= check(expected > 0 && std::abs(measured - expected) < 2, "snow slows ground bugs by bugSpeedMult");
    }

    // U) Plant unlocks (drafted by claude2-0c): locked plants can't be placed, and
    //    each map's new plants unlock there.
    {
        std::printf("Test U: plant unlocks per map\n");
        const std::vector<std::vector<const char *>> fresh = {
            {"sprout", "frost", "mortar", "sunflower"},
            {"corn", "grape"},
            {"mushroom", "cactus"},
            {"chili", "rose", "clover"},
        };
        bool placeOk = true, freshOk = true;
        for (int m = 0; m < int(data::MAPS.size()); m++) {
            Game g;
            g.reset(m);
            for (int type = 0; type < int(data::TOWERS.size()); type++) {
                const bool unlocked = data::TOWERS[type].unlockMap <= m;
                if (g.isUnlocked(type) != unlocked) placeOk = false;
                g.coins = 100000;
                bool placed = false;
                for (int r = 0; r < g.rows && !placed; r++)
                    for (int c = 0; c < g.cols && !placed; c++)
                        if (g.canPlace(c, r)) placed = g.placeTower(type, c, r) != nullptr;
                if (placed != unlocked) {
                    std::printf("    map %d: %s placed=%d unlocked=%d\n", m + 1,
                                qPrintable(data::TOWERS[type].id), placed, unlocked);
                    placeOk = false;
                }
            }
            if (m < int(fresh.size()))
                for (const char *id : fresh[m]) {
                    const int type = typeOf(id);
                    if (data::TOWERS[type].unlockMap != m) {
                        std::printf("    %s unlocks on map %d, expected %d\n", id,
                                    data::TOWERS[type].unlockMap + 1, m + 1);
                        freshOk = false;
                    }
                    if (data::TOWERS[type].counters.isEmpty() && m > 0) freshOk = false;   // card text
                }
        }
        ok &= check(placeOk, "locked plants can't be placed; unlocked ones can");
        ok &= check(freshOk, "each map's new plants exist, unlock there, and have card text");
        Game g;
        g.reset(2);
        const bool hadCactus = g.isUnlocked(cactus);
        g.reset(0);
        ok &= check(hadCactus && !g.isUnlocked(cactus) && !g.isUnlocked(corn),
                    "reset(0) relocks; reset(N) unlocks up to map N");
    }

    // 1) New plants only on map 1: do they fire, poison, burn and pay out?
    {
        std::printf("Test 1: new plants only (Sunny Meadow)\n");
        Game g;
        g.unlockAll = true;
        Stats s;
        watch(g, s);
        g.coins = 100000;
        // Only two chilis: with five short waves, four chilis kill everything before
        // it reaches the mushrooms (which autoPlant puts near the end of the path).
        autoPlant(g, {mushroom, mushroom, chili, chili, sunflower, sunflower});
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
                                          mushroom, mushroom, chili, chili, sunflower,
                                          rose, corn, clover};

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

    // 3) The autopilot (the app's 🤖 button) plays the campaign with normal coins,
    //    thinking every half second of game time, like it does in the app.
    {
        std::printf("Test 3: autopilot campaign (normal coins)\n");
        Game g;
        Autopilot ai(g);
        auto runFor = [&](double seconds) {
            for (int i = 0; i < int(seconds * 60); i++) {
                if (i % 30 == 0) ai.think();
                g.update(1.0 / 60);
            }
        };
        int mapsCleared = 0;
        while (true) {
            while (g.phase == Phase::Build) {
                runFor(1.0);   // shop between waves
                if (!g.startWave()) break;
                for (int i = 0; i < 600 && g.phase == Phase::Wave; i++) runFor(1.0);
                if (std::getenv("AUTOPILOT_VERBOSE")) {
                    std::printf("      wave %2d: lives %2d coins %4d plants:", g.wave, g.lives, g.coins);
                    for (const Tower &t : g.towers)
                        std::printf(" %s%d@%d,%d", qPrintable(data::TOWERS[t.type].id), t.level + 1, t.col, t.row);
                    std::printf("\n");
                }
            }
            std::printf("    map %d %-14s -> %s at wave %d, lives %d, plants %zu\n", g.mapIndex + 1,
                        qPrintable(g.map().name),
                        g.phase == Phase::MapCleared ? "cleared" : g.phase == Phase::Won ? "WON" : "LOST",
                        g.wave, g.lives, g.towers.size());
            if (g.phase != Phase::MapCleared) break;
            mapsCleared++;
            g.nextMap();
        }
        ok &= check(g.phase == Phase::Won, "autopilot wins the whole campaign with normal coins");
        std::printf("    autopilot result: %s\n", g.phase == Phase::Won ? "won the campaign" : "lost");
    }

    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
