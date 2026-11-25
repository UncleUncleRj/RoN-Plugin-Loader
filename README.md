# RoN Plugin Loader

## What this does:

### Adds the ability for **Rise of Nations: Extended Edition** to load advanced .dll plugin mods and sort their loading order!

You download from modders a .dll plugin file into a special `/plugins/` folder in the game directory,
then choose in-game which ones to run, and in what order, from the game's own main menu,
similar to the steam workshop window already in-game.

- No extra launcher needed, no extra program to start.
- Works alongside the game's own mods and Steam Workshop items.
- With no plugins installed, the game plays exactly as before; you only get a new **Plugins**
  button in the menu.

## What you need

- Rise of Nations: Extended Edition on Steam, up to date.
- Windows 10 or higher (Windows 7 support is planned!).

The loader is made for the current Steam version of the game. If a game update ever changes it, the
loader switches itself off (the game then runs without plugins) until a matching release is out.

## Installation instructions for the loader

1. Download `RoN-Plugin-Loader-<version>.zip` from the [Releases page](../../releases/latest).
2. Open the game folder: in Steam, right-click **Rise of Nations: Extended Edition** and choose
   **Manage → Browse local files**.
3. Copy `shfolder.dll` from the zip file into that folder, next to `riseofnations.exe`.
4. Start the game from Steam as usual. In the main menu, open **Tools & Extras**: a new **Plugins**
   button sits under Steam Workshop. If it's there, the loader works.

## Add a plugin mod

1. In the game folder there is now a `plugins` folder (the loader makes it the first time the game
   starts). A plugin is a `.dll` file and an `.ini` file with the same name, often with a folder of
   its own. Copy all of them straight into `plugins`, side by side.

   Let's say you download a plugin called Better Minimap:
   ```
   Rise of Nations\plugins\
       Better Minimap.dll
       Better Minimap.ini
       Better Minimap\        (only if the plugin comes with one)
   ```
3. Start the game. A new plugin is switched on and goes to the bottom of the list.

## Choose your plugins and their order

Main menu → **Tools & Extras → Plugins**.

- **The check box** turns a plugin on or off.
- **Move Up** and **Move Down** change the order. When two plugins change the same thing, **the one
  lower in the list wins**.
- **Apply and Restart** saves your choices and restarts the game with them. **Back** leaves without
  changing anything.

Select a plugin to see its version, author and description.

## Multiplayer

Everyone in a multiplayer game must have the same plugins, in the same order, or the match goes
out of sync.

The loader doesn't check this for you (yet), so agree on your plugins before you start.

Plugins that only change the look or the interface don't have to match; the plugin's author can
tell you which kind theirs is.

## To report a problem

- [open an issue](../../issues) and attach `plugins\loader.log` (its first line
names the loader's version).

## Uninstall

Delete `shfolder.dll` from the game folder; the next start loads no plugins and the Plugins button
is gone. To remove your plugins as well, delete the `plugins` folder.

## For plugin makers and developers

How to write a plugin, build the loader and how it works inside:
[docs/developers.md](docs/developers.md).
