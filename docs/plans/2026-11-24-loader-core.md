# Loader Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the bootstrap `shfolder.dll`, the loader `RonPluginLoader.dll` and the plugin API v1, so plugin DLLs in `<game>\plugins\<Name>\` load in the saved order, share hooks with lowest-first chaining, and receive the seven loader events in a real game run.

**Architecture:** `shfolder.dll` forwards its two exports to `shell32`. From `DllMain` it only checks the exe, warms up bcrypt and patches a `jmp` at `Main::main`. That `jmp` loads `RonPluginLoader.dll` on the game's main thread, outside the loader lock. The loader reads `plugins\load-order.txt` and each `plugin.ini`, installs its own event hooks, loads the plugins top to bottom and hands each the `RonApi` table. Logic that needs no game (identity, ini, load order, map, hook manager, events, loading sequence) is unit-tested in a 32-bit test exe. The whole chain is then proven in one unattended game run.

**Tech Stack:** C11, GNU inline asm (Intel syntax), zig cc `-target x86-windows-gnu` through `python -m ziglang`, Win32 wide-character APIs, PowerShell build and dev scripts, `devtools/rondbg` for the game run.

**Spec:** `docs/design.md`, sections 4–7 and 12. This plan covers items 1, 2 and 6 of section 11's first version. Items 3 (files), 4 (panel) and 5 (multiplayer) get their own plans (see the end).

## Global Constraints

- 32-bit x86 only. The exe's TimeDateStamp must be `0x6674863F` (stock or CBP); otherwise the loader loads no plugins and says so.
- ASLR: every address is applied with the load delta (runtime base − `0x400000`).
- Engine calls only on the main thread. Under the loader lock only: identity check, bcrypt warm-up, the `Main::main` patch. Nothing waits or loads a library in `DllMain`.
- Plugins load only from `<game>\plugins\<Name>\<Name>.dll`; never from `mods\` or Workshop folders.
- When two plugins hook the same function, the lowest plugin's hook runs first; events fire in load order (top first).
- Hooks, byte patches and event registrations are accepted only while plugins load (until `pre_init` has fired); later calls return `RON_E_STATE`. Nothing is ever unhooked except a failed plugin's own links during its load.
- An old `<game>\winmm.dll` present, or an unknown exe → no plugins loaded, one log line saying why.
- All paths are wide (`wchar_t`, `...W` APIs); all text files the loader reads are UTF-8, with an optional BOM and CRLF or LF.
- Build: `python -m ziglang cc -target x86-windows-gnu -O2 -msse2 -mstackrealign`. DLLs add `-shared -s`; link with `-lkernel32 -luser32 -lbcrypt`.
- Every text file in the repo is CRLF (`.gitattributes` `* text=auto eol=crlf`); run the `crlf-convert` skill on files you write.
- Never write into the live game folder without the user's go-ahead in this session (task 10 asks).

## Review Focus

- A game folder with non-ASCII characters (e.g. a Steam library under `D:\Игры\`): discovery, `load-order.txt` and plugin loading must still work. Test in task 4 (discovery in a temp folder named `тест-ü`).
- `load-order.txt` edited by hand with a BOM, blank lines, trailing spaces, duplicates and unknown names: read without complaint, normalised on write. Tests in task 4.
- A plugin DLL that Windows refuses (a 64-bit DLL, a missing dependency, or garbage bytes): that plugin is marked failed with the Windows error code, and the others still load. Test in task 8 (`Garbage`).
- A plugin that hooks something and then returns 0: its links are rolled back, so the function behaves as before and other plugins' chains stay intact. Tests in tasks 6 and 8 (`Zero`).
- The game updated by Steam (new timestamp): the bootstrap does nothing and the game starts as stock. Test in task 9 (`bs_should_run` on a wrong timestamp).

---

## File structure

```
RoN-Plugin-Loader/
  build.ps1                      builds build\shfolder.dll, build\RonPluginLoader.dll, build\plugins\HelloPlugin\;
                                 -Test also builds build\tests.exe + build\testgame\ and runs the tests
  include/ron_plugin.h           public plugin API (the only header plugin authors include)
  src/common/identity.c/.h       exe identity from a mapped PE image (shared by bootstrap and loader)
  src/bootstrap/decide.c/.h      bs_should_run (no DllMain, so tests.exe can link it)
  src/bootstrap/shfolder.c       DllMain, bcrypt warm-up, Main::main patch, loads the loader
  src/bootstrap/shfolder.def     two exports forwarded to shell32
  src/loader/log.c/.h            loader.log writer
  src/loader/manifest.c/.h       plugin.ini parser
  src/loader/order.c/.h          plugin discovery, load-order.txt parse / merge / serialise
  src/loader/symbols.c/.h        rise_z.map index and lookup
  src/loader/hooks.c/.h          hook manager: inline, call site, pointer slot, byte patch; chains; rollback
  src/loader/events.c/.h         event registry + the loader's own hooks on game functions
  src/loader/loader.c/.h         loader_run sequence, plugin table, RonLoaderStart export
  src/loader/api.c               the RonApi table bound to plugin handles
  plugins/HelloPlugin/           HelloPlugin.c, plugin.ini
  tests/check.h                  CHECK / CHECK_EQ macros, test registry
  tests/test_*.c                 one file per module
  tests/targets.c                asm hook targets exported from tests.exe
  tests/plugins/                 Good.c, Zero.c, BadMajor.c, NoIni.c (+ ini files) for task 8
  tests/fixtures/mini.map
  tools/dev-install.ps1, tools/dev-uninstall.ps1
