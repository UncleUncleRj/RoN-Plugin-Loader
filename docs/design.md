# RoN Plugin Loader: design

Status: draft for approval, 2026-11-24. Target: Rise of Nations: Extended Edition on Steam,
`riseofnations.exe` with PE TimeDateStamp `0x6674863F` (32-bit, image base `0x400000`, ASLR on).
Addresses below are virtual addresses at that base, named from the game's own symbol map
(`<game>\sbl\rise_z.map`). The engine facts come from the RoN Engine Atlas (pages "Mods", "Menus",
"Windows & interface files", "Sync", "Online", "Plugin loaders") and from the project notes
`notes/plugin-system.md` and `notes/dll-hooking.md` in the parent workspace.

## 1. Intent

Rise of Nations has no way to load code mods. Today one mod (RON Revived) ships as a fake
`winmm.dll` that hooks the game by itself, and a second code mod could not safely sit beside it.
This project is a **plugin loader**: one DLL that loads any number of plugin DLLs, in an order
the player chooses, and gives them shared, safe ways to change the game.

The model is SimCity 4's `Plugins` folder (drop a file in, it loads), improved where SC4 is weak:
SC4 loads in whatever order Windows lists the files (its community renames files `zzz_...` to
win), has no on/off switch except an `.ini` beside each DLL, has no way to see who overrides
whom, and loads newly dropped DLLs mid-game on a background thread.

## 2. Goals, non-goals, anti-goals

Goals
- Drop-in plugins: `<game>\plugins\<Name>\<Name>.dll`, each plugin in its own folder with its own
  files.
- A **load order the player sets** on a Plugins panel in the main menu. Plugins load from the top
  of the list to the bottom; when two plugins change the same thing, **the lower one wins**.
- Plugins can give the game files: ship them, point at a folder, or **generate them at run time**
  from the version that would have won without them.
- Plugins share game hooks safely: two plugins may hook the same function.
- Multiplayer safety: every player in a lobby can be checked for the same simulation-changing
  plugins, in the same order, with the same files.
- Plugins survive game updates better than hard-coded addresses: lookup by symbol name.

Non-goals
- Loading or unloading plugins while the game runs. Changing the order or the on/off state takes
  effect after a restart; the panel restarts the game for the player.
- Data-only mods. The game's own mod system (local `mods\` folders and Steam Workshop items)
  keeps doing that. Every plugin is a DLL.
- Plugins for the launcher (`patriots.exe`): the bootstrap does not load there.
- Distributing DLLs through the Steam Workshop.

Anti-goals (failure even if everything technically works)
- With no plugins installed, the game behaves differently from stock in any way other than the
  added Plugins button.
- A plugin's change disappears because another plugin overrode it, and nothing anywhere (panel,
  log) says so.
- Two players with different simulation-changing plugins start a match and desync with no warning.
- Code loads from anywhere the player did not put it.
- Anything blocks or does real work under the Windows loader lock (antivirus once deleted a test
  DLL that waited in `DllMain`).

## 3. Approaches considered

