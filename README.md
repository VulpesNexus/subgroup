# Subgroup

An Adobe Illustrator plugin for two things Illustrator refuses to do:

- **nest a group inside itself** — *Ctrl+G* does nothing when the selection is
  already a whole group, which is why subgrouping "needs a third object";
- **align a group's contents to one object inside it** — selecting all of them
  *is* selecting the group, so *Align* has nothing to align to.

Windows x64, built against the Illustrator 2026 SDK, tested on 30.7.

## Install

Put *Subgroup.aip* in *%LOCALAPPDATA%\Adobe Illustrator Plug-ins\30*, point
*Preferences > Plug-ins & Scratch Disks > Additional Plug-ins Folder* at that
folder, and restart Illustrator. No admin rights; deleting the *.aip*
uninstalls it.

The *30* is Illustrator 2026's version number, and next year's Illustrator gets
its own folder beside it: a plugin built against one year's SDK is not
guaranteed to load in another, and one built for Illustrator 2026 does not load
in Illustrator 2025.

**Share that folder with your other Illustrator plugins.** Illustrator has only
one Additional Plug-ins Folder, so giving each plugin a folder of its own means
that pointing the preference at the newest one silently stops the rest from
loading. Everything in the one folder loads.

Prebuilt binary: [*install/*](install), or the
[latest release](../../releases/latest).

Replacing the file needs Illustrator fully closed — it holds the plugin open
while running.

## Use

Everything is under *Object > Subgroup*.

| | |
| --- | --- |
| **Nest Down** | Adds a level of grouping *inside* the selection, including when the selection is an entire group. Anywhere else it groups exactly as Illustrator would. |
| **Nest Up** | Adds that level *around* the selection instead. Same shape, different group object left outermost — which matters if the original carries an appearance. |
| **Align Group To Selected** | Select **one object inside a group**; its siblings move to meet it. Seven alignments, with combined-center between the horizontal and vertical triples. |
| **Nest Whole Groups** | Off, *Nest Down* declines a whole-group selection exactly as Illustrator does. On by default. |
| **Keep New Groups Open** | Leaves a new group expanded in the *Layers* panel. Off by default, matching vanilla. |

Commands gray out when they do not apply. *Align Group To Selected* needs
exactly one child of one group selected — a whole group singles out nothing, and
picks spread across two groups are ambiguous. Partial selection counts, so the
*Direct Selection* tool works.

Ancestors of a new group are always re-expanded regardless of the toggle. Native
grouping collapses the *topmost* ancestor rather than the new group, so artwork
several levels down appears to vanish from the *Layers* panel.

### Shortcuts

Every command is assignable under *Edit > Keyboard Shortcuts… > Menu Commands >
Object > Subgroup*. *Nest Down* is the one worth a key: give it *Ctrl+G* and
grouping starts working where Illustrator declines, with nothing else changed.
Illustrator will warn that *Ctrl+G* belongs to *Object > Group*; accepting is
safe, and clearing the assignment restores stock behavior completely.

The plugin never touches your keyboard set itself —
[it cannot, safely](docs/implementation-notes.md#the-shortcut-has-to-be-assigned-by-hand).

## Build

Visual Studio 2022 (*v143*) against the Illustrator 2026 SDK. The project holds
no machine-specific path, so give it the SDK location one of three ways: a
*/p:AISDK="&lt;path&gt;"* switch, an *AISDK* environment variable, or an
untracked *plugin\AISDK.props*. With none set the build stops with a readable
message.

```
msbuild plugin\Subgroup.vcxproj /p:Configuration=Release /p:Platform=x64 /p:AISDK="<path to SDK>"
```

The SDK is version-gated per suite, so a CS6 SDK cannot produce a plugin that
loads into 30.7 — the v30 SDK from the Adobe Developer Console is required.

*tools\AboutHarness\build.cmd* builds the *About* dialog as a standalone
executable, which is the only way to look at it outside Illustrator.

*tools\package.ps1* assembles the release zip from tracked files, taking the
version from *VERSION* and refusing to run if *install\Subgroup.aip* was built
from a different one.

## Limits

- A click on nested art still resolves to the outermost group. This changes
  structure, not what a selection means — the rule lives upstream of the
  commands, and [nothing can reach it](docs/why-illustrator-declines.md).
- Locked or hidden art cannot be reparented or moved. *Nest Down* stays silent
  about it, matching vanilla; *Align Group To Selected* skips those siblings.
- *Nest Up* needs all selected objects to share one parent. Illustrator's own
  *Group* command already handles a cross-container selection, so that case is
  left alone.
- Windows x64 only as built. ARM64 configurations exist but are untested, as is
  macOS.

## Also here

- [*scripts/*](scripts) — an ExtendScript version, for anyone without the SDK.
  It does rebind *Ctrl+G*, which the plugin avoids.
- [Why Illustrator declines](docs/why-illustrator-declines.md) — what the
  restriction actually is, measured rather than guessed, and whether working
  around it costs anything.
- [Implementation notes](docs/implementation-notes.md) — four approaches that
  look like they should work and do not, and the API traps behind them.

## License

**GPL-3.0-or-later**, with an **Adobe Illustrator SDK linking exception**
([LICENSE](LICENSE), [LICENSE-EXCEPTION](LICENSE-EXCEPTION)).

The exception is load-bearing, not boilerplate: a release build compiles seven
of its nine object files from Adobe's sample framework, under terms GPL section
10 forbids passing on. The repository itself vendors no Adobe code, and
*scripts/* links nothing.

Vibecoded by [Vixen420](https://github.com/VulpesNexus) in September 2026.
Copyright © 2026 Vixen420.
