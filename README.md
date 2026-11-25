# RoN Plugin Loader

A plugin loader for Rise of Nations: Extended Edition (Steam, `riseofnations.exe` build `0x6674863F`).

It loads plugin DLLs from `<game>\plugins\<Name>\<Name>.dll` in an order the player sets on a
Plugins panel in the main menu. Plugins can replace, add or generate game files, hook game
functions through one shared hook manager, and declare whether they change the simulation, so a
multiplayer lobby can check that every player runs the same set.

Status: design. See `docs/design.md`; build plans are in `docs/plans/`.
`python tools/plan_html.py <plan.md> <out.html>` turns a plan into one HTML page for review.