| Approach | Verdict |
|---|---|
| **A separate loader DLL, started by a `shfolder.dll` stand-in; plugins are DLLs in `<game>\plugins\`** | **Chosen.** Independent of any one mod; loads at process start; immune to the Windows compatibility layers that defeat the current `winmm.dll` method |
| Build the loader into RON Revived's DLL | Rejected: other modders could not use plugins without the whole RON Revived mod, and a bug in either takes down both |
| An outside launcher that starts the game and injects the DLLs | Rejected: players would have to start through it instead of Steam's Play button, and DLL injection is what antivirus watches for. It stays a fallback route for `patriots.exe` only |
| Register each plugin's folder as one of the game's own mods (`ModManager`) | Rejected: the plugins would be written into `mods\mod-status.txt`, appear on the Workshop screen and in the multiplayer mod list, and follow the game's drop-down-mod rules |
| Draw the Plugins panel with Dear ImGui over Direct3D 11 | Rejected: looks foreign, needs its own input filtering. The panel uses the game's own GUI code, as RON Revived's in-game panels already do |

Panel layout: two mockups were compared (a panel in the main-menu column vs. a full-screen
manager); **the panel in the main-menu column was chosen** (section 8).

## 4. Pieces

1. **Bootstrap**: `shfolder.dll` in the game folder. The game's network library
   `CrossplayNetLib.dll`, which the exe imports statically, imports `shfolder.dll`, so ours loads
   at process start, before Steam and before the crossplay threads start. It exports only
   `SHGetFolderPathA` and `SHGetFolderPathW`, forwarded to `shell32` through a `.def` (the system
   `shfolder.dll` is a thin wrapper around the same two shell32 functions). Measured: it loads
   even when an AppCompat layer makes the System32 `winmm.dll` win. `DllMain` only loads the
   loader DLL and installs one hook (below); nothing else runs under the loader lock.
2. **Loader**: `RonPluginLoader.dll` (name: builder's call) in the game folder. All work runs on
   the game's main thread from a hook at the entry of `Main::main` (`0x560CF0`), which runs
   before `Main::init` (Steam, at `+0x24`), the game's mod scan (`+0x29`) and any data load. A
   loader-lock-free start, and early enough that plugins can register files before the game
   reads any.
3. **Plugins**: ordinary 32-bit DLLs with one C export. Any compiler works.

The bcrypt warm-up that RON Revived does in its `DllMain` today (a start-up deadlock fix) moves
into the bootstrap: it must still happen before `PartyInitialize` (`Main::init+0x81`).

## 5. Plugins on disk and the load order

```
<game>\plugins\
    load-order.txt
    RON Revived\
        RON Revived.dll
        plugin.ini
        data\...  art\...  (anything the plugin uses)
    Better Minimap\
        Better Minimap.dll
        plugin.ini