```

---

### Task 1: Scaffolding, public header, build script, test harness

**Files:**
- Create: `include/ron_plugin.h`, `build.ps1`, `tests/check.h`, `tests/test_main.c`, `tests/test_smoke.c`

**Interfaces:**
- Produces: `ron_plugin.h` exactly as below, the API every later task implements or consumes.

```c
#define RON_API_MAJOR 1
#define RON_API_MINOR 0
typedef struct RonPlugin RonPlugin;   /* opaque, one per plugin */
typedef enum { RON_OK = 0, RON_E_ARG, RON_E_SIG, RON_E_CONFLICT, RON_E_STATE, RON_E_MEM } RonResult;
typedef enum { RON_EXE_UNKNOWN = 0, RON_EXE_STOCK, RON_EXE_CBP } RonExeKind;
typedef enum { RON_EV_PRE_INIT, RON_EV_POST_INIT, RON_EV_MATCH_BUILT, RON_EV_SIM_FRAME,
               RON_EV_RENDER_FRAME, RON_EV_MATCH_END, RON_EV_SHUTDOWN, RON_EV_COUNT } RonEvent;
typedef struct RonGameInfo { RonExeKind exe; uint32_t timestamp; uintptr_t delta; const wchar_t *game_dir; } RonGameInfo;
typedef void (*RonEventFn)(RonEvent ev, void *game_object, void *user);
typedef struct RonApi {
    uint32_t major, minor;
    void (*log)(RonPlugin *self, const char *fmt, ...);
    const RonGameInfo *(*game)(void);
    uintptr_t (*va)(uintptr_t static_va);                 /* + delta */
    uintptr_t (*sym)(const char *decorated_name);         /* runtime address, 0 if unknown */
    RonResult (*hook_inline)(RonPlugin *self, uintptr_t at, const uint8_t *sig, const uint8_t *mask,
                             uint32_t sig_len, uint32_t stolen, void *detour, void **orig);
    RonResult (*hook_call)(RonPlugin *self, uintptr_t call_site, void *detour, void **orig);
    RonResult (*hook_ptr)(RonPlugin *self, uintptr_t slot, uintptr_t expected, void *detour, void **orig);
    RonResult (*patch_bytes)(RonPlugin *self, uintptr_t at, const uint8_t *expect, const uint8_t *bytes, uint32_t len);
    RonResult (*on_event)(RonPlugin *self, RonEvent ev, RonEventFn fn, void *user);
} RonApi;
typedef struct RonPluginInfo { uint32_t api_major; int affects_sim; } RonPluginInfo;
/* Each plugin exports: RON_PLUGIN_EXPORT int __cdecl RonPluginMain(const RonApi *, RonPlugin *, RonPluginInfo *);
   return nonzero to stay loaded; *orig is valid as soon as a hook call returns RON_OK. */
