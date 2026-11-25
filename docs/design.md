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
- Drop-in plugins: `<game>\plugins\<Name>.dll` with a same-name `.ini`; a plugin's files ship in
  a subfolder of `plugins\` that the ini names.
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
- Mods made only of files, with no DLL (art or data packages). The game's own mod system (local
  `mods\` folders and Steam Workshop items) keeps loading those. Every plugin is a DLL. A plugin
  with no files of its own, such as a developer-tools DLL that changes the game's data in code, is
  an ordinary plugin: a DLL and its ini, with no `Folder` (user, 2026-11-24).
- Plugins for the launcher (`patriots.exe`): the loader does not run there.
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
| **A loader DLL of its own, one file named `shfolder.dll` (a stand-in for the system file); plugins are DLLs in `<game>\plugins\`** | **Chosen.** Independent of any one mod; loads at process start; immune to the Windows compatibility layers that defeat the current `winmm.dll` method |
| A small `shfolder.dll` stand-in that loads a separate `RonPluginLoader.dll` | Built first, then merged into one file (user decision, 2026-11-24): one file to install, and no starter and loader of different versions. Cost: the loader must carry a system file's name, and a new start route later (a game update that drops the import, or the launcher) means building the whole loader under another name instead of swapping a small starter |
| Build the loader into RON Revived's DLL | Rejected: other modders could not use plugins without the whole RON Revived mod, and a bug in either takes down both |
| An outside launcher that starts the game and injects the DLLs | Rejected: players would have to start through it instead of Steam's Play button, and DLL injection is what antivirus watches for. It stays a fallback route for `patriots.exe` only |
| Register each plugin's folder as one of the game's own mods (`ModManager`) | Rejected: the plugins would be written into `mods\mod-status.txt`, appear on the Workshop screen and in the multiplayer mod list, and follow the game's drop-down-mod rules |
| Draw the Plugins panel with Dear ImGui over Direct3D 11 | Rejected: looks foreign, needs its own input filtering. The panel uses the game's own GUI code, as RON Revived's in-game panels already do |

Panel layout: two mockups were compared (a panel in the main-menu column vs. a full-screen
manager); **the panel in the main-menu column was chosen** (section 8).

## 4. Pieces

1. **`shfolder.dll`**, the loader's only file (user decision, 2026-11-24), in the game folder. The
   game's network library `CrossplayNetLib.dll`, which the exe imports statically, imports
   `shfolder.dll`, so ours loads at process start, before Steam and before the crossplay threads start. It exports only
   `SHGetFolderPathA` and `SHGetFolderPathW`, forwarded to `shell32` through a `.def` (the system
   `shfolder.dll` is a thin wrapper around the same two shell32 functions). Measured: it loads
   even when an AppCompat layer makes the System32 `winmm.dll` win. `DllMain` only checks the
   exe, warms up bcrypt (below) and patches a `jmp` at `Main::main`; the loader runs from that
   hook. Nothing else runs under the loader lock (corrected 2026-11-24: the first draft had
   `DllMain` load the loader, which would call `LoadLibrary` under the loader lock).
2. **Loader**: built into the same `shfolder.dll`, which exports nothing else. All work runs on
   the game's main thread from a hook at the entry of `Main::main` (`0x560CF0`), which runs
   before `Main::init` (Steam, at `+0x24`), the game's mod scan (`+0x29`) and any data load. A
   loader-lock-free start, and early enough that plugins can register files before the game
   reads any. The hook is a stub in assembly that keeps every register, the flags and the stack:
   `WinMain` passes the program's `HINSTANCE` to `Main::main` in `ECX` (`0x5609CD`; stored at
   `0x560D11`), not on the stack as the decorated name says. The first build used a C detour,
   which changed `ECX`; the game then kept `HINSTANCE` 0, `d3dgl.dll` (which loads the cursors
   from the exe's resources with it, `graphsys+0x170`) had no cursor, and the mouse pointer was
   invisible in the menus (found by the user, 2026-11-24; fixed with the stub and a test).
3. **Plugins**: ordinary 32-bit DLLs with one C export. Any compiler works.

The bcrypt warm-up that RON Revived does in its `DllMain` today (a start-up deadlock fix) moves
into `shfolder.dll`'s `DllMain`: it must still happen before `PartyInitialize` (`Main::init+0x81`).

## 5. Plugins on disk and the load order

User decision (2026-11-24): the loader looks for DLLs, not folders. A plugin is a DLL in
`plugins\` with an `.ini` of the same name beside it. The ini names the plugin's subfolder, which
the modder ships already filled with the plugin's files in the base game's layout.

```
<game>\plugins\
    load-order.txt
    RON Revived.dll
    RON Revived.ini          (Folder=RON Revived)
    RON Revived\
        data\...  art\...  sounds\...   modded files, laid out like the base game's
        settings.json                   anything else the plugin writes for itself
    Better Minimap.dll
    Better Minimap.ini       (no Folder: a code-only plugin)
