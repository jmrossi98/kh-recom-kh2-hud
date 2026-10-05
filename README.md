# kh-recom-kh2-hud

Kingdom Hearts II's status display in Kingdom Hearts Re:Chain of Memories.
PC, Steam release of HD 1.5+2.5 ReMIX only.

Work in progress.

## What it does

- Sora's portrait frame and KH2's portrait of him in his KH1 clothes, bottom
  right, in place of the game's own portrait and HP ring
- KH2's HP gauge, by KH2's rules: a ring that takes the first 60 HP at 4.5
  degrees each, then 3 units of bar for every HP past that. Re:CoM's HP is
  shown at a quarter, so the gauge is the size it would be at the same point
  in KH2
- Lost HP shows red and fades, the portrait winces and the frame flushes red
  when hit; at low HP the frame and the gauge's backing pulse red
- The map card panel that normally sits in that corner outside battle is hidden
- F8 shows the game's own portrait and gauge again

It works by itself. Together with
[kh-recom-mod](https://github.com/jmrossi98/kh-recom-mod), which replaces the
card combat with a command menu, that menu takes KH2's skin for the world you
are in and the MP bar joins the gauge. The two find each other at run time,
there is nothing to configure.

## Art

No game art is in this repo. `tools_dev/make_assets.py` builds what the overlay
needs from your own extracted copies of Re:CoM and KH2 (both are in HD
1.5+2.5 ReMIX) into `build/assets`.

    python tools_dev/make_assets.py <extracted recom> build/assets <extracted kh2> <openkh Apps folder>

## Layout

- `scripts/kh2hud.lua` - LuaBackend script, loads the dll and calls it every frame.
- `src/hud.cpp` - native side: reads HP, hides the game's own gauge.
- `src/overlay.cpp` - draws over the game. The game presents from a D3D12 swap
  chain, so this hooks Present and draws through D3D11On12. Same file as in
  kh-recom-mod; each mod only switches on its own part.
- `tools_dev/` - `make_assets.py`, two test programs that render the overlay
  to a BMP without the game (`build_test.bat`), and `kh2seq.py`, a software
  renderer for KH2 sequence files.

## Building

Needs the Visual Studio 2022 build tools. Close the game first, the dll is
locked while it runs.

    build.bat

## Installing

1. Install [LuaBackend Hook](https://github.com/Sirius902/LuaBackend) into the
   game folder.
2. In `LuaBackend.toml`, add this repo's `scripts` folder to the `[recom]`
   scripts and use the Steam `game_docs` line.
3. Fix `DLL_PATH` at the top of `scripts/kh2hud.lua` if the repo is not in the
   same place as mine.

## Notes

All addresses are for the Steam build. The script checks a few code bytes on
startup and turns itself off if they do not match.

For the first three minutes after launch the dll watches for the game hanging:
if no frame comes for 20 seconds it writes how far it got to
`build/recom_kh2hud.dll.log` and ends the game.
