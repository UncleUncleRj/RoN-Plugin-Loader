# RoN Plugin Loader: developer notes

For people who build the loader or write plugins. The player-facing guide is the
[README](../README.md); the full design is [design.md](design.md), the build plans are in
[plans/](plans/).

The loader targets Rise of Nations: Extended Edition on Steam, `riseofnations.exe` build
`0x6674863F` (PE TimeDateStamp). It is one file, `shfolder.dll`, in the game folder. It loads plugin
DLLs from `<game>\plugins\<Name>.dll` (each with a `<Name>.ini`) in an order the player sets on a
Plugins panel in the main menu. Plugins can replace, add or generate game files, hook game
functions through one shared hook manager, and declare whether they change the simulation, so a
multiplayer lobby can check that every player runs the same set.

Status: the loader works (bootstrap, load order, plugin API 1.1, shared hooks, events, the file
service, the Plugins panel), checked in the game. RON Revived runs on it as a plugin (its own
repository, Revived-Mod). Not built yet: the multiplayer check (design section 10).
`python tools/plan_html.py <plan.md> <out.html>` turns a plan into one HTML page for review.

## Building and testing

Needs Python with zig (`python -m pip install ziglang`) and Windows PowerShell.

```
powershell -File build.ps1                 # build\shfolder.dll (the loader) and build\plugins\: HelloPlugin, SplashTest1, SplashTest2
powershell -File build.ps1 -Test           # also builds build\tests.exe and build\testgame\, and runs the tests
powershell -File build.ps1 -Test -Package  # tests, then the release zip (see "Version and releases")
powershell -File dist_build.ps1            # only shfolder.dll and the release zip, dist\RoN-Plugin-Loader-<version>.zip; no plugins, no tests
```

The tests need no game: they cover the ini and load-order files, the symbol map, the hook manager
(on test functions with known bytes), the events, the whole loading sequence (with test plugins),
the Plugins panel's state, layout and generated menu file, the safe start, and the built
`shfolder.dll` (the loader is inside it; it exports only its two shell32 forwarders; its version
resource matches `src/common/version.h`). The panel's drawing and the menu hooks need the game and
are checked there.

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

## Version and releases

The version lives in one place, [src/common/version.h](../src/common/version.h):
`LOADER_VERSION_MAJOR`, `LOADER_VERSION_MINOR`, `LOADER_VERSION_PATCH`, and the string
`LOADER_VERSION_STRING` made from them (`"1.1.0"`). Everything else reads it from there:

- the DLL's version resource (`src/bootstrap/version.rc`: `FILEVERSION`, `PRODUCTVERSION` and the
  `FileVersion` / `ProductVersion` strings), which an installer reads to tell this `shfolder.dll`
  from another mod's and to replace an older one (design section 11);
- the first line of `loader.log` (`[loader] RoN Plugin Loader 1.1.0`), so a player's log says which
  loader they run;
- the release zip's name.

MAJOR.MINOR are the plugin API's (`RON_API_MAJOR` / `RON_API_MINOR` in `include/ron_plugin.h`), and
the test `pkg_version_resource` fails when they differ: a release that changes the API raises the
API version and the loader's together. PATCH counts the releases of one API version and goes back
to 0 when the API version rises.

To make a release:

1. Raise the version in `src/common/version.h` (and, if the API changed, `RON_API_MINOR` or
   `RON_API_MAJOR` with it).
2. `powershell -File build.ps1 -Test -Package`. It stops before packaging if a test fails, and
   writes `dist\RoN-Plugin-Loader-<version>.zip`, which holds `shfolder.dll`, the player's
   installation steps (`release/installation.txt`) and the license (`LICENSE` as `LICENSE.txt`),
   and no example or test plugins. `./dist_build.ps1` writes the same zip without building the
   plugins or running the tests.
3. Upload that zip as the release's one download, with a tag such as `v1.1.0`.

## Writing a plugin

A plugin is an ordinary 32-bit Windows DLL with one C export, built with any compiler. It includes
[include/ron_plugin.h](../include/ron_plugin.h), the plugin API and the only header a plugin
needs. It is under the MIT license (its notice is in the file), so a plugin may copy it into its
own source. [plugins/HelloPlugin/](../plugins/HelloPlugin/) is a working example: it logs every
loader event and counts game frames.

Beside the DLL goes a same-name ini, which the Plugins panel reads without running the plugin's
code:

```ini
[Plugin]
Name=HelloPlugin
Version=1.0
Author=RoN Plugin Loader
Description=Logs every loader event and counts game frames.
AffectsSim=0
```

`Name` is shown on the panel (it defaults to the file name), `AffectsSim` (default 1) says whether
the plugin changes anything the simulation reads, and `Folder` (optional) names the plugin's own
subfolder of `plugins\`, which holds its files in the game folder's layout (next section). The
file formats, the API and the hook manager are described in [design.md](design.md), sections 5 to
7.

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

The loader also refuses to load plugins (and says so in the log) when the game build is unknown, or
when an old RON Revived `winmm.dll` is in the game folder.

## Test plugins

`SplashTest1` and `SplashTest2` (one source, `plugins/SplashTest/`) each replace the first intro
video, the SkyBox logo, with a picture saying "Plugin 1 Success" or "Plugin 2 Success". Both hook
the same function, so the one lower in the load order wins: install both, start the game from
Steam, swap them on the panel, Apply and Restart, then start the game again from Steam (a restart
skips the intros). Copy `SplashTestN.dll`, `SplashTestN.ini` and the `SplashTestN\` folder from
`build\plugins\`. They are for testing only and are not part of a release.

## Layout

| Path | What |
|---|---|
| `LICENSE` | Mozilla Public License 2.0, for everything but `include/ron_plugin.h` |
| `include/ron_plugin.h` | The plugin API, the only header a plugin includes. MIT license (its notice is in the file), so any plugin may copy it |
| `src/bootstrap/` | The start of `shfolder.dll`: loads at process start, hooks `Main::main`, runs the loader from there; `version.rc` its version resource |
| `src/loader/` | The loader, built into `shfolder.dll`: load order, plugin ini files, symbol map, hook manager, events, API, safe start, the file service (`files.c` who serves what, `files_game.c` the hook on the game's own lookup, `files_win32.c` KernelBase's file calls and merged folder listings), the Plugins panel (`panel.c` state, `menufile.c` the generated menu file, `menu.c` and `menu_draw.c` the menu hooks and drawing, `game.c` the engine calls they use) |
| `src/common/` | Exe identity; `version.h`, the loader's version |
| `plugins/HelloPlugin/` | The example plugin |
| `plugins/SplashTest/` | The two load-order test plugins (one source) and their ini files |
| `tests/` | The test program, its test plugins and fixtures |
| `release/` | `installation.txt`, the player's steps that go into the release zip |
| `build.ps1`, `dist_build.ps1` | The build (above); `dist_build.ps1` makes only the release zip, in `dist\` (git-ignored, like `build\`) |
| `tools/` | Dev install scripts, `plan_html.py`, `make_splash_videos.ps1` |
| `docs/` | These notes, the design, the build plans |