```

- A plugin is a folder holding `<folder name>.dll` and `plugin.ini`. Folders without the DLL are
  ignored (and listed in the log).
- **`plugin.ini`** (my call; it lets the panel show every plugin, enabled or not, without running
  a disabled plugin's code): `[Plugin]` with `Name`, `Version`, `Author`, `Description`,
  `AffectsSim` (0/1). Reserved for the later rules feature: `Requires`, `LoadAfter`,
  `Incompatible` (comma-separated plugin names). The exact syntax is the builder's call; it must
  be plain text a modder writes by hand.
- **`load-order.txt`** (my call): UTF-8 text, one plugin folder name per line, top line loads
  first; a disabled plugin is kept in the list, marked (for example a leading `-`). The panel
  writes it; a player may also edit it by hand.
- **A newly found plugin** is appended at the bottom, **enabled** (my call, as SC4: putting a
  plugin in the folder means you want it), and shown with a "new" tag until the next restart.
- A listed plugin whose folder is gone is dropped from the file at the next save and noted in the
  log.
- The loader only ever loads DLLs from `<game>\plugins\<Name>\`. Never from `mods\` folders or
  Workshop items (joining a multiplayer game subscribes the player to the host's Workshop mod by
  itself, `ModSteamWorkshop::SubscribeModNow`), never by download. A plugin may load further DLLs
  from its own folder with `LoadLibrary` itself.

## 6. Plugin API

One export, for example `RonPluginMain(const RonApi *api, RonPluginInfo *out)`, called once at
load, in load order. A wrong API major version or a zero return unloads the plugin before it
does anything; the panel shows it as failed with the reason. The API is a versioned table of C
function pointers (no C++ ABI across the boundary). Version 1 provides:

- **Log**: one log file for the loader and every plugin, each line tagged with the plugin's name.
- **Game identity**: stock exe or CBP's `riseofnations_CBP.exe` (same TimeDateStamp; CBP's has an
  extra executable section `.cbp`), the ASLR delta, the game folder.
- **Symbol lookup**: `api->sym("<decorated name>")` from `sbl\rise_z.map`, checked against the
  map header's `Timestamp is 6674863f`, corrected for ASLR. Parse cost is unmeasured; if it is
  slow, cache an index keyed on the timestamp.
- **Hooks** (section 7).
- **Byte patches** (my call): address, expected stock bytes, new bytes; checked and recorded like
  hooks, so a plugin never writes game memory out of the loader's sight (RON Revived uses a
  byte-patch table today).
- **Files** (section 9).
- **Events**, all on the main thread:

| Event | Fired from | Notes |
|---|---|---|
| `pre_init` | `Main::main` entry (`0x560CF0`) | Plugins load here; register files and hooks |
| `post_init` | after `Main::init_menus` (`0x595BE0`) | Renderer, sound and menus are up |
| `match_built` | after `Setup::build_game` (`0x5AC190`) | Map, empires and scenario script ready |
| `sim_frame` | `Game::do_frame` (`0x591EF0`) | One simulation frame (1/15 s of game time) |
| `render_frame` | `Game::loop_render` (`0x5917E0`) | Once per drawn picture |
| `match_end` | `Game::close` (`0x594B80`) | |
| `shutdown` | `Main::shutdown` (`0x5966A0`) | |

Events fire in load order. Plugins must not call engine code from their own threads.

## 7. Shared hooks

All code patching goes through the loader, which keeps one record per patched address:

- The stock bytes are checked once against the plugin's signature; a mismatch fails that one hook
  (the plugin is told and can switch the feature off), never the game.
- Hooks on the same function **chain in load order**: each plugin's "original" is the hook of
  the plugin above it, ending at the game's code. So **the lowest plugin's hook runs first** and
  can decide whether the ones above it run at all, which matches "the lower one wins".
- Forms offered: inline (5-byte `jmp` plus trampoline with relocated stolen bytes), call site
  (`E8 rel32`, no stolen bytes), vtable slot, import address table.
- Hooks and byte patches are installed only while plugins load (during `pre_init`, before the game
  starts its other threads) and are never removed (my call). A plugin that wants an in-game on/off
  switch keeps its hook and checks its own flag. So no patch ever happens while game threads run,
  and the chain never needs a link removed from the middle.
- Every shared address is logged with the plugins involved, and recorded for the later conflict
  view.
- RON Revived's hook table (`g_hooks[]`: name, address, signature bytes, stolen-byte count) and
  its `hook_jmp5`/`hook_trampoline` are the starting point; today they write a `jmp` without
  checking what is already there, which is the bug this section removes.

## 8. The Plugins panel

Opened by a new **Plugins** button in the main menu. It is a panel in the main-menu column, in the
same place and frame as the game's Solo, Multiplayer and Options panels (`topmenu.xml` panels at
`left="30" top="40" width="548" height="768"`), over the moving menu background.

```
+------------------------------------+
| Plugins                            |
| Top loads first. Lower plugins win |
| when two change the same thing.    |
|  1 [x] RON Revived   2026.11.24 ^v |
|  2 [x] Nations Pack  1.0  (!)   ^v |
|  3 [x] CBP Balance   a11        ^v |   <- selected: details unfold below
|        by MHLoppy. Rebalances units, techs and nations.
|        Changes 24 data files and hooks 3 game functions.
|  4 [x] Better Minimap 1.2 (new) ^v |
|  5 [ ] Debug Tools   0.3        ^v |
| Order changed. The game restarts   |
| to apply it.                       |
| [Apply and restart]  [Back]        |
+------------------------------------+
```

- The plugin names in the sketch are examples; no CBP plugin is part of this project.
- Each row: load-order number, on/off box, name, version, up/down arrows. Selecting a row unfolds
  its details: author, description, how many files it serves and game functions it hooks (from
  the previous run's records), and why it failed to load, if it did.
- Tags: "new"; "failed"; later, rule warnings such as "move below CBP Balance".
- A notice appears as soon as the list differs from what is running.
- **Apply and restart** writes `load-order.txt` and restarts the game. **Back** discards
  unapplied changes (my call).
- Built with the game's own interface code, the way RON Revived's panels are: a window object of
  the game's interface library, drawing through `IFaceRenderManager` (global at `0x00EBD1C0`,
  `IFaceRenderManager::add` `0x51F3D0`), text through `Buffer::write_l`/`write_cent_l`, frames
  through `IFaceRenderManager::draw_border`, the mouse through `Window::get_mouse_pos`, and the
  game's click sound. Use only textures the menu files already declare (a texture missing from the
  interface files draws as a white square).
- The button: main-menu buttons come from `Data\topmenu.xml` and act by name in
  `TopMenu::check_result` (`0x59B1E0`); `TopMenu::sub_menu` (`0x59C030`) opens a panel. The
  loader adds the button by serving a generated `topmenu.xml` through its own file service, and
  hooks `check_result` for the name `plugins`. Whether the panel is a new `topmenu.xml` panel
  element or a window the loader builds itself is the builder's call.
- Restarting: write the file, start a new game process the way the current one was started, then
  quit through the game's own exit path (so settings are saved). Whether to relaunch the exe
  directly or through Steam (`steam://rungameid/287450`) must be tested; the new process must not
  start while the old one still holds the single-instance lock, if there is one.

