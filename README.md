# RoN Plugin Loader

A plugin loader for Rise of Nations: Extended Edition (Steam, `riseofnations.exe` build `0x6674863F`).

The loader is one file, `shfolder.dll`, in the game folder. It loads plugin DLLs from
`<game>\plugins\<Name>.dll` (each with a `<Name>.ini`) in an order the player sets on a Plugins
panel in the main menu. Plugins can replace, add or generate game files, hook game
functions through one shared hook manager, and declare whether they change the simulation, so a
multiplayer lobby can check that every player runs the same set.

Status: the loader works (bootstrap, load order, plugin API 1.1, shared hooks, events, the file
service, the Plugins panel), checked in the game (2026-11-24 and 25). RON Revived runs on it as a
plugin (its own repository, Revived-Mod). Not built yet: the multiplayer check. Design:
`docs/design.md`; build plans: `docs/plans/`. `python tools/plan_html.py <plan.md> <out.html>` turns a
plan into one HTML page for review.

## Building and testing

Needs Python with zig (`python -m pip install ziglang`) and Windows PowerShell.

```
powershell -File build.ps1          # build\shfolder.dll (the loader) and build\plugins\: HelloPlugin, SplashTest1, SplashTest2
powershell -File build.ps1 -Test    # also builds build\tests.exe and build\testgame\, and runs the tests
```

The tests need no game: they cover the ini and load-order files, the symbol map, the hook manager
(on test functions with known bytes), the events, the whole loading sequence (with test plugins),
the Plugins panel's state, layout and generated menu file, the safe start, and the built
`shfolder.dll` (the loader is inside it; it exports only its two shell32 forwarders). The panel's
drawing and the menu hooks need the game and are checked there.

The SplashTest plugins' videos are made with ffmpeg (`tools\make_splash_videos.ps1`, called by the
build); without ffmpeg the build warns and the two plugins have no video to show.

To try it in the game, on a developer machine:

```
powershell -File tools\dev-install.ps1      # copies shfolder.dll and HelloPlugin into the game folder
powershell -File tools\dev-uninstall.ps1    # removes exactly what dev-install put there
```

`dev-install.ps1` refuses to run when a `shfolder.dll` it didn't put there is already in the game
folder; both scripts take `-GameDir <path>`. While installed, the loader writes
`<game>\plugins\loader.log` at every start, and HelloPlugin logs every event into it.

## Files from plugins

A plugin's ini `Folder` (a subfolder of `plugins\`, laid out like the game folder) is served to the
game without any code: when the game asks for `Data\rules.xml`, `art\x.tga` or `masks.txt`, the
lowest plugin in the load order that has that file wins, ahead of the game's own mods and the
install. API 1.1 adds `file_use` (one file), `file_folder` (another folder), `file_generate` (a file
made when it is first asked for, from the version that would win without the plugin) and
`file_path` (which file the game reads). Programs and libraries (`.dll`, `.exe`, ...) and anything
under `plugins\` or `mods\` are never served. The service hooks `String::prepend_content_dir`, the
game's own lookup, and KernelBase's `CreateFileW`, `GetFileAttributes(Ex)W` and
`FindFirstFileExW` (with `FindNextFileW` / `FindClose`) for readers that skip it; a folder listing
shows a plugin's added files and folders (a new script appears in the game setup). Reads only:
writes go where the game aims them. With no plugin serving files the hooks are not installed.

`RON_EV_LIBRARY` (API 1.1) fires once each for the renderer (`d3dgl.dll`) and the sound library
(`dssl.dll`) right after the game loads them, with the hook calls open for addresses inside that
library while the callbacks run.

## The Plugins panel

Main menu, **Tools & Extras → Plugins** (directly under Steam Workshop). The button and the panel
copy the look of the Tools & Extras panel they join (window, textures, button size and spacing), so
a mod that restyles the menu keeps its style. The **Load Order** list
shows every plugin with its number, check box, name, version and a "new" or "failed" tag; the
**Description** box shows the selected one's version, author, description, file and status. Move Up
and Move Down change the order, the check box turns a plugin on or off, and **Apply and Restart**
writes `load-order.txt` and restarts the game the way the Steam Workshop window's Reload Assets does
(the restarted game skips the intro videos). Back drops unapplied changes.

Files the loader keeps in `<game>\plugins\`:

| File | What |
|---|---|
| `load-order.txt` | The order, top loads first; a leading `-` turns a plugin off |
| `loader.log` | This start's log |
| `.start-marker` | Written while plugins load, removed once the main menu is up. Found at start, it means the last start never reached the menu: that start loads no plugins (safe start) and the panel says so |
| `.cache\topmenu.xml` | The game's `topmenu.xml` with the Plugins button and panel added, written at every start and served to the game in place of its own |
| `.cache\gen\<plugin>\` | Files plugins generate (`file_generate`), emptied at every start |

## Test plugins

`SplashTest1` and `SplashTest2` (one source, `plugins/SplashTest/`) each replace the first intro
video, the SkyBox logo, with a picture saying "Plugin 1 Success" or "Plugin 2 Success". Both hook
the same function, so the one lower in the load order wins: install both, start the game from
Steam, swap them on the panel, Apply and Restart, then start the game again from Steam (a restart
skips the intros). Copy `SplashTestN.dll`, `SplashTestN.ini` and the `SplashTestN\` folder from
`build\plugins\`.

## Layout

| Path | What |
|---|---|
| `include/ron_plugin.h` | The plugin API, the only header a plugin includes. MIT license (its notice is in the file), so any plugin may copy it |
| `src/bootstrap/` | The start of `shfolder.dll`: loads at process start, hooks `Main::main`, runs the loader from there |
| `src/loader/` | The loader, built into `shfolder.dll`: load order, plugin ini files, symbol map, hook manager, events, API, safe start, the file service (`files.c` who serves what, `files_game.c` the hook on the game's own lookup, `files_win32.c` KernelBase's file calls and merged folder listings), the Plugins panel (`panel.c` state, `menufile.c` the generated menu file, `menu.c` and `menu_draw.c` the menu hooks and drawing, `game.c` the engine calls they use) |
| `src/common/` | Exe identity |
| `plugins/HelloPlugin/` | The example plugin |
| `plugins/SplashTest/` | The two load-order test plugins (one source) and their ini files |
| `tests/` | The test program, its test plugins and fixtures |
| `tools/` | Dev install scripts, `plan_html.py`, `make_splash_videos.ps1` |
