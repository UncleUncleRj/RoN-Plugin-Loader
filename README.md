# RoN Plugin Loader

A plugin loader for Rise of Nations: Extended Edition (Steam, `riseofnations.exe` build `0x6674863F`).

It loads plugin DLLs from `<game>\plugins\<Name>.dll` (each with a `<Name>.ini`) in an order the player sets on a
Plugins panel in the main menu. Plugins can replace, add or generate game files, hook game
functions through one shared hook manager, and declare whether they change the simulation, so a
multiplayer lobby can check that every player runs the same set.

Status: the loader core works (bootstrap, load order, plugin API v1, shared hooks, events), proven in
a real game run. Not built yet: the file service, the Plugins panel, the multiplayer check. Design:
`docs/design.md`; build plans: `docs/plans/`. `python tools/plan_html.py <plan.md> <out.html>` turns a
plan into one HTML page for review.

## Building and testing

Needs Python with zig (`python -m pip install ziglang`) and Windows PowerShell.

```
powershell -File build.ps1          # build\shfolder.dll, build\RonPluginLoader.dll, build\plugins\HelloPlugin.dll + .ini
powershell -File build.ps1 -Test    # also builds build\tests.exe and build\testgame\, and runs the tests
```

The tests need no game: they cover the ini and load-order files, the symbol map, the hook manager
(on test functions with known bytes), the events and the whole loading sequence (with test plugins).

To try it in the game, on a developer machine:

```
powershell -File tools\dev-install.ps1      # copies the two DLLs and HelloPlugin into the game folder
powershell -File tools\dev-uninstall.ps1    # removes exactly what dev-install put there
```

`dev-install.ps1` refuses to run when a `shfolder.dll` or `RonPluginLoader.dll` it didn't put there is
already in the game folder; both scripts take `-GameDir <path>`. While installed, the loader writes
`<game>\plugins\loader.log` at every start, and HelloPlugin logs every event into it.

## Layout

| Path | What |
|---|---|
| `include/ron_plugin.h` | The plugin API, the only header a plugin includes |
| `src/bootstrap/` | `shfolder.dll`: loads at process start, hooks `Main::main`, loads the loader from there |
| `src/loader/` | `RonPluginLoader.dll`: load order, plugin ini files, symbol map, hook manager, events, API |
| `src/common/` | Exe identity, shared by both DLLs |
| `plugins/HelloPlugin/` | The example plugin |
| `tests/` | The test program, its test plugins and fixtures |
| `tools/` | Dev install scripts, `plan_html.py` |
