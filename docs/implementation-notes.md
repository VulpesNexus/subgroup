# Implementation notes

Approaches that look like they should work and do not, and the API behavior
behind them. Written down because each one costs a day to rediscover.

For what the underlying restriction actually is, see
[Why Illustrator declines](why-illustrator-declines.md).

---

## Four dead ends


### The command notifiers cannot carry this feature

The first design listened to `kAIGroupCommandPreNotifierStr` /
`…PostNotifierStr` around *Object > Group*, on the theory that *Ctrl+G* need never
be rebound. **That does not work.**

Illustrator does not dispatch *Object > Group* **at all** when the selection is
already a single group. Measured by hand at the keyboard: grouping two loose
rectangles fires `Before Group` and `After Group` normally, and pressing
*Ctrl+G* again on the resulting group fires **nothing**. The command is
short-circuited before it runs, so there is no notifier in exactly the case this
plugin exists to handle.

This is easy to miss, because `executeMenuCommand("group")` *does* fire both
notifiers — it force-dispatches and bypasses the enablement check. A scripted
test therefore passes while the real keystroke does nothing. **Test the keyboard
path, not the scripting path.** The notifiers are still registered, but only so
the diagnostic build can log whether the native command ran.

### The shortcut has to be assigned by hand

`AIMenuSuite::SetItemCmd` is not a way out. Calling `SetItemCmd(item, 'g', 0)`
returns `kNoErr` and `GetItemCmd` reads the value back correctly, but
Illustrator does not honor a plugin's claim over a shortcut an existing
built-in owns: *Ctrl+G* still reaches *Object > Group*. It reports success and
silently declines.

Worse, it re-asserts that conflicting binding at every launch, quietly editing
the user's saved keyboard set. The call is deliberately absent from this
plugin and should stay absent.

*.kys* files are plain PostScript-style text, so a keyboard set can be inspected
directly: `/Menus { /<command name> { /Context /Modifiers /Represent /Key } }`,
where Modifiers 64 is *Ctrl* and Key 71 is *G*. This is also why
`kSubgroupGroupCmd` must never be renamed — it is the key the assignment is
stored under, and changing it silently orphans the user's *Ctrl+G*.

### A plugin cannot add items to a native submenu's group

`AddMenuGroup` anchored near `kAlignObjectMenuGroup` returns `kNoErr`, and then
adding items to that group fails with `kBadParameterErr` (1346458189 =
`0x5041524D` = `'PARM'`). The group is created and is useless. The align
commands live in the plugin's own submenu for this reason.

The related trap: an early `return` on that failure cost every *later* menu item
and both notifiers, silently disabling the whole plugin. Notifiers are now
registered first, and optional menu placement cannot take the core down with it.

### Menu items lay out in insertion order

Menu *groups* control separators, not sequence. Items appear in the order they
are added, so the combined-center entry is inserted between the two triples
rather than after them, despite its own group being created between theirs.

---

## Traps worth knowing before extending it

**Illustrator's preference getters cannot express "never written."** Measured on
30.7, `GetBooleanPreference` returns `kNoErr` — success — with `false` for a key
that was never set, so a fallback argument can never fire. ExtendScript's
equivalent has the same defect pointing the other way, returning `true`. Neither
can carry a default. Every flag here is therefore stored in the sense where the
unset reading, `false`, *is* the wanted default: `nestingDisabled`, not
`nestingEnabled`.

**A layer is itself backed by a group art object.** It appears in
`GetSelectedArt` as a group with a null parent, so a naive "outermost selected
object" walk picks the layer and nests the whole layer's contents. Skip it with
`AIArtSuite::IsArtLayerGroup`, and do not let it count as a selected parent
either, or every real top-level object looks nested.

**"Did the command act?" cannot be answered by first-child identity.** Grouping
a subset puts the new group at the topmost selected index, which is often not
zero, so the first child is unchanged and a plugin wrongly concludes the
command declined — adding a spurious level to ordinary grouping. The
discriminator is `kArtFullySelected`.

**`GetMatchingArt` with `kArtSelectedTopLevelGroups` returned nothing** here
(`err=0, n=0`), which is why the top level of the selection is derived from
`GetSelectedArt` by hand.

**Key art does not survive a hand-made selection change.** Illustrator cancels
it on every one, so a one-shot `SetKeyArt` never reaches the moment the user
opens *Align*. This is invisible to a scripted test, because setting
`app.selection` from the DOM does not cancel key art the way clicking does.

