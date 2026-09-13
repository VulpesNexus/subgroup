// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vixen420
//
// Subgroup is free software: you may redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. It comes with ABSOLUTELY NO WARRANTY. See the file LICENSE, or
// <https://www.gnu.org/licenses/>, for the full text.
//
// Additional permission under GPL-3.0 section 7: this file may be combined with
// the Adobe Illustrator SDK, whose sample framework sources are compiled into
// every plugin built from it. See LICENSE-EXCEPTION.

#ifndef __SUBGROUPID_H__
#define __SUBGROUPID_H__

#define kSubgroupPluginName         "Subgroup"

/* One place for the version: three numbers, with every other form built out of
   them. Subgroup.rc builds the Windows VERSIONINFO resource from the comma
   form and the About box shows the string form, and neither is written out by
   hand, so the two cannot come to say different things.

   They used to be two hand-written lines, "1,0,3,0" and "1.0.3", which is the
   arrangement where a release bumps one and forgets the other: the binary's
   fixed version field and the version a person reads in the About box would
   then disagree, and nothing would say so. The resource compiler expands these
   macros the same way the C++ compiler does -- the stringify operator and
   literals written side by side both work in a .rc -- so there is no reason to
   keep a second copy for it.

   Keep the numbers in step with the VERSION file at the root of the
   repository; tools\package.ps1 refuses to package a build whose version
   resource disagrees with that file, in either field. */
#define kSubgroupVersionMajor       1
#define kSubgroupVersionMinor       0
#define kSubgroupVersionPatch       3

#define SG_STRINGIFY2(x)            #x
#define SG_STRINGIFY(x)             SG_STRINGIFY2(x)

/* VERSIONINFO's fixed field takes four numbers. The fourth is the build
   number, which nothing here counts, so it stays zero. */
#define kSubgroupVersionCommas      kSubgroupVersionMajor,kSubgroupVersionMinor, \
                                    kSubgroupVersionPatch,0
#define kSubgroupVersionString      SG_STRINGIFY(kSubgroupVersionMajor) "." \
                                    SG_STRINGIFY(kSubgroupVersionMinor) "." \
                                    SG_STRINGIFY(kSubgroupVersionPatch)

/* Wide flavors of the same text, for the Windows dialogs. The two-step
   expansion is what makes the argument expand before L is pasted onto it.

   The non-ASCII characters are escapes rather than literal glyphs so every
   source file that uses them stays plain ASCII: this machine's code page is
   932, and a literal em dash in a narrow or wide literal would be decoded
   through whatever the compiler guessed the file's encoding was. Menu paths use
   ">" rather than an arrow, which is the convention Adobe's own documentation
   follows. */
#define SG_WIDEN2(x)                L ## x
#define SG_WIDEN(x)                 SG_WIDEN2(x)
/* Widened a number at a time. kSubgroupVersionString is three string literals
   side by side rather than one token, and the paste above only ever reaches
   the first of them. */
#define SG_WVERSION                 SG_WIDEN(SG_STRINGIFY(kSubgroupVersionMajor)) L"." \
                                    SG_WIDEN(SG_STRINGIFY(kSubgroupVersionMinor)) L"." \
                                    SG_WIDEN(SG_STRINGIFY(kSubgroupVersionPatch))

#define SG_EMDASH                   L"\x2014"   /* U+2014 em dash */
#define SG_COPY                     L"\x00A9"   /* U+00A9 copyright sign */

#define SG_REPO_URL                 L"https://github.com/VulpesNexus/subgroup"
#define SG_AUTHOR_URL               L"https://github.com/VulpesNexus"

/** The group every plugin from this publisher shares under Help > About. The
    SDK's own default is "About SDK Plug-ins", which describes these as Adobe
    samples and mixes them in with everyone else's.

    The name is what the helper matches on, so identical strings put two
    plugins in one submenu rather than two. The title is taken from whichever
    plugin creates the group first, so it has to match as well or the menu is
    named by load order. LiveShear defines the same pair. */
#define kSubgroupAboutGroupName     "VulpesNexusAboutPluginsGroupName"
#define kSubgroupAboutGroupTitle    "About VulpesNexus Plug-ins"

/* Persisted settings. AIPreferenceSuite takes a prefix and a suffix.
 *
 * Illustrator's preference getters cannot express "never written". Measured on
 * 30.7: GetBooleanPreference returns kNoErr - success, not an error - with
 * false for a key that was never set, so a fallback argument can never fire.
 * (ExtendScript's getBooleanPreference has the same defect in the opposite
 * direction: it returns true. Neither can carry a default.)
 *
 * So every flag is stored in the sense where the unset reading, false, IS the
 * wanted default. Hence "nestingDisabled" rather than "nestingEnabled": unset
 * means not disabled, which means the feature is on out of the box.
 */
