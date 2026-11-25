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
  music-build.mp3   loop, build phase and menus   C major, 88 BPM, 24 bars
  music-wave.mp3    loop, wave phase              A minor, 132 BPM, 32 bars
  stinger-win.mp3   one-shot, about 4 s
  stinger-lose.mp3  one-shot, about 3 s
  music.json        loop points (seconds and samples), levels, sizes
Mono, 44.1 kHz, 80 kbps CBR, about 1.3 MB in all.

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
WHAT TO LISTEN FOR in QA: no click, gap or level jump at the loop point
(build: from the Gsus bar into Cmaj9; wave: from the snare fill into bar
1); a smooth crossfade build <-> wave; stingers that cut through a ducked
loop; SFX still clearly on top.