**`SDKAboutPluginsHelper::PopAboutBox` cannot carry non-ASCII text.** It takes a
`char*` and builds an `ai::UnicodeString` from it with the default encoding,
which is the *platform* one — so an em dash or a copyright sign becomes mojibake
on any machine whose code page is not Latin-1, and the author never sees it
because their own machine usually is.

**Nothing Illustrator or Windows hands you will style a word mid-sentence.**
Three dialogs were tried before the current one:

- `MessageAlert`, which `PopAboutBox` ends in, is a plain OS alert — one run of
  unstyled text, no emphasis, no links.
- Windows' **task dialog** looks the part and holds links, but only in its
  content and footer: the main instruction, the one piece of text with any
  visual weight, cannot be one. Its markup is links and nothing else.
- A dialog of **static controls** gets bold, because a static can be given a
  bold font. It cannot get italics *inside a sentence*, because a static has
  exactly one font.

So the prose lives in a **rich edit control**, the one common control that can
change font mid-sentence, fed RTF. Command names are bold where they head a
paragraph and italic where they are mentioned inside one. Links are `SysLink`
controls. The two-band background — white above, button face below, divided by
a rule — is the task dialog's own layout, reproduced by hand; that appearance
never depended on the task dialog.

*Msftedit.dll* must be loaded before the dialog is created, since that is what
registers the `RICHEDIT50W` class the template names. Without it the control
silently fails to create and the dialog comes up with a hole in it.

**DOM-driven verification does not reproduce hand gestures**, and this is the
methodological lesson of the whole project. `executeMenuCommand` is not
*Ctrl+G*; `app.selection = [...]` is not a click — it never leaves an object
partially selected, and `app.selection = null` triggers deselect handling a
click does not. Every one of the dead ends above passed a scripted test before
failing in the hand.

---

## Looking at the *About* dialog

A modal dialog inside a host application is close to untestable: you cannot
drive it, screenshot it, or measure it without a person sitting in front of it.
So *SubgroupAbout.cpp* is written free of every Illustrator type — it takes a
module handle and a parent window and touches nothing else — and
*tools/AboutHarness/build.cmd* compiles that same file and that same dialog
resource into a small executable that just shows it.

```
tools\AboutHarness\build.cmd     from a Visual Studio x64 command prompt
AboutHarness.exe                 show the dialog
AboutHarness.exe /dark           show it in Illustrator's darkest colours
AboutHarness.exe /exit3000       show it, then close after three seconds
AboutHarness.exe /owner:x,y,w,h  stand a window in for Illustrator's
AboutHarness.exe /report:<file>  append where the dialog landed, then close it
```

The harness `#include`s the plugin's dialog template rather than copying it. A
copy would stop being evidence about what ships.

`build.cmd` needs a working directory that *cmd.exe* can change into, which a path containing characters outside the system codepage is not. *tools/probe-about-placement.ps1* sidesteps that by staging the sources into a plain-ASCII directory and driving the compiler from PowerShell, so it runs wherever the repository happens to live.

### Where the window opens

The last two switches exist to measure the placement. The About box centers on Illustrator's window; until 1.0.4 it did only that, so a host sitting against a screen edge could put the box partly off the desktop or under the taskbar, where prose that does not scroll cannot be read. It is now pushed back inside the work area of whatever monitor it lands on, by *plugin/Source/DialogPlacement.h* — the same file, with the same reasoning, that *LiveShear* uses, because both plugins show the same window.

None of this needs Illustrator: where the box opens is decided entirely by the owner window's rectangle and the monitor that rectangle falls on, so a plain window is a complete stand-in for the host. `tools\probe-about-placement.ps1` drives the cases that matter — against each edge, off each edge, and on a monitor whose origin is negative — and writes *docs/about-placement.tsv*. Each case also records where the pre-1.0.4 arithmetic would have put the window, because a check that passes equally before and after a fix has not tested the fix.

Two traps are worth knowing if you extend it. *SPI_GETWORKAREA* returns the primary monitor's work area and nothing else, so it is wrong the moment a second monitor exists; ask *MonitorFromRect* and *GetMonitorInfoW* instead. And *CW_USEDEFAULT* is documented for overlapped windows only — for a popup the coordinates are taken as zero, which is not a fallback position but the top-left corner of the primary monitor.

