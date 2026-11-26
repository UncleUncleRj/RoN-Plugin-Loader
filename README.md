# RoN Plugin Loader

Plugins for **Rise of Nations: Extended Edition**. Put plugins in a folder, then choose which ones
run, and in what order, from the game's own main menu.

- Works with Steam's Play button: no launcher, no extra program to start.
- Works alongside the game's own mods and Steam Workshop items.
- With no plugins installed, the game plays exactly as before; you only get a new **Plugins**
  button in the menu.

## What you need

- Rise of Nations: Extended Edition on Steam, up to date.
- Windows.

The loader is made for the current Steam version of the game. If a game update ever changes it, the
loader switches itself off (the game then runs without plugins) until a matching release is out.

## Install

1. Download `RoN-Plugin-Loader-<version>.zip` from the [Releases page](../../releases/latest).
2. Open the game folder: in Steam, right-click **Rise of Nations: Extended Edition** and choose
   **Manage → Browse local files**. It's the folder that holds `riseofnations.exe`.
3. Unzip the download and copy `shfolder.dll` into that folder, next to `riseofnations.exe`. Only
   `shfolder.dll` goes there: the zip's other files are these steps (`installation.txt`) and the
   license (`LICENSE.txt`).
4. Start the game from Steam as usual. In the main menu, open **Tools & Extras**: a new **Plugins**
   button sits under Steam Workshop. If it's there, the loader works.

The file shares its name with a small Windows file on purpose: the game picks up this copy from its
own folder. Windows' own copy is not touched.

## Add a plugin

1. In the game folder there is now a `plugins` folder (the loader makes it the first time the game
   starts). A plugin is a `.dll` file and an `.ini` file with the same name, often with a folder of
   its own. Copy all of them straight into `plugins`, side by side. For a plugin called Better
   Minimap:
   ```
   Rise of Nations\plugins\
       Better Minimap.dll
       Better Minimap.ini
       Better Minimap\        (only if the plugin comes with one)
   ```
2. Start the game. A new plugin is switched on and goes to the bottom of the list.

## Choose your plugins and their order

Main menu → **Tools & Extras → Plugins**.

- **The check box** turns a plugin on or off.
- **Move Up** and **Move Down** change the order. When two plugins change the same thing, **the one
  lower in the list wins**.
- **Apply and Restart** saves your choices and restarts the game with them. **Back** leaves without
  changing anything.

Select a plugin to see its version, author and description.

## Multiplayer

Everyone in a multiplayer game needs the same gameplay plugins, in the same order, or the match goes
out of sync. The loader doesn't check this for you yet, so agree on your plugins before you start.
Plugins that only change the look or the interface don't have to match; the plugin's author can
tell you which kind theirs is.

## If something goes wrong

- **No Plugins button.** Check that `shfolder.dll` is right next to `riseofnations.exe`, and that
  the game is the current Steam version. Then look in `plugins\loader.log` in the game folder,
  which the loader writes at every start; no `plugins` folder at all means the loader didn't run.
- **The game crashed or froze while starting.** The next start runs without any plugins, and the
  Plugins panel says so. The plugin that was loading at the time is marked **failed**: turn it off,
  then Apply and Restart.
- **A plugin is marked failed.** Select it on the Plugins panel; its description says why.
- **You have an older RON Revived installed** (one that put a `winmm.dll` in the game folder). The
  loader loads no plugins while it's there. Uninstall that version first.

To report a problem, [open an issue](../../issues) and attach `plugins\loader.log` (its first line
names the loader's version).

## Update

Download the new release and replace `shfolder.dll` in the game folder. Your plugins and their order
stay as they are.

## Uninstall

Delete `shfolder.dll` from the game folder; the next start loads no plugins and the Plugins button
is gone. To remove your plugins as well, delete the `plugins` folder.

## For plugin makers and developers

How to write a plugin, build the loader and how it works inside:
[docs/developers.md](docs/developers.md).

## License

RoN Plugin Loader is copyright 2026 Rj Simas. It is under the Mozilla Public License 2.0
([LICENSE](LICENSE)), except the plugin API header, `include/ron_plugin.h`, which is under the MIT
license (its notice is in the file) so that any plugin may copy it.

This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
