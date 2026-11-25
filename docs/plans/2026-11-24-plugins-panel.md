# Plugins Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A **Plugins** button in the main menu's Tools & Extras panel, under Steam Workshop, opens a Plugins panel where the player turns plugins on and off, changes their order, reads each one's details, and applies the list with a restart; a start that never reached the menu is followed by a start without plugins.

**Architecture:** Everything is part of the loader (`shfolder.dll`). Pure C modules, unit-tested without the game, hold the logic: the start marker (`safestart`), the `topmenu.xml` edit (`menufile`) and the panel's state, layout and texts (`panel`). A thin game layer (`game`) wraps the engine calls. `menu` installs six owner-0 hooks after the plugins have loaded:
- `GetTopMenuXMLFile`, which serves the generated menu file;
- `TopMenu::check_result`, which opens the panel and applies the list;
- four slots of `TopMenuSelection`'s virtual table: drawing, left click, mouse wheel and button clicks.

The game itself draws the panel's frame and buttons; the loader draws only the list.

**Tech Stack:** C11, zig cc `-target x86-windows-gnu` through `python -m ziglang`, Win32 wide-character APIs, the existing hook manager and event registry, `devtools/rondbg` for the game run.

**Spec:** `docs/design.md`, section 8 (approved 2026-11-24), with section 12 (old `winmm.dll`) and the events table in section 6. The engine facts behind every address are in `decomp/ron-ee-6674863f/notes/2026-11-24-plugins-panel-menu.txt` (F1–F26) and `…-plugins-panel-draw.txt` (root repo); cite them by number.

## Global Constraints