#define RON_PLUGIN_EXPORT __declspec(dllexport)
```
  Header comments must state: all `at`/`slot`/`call_site` values are runtime addresses (use `va` or `sym`); `mask` may be NULL (all bytes significant) and a 0x00 mask byte skips that byte (for relocated absolute addresses); `stolen <= sig_len`, and the stolen bytes must hold no relative branch; plugins must not call engine code from their own threads.
- Produces: the test harness `TEST(name)` (self-registering) plus `CHECK(cond)`, `CHECK_EQ(a, b)` (integers, prints both) and `CHECK_STR(a, b)`. `build.ps1 -Test` exits nonzero when any test fails.

- [ ] **Step 1:** Write `tests/test_smoke.c` with `TEST(smoke_header)`: `CHECK_EQ(RON_API_MAJOR, 1)`, `CHECK_EQ(RON_EV_COUNT, 7)`, `CHECK_EQ(sizeof(void *), 4)`.
- [ ] **Step 2:** Write `build.ps1` (params `-Test`, `-GameDir`) that compiles `tests\*.c` with every `src\common\*.c`, `src\loader\*.c` and `src\bootstrap\decide.c` (none exist yet) into `build\tests.exe` and runs it. Run `powershell -File build.ps1 -Test`. Expected: compile error (no header yet).
- [ ] **Step 3:** Create `include/ron_plugin.h` and `tests/check.h` as specified.
- [ ] **Step 4:** Run `powershell -File build.ps1 -Test`. Expected: `tests: 1 passed, 0 failed`, exit code 0.
- [ ] **Step 5:** Convert to CRLF, then commit: `git add -A; git commit -m "Scaffolding: public API header, build script, test harness"`.

### Task 2: Exe identity and log

**Files:**
- Create: `src/common/identity.c/.h`, `src/loader/log.c/.h`, `tests/test_identity.c`, `tests/test_log.c`

**Interfaces:**
- Produces: `RonExeKind identity_kind(const uint8_t *image, uint32_t *timestamp_out)`: reads the DOS and NT headers of a mapped image. Timestamp `0x6674863F` with no section named `.cbp` gives `RON_EXE_STOCK`; with one, `RON_EXE_CBP`; anything else (other timestamp, bad `MZ`/`PE` signature, machine not `0x14C`) gives `RON_EXE_UNKNOWN`.
- Produces: `uintptr_t identity_delta(const uint8_t *image)` = `(uintptr_t)image - 0x400000`.
- Produces: `int log_open(const wchar_t *path)` (truncates; UTF-8), `void log_line(const char *owner, const char *fmt, ...)` (writes `HH:MM:SS.mmm [owner] text\r\n`, flushes each line), `void log_close(void)`.

- [ ] **Step 1:** Tests on a synthesized in-memory PE (a 0x400-byte buffer: `MZ`, `e_lfanew=0x40`, `PE\0\0`, machine `0x14C`, two section headers): `identity_stock` (timestamp `0x6674863F`, sections `.text`, `.rdata` → STOCK, `*timestamp_out == 0x6674863F`), `identity_cbp` (second section `.cbp` → CBP), `identity_other_build` (timestamp `0x66748640` → UNKNOWN), `identity_bad_mz` (→ UNKNOWN), `identity_delta_zero` (buffer treated as based at `0x400000` → delta arithmetic). `log_writes_lines`: open a temp file, `log_line("loader", "x=%d", 5)`, close; the file has one line ending in `[loader] x=5\r\n`.
- [ ] **Step 2:** Run `build.ps1 -Test`. Expected: FAIL (undefined `identity_kind`, `log_open`).
- [ ] **Step 3:** Implement both modules.
- [ ] **Step 4:** Run `build.ps1 -Test`. Expected: all pass.
- [ ] **Step 5:** CRLF, commit `"Exe identity (stock, CBP, unknown) and loader log"`.

### Task 3: plugin.ini parser

**Files:**
- Create: `src/loader/manifest.c/.h`, `tests/test_manifest.c`

**Interfaces:**
- Produces: `typedef struct Manifest { char name[64]; char version[32]; char author[64]; char description[256]; int affects_sim; } Manifest;` and `int manifest_parse(const char *utf8, size_t len, Manifest *m)`, which returns 1 when a `[Plugin]` section exists. Section and key names are case-insensitive; values are trimmed; lines starting with `;` or `#` are comments; a BOM is skipped; values longer than a field are truncated at a UTF-8 character boundary; `AffectsSim` is 0 only for the exact value `0`, and the default is 1. A missing `Name` leaves `name` empty (the loader fills in the folder name).

- [ ] **Step 1:** Tests: `manifest_full` (all five keys → exact values), `manifest_defaults` (only `[Plugin]` → empty strings, `affects_sim == 1`), `manifest_case_bom_crlf` (`\xEF\xBB\xBF[plugin]\r\nname = X \r\n` → `name == "X"`), `manifest_no_section` (returns 0), `manifest_comments_other_sections` (keys under `[Other]` ignored), `manifest_truncate_utf8` (a 300-byte description of 2-byte characters → ends on a whole character, length ≤ 255).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement `manifest_parse`.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** CRLF, commit `"plugin.ini parser"`.

### Task 4: Plugin discovery and load-order.txt

**Files:**
- Create: `src/loader/order.c/.h`, `tests/test_order.c`

