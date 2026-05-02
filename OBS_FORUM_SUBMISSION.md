# OBS Forum Submission Draft: Scene Organiser

## Resource Title

Scene Organiser

## Version

1.1.0

## Category

OBS Studio Plugins

## Tags

scenes, scene collection, organiser, organizer, folders, drag and drop, dock,
workflow

## Short Tagline

Organise OBS scenes into folders, separators, and notes.

## Supported Bit Versions

64-bit

## Supported Platforms

Windows

## Minimum OBS Studio Version

30.0.0

## Source Code URL

https://github.com/KSTYER1/obs-scene-organiser

## Download URL

TODO: publish or upload the 1.1.0 release package

## Overview

Scene Organiser is an unofficial third-party native dock for OBS Studio. It
lets you organise scenes into colour-coded folders with drag and drop, live
search, separators, and free-text annotations.

It is useful for large scene collections where the default flat scene list gets
hard to scan.

All state is persisted per scene collection, so each OBS collection can have
its own folder structure and notes.

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
- Header row at the top of the dock with total scenes and sources in the
  current scene collection.
- Recursive scene/source count badge on every folder.
- Live counter updates when scenes or sources are added or removed.

## Installation

Download the Windows x64 release archive and extract or copy its contents into
your OBS Studio installation directory.

The final layout should include:

```text
obs-plugins/64bit/obs-scene-organiser.dll
data/obs-plugins/obs-scene-organiser/locale/en-US.ini
data/obs-plugins/obs-scene-organiser/locale/de-DE.ini
```

The release archive also includes `INSTALL.bat`, which can copy the plugin into
a selected OBS directory.

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

## What's New in 1.1.0

- Added a header row showing total scenes and sources in the current
  collection.
- Added a per-folder badge with recursive scene and source counts.
- Counters update live when scenes or sources are added or removed.

## Version 1.0.0

- Initial release.
- Added nested folders for OBS scenes.
- Added drag and drop reordering.
- Added live search.
- Added separators and text notes.
- Added per-scene-collection persistence.
- Added native OBS scene context menu access.

## Support / Bugs

Please report issues in the resource discussion thread or in the GitHub issue
tracker once the repository is published.

## License

GPL-2.0-or-later.

## Disclaimer

Scene Organiser is an unofficial third-party plugin and is not affiliated with
or endorsed by the OBS Project.

AI-assisted tools were used during development and release preparation. The
maintainer is responsible for reviewing, testing, and publishing the released
plugin.

## Pre-Submit Checklist

- [x] Public GitHub repository exists.
- [x] README is visible on GitHub.
- [x] GPL license is visible on GitHub.
- [x] Source Code URL field points to the repository.
- [ ] Release ZIP is attached to GitHub Releases or uploaded to the forum.
- [ ] At least one screenshot/GIF is added to the resource description.
- [ ] Description is in English.
- [ ] No OBS logo is used as resource icon or marketing artwork.