#define kSubgroupPrefPrefix         "Subgroup"
#define kSubgroupPrefNestingOff     "nestingDisabled"
#define kSubgroupPrefDebugLog       "debugLog"
/* Unset reads false, which is vanilla: a newly made group arrives collapsed. */
#define kSubgroupPrefKeepOpen       "keepNewGroupsOpen"

/* Menu wiring. */
/* Our own group in the Object menu, placed above the Group/Ungroup block. */
#define kSubgroupRootMenuGroup      "Subgroup Root Group"
#define kSubgroupMenuGroup          "Subgroup Group"
/* A second group inside the same submenu, carrying a separator above it, so the
   toggles are visually divided from the commands. */
#define kSubgroupOptionsMenuGroup   "Subgroup Options Group"
#define kSubgroupSDKString          "SDK"
#define kSDKMenuGroup               "SDK Group"
/* Nest Down and Nest Up are deliberately mirrored, because the operations
   mirror each other: Down puts the new level inside the selected group, Up puts
   it around. "Nest Whole Groups" says what the toggle actually governs - whether
   Nest Down acts on a selection that is an entire group, or declines the way
   Illustrator does. */
#define kSubgroupToggleString       "Nest Whole Groups"
#define kSubgroupWrapString         "Nest Up"

/* Internal menu command names. Kept separate from the preference keys so
   renaming a stored flag cannot silently rename a menu command. */
#define kSubgroupToggleCmd          "SubgroupToggleNesting"
#define kSubgroupWrapCmd            "SubgroupWrap"
#define kSubgroupLogCmd             "SubgroupToggleLog"
/* In-group alignment.
 *
 * The only thing vanilla Illustrator cannot do. Everywhere else its key object
 * covers it: with a proper subset of a group selected you can click one of them
 * with the Selection tool to make it the key, and Object > Align works. What it
 * cannot reach is a group's contents as a whole, because selecting all of them
 * IS selecting the group - one object, so Align shifts the group against the
 * artboard instead of arranging what is inside it.
 *
 * So the plugin offers exactly that: select ONE object inside a group, and its
 * siblings move to meet it. The selection already says which object should hold
 * still, so there is no anchor to name and nothing to remember.
 *
 * An earlier version had Set / Clear Align Anchor for feeding Illustrator's own
 * key object. Redundant - vanilla can already nominate a key object in every
 * case where Align is capable of using one.
 */
#define kSubgroupAlignMenuGroup     "Subgroup Align Group"
#define kSubgroupAlignVMenuGroup    "Subgroup Align Vertical Group"
#define kSubgroupAlignBothMenuGroup "Subgroup Align Both Group"

#define kSubgroupAlignRootCmd       "SubgroupAlignRoot"
#define kSubgroupAlignRootString    "Align Group To Selected"

#define kSubgroupAlignLeftCmd       "SubgroupAlignLeft"
#define kSubgroupAlignHCenterCmd    "SubgroupAlignHCenter"
#define kSubgroupAlignRightCmd      "SubgroupAlignRight"
#define kSubgroupAlignTopCmd        "SubgroupAlignTop"
#define kSubgroupAlignVCenterCmd    "SubgroupAlignVCenter"
#define kSubgroupAlignBottomCmd     "SubgroupAlignBottom"
/* Both axes at once, matching the combined center align Adobe added recently. */
#define kSubgroupAlignBothCmd       "SubgroupAlignBoth"

/* Diagnostic: writes the selected/fully-selected/targeted state of everything
   in the document to the log, so a gesture can be measured rather than guessed
   at. Goes away with the rest of the diagnostics. */
#define kSubgroupProbeCmd           "SubgroupProbeSelection"
#define kSubgroupProbeString        "Log Selection State"

#define kSubgroupKeepOpenCmd        "SubgroupToggleKeepOpen"
#define kSubgroupKeepOpenString     "Keep New Groups Open"
/* Do NOT rename kSubgroupGroupCmd. It is the key this command is stored under
   in the user's keyboard set (.kys), so changing it silently orphans their
   Ctrl+G assignment. The visible label can change freely; this cannot. */
#define kSubgroupGroupCmd           "SubgroupGroup"
#define kSubgroupGroupString        "Nest Down"
#define kSubgroupLogToggleString    "Write Diagnostic Log"

#endif /* __SUBGROUPID_H__ */