**Interfaces:**
- Produces: `#define ORDER_MAX 128`, `typedef struct OrderItem { wchar_t name[64]; int enabled; int is_new; } OrderItem;`
- `int order_discover(const wchar_t *plugins_dir, wchar_t (*names)[64], int max, void (*on_skip)(const wchar_t *folder))`: the subfolders that hold `<folder>\<folder>.dll`, sorted case-insensitively (`CompareStringOrdinal` with ignore-case). A subfolder without that DLL is reported through `on_skip` (may be NULL). Returns the count.
- `int order_parse(const char *utf8, size_t len, OrderItem *out, int max)`: one name per line. A leading `-` means disabled. `#` lines and blank lines are skipped, names are trimmed, a BOM is tolerated, and a later duplicate (case-insensitive) is dropped.
- `int order_merge(const OrderItem *listed, int n_listed, const wchar_t (*found)[64], int n_found, OrderItem *out, int max, int *dropped)`: listed names that were found keep their order and state; listed names not found are dropped and counted; found names not listed are appended in discovery order with `enabled=1, is_new=1`.
- `size_t order_serialize(const OrderItem *items, int n, char *buf, size_t cap)`: UTF-8 without BOM, CRLF, `-Name` for a disabled plugin, a first line `# Load order: top loads first; the lowest plugin wins. A leading - turns a plugin off.`

