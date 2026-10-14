<img src="logo.png" alt="reVC logo" width="160">

# reVC-PS3

**reVC (GTA Vice City) for the PlayStation 3**, as homebrew built with PSL1GHT.

reVC is the reverse-engineered source code of GTA Vice City. This port runs
it natively on the PS3, with a hardware renderer on the RSX (a PS3 backend
for librw), DualShock 3 controls in the PS2 style, and the game's data read
from your own PC copy. It is the sibling of
[re3-PS3](https://github.com/stingziz0u/re3-PS3) (GTA III) and shares its
PS3 layer.

> **Status: 1.3 alpha.** Every mission script of the game, the odd jobs, the
> collectibles, a flight over the whole map, saving/loading, vehicles and
> garages have been run through the automatic test on real hardware without
> crashes, but it hasn't had long play-throughs yet. Expect bugs and please
> report them with the log (see [Troubleshooting](#troubleshooting)).
> Only tested on real consoles, not on RPCS3.
>
> 1.1 alpha fixed the crash in the mission *Riot* (the game going back to
> the XMB during the fights). 1.1.1 alpha fixes other code that only worked
> with the PC compiler (unset variables, loops that could freeze the game,
> compiler optimizations that broke the original code), and the log now
> records what the game was doing when it crashes or freezes.
> 1.1.2 alpha fixes a crash (and a full console freeze) from particles
> (debris, sparks) animated past the end of their texture list, and
> halves the dust and smoke particles at 60 fps (helicopters near the
> ground no longer drop the frame rate), adds right stick sensitivity
> settings, and X on the pause menu map sets a waypoint without closing
> the map.
> 1.2 alpha adds graphics options: mipmaps, texture filtering (bilinear,
> trilinear, anisotropic) and antialiasing (FXAA), and fewer hitches when
> new areas stream in (textures converted several times faster, spread
> over frames, and the log written in the background).
> 1.3 alpha keeps the DXT textures compressed in video memory, where the RSX
> reads them as they are: 4 times less VRAM, and the flights over the map
> hold 59-60 fps instead of 55-57 with far fewer hitches. Textures are
> converted on the PPU's second thread, mip levels use AltiVec, and big
> smoke and explosion particles right in front of the camera fade out
> (explosions no longer swamp the RSX). It fixes a crash in the Cubans'
> *Naval Engagement* (a deleted object left in the physics list), and after
> a crash the game goes back to the XMB instead of freezing the console.
>
> **Known issue:** a freeze after crashing a bike at high speed has been
> reported. 1.2 and 1.3 alpha have fixes for what could cause it (the audio
> no longer makes the game wait for the disk, the world's sector loops
> can't run away on bad coordinates, deleted objects are taken out of the
> physics list), but it hasn't been confirmed fixed. If it happens to you,
> please send the log.

**No game data is included.** You need a legally purchased copy of GTA Vice
City for PC. The PKG only has the program, its icon and two reVC files (the
PlayStation button icons and the controller picture of the settings page).

## Features

- Native 720p, rendered on the RSX: tiled colour/depth buffers, compressed
  depth and ZCULL. About 60 fps across the automatic test (missions, map,
  weather).
- **30 fps** by default (the rate the game was made for) or **60 fps**:
  *Options > Graphics > Frame rate*. Always in sync with the TV (no tearing).
- PS2-style controls (Vice City's PS2 setup) with lock-on aiming, pad setups,
  inverted look, free camera, third and first person sensitivity
  (*Controller setup > Gamepad settings*).
  PlayStation buttons in the on-screen help.
- The pause menu **map**: L1 shows the legend, X sets a waypoint.
- **Collectibles on the map** (*Display > Hidden packages on map / Rampages
  on map*, off by default): the hidden packages in lime green and the
  rampages in blood red, on the radar (the nearest one at its edge when none
  is in range) and the pause menu map. One you pick up (or pass) disappears.
- All the radio stations, and the **MP3 player** station: put `.mp3` files
  in `USRDIR/mp3`.
- **Skins**: put `.bmp` skins in `USRDIR/skins`, choose them in *Player setup*.
- *Display > Screen size* for overscan, FPS counter (*Graphics > Show FPS*).
- *Options > Graphics*:
  - **Mipmaps**: textures that come without smaller versions get them (less
    shimmering in the distance). They are made as textures load: restart
    the game (or travel) for all of them. Uses more video memory.
  - **Texture filtering**: Default (the game's own), Bilinear, Trilinear,
    Anisotropic 2x to 16x (sharper ground and roads at an angle). With
    Mipmaps off, only the textures that have them in their TXD use them.
  - **Antialiasing**: Off or FXAA (smoother edges, a filter over the
    finished picture: ~2 ms of GPU time and 7 MB of video memory at 720p).
  All three are off/default unless you choose them.
- DXT textures (most of the PC game's) stay compressed in video memory and
  the RSX reads them as they are: 4 times less VRAM than converted, and
  nothing to convert when an area streams in. Mip levels made on the CPU use
  AltiVec.
- Settings saved in `USRDIR` (`reVC.ini`, `gta_vc.set`), saves in
  `USRDIR/userfiles`. Saves are in the PS3's format: PC saves can't be
  loaded, and the other way round.

## Installation

1. Install `revc-ps3-1.3-alpha.pkg` on a PS3 with homebrew support (CFW or
   HEN), e.g. copy it by FTP and install it with the package manager. It
   appears in the XMB as **reVC**.
2. The PKG creates `/dev_hdd0/game/REVCPS300/USRDIR` with these empty
   folders. From your GTA Vice City PC installation, copy into them (by FTP):

   | Folder  | Contents                                   |
   |---------|--------------------------------------------|
   | `anim`  | the PC game's `anim` folder                |
   | `audio` | the PC game's `audio` folder (big: the radio stations) |
   | `data`  | the PC game's `data` folder                |
   | `models`| the PC game's `models` folder (keep the two reVC `.txd` already there) |
   | `text`  | the PC game's `text` folder                |
   | `txd`   | the PC game's `txd` folder (loading screens) |
   | `mp3`   | optional: your own music                   |
   | `skins` | optional: player skins (`.bmp`)            |

   The `movies` and `mss` folders of the PC game aren't needed. Keep the
   folder names lowercase, exactly as above.
3. Start **reVC** from the XMB.

## Mods

Anything made for the PC version of GTA Vice City as data files works:

- TXD textures (loose in `models`/`txd`, or inside `gta3.img` with an IMG
  tool on the PC, then copy `gta3.img` and `gta3.dir`): HD loading screens,
  HUD, fonts, vehicles, peds, buildings. All PC formats are read
  (palettized, 16/32 bit, DXT1/3/5).
- DFF models in GTA Vice City PC format, inside `gta3.img`.
- Data files (`handling.cfg`, `carcols.dat`, `timecyc.dat`, `gta_vc.dat`...)
  and a modified `main.scm`.

Limits: on the PS3 DXT textures (power of two sizes) stay compressed, as on
the PC, and every other texture takes 4 bytes per pixel (uncompressed) in
the RSX's 216 MB, so a pack of uncompressed textures that raises
*everything* to 1024×1024 can run out of video memory (models get dropped,
textures turn white). The largest texture size is 4096×4096. Code mods
(CLEO, ASI, plugins that patch the executable) and unconverted assets from
other games or platforms don't work.

## Troubleshooting

- The log is `USRDIR/revc-ps3.log` (the previous one: `revc-ps3.old.log`).
  Send it with any report: it says what the game was doing when it crashed.
- A black screen at start: create an empty `USRDIR/notiles.txt` (turns off
  the tiled buffers), and report it.
- A texture that looks wrong: create an empty `USRDIR/nodxt.txt` (DXT
  textures converted on the CPU, as before), and report it. If that
  isn't it, an empty `USRDIR/notexthread.txt` converts the textures on the
  game's own thread again (they are converted on a second PPU thread);
  `USRDIR/novmx.txt` makes the mip levels without AltiVec; and
  `USRDIR/dxt32.txt` keeps converted DXT1 textures in 32 bits instead of 16.
- `USRDIR/autotest.txt` runs an automatic test while you watch (load a save
  in a safehouse, with no mission running, and walk out: it starts 10 s
  after the player is outdoors, with a countdown on screen). One line in it chooses
  the test: nothing (weather, a flight over the map, save/load, a car, a
  bike, a helicopter and a boat, side missions, garages; `loops=N` repeats
  it), `mode=full` (every mission 30 s — `secs=N`, `from=N`, `to=N` —
  odd jobs, rampage, collectibles; it changes the game's progress and money,
  don't save after it; if the game dies in a mission, the next start goes on
  after it, `resume=0` turns that off), `mode=gpu` (GPU benchmark),
  `mode=rain`, `mode=leak`, `mode=stress` (the heaviest loads in Little
  Haiti: rain, hurricane, night, traffic, a riot with armed peds, cars blown
  up, wanted level 6 in a tank, all of it at once; then flights in the rain;
  about 7 minutes), `mode=tankstress` (you drive a tank with its doors
  locked and wanted level 6 kept up, 5 minutes; for performance logs of
  the chaos). The file is renamed to `autotest.done.txt` when the test
  ends.

## Building

On Linux or WSL (Ubuntu):

1. Install the PS3 toolchain and PSL1GHT (https://github.com/ps3dev/ps3toolchain),
   which ends up in `/usr/local/ps3dev`.
2. Build:

   ```sh
   git clone https://github.com/stingziz0u/reVC-PS3.git
   cd reVC-PS3
   export PS3DEV=/usr/local/ps3dev PSL1GHT=/usr/local/ps3dev
   export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH
   make -C ps3 -j$(nproc)
   make -C ps3 verify
   ```

   The result is `ps3/revc-ps3-1.3-alpha.pkg`.

Other targets: `make -C ps3 DEBUG=1` (debug build with asserts),
`make -C ps3 FTRACE=1` (function trace of peds, weapons, collision, HUD,
radar... dumped to the log at a crash), `make -C ps3 STAGE=1` (headless test
build: loads the game without video, audio or pad, everything to the log),
`make -C ps3 crash LOG=<revc-ps3.log>` (the crash lines of a log as function
names, for the build that made it), `make -C ps3 clean`.

The RSX shaders are in the tree already compiled
(`vendor/librw/src/ps3/ps3shaders.h`). To change them you need NVIDIA's `cgc`
and PSL1GHT's `cgcomp` with `cgcomp-relative-constants.patch`; see
`vendor/librw/src/ps3/shaders/build_shaders.sh`.

### Where things are

- `ps3/` — Makefile, PKG icon, patch level, `tools/crashaddr.py`.
- `src/skel/ps3/` — platform layer: main loop, file system, pad, log, test.
- `src/audio/sampman_ps3.cpp` — audio (software mixer on libaudio, the radio's ADF, MP3/WAV streams).
- `vendor/librw/src/ps3/` — librw's RSX backend: VRAM, rasters, pipelines, shaders.
- `src/core/config.h` (`__PS3__` section) — what the PS3 build turns on and off.

## Credits

- The [re3 team](https://github.com/GTAmodding/re3) for reVC, and aap for
  [librw](https://github.com/aap/librw).
- The [ps3dev](https://github.com/ps3dev) people for ps3toolchain and PSL1GHT.
- lieff for [minimp3](https://github.com/lieff/minimp3).

The original reVC README is kept as [README.reVC.md](README.reVC.md).

## License

See [LICENSE](LICENSE). Grand Theft Auto: Vice City is © Rockstar Games /
Take-Two Interactive. This project is not affiliated with or endorsed by
Rockstar Games, Take-Two Interactive or Sony Interactive Entertainment.

## AI disclosure

This project's code was written collaboratively with Claude (Anthropic),
working through this port with me in real time over many sessions. Every
bit of testing and debugging was done by me on real hardware.