## 9. Files from plugins

A plugin tells the loader about files; it does not move files into the game folder.

- **Use this file**: `api->file_use("data\\unitrules.xml", "<path>")`.
- **Look in this folder**: `api->file_folder("<path>")`. Any file in it, at the same relative
  path as the game's own (`art\...`, `data\...`, `sounds\...`), replaces the game's copy.
- **Generate it**: `api->file_generate("data\\unitrules.xml", callback)`. When the file is first
  asked for, the callback receives the path of the version that would win without this plugin
  (from the plugins above it, else the game's mods, else the base game) and writes its own
  version. This is how two plugins both change `unitrules.xml` without losing each other's
  changes: the lower plugin edits the upper plugin's result.

Resolution, for one requested name:
1. Start from the path the game would use on its own: the original `String::prepend_content_dir`
   (`0xA1D690`, 60 calls in 36 functions, among them `File::open`, `XML::init`, `Text::open`,
   `Filemap::open`, `ImageIO::load`), which applies the game's mods and the base install.
2. Apply every plugin that claims the name, top to bottom: "use" and "folder" replace the path,
   "generate" transforms it.
3. Remember the result for the rest of the run.

Rules:
- Hook `String::prepend_content_dir`. Workshop items prove the game accepts absolute paths from
  this function for data files.
- Generated files go to a cache folder outside the game install that needs no admin rights, one
  subfolder per plugin, emptied at every start, so a changed plugin never leaves a stale file. The
  folder stays readable so modders can open what a plugin produced.
- Readers that bypass `prepend_content_dir` need a fallback: the renderer DLL most likely reads
  its shader files (`shaders\*.hlsl`) itself (not checked). RON Revived's `CreateFileW` redirect
  (`src/override.c`) is the pattern.
- **Added files**, not only replaced ones (a new map script, a new scenario), must also appear
  when the game lists a folder. RON Revived hooks `FindFirstFileExW` for this; check which game
  listings need it.
- Generators that feed the simulation (rules, units, techs, balance) must give the same output on
  every machine: no clock, no random numbers, no local settings.
- Names are matched literally (my call). Under CBP's `riseofnations_CBP.exe` the game asks for
  CBP's renamed files (`CBPa11_unitrules.xml` and so on, through CBP's own string table); a plugin
  that wants to change those registers CBP's names itself, using the game identity from the API.
  Translating names automatically is a later option. Check during the build that the main menu
  still reads `topmenu.xml` under CBP (it is not one of the five names CBP's exe renames, but CBP's
  string table might point it elsewhere).

## 10. Multiplayer: every player checked before the match

User decision (2026-11-24): before a match starts, every player is checked for mod and version
information, so mismatches are caught before they can desync. Another developer is working on
multiplayer features; agree with them on the channel below so both use one.

- **What each machine publishes** whenever the loader is installed, even with no plugins (my call:
  publishing data changes nothing in play): the loader's version; the game identity (stock or CBP
  exe); the game's own selected mod (name, Workshop id); and every enabled plugin in load order with
  name, version and `AffectsSim`. For simulation-changing plugins also the DLL hash and the hash of
  every file they serve or generate.
- **Comparison, on every machine:** simulation-changing plugins must match exactly (same plugins,
  same order, same versions and hashes), and so must the game identity. Differences in plugins that
  don't change the simulation are shown but don't block (my call).
- **A player who publishes nothing** has no loader, which counts as no plugins: if anyone else has
  simulation-changing plugins, that player is a mismatch.
- **While anyone mismatches, the host's Start button stays disabled**, and every player sees on the
  setup screen who differs and how. The setup screen already disables Start through
  `SetupWin::start_button_disable` (`0x5AE570`), which finds the button by its name `start` in the
  players panel.