- 32-bit x86. Every game address below is static (base `0x400000`); add the load delta (`VA(x) = x + delta`).
- Engine calls only on the game's main thread (all six hooks run there).
- The loader's menu hooks are owner 0 and are installed **after** every plugin's `RonPluginMain` and **before** `hm_lock`, so they head their chains (design §8, "No plugin can take the panel away").
- With an unknown exe or an old `<game>\winmm.dll`, `loader_run` returns early as today: no plugins, no marker, no button, no panel.
- Every engine function called through a pointer is declared with the convention verified in the ledgers, never the one the decorated name suggests (see `include/ron_plugin.h`'s rule; the `Main::main` lesson).
- The game's `String` (0x14 bytes, aligned 4) is built only through the game's constructors and freed with `String::close`. `String::String(wchar_t const*)` (`0xA1EDD0`) keeps the pointer, so use it only on buffers that live for the whole process.
- Texts the player sees are English and fixed by this plan (copy below); UTF-8 internally, converted to wide for the game.
- Every text file is CRLF; run `crlf-convert` on files you write. Tests: `powershell -File build.ps1 -Test` (all must pass; today 62).
- Writing into the live game folder needs the user's go-ahead in this session (task 7 asks).

### Copy (exact)

| Where | Text |
|---|---|
| Button in Tools & Extras | `Plugins` |
| Panel title | `Plugins` |
| Intro above the rows | `Top loads first. Lower plugins win when two change the same thing.` |
| Empty list | `No plugins installed. Put a plugin's .dll and .ini in the plugins folder.` |
| Notice, list changed | `Not applied yet. Apply and Restart to use this list.` |
| Notice, safe start | `The last start did not reach the menu, so this start runs without plugins. Turn off the plugin that stopped it, then Apply and Restart.` |
| Notice, save failed | `Could not save plugins\load-order.txt. Nothing was changed.` |
| Buttons | `Move Up`, `Move Down`, `Apply and Restart`, `Back` |
| Tags | `new`, `failed` |
| Details | `by <author>`; description; `Hooks <n> game functions.` / `Hooks 1 game function.` / `Hooks no game functions.`; `Failed: <reason>`; `Turned off.`; `Not loaded in this start.` |
| Culprit's reason | `the game stopped while it loaded` |

## Review Focus

- **A mod's own `topmenu.xml`.** It may lack the `workshop` button, use single quotes or upper-case tags, or already contain a `plugins` button. The button then goes above `back`, or isn't added twice, and the game still starts. Tests `mf_above_back_without_workshop`, `mf_single_quotes_and_case`, `mf_idempotent` (task 2).
- **More plugins than rows fit.** The wheel scrolls; clicks hit the right row when scrolled and when a selected row's details push later rows down. Tests `panel_hit_scrolled` and `panel_hit_below_details` (task 3).
- **Long or non-ASCII names and descriptions.** Wrapping never splits a UTF-8 character and never exceeds the line count. Test `panel_details_wraps_utf8` (task 3).
- **`load-order.txt` cannot be written** (read-only, locked). No restart, and the "save failed" notice shows. Test `panel_save_fails_cleanly` (task 3).
- **Safe-start edges.** A missing or read-only `plugins` folder never blocks a normal start, and two failed starts in a row stay safe. Tests `ss_unwritable_dir_is_normal` and `ss_safe_start_twice` (task 1).

---

## File structure

```
RoN-Plugin-Loader/
  src/loader/safestart.c/.h   the start marker: was the last start stopped before the menu, and by which plugin
  src/loader/menufile.c/.h    topmenu.xml text edit: the Plugins button and the PLUGINS panel element
  src/loader/panel.c/.h       panel state (rows, selection, scroll), edits, change check, layout, hit test, texts, save
  src/loader/game.c/.h        thin wrappers over the engine calls the menu needs (strings, sub_menu, restart, drawing)
  src/loader/menu.c/.h        menu_install: the six hooks; the menu file cache; button and click handling
  src/loader/menu_draw.c      the list's drawing (rows, check boxes, details, intro, notice)
  src/loader/hooks.c/.h       + hm_owner_count
  src/loader/loader.c/.h      + safe start, the post_init marker removal, menu_install, loader_safe_start
  tests/test_safestart.c, tests/test_menufile.c, tests/test_panel.c   new
  tests/test_hooks.c, tests/test_loader.c                             extended
  build.ps1                   no change (src\loader\*.c is globbed)
```

`menu.c`, `menu_draw.c` and `game.c` need the game and are proven in the run of task 7; everything they decide is in the tested modules.

---

### Task 1: Safe-start marker

**Files:**
- Create: `src/loader/safestart.c`, `src/loader/safestart.h`
- Test: `tests/test_safestart.c`

**Interfaces:**
- Produces:
  - `int ss_begin(const wchar_t *plugins_dir, wchar_t *culprit, size_t cap)`: returns 1 when the marker from an earlier start is still there (the start stopped early). In that case `culprit` gets the plugin id it names, or `L""`. It then writes a fresh marker reading `starting`. Returns 0 when there was no marker, or when the marker can't be read or written.
  - `void ss_loading(const wchar_t *id)`: marker becomes `loading <id>`.
  - `void ss_loaded(void)`: marker becomes `loaded`.
  - `void ss_menu_reached(void)`: deletes the marker.
- The marker is `<plugins_dir>\.start-marker`, one UTF-8 line with CRLF. The functions keep the path from `ss_begin` in a static.

- [ ] **Step 1: Write the failing tests** (each in a fresh temp folder, made like `test_loader.c`'s `make_game_from`):

```c
TEST(ss_first_start_is_normal)        /* ss_begin(dir) == 0, culprit == L"", file .start-marker reads "starting\r\n" */
TEST(ss_after_menu_is_normal)         /* ss_begin; ss_menu_reached; marker gone; ss_begin == 0 */
TEST(ss_stopped_while_loading)        /* ss_begin; ss_loading(L"Bad"); ss_begin == 1, culprit == L"Bad", marker reads "starting\r\n" */
TEST(ss_stopped_after_loading)        /* ss_begin; ss_loading(L"A"); ss_loaded(); ss_begin == 1, culprit == L"" */
TEST(ss_non_ascii_id)                 /* ss_loading(L"Plug-üж"); ss_begin == 1, culprit round-trips exactly */
TEST(ss_safe_start_twice)             /* stop, ss_begin == 1, no menu, ss_begin == 1 again (culprit L"") */
TEST(ss_unwritable_dir_is_normal)     /* ss_begin(L"<temp>\\missing\\plugins") == 0; ss_loading/ss_menu_reached do nothing */
```

- [ ] **Step 2: Run** `powershell -File build.ps1 -Test`. Expected: link errors for `ss_*` (no `safestart.c` yet).
- [ ] **Step 3: Implement `safestart.c`.** Use `_wfopen`/`fwrite`/`DeleteFileW`; ids are converted with `WideCharToMultiByte`/`MultiByteToWideChar` (CP_UTF8). Each write replaces the whole file.
- [ ] **Step 4: Run the tests.** Expected: `tests: 69 passed, 0 failed`.
- [ ] **Step 5: Commit** `git add src/loader/safestart.* tests/test_safestart.c` and `git commit -m "Safe start: the start marker (safestart.c)"`.

### Task 2: The menu file edit

**Files:**
- Create: `src/loader/menufile.c`, `src/loader/menufile.h`
- Test: `tests/test_menufile.c`

**Interfaces:**
- Produces:
  - `typedef enum { MF_FAILED = 0, MF_AFTER_WORKSHOP, MF_ABOVE_BACK, MF_ALREADY } MfWhere;`
  - `size_t menufile_add_plugins(const char *xml, size_t len, char *out, size_t cap, MfWhere *where)`: returns the length of the new text, NUL-terminated in `out`. Returns 0, with `*where = MF_FAILED`, when it adds nothing or `cap` is too small.
- Rules:
  - Search only between `<TOPMENU>` and its `</TOPMENU>`, never `TOPMENU_TRIAL`. Skip `<!-- … -->` comments everywhere. Match tag and attribute names case-insensitively, with either quote character.
  - In that block's `<OPTIONS` element: if a `BUTTON` named `plugins` already exists, the button isn't added again. Otherwise the button line goes after the end of the `BUTTON` named `workshop` (its `/>` or `</BUTTON>`), or else before the line holding the `BUTTON` named `back`.
  - The inserted line starts on a new line, with the indentation of the `workshop` line (or the `back` line), and uses the file's line ending (CRLF when the text has any `\r\n`, else LF).
  - The panel element `PLUGINS` goes before `</TOPMENU>`, unless that block already has one.
  - No `<TOPMENU>` block, no `OPTIONS` element, or neither `workshop` nor `back` means `MF_FAILED`.
  - `where` describes only the button: `MF_ALREADY` when a `plugins` button was already there (the panel is still added if it was missing). When nothing at all is added, the text comes back unchanged with `MF_ALREADY`.
- Exact button line: `<BUTTON name="plugins" title="Plugins" width="454" height="35"/>`
- Exact panel element (4-space base indent; the line endings follow the file):

```xml
    <PLUGINS name="Plugins" title="Plugins" left="30" top="40" width="548" height="768">
      <STYLE CHILD="0" TOP="1"/>
      <IFACETEXLOAD id="TEX_TOPMENU_0" sort="0" file=".\art\iface_wrapper1.tga" pixformat="PF_32"/>
      <IFACETEX name="back" texture="TEX_TOPMENU_0" left="0" top="0" width="1024" height="768"/>
      <IFACETEXLOAD id="TEX_PLUGINS_1" file=".\art\wrapper_pieces_02.tga"/>
      <IFACETEX name="check_off" texture="TEX_PLUGINS_1" left="1" top="1" width="25" height="22"/>
      <IFACETEX name="check_on" texture="TEX_PLUGINS_1" left="1" top="70" width="25" height="22"/>
      <IFACERECT name="plugin_list" left="41" top="114" width="454" height="328"/>
      <IFACERECT name="plugin_notice" left="41" top="446" width="454" height="40"/>
      <POSITIONING x="41" y="492">
        <POSITIONING inc_y="36">
          <BUTTON name="plugins_up" title="Move Up" width="454" height="35"/>
          <BUTTON name="plugins_down" title="Move Down" width="454" height="35"/>
          <BUTTON name="plugins_apply" title="Apply and Restart" width="454" height="35"/>
          <BUTTON name="back" title="Back" width="454" height="35"/>
        </POSITIONING>
      </POSITIONING>
    </PLUGINS>
```

- [ ] **Step 1: Write the failing tests.** The fixtures are short synthetic texts in the test file, shaped like the stock file:
  - `<ROOT>` holds a `<TOPMENU>` with `MAINMENU` and `OPTIONS` (seven buttons, the `workshop` line indented with tabs and spaces), then a `<TOPMENU_TRIAL>` whose `OPTIONS` has no `workshop` button.
  - Include one commented-out `BUTTON` line. CRLF unless the test says otherwise.
  - Never copy the game's own file into the repo.

```c
TEST(mf_after_workshop)            /* where == MF_AFTER_WORKSHOP; out contains "...height=\"35\"/>\r\n\t\t  <BUTTON name=\"plugins\"" right after
                                      the workshop element; exactly one "name=\"plugins\"" in out; TOPMENU_TRIAL text byte-identical */
TEST(mf_panel_before_topmenu_end)  /* "<PLUGINS name=\"Plugins\"" occurs once, after "</OPTIONS>" and before the first "</TOPMENU>" */
TEST(mf_above_back_without_workshop)  /* no workshop in OPTIONS: where == MF_ABOVE_BACK; the button line directly precedes the back line */
TEST(mf_skips_comments)            /* a commented workshop button before the real one: inserted after the real one;
                                      only a commented workshop: MF_ABOVE_BACK */
TEST(mf_single_quotes_and_case)    /* <button NAME='workshop' .../> inside <options ...>: MF_AFTER_WORKSHOP */
TEST(mf_keeps_lf)                  /* an LF-only text: out contains no '\r' */
TEST(mf_idempotent)                /* feeding out back in: where == MF_ALREADY, same text */
TEST(mf_no_options_fails)          /* no OPTIONS in TOPMENU: returns 0, where == MF_FAILED */
TEST(mf_small_cap_fails)           /* cap = len + 10: returns 0, MF_FAILED */
```

- [ ] **Step 2: Run** the tests. Expected: link errors for `menufile_add_plugins`.
- [ ] **Step 3: Implement `menufile.c`.** It's a forward scan with a "skip comment" helper and a helper that reads the `name` attribute of the element starting at a `<`. No XML library.
- [ ] **Step 4: Run** the tests. Expected: all pass (78).
- [ ] **Step 5: Commit**: `git commit -m "Menu file: add the Plugins button and panel to topmenu.xml text (menufile.c)"`.

### Task 3: The panel model

**Files:**
- Create: `src/loader/panel.c`, `src/loader/panel.h`
- Test: `tests/test_panel.c`

**Interfaces:**
- Consumes:
  - `LoaderPlugin` and `ORDER_MAX` (`loader/loader.h`, `loader/order.h`).
  - `OrderItem` and `order_serialize` (`loader/order.h`).
- Produces (`panel.h`):

```c
#define PANEL_INTRO_H 36   /* intro: two 16-px lines and a 4-px gap above the first row */
#define PANEL_ROW_H   24
#define PANEL_LINE_H  16   /* one details line */
#define PANEL_BOX_X   28   /* check box, from the list's left edge; 25 x 22, vertically centred in the row */
#define PANEL_BOX_W   25
#define PANEL_NAME_X  60
#define PANEL_WRAP    60   /* characters per description line, at most 2 lines, "..." when cut */

typedef struct PanelRow {
    wchar_t id[64];
    char name[64], version[32], author[64], description[256], reason[128];
    int enabled, is_new, failed, loaded, hooks;
} PanelRow;

typedef struct Panel {
    PanelRow rows[ORDER_MAX];
    int n, selected, scroll, safe_start, save_failed;
    wchar_t start_ids[ORDER_MAX][64];  /* the order and on/off state read at this start */
    int start_enabled[ORDER_MAX];
} Panel;

typedef struct PanelSlot { int row, y, h; } PanelSlot;   /* y from the list's top */

void panel_init(Panel *p, const LoaderPlugin *plugins, int n, const int *hooks, int safe_start);
void panel_select(Panel *p, int row);           /* -1 clears */
void panel_toggle(Panel *p, int row);
int  panel_move(Panel *p, int delta);           /* the selected row, -1 up / +1 down; 1 if it moved; selection follows */
int  panel_changed(const Panel *p);
int  panel_details(const Panel *p, int row, char lines[4][128]);   /* the line count */
int  panel_layout(const Panel *p, int list_h, PanelSlot *out, int max);  /* visible rows only */
int  panel_hit(const Panel *p, int list_h, int x, int y, int *on_box);  /* row or -1; x,y from the list's top-left */
void panel_scroll(Panel *p, int list_h, int notches);  /* positive = toward row 0; clamped */
const char *panel_notice(const Panel *p);       /* NULL or one of the notices in the copy table */
int  panel_save(Panel *p, const wchar_t *path); /* writes order_serialize text; 0 and save_failed = 1 on failure */
```

- Rules:
  - `failed = enabled && reason[0]`; `loaded = (state == LP_LOADED)`; `hooks[i]` belongs to the plugin at `i`.
  - The details lines, in this order: `by <author>` when there's an author; the description wrapped at `PANEL_WRAP`; then one status line:
    - failed: `Failed: <reason>`;
    - turned off: `Turned off.`;
    - safe start and not loaded: `Not loaded in this start.`;
    - otherwise the hooks line.
  - Only the selected row has details. Its slot height is `PANEL_ROW_H + lines * PANEL_LINE_H + 4`.
  - Rows start at `PANEL_INTRO_H`.
  - `panel_notice` gives priority to: save failed, then safe start, then changed.

- [ ] **Step 1: Write the failing tests** on a three-plugin array: `A` loaded with 2 hooks, `B` disabled, `C` with the reason `"LoadLibrary error 193"`.

```c
TEST(panel_init_rows)              /* n == 3; rows[2].failed == 1; rows[1].enabled == 0; selected == -1; panel_changed == 0 */
TEST(panel_toggle_twice_unchanged) /* toggle(1) -> changed 1; toggle(1) -> changed 0 */
TEST(panel_move_follows_selection) /* select(2); move(-1) == 1 -> rows[1].id == L"C", selected == 1, changed 1; move(+1) -> changed 0 */
TEST(panel_move_at_edges)          /* select(0); move(-1) == 0; select(2); move(+1) == 0; no selection: move == 0 */
TEST(panel_details_lines)          /* A with author "Ann", description 70 chars: lines = "by Ann", 2 wrapped lines, "Hooks 2 game functions." */
TEST(panel_details_wraps_utf8)     /* a description of 100 two-byte characters: no line ends inside a character; at most 2 lines; the 2nd ends "..." */
TEST(panel_details_status)         /* B: "Turned off."; C: "Failed: LoadLibrary error 193"; 1 hook: "Hooks 1 game function." */
TEST(panel_hit_rows)               /* list_h 328: y = 36 + 5 -> row 0; y = 36 + 24 + 5 -> row 1; x = 30 (box) -> on_box 1; x = 70 -> 0; y = 10 -> -1 */
TEST(panel_hit_below_details)      /* select(0) with 4 detail lines: row 1 starts at 36 + 24 + 4*16 + 4 */
TEST(panel_hit_scrolled)           /* 20 rows, scroll(-5 notches) -> scroll == 5; y = 36 + 5 -> row 5; scroll clamps at n - visible */
TEST(panel_notice_order)           /* unchanged: NULL; changed: the "Not applied yet." text; safe_start: the safe start text; save_failed wins */
TEST(panel_save_writes_order)      /* move C to top, turn B off, save to temp file: text == order_serialize of C, A, -B */
TEST(panel_save_fails_cleanly)     /* save to an existing directory's path: returns 0, save_failed == 1, notice is the save-failed text */
TEST(panel_empty)                  /* n == 0: hit == -1, layout == 0, notice NULL */
```

- [ ] **Step 2: Run.** Expected: link errors for `panel_*`.
- [ ] **Step 3: Implement `panel.c`.** `panel_save` builds `OrderItem`s from the rows (`is_new` = 0) and writes `order_serialize`'s text with `_wfopen(path, L"wb")`.
- [ ] **Step 4: Run.** Expected: all pass (92).
- [ ] **Step 5: Commit**: `git commit -m "Panel model: rows, edits, layout, hit test, texts, save (panel.c)"`.

### Task 4: Hook counts and the loader's start sequence

**Files:**
- Modify: `src/loader/hooks.c`, `src/loader/hooks.h`, `src/loader/loader.c`, `src/loader/loader.h`
- Create: `src/loader/menu.h` (the declaration only; `menu.c` comes in task 5)
- Test: `tests/test_hooks.c`, `tests/test_loader.c`

**Interfaces:**
- Consumes: `ss_*` (task 1); `ev_register(int owner, RonEvent ev, RonEventFn fn, void *user)`.
- Produces:
  - `int hm_owner_count(int owner)`: how many distinct addresses the owner has a link or a patch at.
  - `int loader_safe_start(void)`.
  - `int menu_install(uintptr_t delta)` (declared in `menu.h`): returns how many of its 6 hooks succeeded. For the tests, link a stub that returns 0.
- In `loader_run`, after `prepare()` for every plugin and before the symbol map:
  1. `ss_begin(g_plugins_dir, culprit, 64)` sets `g_safe`.
  2. When `g_safe` is set: log `the last start did not reach the menu: no plugins this start`, plus ` (it stopped while loading <name>)` when `culprit` names an enabled plugin. Give that plugin the reason `the game stopped while it loaded` (via `fail()`). Load no plugin.
  3. Otherwise, call `ss_loading(id)` before each `load_plugin(i)` and `ss_loaded()` after the loop.
  4. Before the loop: `ev_register(0, RON_EV_POST_INIT, on_menu_up, NULL)`, where `on_menu_up` calls `ss_menu_reached()`.
  5. After the loop and before `hm_lock()`: when `cfg->install_game_hooks` is set, log `menu hooks: %d of 6 installed` with `menu_install(g_game.delta)`.
- **Test-suite rule:** a test that runs `loader_run` twice on one folder fires `ev_fire(RON_EV_POST_INIT, NULL)` in between, because the menu was reached. Adjust the existing tests that do this.

- [ ] **Step 1: Write the failing tests.**

```c
TEST(hm_owner_count_counts_addresses)  /* owner 1: hm_inline on hk_add1 and hm_patch elsewhere -> 2; owner 2: 0 */
TEST(run_safe_start_after_stop)        /* testgame copy; write plugins\.start-marker "loading Good\r\n"; loader_run loads 0;
                                          loader_safe_start() == 1; Good's reason == "the game stopped while it loaded" */
TEST(run_normal_after_menu)            /* loader_run (normal); ev_fire(RON_EV_POST_INIT, NULL); marker gone; loader_run again loads Good */
TEST(run_marker_after_loading)         /* after a normal loader_run and before POST_INIT, the marker reads "loaded\r\n" */
```

- [ ] **Step 2: Run.** Expected: the new tests fail; the existing ones still pass.
- [ ] **Step 3: Implement** `hm_owner_count`, the sequence above, and `loader_safe_start`. Put the test stub for `menu_install` in `tests/stub_menu.c`, and keep `menu.c` out of the test build: `build.ps1`'s test source list replaces `src\loader\*.c` with that list minus `menu.c`, `menu_draw.c` and `game.c`.
- [ ] **Step 4: Run.** Expected: all pass (96).
- [ ] **Step 5: Commit**: `git commit -m "Loader: safe start, hook counts per owner, menu hooks after plugins"`.

### Task 5: Game calls and the menu hooks

**Files:**
- Create: `src/loader/game.c`, `src/loader/game.h`, `src/loader/menu.c`
- Modify: `build.ps1` (if task 4's test list needs it)

**Interfaces:**
- Consumes: `menufile_add_plugins` (task 2), `Panel` and its functions (task 3), `loader_plugins`, `loader_safe_start`, `hm_owner_count`, `hm_inline` and `hm_ptr` (task 4).
- Produces (`game.h`; every function uses the conventions below, each checked in the ledgers):

| Wrapper | Game function | Convention |
|---|---|---|
| `void game_init(uintptr_t delta)` | | |
| `void gs_from_ascii(GString *s, const char *a)` | `String::String(char const*)` `0xA1D660` | thiscall(s, a) |
| `void gs_from_static(GString *s, const wchar_t *w)` | `String::String(wchar_t const*)` `0xA1EDD0` | thiscall(s, w), ret 4; keeps `w` |
| `void gs_close(GString *s)` | `String::close` `0xA1CF40` | ECX only |
| `int gs_is(const void *s, const char *ascii)` | `String::str()` `0xA1F0A0` (thiscall → `const wchar_t*`); length = u16 at `+8` | case-insensitive compare |
| `int game_sub_menu(void *topmenu, const char *panel)` | `TopMenu::sub_menu` `0x59C030` | thiscall(topmenu, String*), ret 4 |
| `void game_restart(void)` | `WorkshopWin::reloadAssets` `0x6AC3E0` | no args, plain ret, ECX unused |
| `int game_data_path(const wchar_t *name, wchar_t *out, int cap)` | `L"data\\" + name` through `String::prepend_content_dir` `0xA1D690` | thiscall(src, ret String*, force 0, file "loader", line 0), ret 0x10; result copied to `out` |
| `void game_click(void)` | `SoundGlobal::play` `0x97F770` | category `0xAF` by value, ret 4, ECX ignored |

`typedef struct { unsigned char b[0x14]; } __attribute__((aligned(4))) GString;`

- `menu.c`: `int menu_install(uintptr_t delta)` calls `game_init(delta)`, then installs these six owner-0 hooks (static addresses + delta):

| Hook | Where | Signature / expected | Detour (gcc) |
|---|---|---|---|
| inline | `GetTopMenuXMLFile` `0x59F7C0` | sig `55 8B EC 6A FF 68 ?? ?? ?? ??` (mask 0 on the last 4), stolen 5 | `void *THISCALL d_menu_file(void *out)` |
| inline | `TopMenu::check_result` `0x59B1E0` | sig `55 8B EC 64 A1 00 00 00 00`, stolen 9 | `int THISCALL d_check_result(void *topmenu, const void *name)` |
| slot | vtable `0xB3F3F4` `+0xB8` | expected `0x59EE80` | `void THISCALL d_sel_redraw(void *sel)` (task 6) |
| slot | `+0xD8` | expected `0x41C180` | `void THISCALL d_sel_left_click(void *sel, int x, int y)` (task 6) |
| slot | `+0x114` | expected `0x41C360` | `void THISCALL d_sel_wheel(void *sel, int delta, int x, int y)` (task 6) |
| slot | `+0x2AC` | expected `0x59E930` | `void THISCALL d_sel_button(void *sel, const void *name)` |

- **Behaviour:**
  - **`d_menu_file`, first call:**
    1. Call the chain (`o_menu_file(&tmp)`) and read its name.
    2. When the name has no `\` or `/`, resolve it with `game_data_path`; otherwise use it as it is. Relative paths are relative to the game folder.
    3. Read that file, run `menufile_add_plugins`, and on success write the text to `<game>\plugins\.cache\topmenu.xml` (create `.cache`).
    4. Log `menu: Plugins button after Steam Workshop` / `above Back` / `already there`, or `menu: no Plugins button (<why>)`.
    5. `gs_close(&tmp)`.
  - **`d_menu_file`, every call:** if the cache was written, `gs_from_static(out, g_cache_path)` (a static buffer) and return `out`; otherwise `return o_menu_file(out)`.
    - **Known limit:** a mod's language-suffixed `topmenu.xml.N` isn't looked for (ledger F8).
  - **`d_check_result`:**
    - `plugins`: `panel_init(&g_panel, …)` with `hm_owner_count(i + 1)` per row, then `return game_sub_menu(topmenu, "PLUGINS")`.
    - `plugins_apply`: `panel_save(&g_panel, <plugins>\load-order.txt)`. On success: log `panel: applied, restarting`, call `game_restart()` and return -1. On failure, return -1 (the panel opens again, showing the save-failed notice).
    - Any other name: `return o_check_result(topmenu, name)`.
  - **`d_sel_button`:**
    - When `gs_is(sel + 0x4C0, "PLUGINS")` (`TopMenuSelection.current_menu`):
      - `plugins_up` / `plugins_down`: `panel_move(&g_panel, ∓1)`, `game_click()`, return without the original; the panel stays.
      - `plugins_apply` with neither `panel_changed` nor `safe_start`: `game_click()`, return.
    - Everything else: `o_sel_button(sel, name)`.

- [ ] **Step 1: Write `game.c`, `game.h` and `menu.c`** (no unit test: these need the game; their logic is in tasks 2–3).
- [ ] **Step 2: Build** with `powershell -File build.ps1 -Test`. Expected: `built shfolder.dll` and all tests pass. `pkg_exports_two_forwarders` must still pass (nothing new exported).
- [ ] **Step 3: Commit**: `git commit -m "Menu hooks: generated topmenu.xml, Plugins button and panel, Move Up/Down, Apply and Restart"`.

### Task 6: Drawing the list and taking its clicks

**Files:**
- Create: `src/loader/menu_draw.c`
- Modify: `src/loader/game.c`, `src/loader/game.h`, `src/loader/menu.c`

**Interfaces:**
- Consumes: `Panel`, `panel_layout`, `panel_hit`, `panel_scroll`, `panel_details`, `panel_notice`, `panel_select`, `panel_toggle` (task 3).
- Produces: `void menu_draw_list(void *sel)` (called by `d_sel_redraw` after the original), plus more `game.h` wrappers:

| Wrapper | Game function | Convention |
|---|---|---|
| `const short *game_find_rect(void *win, const char *name)` | vt `+0x488` | thiscall(win, String const&, -1, NULL), ret 0xC; `short left, right, top, bottom`, window-local; NULL when missing |
| `void *game_find_tex(void *win, const char *name)` | vt `+0xA4` | same |
| `void game_tex(void *tex, int x, int y, uint32_t rgba)` | `IFaceRenderManager::add` `0x51F440` | ECX = `0xEBD1C0`; (tex, x, y, color, type 1); ret 0x14 |
| `void game_fill(const short r[4], uint32_t rgba)` | `IFaceRenderManager::add` `0x51F3D0` | ECX = `0xEBD1C0`; (NULL, r, color, 5); ret 0x10 |
| `int game_text(void *win, const char *utf8, const short r[4], int right, const void *color, int size)` | `FontFamily::get(size, -1)` `0x522810` (ECX `0xED8160`, ret 8); on Buffer = `win + 0x154`: `set_font(font, 0, 0)` `0x50DAB0` (ret 0xC), `set_text_color(color, BLACK, -1, 0)` `0x50DAF0` (ret 0x10), `write_l` `0x50F6E0` or `write_right_l` `0x50FA40` (String const&, rect, length; ret 0xC) | graphsys (`[0xE335A0]`) depth `+0x164` raised by 1 around the text and restored. Returns the left x from `write_right_l`, else 0 |

- Colours: `WHITE` `0xC8D26C`, `GREY` `0xC8D218`, `RED` `0xC8D248`, `YELLOW` `0xC8D278`, `BLACK` `0xC8D260` (12-byte Color; its first dword is the `rgba` value for `game_tex`/`game_fill`).
- **Drawing** (Workshop row style, ledger draw F8 and F9), relative to `game_find_rect(sel, "plugin_list")` = `L`:
  - The intro at the top, in 2 lines, GREY, size 12.
  - **Each visible slot:**
    - The number, right-aligned in `[0, 24)`.
    - `check_off` at `(PANEL_BOX_X, row centre - 11)`, then `check_on` over it when enabled.
    - The name at `PANEL_NAME_X`, clipped left of the version.
    - The version right-aligned.
    - The tag (`new` YELLOW, `failed` RED) right-aligned before the version.
  - **Text colour:** WHITE when enabled, GREY when off.
  - **The selected row:** filled with its text colour, its text BLACK. Its details lines are GREY at `PANEL_NAME_X`, size 12.
  - **With no rows:** the empty text in GREY.
  - **The notice** goes in `game_find_rect(sel, "plugin_notice")`, YELLOW, size 12, wrapped at 60 characters.
- **Clicks:**
  - **`d_sel_left_click`:** calls the original first. Then, on the Plugins panel, when (x, y) is inside `L`: `panel_hit` on (x − L.left, y − L.top). On a row: when `on_box`, `panel_toggle`, otherwise `panel_select`; then `game_click()`.
  - **`d_sel_wheel`:** on the Plugins panel, when inside `L`: `panel_scroll(&g_panel, L.bottom - L.top, delta / 120 when |delta| >= 120, else delta)`. Otherwise call the original.
  - **Redraw:** none needed. The modal loop redraws every frame (ledger draw F11).

- [ ] **Step 1: Write `menu_draw.c`** and the wrappers; wire `d_sel_redraw`, `d_sel_left_click` and `d_sel_wheel` in `menu.c`.
- [ ] **Step 2: Build and test.** Expected: all tests pass, and `pkg_exports_two_forwarders` still passes.
- [ ] **Step 3: Commit**: `git commit -m "Plugins panel: draw the list, clicks and wheel"`.

### Task 7: In the game

**Files:** none (the run's findings go to `decomp/ron-ee-6674863f/notes/2026-11-24-loader-runs.txt`, root repo).

- [ ] **Step 1: Ask the user** before writing into the game folder: deploy `shfolder.dll` and HelloPlugin and run the game.
- [ ] **Step 2: Unattended run** (`rondbg start`, `wait menu`, `stop`), then read `plugins\loader.log`. Expected:
  - `menu hooks: 6 of 6 installed`;
  - `menu: Plugins button after Steam Workshop`;
  - `plugins\.cache\topmenu.xml` holds one `name="plugins"` button and one `<PLUGINS`;
  - after `stop`, the marker is gone (`post_init` was reached).
- [ ] **Step 3: Safe-start run.** Write `plugins\.start-marker` = `loading HelloPlugin`, start, `wait menu`, stop. Expected: the log says `no plugins this start` and `HelloPlugin` gets the reason. The next run is normal.
- [ ] **Step 4: Visual check, by the user.** Ask the user to open Tools & Extras → Plugins and report:
  - the button sits under Steam Workshop;
  - the panel shows HelloPlugin with a ticked box, its number and its version;
  - clicking the row unfolds `Hooks 1 game function.` (or 2);
  - the box toggles, and the notice appears;
  - Move Up and Move Down move a row without the panel sliding in again;
  - Back returns to Tools & Extras, and that panel's Back still reaches the main menu;
  - Apply and Restart restarts the game with the new state, as shown in `load-order.txt`.
  - In the restarted game, the Steam overlay (Shift+Tab) and the player's profile still work. The restart isn't started through Steam (design §8); report whether that matters.

  Fix whatever they report through systematic debugging, with a test for any logic error.
- [ ] **Step 5: Commit** the run notes in the root repo.

### Task 8: Documents

**Files:**
- Modify: `README.md` (the panel; `.start-marker`, `.cache`), `docs/design.md` (§11: item 4 built; §14: the open points the run settled), `include/ron_plugin.h` (nothing unless the API changed).
- Root `CLAUDE.md`: the Locations row and the decisions sentence (Plugins under Tools & Extras; automatic safe start). Use the claude-md-improver skill first.
- RoN-Engine-Atlas `re/plugin-loaders.html`: a section on adding a main-menu button and panel, covering:
  - `GetTopMenuXMLFile` and its 5 callers;
  - the `sub_menu` loop and `check_result`'s results (-1, `0x15`);
  - `on_button_clicked` and `release_modal`;
  - the game's own restart through `reloadAssets`.

  Plus an Updates entry. Follow the frontend-design skill and `atlas_finish.py`.
- Memory: `plugin-system-design.md`.

- [ ] **Step 1: Write the documents.** No local paths or tools on the Atlas; engine facts only, each with its evidence label.
- [ ] **Step 2: Checks**: `crlf-convert --check`; `atlas_finish.py --pages plugin-loaders` all ok.
- [ ] **Step 3: Commit** each repo; then merge per `CLAUDE.md`.

---

## Later plans

The file service (it takes over the menu file cache), localisation of the loader's texts, and the multiplayer check (design §10).
