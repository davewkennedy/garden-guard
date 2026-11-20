# Garden Guard++ (web) 🌱

A cozy tower-defense game. Bugs march along the garden path, or fly along sky lanes, toward your house. Plant defenders to stop them. Clear **10 maps × 5 waves**, from spring to winter, to win the campaign.

This is a port of the Qt desktop game to plain HTML, CSS and JavaScript. It plays the same, rule for rule, on a PC and on phones held sideways.

## Play on a PC

Double-click **index.html**. It opens in your browser and needs no install, no server and no internet.
(Offline caching and "install as an app" don't work this way: browsers only allow them for pages served over https. The game itself plays fine.)

## Host it (to play on phones)

Copy the whole folder to any static web host, such as GitHub Pages, Netlify, Cloudflare Pages or your own web server. It needs no build step and no server code.

- Use **https**. Phones only allow offline play and "Add to Home Screen" as a full-screen app on https pages.
- After the first visit the game is cached (sw.js), so it also plays with no signal.
- Updating the game: change the files, then bump `CACHE` in sw.js (e.g. `ggweb-v1` → `ggweb-v2`) so phones download the new version. Close and reopen the game once to see it.
- For a quick test on your home Wi-Fi, run `python3 -m http.server 8000` in the folder and open `http://<your-PC's-IP>:8000` on the phone. This plays fine, but without https there's no offline mode or install.

## Play on a phone

Turn the phone **sideways** (landscape). In portrait it asks you to rotate.

**Android (Chrome)**
- Tap **⛶** at the top right to play full screen.
- To keep it like an app: Chrome menu **⋮** → **Add to Home screen** (or **Install app**). It then opens full screen from its 🌻 icon.

**iPhone (Safari)**
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
| Double-tap a plant | Select every plant of that type |
| Long-press | Show info about a tile, plant or bug (like hovering with a mouse) |
| Pinch | Zoom in and out |
| One-finger drag (when zoomed in) | Move around the garden |
| 🔍 | Zoom back out to the whole garden |
| ▸ | Hide or show the side panel |

Every keyboard shortcut also has an on-screen button (📖 🤖 1× ⏸ 🔊 ⛶ in the header, and the plant panel's buttons).

## Files

- `index.html`: the game. `css/`, `js/`: its code (one file per part, plain scripts).
- `manifest.json`, `icon-*.png`, `sw.js`: the phone app setup (name, icons, offline cache).
- `tools/`: developer scripts (Node parity test against the C++ game, screenshots, `make-icons.sh`).