- [ ] **Step 1:** Tests: `discover_sorted_requires_dll` (temp dir with `b\b.dll`, `A\A.dll`, `c\` without a DLL, `d\other.dll` → `A`, `b`; `on_skip` called for `c` and `d`), `discover_non_ascii` (temp folder `тест-ü` holding `Ünï\Ünï.dll` → found), `parse_rules` (BOM, CRLF, `  -Foo  `, `# x`, blank, `foo` duplicate → one item `Foo`, disabled), `merge_new_appended` (listed `B`; found `A`, `B` → `B` (not new), `A` (new, enabled)), `merge_missing_dropped` (listed `X`; found nothing → empty, `dropped == 1`), `serialize_roundtrip` (serialise then parse gives the same names and states).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement the four functions.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** CRLF, commit `"Plugin discovery and load-order.txt"`.

### Task 5: Symbol map

**Files:**
- Create: `src/loader/symbols.c/.h`, `tests/test_symbols.c`, `tests/fixtures/mini.map`

**Interfaces:**
- Produces: `int sym_load(const wchar_t *map_path, uint32_t exe_timestamp)`, which returns the number of names indexed, or -1 when the file is missing or its `Timestamp is xxxxxxxx` header differs from `exe_timestamp` (then every lookup returns 0). Also `uint32_t sym_static(const char *decorated)` (the map's VA, 0 if absent) and `void sym_free(void)`.
- Map line format (from `rise_z.map`): ` 0001:0019a1e0       ?check_result@TopMenu@@QAE?AW4GameIndex@@ABVString@@@Z 0059b1e0 f   topmenu.obj`: a section:offset token, the decorated name, 8 hex digits of VA, then flags and the object. Lines that don't fit this shape are skipped. For duplicate names the first one wins. The loader times `sym_load` and logs `symbols: <n> names in <ms> ms`.

- [ ] **Step 1:** Write `mini.map`: header lines including ` Timestamp is 6674863f (Thu Jun 20 19:42:55 2024)`, then the `check_result`, `?main@Main@@SAHPAUHINSTANCE__@@0PADH@Z 00560cf0` and `?do_frame@Game@@QAEXXZ 00591ef0` lines, a malformed line and a duplicate `do_frame` line with VA `00999999`. Tests: `sym_lookup` (`?do_frame@Game@@QAEXXZ` → `0x591EF0`), `sym_first_duplicate_wins`, `sym_unknown_zero`, `sym_wrong_timestamp` (load with `0x66748640` → -1, lookups 0), `sym_missing_file` (-1).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement (read the whole file, index name → VA in a hash table sized for about 60,000 names).
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** CRLF, commit `"rise_z.map symbol index"`.

### Task 6: Hook manager

**Files:**
- Create: `src/loader/hooks.c/.h`, `tests/targets.c`, `tests/test_hooks.c`

**Interfaces:**
- Produces (owner 0 is the loader, owners 1..n are plugins in load order):
  - `RonResult hm_inline(int owner, uintptr_t at, const uint8_t *sig, const uint8_t *mask, uint32_t sig_len, uint32_t stolen, void *detour, void **orig)`
  - `RonResult hm_call(int owner, uintptr_t site, void *detour, void **orig)` (the byte at `site` must be `E8`)
  - `RonResult hm_ptr(int owner, uintptr_t slot, uintptr_t expected, void *detour, void **orig)` (`expected` 0 = don't check)
  - `RonResult hm_patch(int owner, uintptr_t at, const uint8_t *expect, const uint8_t *bytes, uint32_t len)`
  - `void hm_rollback_owner(int owner)`: undoes that owner's links and patches. The owner is always the newest link in each chain it touched, so its link is removed and the previous head restored. A record it created goes back to the stock bytes.
  - `void hm_lock(void)` (afterwards every call returns `RON_E_STATE`), `void hm_reset(void)` (tests: roll back everything, unlock)
  - `typedef void (*HmNotify)(int owner, int other, uintptr_t at, int kind); /* kind: HM_SHARED=1, HM_CONFLICT=2 */` and `void hm_set_notify(HmNotify)`
- Rules:
  - One record per address keeps the stock bytes and the current head.
  - A second inline hook at the same address checks `sig` against the saved stock bytes, not the live ones. It doesn't steal again: the patched `jmp` goes to the new detour and the new `*orig` is the previous head. So the lowest hook runs first.
  - `hm_call` and `hm_ptr` chain the same way on their rel32 or pointer.
  - Two requests conflict when their byte ranges overlap without being same-address, same-kind chaining. That includes any patch overlapping a hook or another patch. The later request then gets `RON_E_CONFLICT` and the notify callback with `HM_CONFLICT`.
  - Trampolines come from `VirtualAlloc` RWX pages (bump allocator).
  - Every write goes through `VirtualProtect` and then `FlushInstructionCache`.
- `tests/targets.c` exports, in Intel-syntax asm:
  - `hk_add1`: bytes `55 8B EC 8B 45 08 83 C0 01 5D C3`, returns x + 1; stolen 6.
  - `hk_caller`: `push dword ptr [esp+4]; call hk_add1; add esp, 4; ret`; the `E8` is at offset 4.
  - `hk_slot`: an `int (*)(int)` initialised to `hk_add1`.
  - `hk_add1` is exported from `tests.exe` (a `.drectve` `-export:` entry or a linker flag), so task 8's test plugins can find it with `GetProcAddress`.

- [ ] **Step 1:** Tests (each starts with `hm_reset()`):
  - `inline_single`: owner 1's detour returns `orig(x) * 10`, so `hk_add1(1) == 20`.
  - `inline_chain_lowest_first`: owner 1 as above, owner 2 returns `orig(x) + 100`, so `hk_add1(1) == 120`, and notify was called with `(2, 1, at, HM_SHARED)`.
  - `inline_sig_mismatch`: `RON_E_SIG`, and the bytes still equal the stock copy.
  - `inline_mask`: sig byte 3 wrong but masked 0x00 → `RON_OK`.
  - `inline_overlap_conflict`: owner 2 at `hk_add1 + 1` → `RON_E_CONFLICT`.
  - `rollback_newest`: after the chain, `hm_rollback_owner(2)` gives `hk_add1(1) == 20`, and `hm_rollback_owner(1)` makes the bytes `memcmp`-equal to stock.
  - `call_chain`: two `hm_call` on `hk_caller + 4` → the lower detour runs first.
  - `ptr_chain_and_expected`: `hm_ptr(&hk_slot, (uintptr_t)hk_add1, …)` OK; with a wrong `expected` → `RON_E_SIG`.
  - `patch_conflicts_with_hook`: patch inside a hooked range → `RON_E_CONFLICT`.
  - `patch_expect_mismatch`: → `RON_E_SIG`.
  - `locked_refuses`: after `hm_lock()`, every call returns `RON_E_STATE`.
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement `hooks.c`. RON Revived's `hook_jmp5`/`hook_trampoline` (`RON-Revived/camera-smooth/src/ron_camera.c:1396-1409`) show the jmp and trampoline layout.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** CRLF, commit `"Hook manager: inline, call-site, pointer and byte patches with lowest-first chains"`.

### Task 7: Events and the loader's game hooks

**Files:**
- Create: `src/loader/events.c/.h`, `tests/test_events.c`

**Interfaces:**
- Produces: `RonResult ev_register(int owner, RonEvent ev, RonEventFn fn, void *user)` (`RON_E_STATE` after `ev_lock()`), `void ev_fire(RonEvent ev, void *game_object)` (owners ascending, then registration order), `void ev_drop_owner(int owner)`, `void ev_lock(void)`, `void ev_reset(void)`, `int ev_install_game_hooks(uintptr_t delta)` (installs owner-0 inline hooks through `hm_inline` and returns how many succeeded; a failure is logged and leaves that event silent), and `typedef struct GameHook { RonEvent ev; uint32_t va; uint8_t sig[10], mask[10]; uint32_t sig_len, stolen; } GameHook;` with `const GameHook *ev_game_hooks(int *count)` for the table below.
- The game hooks, from `rise_z.map` and the stock bytes. Masked bytes (`??`) are absolute addresses that ASLR relocates. "Pass-through ecx" means the detour is declared `thiscall` even for a static function, because the code reads `ecx` on entry.

| Event | Function (static VA) | Convention | Signature (stolen bytes) | Fires |
|---|---|---|---|---|
| post_init | `Main::init_menus` `0x595BE0` | cdecl `void(unsigned int&)` | `55 8B EC 6A FF` (5) | after |
| match_built | `Setup::build_game` `0x5AC190` | thiscall `int(int,int,int)` | `53 8B DC 83 EC 08` (6) | after, object = `this` |
| sim_frame | `Game::do_frame` `0x591EF0` | thiscall `void()` | `55 8B EC 6A FF` (5) | after, object = `this` |
| render_frame | `Game::loop_render` `0x5917E0` | thiscall `int(int,int) const` | `55 8B EC 83 3D ?? ?? ?? ?? 00` (10) | after, object = `this` |
| match_end | `Game::close` `0x594B80` | thiscall `void(int)` | `53 8B DC 83 EC 08` (6) | before, object = `this` |
| shutdown | `Main::shutdown` `0x5966A0` | pass-through ecx, `void(int)` | `57 8B F9 B9 ?? ?? ?? ??` (8) | before |

`pre_init` has no hook: `loader_run` fires it (task 8).

- [ ] **Step 1:** Tests:
  - `fire_order`: register owner 2 then owner 1 → owner 1's callback runs first; the `game_object` and `user` values arrive unchanged.
  - `drop_owner`: dropped callbacks don't run.
  - `locked_refuses`: → `RON_E_STATE`.
  - `game_hook_table_shape`: each table row has `stolen <= sig_len`, stolen ≥ 5, and mask 0x00 only on the bytes listed above.
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement the registry, the table and six detours.
- [ ] **Step 4:** Run. Expected: PASS. The detours themselves are proven in task 10.
- [ ] **Step 5:** CRLF, commit `"Event registry and the loader's six game hooks"`.

### Task 8: Loading sequence and the API table

**Files:**
- Create: `src/loader/loader.c/.h`, `src/loader/api.c`, `tests/test_loader.c`, `tests/plugins/Good.c`, `Zero.c`, `BadMajor.c`, `NoIni.c` and their `plugin.ini` files. Modify: `build.ps1` (`-Test` builds `build\testgame\plugins\<Name>\<Name>.dll` for each, copies their ini files except for `NoIni`, and writes 64 random bytes to `build\testgame\plugins\Garbage\Garbage.dll` with a valid ini).

**Interfaces:**
- Consumes: tasks 2–7.
- Produces:
  - `typedef enum { LP_DISABLED, LP_LOADED, LP_FAILED } LpState;`
  - `typedef struct LoaderPlugin { wchar_t folder[64]; Manifest m; int enabled, is_new; LpState state; char reason[128]; } LoaderPlugin;`
  - `typedef struct LoaderConfig { RonGameInfo game; int install_game_hooks; } LoaderConfig;`
  - `int loader_run(const LoaderConfig *cfg)`: returns the number loaded.
  - `const LoaderPlugin *loader_plugins(int *count)`
  - The DLL export `void __cdecl RonLoaderStart(void)`: builds the config from `GetModuleHandleW(NULL)` with `install_game_hooks = 1`.
- Sequence of `loader_run`, in this order:
  1. Open `<game>\plugins\loader.log`.
  2. Log `loader: game <stock|cbp|unknown> <timestamp hex>`.
  3. If the exe is unknown: log `loader: unknown game build, no plugins loaded` and return 0.
  4. If `<game>\winmm.dll` exists: log `loader: old RON Revived winmm.dll found; uninstall it. No plugins loaded` and return 0.
  5. Discover (log `loader: skipped folder <X>: no <X>.dll` for each skip), merge with `load-order.txt` (log `loader: <n> listed plugins no longer present` when any were dropped), and rewrite the file only if its contents change.
  6. Read each `plugin.ini`. Missing → failed, `no plugin.ini`.
  7. `sym_load(<game>\sbl\rise_z.map)`.
  8. Install the game hooks if asked.
  9. For each enabled plugin in order: `LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH)` (failure → `LoadLibrary error <GetLastError>`); `RonPluginMain` missing → `no RonPluginMain`; call it; `api_major != 1` → `API version <n>`; a 0 return → `returned 0`. On any failure: `hm_rollback_owner`, `ev_drop_owner`, `FreeLibrary`, log `loader: plugin <Name> failed: <reason>`. On success log `loader: plugin <Name> <Version> loaded`. `affects_sim` is 1 when the ini or the plugin says 1.
  10. `ev_fire(RON_EV_PRE_INIT, NULL)`, then `hm_lock()` and `ev_lock()`.
- `api.c` binds each `RonApi` function to the calling plugin's owner index and writes conflict and shared notices as `loader: <PluginA> and <PluginB> both patch 0x<addr> (<shared|conflict>)` (owner 0 is named `loader`). `api->sym(name)` returns `sym_static(name) + delta`, or 0 when the name is unknown.
- Test plugins:
  - `Good` registers `RON_EV_PRE_INIT` and returns 1.
  - `Zero` hooks `hk_add1` (found with `GetProcAddress(GetModuleHandleW(NULL), "hk_add1")`) and returns 0.
  - `BadMajor` sets `api_major = 99`.
  - `NoIni` has a valid DLL without an ini.

- [ ] **Step 1:** Tests (temp copy of `build\testgame`, `install_game_hooks = 0`, identity forced to stock):
  - `run_loads_good_only`: returns 1; `Good` is loaded; `Zero`, `BadMajor`, `NoIni` and `Garbage` are failed with the reasons above (`Garbage`: `LoadLibrary error 193`).
  - `run_zero_rolled_back`: `hk_add1(1) == 2` after the run.
  - `run_writes_order`: `load-order.txt` lists all five, sorted, enabled. A second run leaves the file byte-identical.
  - `run_disabled_skipped`: `-Good` in the file → state `LP_DISABLED`, returns 0.
  - `run_old_winmm`: with `winmm.dll` present → returns 0 and the log holds the winmm line.
  - `run_unknown_exe`: → returns 0 and logs the unknown-build line.
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement `loader.c`, `api.c` and the test plugins; extend `build.ps1`.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** CRLF, commit `"Loading sequence, plugin table and RonApi v1"`.

### Task 9: Bootstrap shfolder.dll

**Files:**
- Create: `src/bootstrap/decide.c/.h` (no `DllMain`, so `tests.exe` can link it), `src/bootstrap/shfolder.c`, `src/bootstrap/shfolder.def` (copy of `devtools/dll-route-test/shfolder.def`: `SHGetFolderPathA=shell32.SHGetFolderPathA @1`, `SHGetFolderPathW=shell32.SHGetFolderPathW @2`), `tests/test_bootstrap.c`. Modify: `build.ps1` (build `build\shfolder.dll` from `shfolder.c` + `decide.c` + `identity.c` + the `.def`, and `build\RonPluginLoader.dll` from `src\loader\*.c` + `identity.c`; `tests.exe` also links `decide.c`).

**Interfaces:**
- Produces, in `decide.c`: `int bs_should_run(const uint8_t *image)`, which is 1 only for `RON_EXE_STOCK` or `RON_EXE_CBP`.
- `DllMain`, on attach only:
  1. `DisableThreadLibraryCalls`.
  2. If `bs_should_run(GetModuleHandleW(NULL))`: call `BCryptGenRandom(NULL, b, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG)` (the same warm-up as RON Revived's `warm_up_bcrypt`, `ron_camera.c:1650-1661`, whose comment explains the start-up deadlock), check the 5 bytes at `Main::main` (`0x560CF0` + delta) equal `55 8B EC 6A FF`, build a 5-byte-stolen trampoline and write the `jmp`.
  3. Nothing else.
- The `Main::main` detour is cdecl `int(HINSTANCE, HINSTANCE, char*, int)`. Its first call loads `<dir of shfolder.dll>\RonPluginLoader.dll` with `LoadLibraryW` and calls `RonLoaderStart`; a failure goes to `OutputDebugStringW`, and the game goes on as stock. It then returns the original's result.

- [ ] **Step 1:** Tests: `bs_runs_stock`, `bs_runs_cbp`, `bs_skips_other_build` (reusing task 2's synthetic images).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement and extend `build.ps1`.
- [ ] **Step 4:** Run `build.ps1 -Test`. Expected: PASS. Then run `python -P -E -c "import pefile,sys; p=pefile.PE(sys.argv[1]); print(sorted((e.name.decode(), (e.forwarder or b'').decode()) for e in p.DIRECTORY_ENTRY_EXPORT.symbols))" build\shfolder.dll`. Expected: `[('SHGetFolderPathA', 'shell32.SHGetFolderPathA'), ('SHGetFolderPathW', 'shell32.SHGetFolderPathW')]`.
- [ ] **Step 5:** CRLF, commit `"Bootstrap shfolder.dll: bcrypt warm-up, Main::main patch, loads the loader"`.

### Task 10: HelloPlugin, dev install, and a real game run

**Files:**
- Create: `plugins/HelloPlugin/HelloPlugin.c`, `plugins/HelloPlugin/plugin.ini`, `tools/dev-install.ps1`, `tools/dev-uninstall.ps1`, `README.md` section "Building and testing". Modify: `build.ps1` (build `build\plugins\HelloPlugin\`).

**Interfaces:**
- `plugin.ini`: `[Plugin]`, `Name=HelloPlugin`, `Version=1.0`, `Author=RoN Plugin Loader`, `Description=Logs every loader event and counts game frames.`, `AffectsSim=0`.
- HelloPlugin does three things:
  - It registers all seven events. It logs `pre_init`, `post_init`, `match_built`, `sim_frame first`, `render_frame first`, `match_end sim_frames=<a> hook_calls=<b>` and `shutdown`.
  - It hooks `api->sym("?do_frame@Game@@QAEXXZ")` inline with signature `55 8B EC 6A FF`, stolen 5. Its thiscall detour counts calls, so the hook chains with the loader's own `do_frame` hook.
  - It returns 1.
- `dev-install.ps1 [-GameDir <path>]` copies `shfolder.dll` and `RonPluginLoader.dll` into the game folder and `plugins\HelloPlugin\*` into `<game>\plugins\HelloPlugin\`. It records every path it created in `<game>\plugins\.dev-installed.txt`, and refuses (exit 1) if `shfolder.dll` or `RonPluginLoader.dll` already exists without that record. `dev-uninstall.ps1` deletes exactly the recorded paths plus `loader.log` and `load-order.txt`, then `plugins\` if it is empty. The default game folder is `C:\Program Files (x86)\Steam\steamapps\common\Rise of Nations`.

- [ ] **Step 1:** Write the HelloPlugin and the two scripts. Run `build.ps1`. Expected: `build\shfolder.dll`, `build\RonPluginLoader.dll`, `build\plugins\HelloPlugin\HelloPlugin.dll` and `plugin.ini` exist.
- [ ] **Step 2:** Ask the user for permission to install into the live game folder for one unattended run. Stop here without it.
- [ ] **Step 3:** Run `powershell -File tools\dev-install.ps1`. Then, from the workspace root `C:\Users\minus\Projects\RoN`, run:
  - `python -P -E devtools/rondbg/rondbg.py start --autostart "--switch=-automation" --unpause`
  - `python -P -E devtools/rondbg/rondbg.py wait frame 300 --timeout 240`
  - `python -P -E devtools/rondbg/rondbg.py stop`
- [ ] **Step 4:** Check `<game>\plugins\loader.log`. Expected, in this order:
  - `loader: game stock 6674863f`
  - `symbols: <n> names in <ms> ms`, with n > 50000
  - `loader: plugin HelloPlugin 1.0 loaded`
  - `loader: HelloPlugin and loader both patch 0x<do_frame> (shared)`
  - `[HelloPlugin] pre_init`, `post_init`, `match_built`, `sim_frame first`, `render_frame first`
  - `match_end sim_frames=<a> hook_calls=<a>`, with a ≥ 300 and both numbers equal
  - `shutdown`

  If `match_end` or `shutdown` is missing, the console `quit` path skips `Game::close` or `Main::shutdown`. Investigate with the `superpowers:systematic-debugging` skill before changing anything. Record the parse time from the `symbols:` line in `docs/design.md` section 14 (it answers "how long parsing takes").
- [ ] **Step 5:** Run `powershell -File tools\dev-uninstall.ps1`. Then check `Test-Path` on `<game>\shfolder.dll`, `<game>\RonPluginLoader.dll` and `<game>\plugins`. Expected: all `False`, so the install is stock again.
- [ ] **Step 6:** CRLF, commit `"HelloPlugin, dev install scripts, first real game run"`. Write the run's result (log excerpt, parse time) into the commit message body.

---

## Not in this plan (next plans, in this order)

1. **File service** (design section 9): hook `String::prepend_content_dir` (`0xA1D690`, decorated `?prepend_content_dir@String@@QBE?AV1@HPADH@Z`); use, folder and generate; the cache folder; the `CreateFileW`/`FindFirstFileExW` fallback; an added `RonApi` minor version.
2. **Plugins panel and restart** (section 8). Needs the file service, which serves the generated `topmenu.xml`.
3. **Multiplayer check** (section 10). Starts with reverse engineering of the per-player lobby channel, shared with the multiplayer developer.
