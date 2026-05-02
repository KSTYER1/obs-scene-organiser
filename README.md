# Scene Organiser

Scene Organiser is a third-party native OBS Studio dock for organising scenes
into colour-coded folders with drag and drop, live search, separators, and
free-text annotations.

All state is stored per scene collection, so each collection can have its own
organisation structure.

## Features

- Group scenes into nested folders.
- Colour bar and folder icon per folder, scene, separator, and text item.
- Drag and drop to reorder items or move them into and out of folders.
- Live search filters the tree while typing.
- Separators for visual grouping.
- Text fields for inline notes and section headers.
- Bottom toolbar for add, delete, move up, and move down actions.
- Keyboard shortcuts: `Ctrl + Up`, `Ctrl + Down`, and `Del`.
- Right-click scene entries to open OBS' native scene context menu.
- Per-scene-collection persistence as JSON in the OBS plugin config folder.
- Bold highlight for the currently active scene.

## Requirements

- OBS Studio 30.x, 31.x, or 32.x
- Windows x64 for the packaged release
- Qt 6, provided by OBS Studio

## Installation

### Windows

Download the release archive and extract or copy its contents into your OBS
Studio installation directory.

The final layout should include:

```text
obs-plugins/64bit/obs-scene-organiser.dll
data/obs-plugins/obs-scene-organiser/locale/en-US.ini
data/obs-plugins/obs-scene-organiser/locale/de-DE.ini
```

The packaged release also includes `INSTALL.bat`, which can copy the plugin
files into a selected OBS directory.

Restart OBS after installation. The dock appears under:

```text
View -> Docks -> Scene Organiser
```

## Basic Usage

1. Open `View -> Docks -> Scene Organiser`.
2. Use the toolbar to create folders, separators, or text notes.
3. Drag scenes into folders or reorder them in the tree.
4. Use search to filter larger scene collections.
5. Right-click a scene to access OBS' native scene context menu.

Scene Organiser stores its layout separately for each OBS scene collection.

## Building from Source

Requires CMake 3.28 or newer, Qt 6, OBS development headers, and a supported
compiler toolchain.

```bash
git clone https://github.com/kstyer/obs-scene-organiser.git
cd obs-scene-organiser
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

The project uses the
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) build
structure.

## Version History

### 1.1.0

- Added a header row showing total scenes and sources in the current
  collection.
- Added a per-folder badge showing recursive scene and source counts.
- Counters update live when scenes or sources are added or removed.

### 1.0.0

- Initial release.
- Added nested folders for OBS scenes.
- Added drag and drop reordering.
- Added live search.
- Added separators and text notes.
- Added per-scene-collection persistence.
- Added native OBS scene context menu access.

## License

Scene Organiser is licensed under GPL-2.0-or-later.

## Disclaimer

Scene Organiser is an unofficial third-party plugin and is not affiliated with
or endorsed by the OBS Project.

AI-assisted tools were used during development and release preparation. The
maintainer is responsible for reviewing, testing, and publishing the released
plugin.