- **Channel.** The game's lobby keys are written by the host, so they can't carry each player's
  own data. RON Revived's unit-ban feature shows the host side: `MultiplayerManager::LobbySetKey`
  (`0xA30B70`) after `ConnectionData::send_game` (`0x94E200`), read back in
  `GameSpy::UpdateLobbyFromDTO` (`0x962C70`). The per-player side (lobby member data, or a message
  over the game's own network) is still to be found: reverse-engineering work that belongs to the
  first version, shared with the multiplayer developer.
- Each plugin declares `AffectsSim` in `plugin.ini` (and again at load).
- The game's own mod checksum does not help: nothing compares the `mod_checksum` lobby key, and
  `GameMod::compute_checksum` adds up only a mod's `.xml` files.
- Backstop: the game checksums the whole simulation, rules data included, every turn, so a drift
  that gets past the check shows up as an out-of-sync, not as silent corruption.
- Plugin-added script functions are numbered in the order they are added; the shared load order
  keeps the numbering equal on every machine.

## 11. First version and later

First version
1. Bootstrap (`shfolder.dll`) and loader at `Main::main`; `load-order.txt`; `plugin.ini`.
2. API v1: log, game identity, symbol lookup, events, hooks with chaining.
3. File service: use, folder, generate; the `CreateFileW`/`FindFirstFileExW` fallback where needed.
4. Plugins panel (on/off, order, details, "new"/"failed" tags, apply and restart).
5. The multiplayer check of every player before the match (section 10).
6. A hello-world plugin, tested unattended with the game's `autostart.ini` and the workspace's
   debug tool.

Release (user decision, 2026-11-24): the loader first reaches players with the next RON Revived
version, in which RON Revived is itself a plugin (that move is its own project in the mod's
repository: hooks onto the shared hook manager, `override.c` replaced by the file service, the
`winmm.dll` proxy retired). RON Revived's install script installs the bootstrap and the loader when
it doesn't find them, or finds an older version (my call: "older" too), and tells players to
uninstall old RON Revived versions.

Later
- Declared rules (`Requires`, `LoadAfter`, `Incompatible`), warnings on the panel and a "sort
  automatically" button that fixes the order with the fewest moves.
- Conflict view: for each plugin, the files and game functions it shares with others, and who wins.
- An XML helper that merges entries (one unit, one tech) in load order, built on "generate".
- Services: console commands (hook `ConsoleWin::parse_cmd`, `0x7D6470`), script functions
  (after `ScenarioFuncSet::init_funcs`), headers generated from `rise.pdb` for plugin authors.
- A list of trusted plugin hashes.
- Saves and replays record the list of simulation-changing plugins (a file beside the save keeps
  the save format unchanged) and warn when loaded with a different list (my call to leave this out
  of the first version; until then a replay played without its plugins diverges silently).
- The in-game debug overlay discussed earlier becomes a plugin.

## 12. Constraints

- 32-bit x86 only; the exe's TimeDateStamp must be `0x6674863F` (stock or CBP), otherwise the
  loader loads no plugins and says so.
- ASLR: every address is applied with the load delta.
- Engine calls only on the main thread; nothing but loading the loader under the loader lock.
- Toolchain for the loader: zig cc `-target x86-windows-gnu`, as RON Revived (builder may change
  it with reason). Plugins: any compiler that makes a 32-bit DLL with a C export.
- Old RON Revived versions install a `winmm.dll` that hooks the game by itself, before the loader
  starts. If the loader finds one (a `winmm.dll` in the game folder that is not the system's, or its
  hooks' `jmp` bytes at known addresses), it loads no plugins and says why on the Plugins panel and
  in the log, rather than patching on top of it (my call). The installer's warning should make this
  rare.

## 13. Left to the builder

- File and export names, the `plugin.ini` and `load-order.txt` syntax, the cache folder location.
- How the panel window is built (topmenu panel element or a window built in code).
- How the restart relaunches the game, after testing.
- Log location and format.

## 14. Still open (to verify during the build)

- Online multiplayer through the `shfolder.dll` proxy is untested (needs two machines);
  `CrossplayNetLib` really calls `SHGetFolderPathW`, so the forward must be exact.
- Which game readers bypass `String::prepend_content_dir`, and which folder listings need added
  files merged in.
- How long parsing `rise_z.map` takes at start-up.
- The per-player lobby channel (section 10), and whether LAN or direct-connect games use the same
  lobby path.
- That disabling Start through `SetupWin::start_button_disable` holds while the lobby updates.
- Plugin state in save games: `Game::walk_data` (`0x589600`) serves save, load and the checksum,
  but adding data there changes the save format.
