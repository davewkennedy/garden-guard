# Garden Guard++ (web) 🌱

A cozy tower-defense game. Bugs march along the garden path, or fly along sky lanes, toward your house. Plant defenders to stop them. Clear **10 maps × 5 waves**, from spring to winter, to win the campaign.

This is a port of the Qt desktop game to plain HTML, CSS and JavaScript. It plays the same, rule for rule, on a PC, on tablets, and on phones held sideways.

## Play on a PC

Double-click **index.html**. It opens in your browser and needs no install, no server and no internet.
(Offline caching and "install as an app" don't work this way: browsers only allow them for pages served over https. The game itself plays fine.)

## Host it (to play on phones and tablets)

1. Make the upload folder: `bash tools/make-dist.sh` (in WSL/Linux/macOS). It writes **dist/gardenguard-web/** (only the game files, no notes, tools or screenshots) and **dist/gardenguard-web.zip**.
2. Upload the **contents** of `dist/gardenguard-web/` (or unpack the zip) to any static web host, such as GitHub Pages, Netlify, Cloudflare Pages or your own web server. It needs no build step and no server code.

- Use **https**. Phones only allow offline play and installing it as a full-screen app on https pages.
- After the first visit the game is cached (sw.js), so it also plays with no signal.
- **Updating the game:** change the files, run `bash tools/make-sw-list.sh` (it refreshes the offline file list and sets a new cache version from the files' contents, so there's nothing to bump by hand), then `bash tools/make-dist.sh`, then upload `dist/gardenguard-web/` (or the zip) again. Phones pick up the new version the next time the game is closed and reopened.
- Quick test on your home Wi-Fi: run `python3 -m http.server 8000` in the folder and open `http://<your-PC's-IP>:8000` on the phone. This plays fine, but without https there's no offline mode or install.
- Developer checks: `node tools/pwa-check.js` confirms that Chrome considers the game installable and that it loads offline.

## Play on a phone or tablet

Phones: turn the phone **sideways** (landscape). In portrait it asks you to rotate. Tablets (iPad etc.) can play either way.

Your run is saved at the start of every map. If the phone closes the tab (or you do), the start card offers **▶ Resume your run**, which takes you back to the start of that map with the lives you had then. Losing a run, or starting a new one with "Let's grow!", replaces the saved run.

**Android (Chrome)**
- Tap the **full-screen button** (four corners, top right) to play full screen.
- To keep it like an app: tap **📲 Install as an app** on the start card (shown when the game is hosted over https), or use the Chrome menu **⋮** → **Add to Home screen** / **Install app**. It then opens full screen from its 🌻 icon, and works offline.

**iPhone and iPad (Safari)**
- Tap **Share** (the square with the arrow) → **Add to Home Screen** → **Add**.
- Open it from the 🌻 icon on your home screen. It runs full screen, with no Safari bars.
- (Safari can't go full screen from a normal tab, so use the home screen icon.)

## How to play

- Pick a plant from the shop (click/tap it, or press its key), then click/tap a grass tile to plant it. Plants can't go on the path, boulders or open water, but they can go on lily pads.
- Click/tap a plant to see its stats, upgrade it (3 levels), sell it (70% refund), set a fire point 🎯, or focus fire.
- Squashed bugs pay coins. Clearing wave *n* pays 20 + 5·*n* coins.
- A bug that reaches the house costs lives (a snail costs 5). You have **20 lives for the whole campaign**: they carry over between maps, while coins and plants reset.
- New plants unlock as the campaign goes on, each one the answer to a new bug.

## Controls

### Mouse and keyboard (PC)

| Input | Action |
|---|---|
| `1`–`9`, `0`, `-` | Select a plant in shop order (press again to cancel) |
| Click | Place the selected plant / select a planted one / pick a focus target |
| Double-click a plant | Select **every plant of that type** (for focus fire or Upgrade all) |
| Mouse wheel | Zoom in and out on the garden |
| Drag (when zoomed in) | Move around the garden |
| `T` / 🎯 | Set (or clear) a fire point for the selected plant |
| `N` | Start the next wave |
| `A` / 🤖 | Autopilot: the AI plays at 2× while you watch (press again to take over) |
| `F` | Speed 1× → 2× → 3× |
| `Space` / ⏸ | Pause / resume |
| `J` / 📖 | Garden Journal (open / close) |
| `Esc` | Cancel placing, aiming or focus fire; deselect |

### Touch (phones and tablets)

| Gesture | Action |
|---|---|
| Tap | Same as a click: plant, select, pick a focus target |
| Placing or aiming | A **magnifier bubble** shows the tile under your finger; the plant goes in (or the fire point is set) when you **lift** your finger |
| Tap on a big map | When the tiles are tiny, the first tap **zooms in** on that spot; tap again to plant. Or **press and hold** to aim with the magnifier, then lift to plant directly (no zoom step) |
| Double-tap a plant | Select every plant of that type |
| Long-press (when not placing) | Show info about a tile, plant or bug (like hovering with a mouse) |
| Pinch | Zoom in and out |
| One-finger drag (when zoomed in) | Move around the garden |
| 🔍 | Zoom back out to the whole garden |
| ✕ Cancel focus | Stop focus fire for the selected plants |
| ▸ | Hide or show the side panel |

Every keyboard shortcut also has an on-screen button: 📖 🤖 1× ⏸ 🔊 and the full-screen button in the header, plus the plant panel's buttons.

## Web-only extras (not in the Qt version)

- **▶ Resume your run**: the game saves at the start of every map, so a closed tab doesn't cost you the run.
- **Touch loupe**: a magnifier bubble under your finger while placing or aiming; it acts when you lift.
- **Tiny-tile auto-zoom**: on big maps the first tap zooms in on that spot, the second one plants.
- **✕ Cancel focus** button (the Qt version uses `Esc` only).
- **📲 Install as an app** button, **full screen**, and **Add to Home Screen** for an app-like, full-screen game.
- **Offline play** once the hosted game has been opened.
- **Keeps the screen awake while playing**: during a wave or on autopilot, the phone won't dim or lock; paused, on a card, or between waves it can sleep as usual. (Needs the hosted https version.)
- A **short buzz** on Android when a bug reaches the house (off when the sound is muted or on autopilot).
- **Touch tips** on the start card on touch screens.
- **Tablet upright layout**: iPads can play in portrait as well as landscape.

## Known limits

- iPhone Safari can't go full screen in a normal tab: use **Share → Add to Home Screen** and start it from the icon.
- On iPhone the **silent switch** can mute the game's sounds.
- Offline play and installing need the game to be hosted over **https** (double-clicking index.html plays fine, but without those).
- The resume checkpoint is from the **start of the map**: lives lost partway through a map aren't kept when you resume. A hard crash of the browser right after a map starts can also lose that map's checkpoint (browsers write saved data to disk with a short delay); you'd then resume from the map before.

## Files

- `index.html`: the game. `css/`, `js/`: its code (one file per part, plain scripts).
- `manifest.json`, `icon-*.png`, `sw.js`: the phone app setup (name, icons, offline cache).
- `tools/`: developer scripts: `make-dist.sh` (upload folder + zip), `make-sw-list.sh` (offline file list), `pwa-check.js` (installable + offline check), `parity.js` (same results as the C++ game), `browser-run.js` and `shot.sh` (browser tests and screenshots), `make-icons.sh`.
