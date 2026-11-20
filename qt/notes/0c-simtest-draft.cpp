// claude2-0c: draft unlock checks for tests/simtest.cpp (claude2-de owns that file).
// Needs: data::TowerDef::unlockMap (0-based), Game::isUnlocked(type), Game::unlockAll (test override,
// default false). Mechanics tests that plant every type on map 1 (Test 0, 0b, ...) should set
// `g.unlockAll = true;` right after `Game g;`, otherwise autoPlant hits exit(2) on locked plants.

    // U) Plant unlocks: locked plants can't be placed, each map's new plants unlock there.
    {
        std::printf("Test U: plant unlocks per map\n");
        // Expected new plants per map (0-based map index). Maps 5+ add none.
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
                // Try a free tile with plenty of coins: must succeed iff unlocked.
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
                    const int type = typeOf(id);   // exits if the plant doesn't exist
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
        // Play again relocks: reach map 3, then reset(0).
        Game g;
        g.reset(2);
        const bool hadCactus = g.isUnlocked(cactus);
        g.reset(0);
        ok &= check(hadCactus && !g.isUnlocked(cactus) && !g.isUnlocked(corn),
                    "reset(0) relocks; reset(N) unlocks up to map N");
    }