```

- A plugin is `plugins\<Name>.dll` with `plugins\<Name>.ini`. `<Name>`, the file name without
  `.dll`, is the plugin's id in `load-order.txt`. A DLL without a same-name ini is not a plugin
  and is left alone, so a plugin may keep helper DLLs there; the log lists them. Subfolders are
  never searched for plugins.
- **`<Name>.ini`** (it lets the panel show every plugin, enabled or not, without running a
  disabled plugin's code): `[Plugin]` with `Name` (shown on the panel; defaults to the id),
  `Version`, `Author`, `Description`, `AffectsSim` (0/1, default 1) and `Folder`. Reserved for the
  later rules feature: `Requires`, `LoadAfter`, `Incompatible` (comma-separated plugin ids). The
  exact syntax is the builder's call; it must be plain text a modder writes by hand.
- **`Folder`** names the plugin's subfolder of `plugins\`. It is optional: a plugin with no files
  (code only, or one that generates all its data) leaves it out. My calls: it must be a single
  folder name (no `\`, `/`, `:` or `..`), the folder must exist, and a folder belongs to one
  plugin only. The first enabled plugin in the load order that names it keeps it, and a later one
  fails to load, with the reason.
- **The standard:** the modder ships the modded files already in that subfolder, in the base
  game's layout, and the loader serves them (section 9) without any code. Plugins may also write
  files of their own (settings such as `.json` files, caches, anything they need), wherever they
  need them; there are no hard rails. Inside the plugin's folder such a file is never served by
  accident, because the game only asks for paths of its own.
- **`load-order.txt`** (my call): UTF-8 text, one plugin id per line, top line loads first; a
  disabled plugin is kept in the list, marked (for example a leading `-`). The panel writes it; a
  player may also edit it by hand.
- **A newly found plugin** is appended at the bottom, **enabled** (my call, as SC4: putting a
  plugin in the folder means you want it), and shown with a "new" tag until the next restart.
- A listed plugin whose DLL is gone is dropped from the file at the next save and noted in the
  log.
- The loader only ever loads plugin DLLs from `<game>\plugins\`. Never from `mods\` folders or
  Workshop items (joining a multiplayer game subscribes the player to the host's Workshop mod by
  itself, `ModSteamWorkshop::SubscribeModNow`), never by download. A plugin may load further DLLs
  itself, for example from its own folder.

## 6. Plugin API

One export, for example `RonPluginMain(const RonApi *api, RonPluginInfo *out)`, called once at
load, in load order. A wrong API major version or a zero return unloads the plugin before it
does anything; the panel shows it as failed with the reason. The API is a versioned table of C
function pointers (no C++ ABI across the boundary). Version 1 provides:

- **Log**: one log file for the loader and every plugin, each line tagged with the plugin's name.
- **Game identity**: stock exe or CBP's `riseofnations_CBP.exe` (same TimeDateStamp; CBP's has an
  extra executable section `.cbp`), the ASLR delta, the game folder.
- **Symbol lookup**: `api->sym("<decorated name>")` from `sbl\rise_z.map`, checked against the
  map header's `Timestamp is 6674863f`, corrected for ASLR. Measured in the first game runs
  (2026-11-24): 111,091 names in 34-41 ms at start-up, so no cached index is needed.
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
| `sim_frame` | the only call of `Game::do_frame` (`0x591EF0`), at `Game::loop+0x14E` (`0x5916BE`) | One simulation frame (1/15 s of game time). A call-site hook, so a tool that hooks `do_frame`'s entry (a Frida agent does) doesn't silence it |
| `render_frame` | `Game::loop_render` (`0x5917E0`) | Once per drawn picture of a match, from its loading screen on. Not in the menus: in 12 s on the main menu it never fired (2026-11-24) |
| `match_end` | `Game::close` (`0x594B80`) | Once per match: the first `Game::close` after `match_built`. The game also calls `Game::close` to clear before it builds a match, and twice when a match ends (`Game::run`) |
| `shutdown` | the return of `Main::run_game_loop`, called once at `Main::main+0x913` (`0x561603`) | `Main::shutdown` (`0x5966A0`) is not reached on a console quit: a trace showed the process ending inside `SteamAuthentication` (Get, CancelSteamAuthTicket) first, in a run with Steam kept out |

Events fire in load order. Plugins must not call engine code from their own threads.

## 7. Shared hooks

All code patching goes through the loader, which keeps one record per patched address:

- The stock bytes are checked once against the plugin's signature; a mismatch fails that one hook
  (the plugin is told and can switch the feature off), never the game.
- Hooks on the same function **chain in load order**: each plugin's "original" is the hook of
  the plugin above it, ending at the game's code. So **the lowest plugin's hook runs first** and
  can decide whether the ones above it run at all, which matches "the lower one wins".
- Forms offered: inline (5-byte `jmp` plus a trampoline holding the stolen bytes, copied as they
  are: they must hold no relative branch or call, as the header says; a length decoder that
  relocates or refuses them is a later option), call site (`E8 rel32`, no stolen bytes), vtable
  slot, import address table. A plugin may chain onto an inline hook with a longer signature than
  the first one's: the extra bytes are checked against memory.
- An address that is not committed, readable memory fails that one request (`RON_E_ARG`), never
  the game.
- Hooks and byte patches are installed only inside `RonPluginMain` (before the game starts its
  other threads) and are never removed (my call). Event registration stays open through
  `pre_init`. A hook made from a `pre_init` callback would land on top of every chain and run
  before lower plugins' hooks, so it is refused (found in review, 2026-11-24). A plugin that wants an in-game on/off
  switch keeps its hook and checks its own flag. So no patch ever happens while game threads run,
  and the chain never needs a link removed from the middle.
- Every shared address is logged with the plugins involved, and recorded for the later conflict
  view.
- RON Revived's hook table (`g_hooks[]`: name, address, signature bytes, stolen-byte count) and
  its `hook_jmp5`/`hook_trampoline` are the starting point; today they write a `jmp` without
  checking what is already there, which is the bug this section removes.

## 8. The Plugins panel

User decisions (2026-11-24, second round): the button is **Plugins**, in the main menu's **Tools &
Extras** panel, directly under **Steam Workshop** (`Data\topmenu.xml`, panel `OPTIONS`, button
`workshop`). It opens the Plugins panel in the main-menu column (layout A, as chosen before), styled
after the game's Steam Workshop window (`Data\workshopwin.xml`, `WorkshopWin`), with this design's
own features: details, tags, the change notice, Apply and Restart.

The panel is the loader's own (part of `shfolder.dll`, not a plugin) and shows even with no plugin
installed. Same place and frame as the game's Solo, Multiplayer and Tools & Extras panels
(`left="30" top="40" width="548" height="768"`, buttons at `x="41"`, 36 apart), over the moving
menu background.

User decisions (2026-11-25, after the first look in the game): the panel gets a dark background,
and, like the Steam Workshop window's sections, the selected plugin's information moves out of the
list into its own **Description** box, which also shows the plugin's exact file name.

```
+------------------------------------------+
| Plugins                                  |
| ######## dark pane (Workshop 0xD2000000) |
|               Load Order                 |
| +--------------------------------------+ |
| | Top loads first. Lower plugins win   | |
| | when two change the same thing.      | |
| |  1 [x] RON Revived     2026.11.24    | |
| |  2 [x] Nations Pack    1.0   failed  | |
| |  3 [x] Better Minimap  1.2   new     | |  <- selected
| |  4 [ ] Debug Tools     0.3           | |
| +--------------------------------------+ |
|               Description                |
| +--------------------------------------+ |
| | Better Minimap                       | |
| | Version 1.2, by Someone              | |
| | Shows ... (description, 2 lines)     | |
| | File: BetterMinimap.dll              | |
| | Hooks 3 game functions.              | |
| +--------------------------------------+ |
| Not applied yet. Apply and Restart to    |
| use this list.                           |
| [ Move Up                              ] |
| [ Move Down                            ] |
| [ Apply and Restart                    ] |
| [ Back                                 ] |
+------------------------------------------+
```

- The plugin names in the sketch are examples; no CBP plugin is part of this project.
- Each row: load-order number, check box, name, version, and a tag: "new" (appended to the order
  at this start) or "failed" (enabled but did not load). Clicking a row selects it; clicking its
  check box turns that plugin on or off.
- The **Description** box shows the selected plugin: its name; version and author, description
  (2 lines, "..." when cut) from its ini; its file (`<id>.dll`); and its status: how many game
  functions it hooked in this run, or why it failed, or that it is off or not loaded in a safe
  start. With nothing selected it says "Click a plugin to see its description." How many files it
  serves joins them once the file service exists.
- Look of the sections (my calls, after the Workshop: `WorkshopWin::on_redraw` fills its pane
  black at alpha `0xD2`, `PaneModManager::on_redraw` writes its section titles with
  `FontFamily::get(14, 1)` centred): one dark pane from the panel's first row to above its
  buttons, a centred title and a 1-px grey outline per section, the notice at the pane's foot.
- **Move Up** and **Move Down** move the selected plugin one place (the Steam Workshop window's
  Priority Up and Priority Down; my call, in place of the arrows on every row of the first sketch).
- When more plugins exist than rows fit, the list scrolls (mouse wheel; more is the builder's call).
- A notice appears as soon as the list differs from what is running (the order and on/off state
  read at this start).
- **Apply and Restart** writes `load-order.txt` and restarts the game; it does nothing while
  nothing changed. **Back** discards unapplied changes (my call) and returns to Tools & Extras.
- With no plugin installed the list says so and names the folder: "No plugins installed. Put a
  plugin's .dll and .ini in the plugins folder."
- **Safe start** (user decision, 2026-11-24: automatic). If a start never reached the main menu
  (a plugin crashed or hung the game first), the next start loads no plugins, so the panel stays
  reachable. A marker file in `plugins\` (name: builder's call) is written before plugins load,
  names each plugin while it loads (my call: so the culprit can be named), and is removed once the
  main menu has drawn its first frame (so after every plugin's `post_init` callback and menu
  hooks; at `post_init` only when the panel's redraw hook could not be installed, and at once
  when neither hook could). Found at start, it means the last start stopped early: the loader
  reads the order and the ini files as usual but loads no plugin, logs why, and the panel says so
  at the top: "The last start did not reach the menu, so this start runs without plugins. Turn off
  the plugin that stopped it, then Apply and Restart." The plugin named in the marker is tagged
  "failed" with "the game stopped while it loaded". In a safe start, Apply and Restart always
  restarts, changed list or not (my call), and the marker is gone by then, so that start is normal.
  Closing the game during loading counts as a stop too; one plugin-free start follows (accepted).
- The loader's texts are English (my call); localisation is later.
- Look: the Steam Workshop window's pieces. Its check boxes (`check_on` / `check_off` from
  `art\wrapper_pieces_02.tga`), its list rows and text, the game's own menu buttons and frame.
  Use only textures the menu files declare (a texture missing from the interface files draws as a
  white square): the generated panel declares the ones it uses, as `workshopwin.xml` does.

How it is built (my calls, 2026-11-24):
- **The button and the panel come from a generated `topmenu.xml`.** The game names the file in one
  function, `GetTopMenuXMLFile` (`0x59F7C0`, returns the String `topmenu.xml`), called by
  `TopMenu::init`, `TopMenu::reinit`, `TopMenu::init_logo`, `TopMenu::init_light` and
  `TopMenuSelection::exec` (which reads the file each time a panel opens). At start the loader
  reads `topmenu.xml` as the game would get it (so a version another mod serves counts), adds
  `<BUTTON name="plugins" title="Plugins" .../>` after the `workshop` button of the full game's
  `OPTIONS` panel (not the `TOPMENU_TRIAL` copy) and a `PLUGINS` panel element, writes the result
  to a cache file under `plugins\`, and hooks `GetTopMenuXMLFile` to return it. Without a
  `workshop` button the Plugins button goes above `back`; if anything fails, the game runs with
  its own file and no button (logged). The file service takes this over later.
- **The panel is a real menu panel.** `TopMenu::sub_menu` (`0x59C030`) runs a panel by name in a
  loop: `TopMenuSelection::exec` waits for a button, `TopMenu::check_result` (`0x59B1E0`) turns its
  name into a result, -1 stays in the panel. A hook on `check_result` answers `plugins` with
  `sub_menu("PLUGINS")` and the panel's own buttons, as RON Revived's Survival button and PLAY
  panel do (`survival.c`). So the game supplies the frame, background, slide-in, button look,
  hover and click sound; Move Up, Move Down, Apply and Restart and Back are ordinary panel buttons.
- **Only the plugin list is the loader's drawing**, in the panel's free area, through the game's
  interface code: textures and frames through `IFaceRenderManager` (global at `0x00EBD1C0`,
  `IFaceRenderManager::add` `0x51F3D0`, `draw_border`), text through `Buffer::write_l` /
  `write_cent_l`, row style as `WorkshopWin::on_listbox_entry_redraw` (`0x6ADAD0`). Clicks and the
  wheel come from a hook on the panel's input. Which redraw and input functions to hook is the
  builder's to find (`TopMenu::on_redraw`, `TopMenuSelection`'s handlers; RON Revived's
  `menuhelp.c` hooks `TopMenuSelection::on_redraw`). Found while writing this: `TopMenuSelection`'s
  table of virtual functions (`0xB3F3F4`) holds its `on_redraw` in slot 46 (`+0xB8`) and the
  `Window` defaults for `on_left_click` (slot 54, `+0xD8`), `on_left_down` (55), `on_mouse_move`
  (52) and `on_mouse_wheel` (69, `+0x114`), so slot hooks there reach only the menu panels.
- **No plugin can take the panel away** (my call, 2026-11-24): it is the player's way to turn a
  plugin off. The loader installs its own menu hooks after every plugin has loaded (before the hook
  manager locks), so its links head their chains and see the `plugins` button first; every other
  button passes on down the chain. Its `topmenu.xml` change is made to whatever file the chain
  below its `GetTopMenuXMLFile` link returns, so it lands on top of any plugin's version.
- **Restarting uses the game's own restart** (found 2026-11-24). The Steam Workshop window's Reload
  Assets button restarts the game: `WorkshopWin::reloadAssets` (`0x6AC3E0`) sets
  `s_selfRestartDesired` (`0xCAA314`) to 1, `game_type` (`0xCAB374`) to `0x15`, `quit`
  (`0xE335C4`) to 1 and `Game+0x558` to 0. The game then leaves through its normal exit path, and
  after `Main::main` returns, `handleSelfRestartAndFinalShutdown` (`0x560A40`) creates the mutex
  `RiseOfNationsRestart`, starts the exe again with the same command line plus ` +skipIntro`
  (`CreateProcessW`), waits 500 ms and releases the mutex; the new process's `WinMain` waits up to
  8 s on that mutex before it starts. Apply and Restart writes `load-order.txt`, then does what
  `reloadAssets` does. The new process is not started through Steam, as with Reload Assets;
  whether that matters (Steam overlay, achievements, multiplayer) is to be watched in a run.

## 9. Files from plugins

A plugin tells the loader about files; it does not move files into the game folder.

- **Its ini's `Folder`** is served without any code, as if the plugin had called
  `file_folder` on it, at the plugin's place in the load order.

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
- Only names inside the game's content folders are served (`art\`, `data\`, `sounds\` and the
  game's other folders). Top-level names never are: the game also looks up its renderer, sound
  and network libraries (`d3dgl.dll`, `dssl.dll`, `CrossplayNetLib.dll`) through
  `prepend_content_dir`, and no plugin folder may replace those (my call).
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
- Each plugin declares `AffectsSim` in its `.ini` (and again at load). A plugin that changes any
  data the simulation reads (rules, units, techs) counts, even with no files of its own; one that
  only reads or shows things can declare 0.
- The game's own mod checksum does not help: nothing compares the `mod_checksum` lobby key, and
  `GameMod::compute_checksum` adds up only a mod's `.xml` files.
- Backstop: the game checksums the whole simulation, rules data included, every turn, so a drift
  that gets past the check shows up as an out-of-sync, not as silent corruption.
- Plugin-added script functions are numbered in the order they are added; the shared load order
  keeps the numbering equal on every machine.

## 11. First version and later

First version
1. The loader (`shfolder.dll`, one file), started at `Main::main`; `load-order.txt`; the plugin
   `.ini` files.
2. API v1: log, game identity, symbol lookup, events, hooks with chaining.
3. File service: use, folder, generate; the `CreateFileW`/`FindFirstFileExW` fallback where needed.
4. Plugins panel under Tools & Extras (on/off, order, details, "new"/"failed" tags, apply and
   restart, safe start). Built and checked in the game 2026-11-25, with the load-order test
   plugins SplashTest1 and SplashTest2 (each replaces the first intro video; the lower one wins).
   Items 1, 2 and 6 were built 2026-11-24.
5. The multiplayer check of every player before the match (section 10).
6. A hello-world plugin, tested unattended with the game's `autostart.ini` and the workspace's
   debug tool.

Release (user decision, 2026-11-24): the loader first reaches players with the next RON Revived
version, in which RON Revived is itself a plugin (that move is its own project in the mod's
repository: hooks onto the shared hook manager, `override.c` replaced by the file service, the
`winmm.dll` proxy retired). RON Revived's install script installs the loader (`shfolder.dll`) when
it doesn't find it, or finds an older version (my call: "older" too), and tells players to
uninstall old RON Revived versions. A file name can't tell ours from another mod's `shfolder.dll`,
so the script needs a mark inside the file, such as a version resource (still to add).

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
- An event when the renderer (`d3dgl.dll`) or the sound library loads, with hooks allowed in it in
  load order, so plugins can hook their code through the API. They load inside `System::init`,
  after plugins; the SplashTest plugins work around it by hooking `Video::init` in the executable
  and giving the new video player a copy of its virtual table.

## 12. Constraints

- 32-bit x86 only; the exe's TimeDateStamp must be `0x6674863F` (stock or CBP), otherwise the
  loader loads no plugins and says so.
- ASLR: every address is applied with the load delta.
- Engine calls only on the main thread. Under the loader lock only the exe check, the bcrypt
  warm-up and the `Main::main` patch.
- Toolchain for the loader: zig cc `-target x86-windows-gnu`, as RON Revived (builder may change
  it with reason). Plugins: any compiler that makes a 32-bit DLL with a C export.
- Old RON Revived versions install a `winmm.dll` that hooks the game by itself, before the loader
  starts. If the loader finds one (a `winmm.dll` in the game folder that is not the system's, or its
  hooks' `jmp` bytes at known addresses), it loads no plugins and says why in the log, rather than
  patching on top of it (my call). The Plugins button and panel stay off too (my call, 2026-11-24):
  the old DLL hooks `TopMenu::check_result` and the menu's drawing itself and serves its own
  `topmenu.xml`, so the panel would have to sit on hooks that write without checking. The
  installer's warning should make this rare.

## 13. Left to the builder

- File and export names, the plugin `.ini` and `load-order.txt` syntax, the cache folder location.
- How the panel window is built (topmenu panel element or a window built in code).
- How the restart relaunches the game, after testing.
- Log location and format.

## 14. Still open (to verify during the build)

- Online multiplayer through the `shfolder.dll` proxy is untested (needs two machines);
  `CrossplayNetLib` really calls `SHGetFolderPathW`, so the forward must be exact.
- Which game readers bypass `String::prepend_content_dir`, and which folder listings need added
  files merged in.
- The per-player lobby channel (section 10), and whether LAN or direct-connect games use the same
  lobby path.
- That disabling Start through `SetupWin::start_button_disable` holds while the lobby updates.
- Plugin state in save games: `Game::walk_data` (`0x589600`) serves save, load and the checksum,
  but adding data there changes the save format.
- Whether `shutdown` (the return of `Main::run_game_loop`) also fires on the menu's Quit and on
  closing the window; only the console `quit` was traced.
- Whether loading a save or a replay goes through `Setup::build_game` (callers: `Game::init+0xD33`,
  `ConsoleWin::run_cmd`), so that `match_built` and `match_end` fire for those matches too.
- After Apply and Restart the game is no longer started by Steam: whether the Steam overlay and
  sign-in still work then (the same holds for the Workshop window's Reload Assets). The panel's
  look, order, on/off, Apply and Restart and the test plugins were checked in the game
  (2026-11-25); this was not.
- A start closed during loading counts as a stop, so one plugin-free start follows (accepted in
  section 8); not yet seen in the game.
