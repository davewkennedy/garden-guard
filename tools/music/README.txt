GARDEN GUARD MUSIC (owner: Dale)
==============================================================================
Original music, composed and synthesized by gen.js: no samples, no
recordings, no third-party audio, no melodies from existing songs. The
same synth vocabulary as the game's sound effects (web/js/audio.js): sine,
triangle, square and saw oscillators, exponential decays, RBJ filters,
filtered noise, plus a small Schroeder reverb. Deterministic: rebuilding
gives byte-identical MP3s (checked with ffmpeg 7.1.1).

REBUILD (WSL, from the repo root)
  node tools/music/build.js     render + level + encode + QA
                                -> web/audio/*.mp3, web/audio/music.json
  node tools/music/sfx-level.js measures the SFX (for the music gain)
Needs ffmpeg with libmp3lame: $FFMPEG, ffmpeg on PATH, or the Windows
one at C:\ProgramData\chocolatey\bin\ffmpeg.exe (called from WSL).
build.js prints PASS/FAIL lines and exits 1 on a FAIL.

FILES (web/audio/; Qt embeds the same files)
  music-build.mp3   map 1 (Sunny Meadow) and menus, build phase
                                                  C major, 88 BPM, 24 bars
  music-wave.mp3    map 1, wave phase             A minor, 132 BPM, 32 bars
  music-build-mN.mp3, music-wave-mN.mp3
                    map N = 2..10, its own two loops (see MAPS below),
                    26-33 s each
  stinger-win.mp3   one-shot, about 4 s (all maps)
  stinger-lose.mp3  one-shot, about 3 s (all maps)
  music.json        loop points (seconds and samples), levels, sizes, and
                    "maps": which two loops each map plays
Mono, 44.1 kHz, CBR: 80 kbps for map 1 and the stingers (unchanged since
the first release), 64 kbps for maps 2-10. About 5.6 MB in all; build.js
fails above 6 MiB.
Rebuild one track to listen to it quickly:
  node tools/music/gen.js --out /tmp/gg --only build-m7,wave-m7

MAPS (players: web/js/music.js, qt/src/music.cpp; same rule in both)
Map index 0 (Sunny Meadow) plays build/wave; map index i plays
build-m(i+1)/wave-m(i+1). Menus (the intro card and the map list) play
Sunny Meadow's build loop. A map change crossfades over 1.5 s, like a
phase change. Only the current map's loops, the menu loop and the
stingers stay decoded; the service worker precaches every file, so all
maps play offline.

SEAMLESS LOOPS
A loop file holds 0.5 s before bar 1 and 1.5 s after the last bar, and
the whole file is exactly periodic: the loop is rendered three times in a
row and the middle copy kept, so note and reverb tails wrap onto the start
and nothing fades. Players loop [loopStart, loopEnd) and start AT
loopStart (bar 1). Because the signal is periodic around both points, it
loops cleanly whether a decoder keeps or trims the MP3 encoder delay.
build.js checks (a) the source is periodic to better than -80 dB (it is
about -220 dB) and (b) the decoded MP3 shows no click at the splice.

LEVELS
Files: loops about -20 LUFS (true peak capped at -3 dBTP, so -21 to -22
in practice), stingers -17 to -19 LUFS. The game's SFX are much quieter
(sfx-level.js: median peak -30 dBFS, loudest momentary -38.6 LUFS), so the
music sits under them by its PLAYBACK gain, not in the file: base gain 0.1
(-20 dB) times the music volume (default 0.6) puts it near -45 LUFS.

LISTEN GUIDE (what each track should sound like)
  build  Calm garden morning. A soft sine bass and a warm, slow pad under a
         kalimba-like pluck playing eighth-note arpeggios (Cmaj9 - Am7 -
         Fmaj7 - G6 - Em7 - Am7 - Dm9 - Gsus). Bars 1-8: just that, a soft
         shaker joins at bar 5. Bars 9-16: a bell melody enters. Bars 17-24:
         a second, answering bell phrase over a sparser pluck, ending on
         Gsus so bar 1 feels like coming home. No drums.
  wave   Same garden, now busy. A minor, 132 BPM (3:2 against the build
         loop, so switching feels related). Kick on 1 and 3, a short
         filtered-noise snare on 2 and 4, eighth hats; a square "pump" bass
         in octaves; a square arpeggio in sixteenths. Bars 9-16: a saw and
         triangle lead melody. Bars 17-24: a higher, syncopated lead with
         open hats and an extra kick. Bars 25-32: breakdown (no snare, the
         lead drops out, a kalimba counter-line), then a sixteenth-note
         snare fill in bar 32 back into bar 1. Upbeat, never frantic.
  win    A quick rising pluck (G C E G C), then a rolled bright C chord
         with bells, a soft pad and a light sparkle on top; rings out.
  lose   Three falling bell notes (E C A), then a low A minor chord that
         slowly droops a little flat and darkens. Not comic; a sigh.
  MAP LOOPS (build first, then wave)
  m2  Apple Orchard (late spring). Build: a waltz in G, 3/4, 100 BPM;
      oom-pa-pa bass and plucked strings, a flute tune, a music box
      joins in bars 9-16. Wave: G major, 140 BPM, a bouncy country
      two-beat with offbeat strums, flute lead, music box doubling.
  m3  Thorny Ridge (high summer, dry). Build: D dorian, 70 BPM; a low
      drone, a picked guitar, hand drums, crickets and a lonely whistle.
      Wave: D minor, 144 BPM; a galloping palm-muted riff, toms on the
      pickup, a square lead in bars 9-16.
  m4  Harvest Hollow (late summer, golden). Build: F major, 96 BPM;
      banjo rolls, warm pad, flute from bar 5, a music box answers.
      Wave: F major, 136 BPM; a hoedown two-step with a fiddle.
  m5  Moonlit Garden (night). Build: E minor, 66 BPM; a music box with
      echoes, crickets, firefly sparkles, no drums. Wave: E minor,
      118 BPM; a soft night pulse, rim clicks, echoing plucks, and a
      gliding theremin tune in bars 9-16.
  m6  Amber Woods (autumn). Build: B minor, 78 BPM; nylon guitar, a low
      cello line, falling-leaf bells in bars 1-2, flute. Wave: B minor,
      136 BPM; a galloping bass, a horn tune, then a saw lead with the
      horn holding chords; tom fills.
  m7  Pumpkin Patch (autumn, crossroads; playful-spooky). Build: C minor,
      100 BPM; a walking bassoon, pizzicato, woodblocks and a tiptoeing
      celesta with chromatic turns. Wave: C minor, 148 BPM; two lines
      calling and answering (square high, saw low), like the two paths.
  m8  Frost Fen (winter arrives). Build: F# minor, 68 BPM; glassy bells
      over a cold drone and shimmering pad, ice sparkles. Wave: F# minor,
      128 BPM; claps, sixteenth hats, an icy plucked arpeggio, a bell
      lead in bars 9-16.
  m9  Snowdrift Hollow (cozy deep winter). Build: Eb major, 64 BPM;
      sleigh bells, kalimba, a celesta tune. Wave: G minor, 140 BPM;
      sledding: sleigh bells on every eighth, music-box tune, then a
      saw lead.
  m10 Evergreen Summit (the finale). Build: C major, 80 BPM; home key:
      Sunny Meadow's opening bell phrase returns on a horn over the
      meadow's kalimba, timpani, a borrowed F minor bar. Wave: C minor,
      152 BPM, 20 bars; the biggest track: crash and timpani, octave
      bass, sixteenth arpeggio, lead with horns an octave below.
WHAT TO LISTEN FOR in QA: no click, gap or level jump at the loop point
(build: from the Gsus bar into Cmaj9; wave: from the snare fill into bar
1); a smooth crossfade build <-> wave; stingers that cut through a ducked
loop; SFX still clearly on top; a smooth crossfade when the map changes
(next map, map list, Continue); every map at a similar level.
