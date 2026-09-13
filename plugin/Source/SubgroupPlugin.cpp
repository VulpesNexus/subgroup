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

#include "IllustratorSDK.h"
#include "SubgroupPlugin.h"
#include "AIMenuCommandNotifiers.h"

#include <vector>
#include <cstdio>
#include <cstdarg>
#include <string>

#ifdef WIN_ENV
/* The About dialog. Kept in a file of its own, free of Illustrator types, so it
   can also be built into tools/AboutHarness and actually looked at. */
#include "SubgroupAbout.h"
#endif

/* Diagnostic log. A plugin has no console, and guessing at which step fails is
   slower than writing the answer down.
 *
 * Off unless switched on from Object > Subgroup, so a normal install writes
 * nothing. The path is resolved from the environment rather than hard-coded,
 * so it carries to any machine. */
/* Diagnostics are a development tool, not a shipped feature. They compile away
   entirely in Release: no log file, no menu items, and SGLog reduces to nothing
   at every call site. Build with SUBGROUP_FORCE_DIAGNOSTICS to get them back in
   a Release build when something needs investigating. */
#if defined(_DEBUG) || defined(SUBGROUP_FORCE_DIAGNOSTICS)
#define SUBGROUP_DIAGNOSTICS 1
#endif

#ifdef SUBGROUP_DIAGNOSTICS
bool gSGLogEnabled = false;
#endif

#ifdef SUBGROUP_DIAGNOSTICS

static const char* SGLogPath()
{
    static char path[MAX_PATH + 32] = { 0 };
    if (path[0] == 0) {
        char tmp[MAX_PATH] = { 0 };
        DWORD n = GetTempPathA(MAX_PATH, tmp);
        if (n == 0 || n > MAX_PATH) return nullptr;
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%sSubgroup.log", tmp);
    }
    return path;
}

static void SGLog(const char* fmt, ...)
{
    if (!gSGLogEnabled) return;
    const char* p = SGLogPath();
    if (p == nullptr) return;

    FILE* f = nullptr;
    if (fopen_s(&f, p, "a") != 0 || f == nullptr) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

#else   /* diagnostics compiled out */

static void SGLog(const char*, ...) {}

#endif

Plugin* AllocatePlugin(SPPluginRef pluginRef)
{
    return new SubgroupPlugin(pluginRef);
}

void FixupReload(Plugin* plugin)
{
    SubgroupPlugin::FixupVTable((SubgroupPlugin*) plugin);
}

SubgroupPlugin::SubgroupPlugin(SPPluginRef pluginRef)
    : Plugin(pluginRef),
      fGroupPreNotifier(nullptr),
      fGroupPostNotifier(nullptr),
      fGroupCmdMenu(nullptr),
      fToggleMenu(nullptr),
      fWrapMenu(nullptr),
      fKeepOpenMenu(nullptr),
      fProbeMenu(nullptr),
      fLogMenu(nullptr),
      fAboutPluginMenu(nullptr)
{
    for (int i = 0; i < 7; ++i) { fAlignMenu[i] = nullptr; fAlignMode[i] = kAlignLeft; }
    strncpy(fPluginName, kSubgroupPluginName, kMaxStringLength);
}

ASErr SubgroupPlugin::Message(char* caller, char* selector, void* message)
{
    ASErr error = kNoErr;
    try {
        error = Plugin::Message(caller, selector, message);
    }
    catch (ai::Error& ex) { error = ex; }
    catch (...)           { error = kCantHappenErr; }

    if (error) {
        if (error == kUnhandledMsgErr) error = kNoErr;
        else Plugin::ReportError(error, caller, selector, message);
    }
    return error;
}

ASErr SubgroupPlugin::StartupPlugin(SPInterfaceMessage* message)
{
    ASErr error = Plugin::StartupPlugin(message);
    if (error) return error;

#ifdef SUBGROUP_DIAGNOSTICS
    gSGLogEnabled = GetFlag(kSubgroupPrefDebugLog) ? true : false;
#endif

    /* Notifiers first: they are what makes the anchor work, and a menu
       placement problem must not be able to take them down. */
    error = this->AddNotifiers(message);
    if (error) return error;

    error = this->AddMenus(message);
#ifdef SUBGROUP_DIAGNOSTICS
    DumpMenus();
#endif
    return error;
}

ASErr SubgroupPlugin::AddNotifiers(SPInterfaceMessage* message)
{
    ASErr error = sAINotifier->AddNotifier(message->d.self, kSubgroupPluginName,
                                           kAIGroupCommandPreNotifierStr,
                                           &fGroupPreNotifier);
    SGLog("AddNotifier PRE  err=%d handle=%p", (int) error, (void*) fGroupPreNotifier);
    if (error) return error;

    error = sAINotifier->AddNotifier(message->d.self, kSubgroupPluginName,
                                     kAIGroupCommandPostNotifierStr,
                                     &fGroupPostNotifier);
    SGLog("AddNotifier POST err=%d handle=%p", (int) error, (void*) fGroupPostNotifier);
    if (error) return error;

    return error;
}

/* Every step is logged and, past the point where the plugin is usable,
   nothing is fatal.

   An earlier version returned on the first error. When AddMenuGroup could not
   find "Align Objects" - AIMenu.h warns that plugin load order is
   indeterminate - that single failure cost every later menu item AND both
   notifiers, silently disabling the whole plugin. The core commands are now
   built first, and the optional placement cannot take them down with it. */
ASErr SubgroupPlugin::AddMenus(SPInterfaceMessage* message)
{
    ASErr error = kNoErr;

    /* Our own group, not the SDK's. Its default files third-party plugins under
       "About SDK Plug-ins", which reads as though this were one of Adobe's
       samples, and puts each publisher's plugins in with everybody else's.

       The name is the key the helper matches on: whichever plugin loads first
       creates the group and the rest find it and add themselves, which is how
       every plugin from one publisher ends up in one submenu. The *title* comes
       from whoever creates it, so every plugin sharing the group has to pass
       the same title too -- otherwise the menu is named by load order. Both
       strings are duplicated verbatim in LiveShear for that reason. */
    SDKAboutPluginsHelper aboutPluginsHelper;
    aboutPluginsHelper.AddAboutPluginsMenuItem(message,
        kSubgroupAboutGroupName,
        ai::UnicodeString(kSubgroupAboutGroupTitle),
        "Subgroup...",
        &fAboutPluginMenu);

    /* --- the Subgroup submenu in the Object menu ------------------------- */

    AIMenuGroup rootGroup;
    error = sAIMenu->AddMenuGroup(kSubgroupRootMenuGroup, kMenuGroupAddAboveNearGroup,
                                  kArrangeGroupMenuGroup, &rootGroup);
    SGLog("menu: root group err=%d", (int) error);
    if (error) return error;

    AIPlatformAddMenuItemDataUS rootData;
    rootData.groupName = kSubgroupRootMenuGroup;
    rootData.itemText  = ai::UnicodeString(kSubgroupPluginName);
    AIMenuItemHandle rootItem = nullptr;
    error = sAIMenu->AddMenuItem(message->d.self, kSubgroupPluginName, &rootData, 0, &rootItem);
    SGLog("menu: root item err=%d", (int) error);
    if (error) return error;

    AIMenuGroup subMenuGroup;
    error = sAIMenu->AddMenuGroupAsSubMenu(kSubgroupMenuGroup, 0, rootItem, &subMenuGroup);
    SGLog("menu: submenu err=%d", (int) error);
    if (error) return error;

    AIPlatformAddMenuItemDataUS itemData;
    itemData.groupName = kSubgroupMenuGroup;

    itemData.itemText = ai::UnicodeString(kSubgroupGroupString);
    error = sAIMenu->AddMenuItem(message->d.self, kSubgroupGroupCmd, &itemData,
                                 kMenuItemWantsUpdateOption, &fGroupCmdMenu);
    SGLog("menu: Nest Down err=%d", (int) error);

    itemData.itemText = ai::UnicodeString(kSubgroupWrapString);
    error = sAIMenu->AddMenuItem(message->d.self, kSubgroupWrapCmd, &itemData,
                                 kMenuItemWantsUpdateOption, &fWrapMenu);
    SGLog("menu: Nest Up err=%d", (int) error);

    /* --- the toggles, built before anything optional -------------------- */

    AIMenuGroup optionsGroup;
    error = sAIMenu->AddMenuGroup(kSubgroupOptionsMenuGroup, kMenuGroupSeparatorOption,
                                  kSubgroupMenuGroup, &optionsGroup);
    SGLog("menu: options group err=%d", (int) error);

    if (error == kNoErr) {
        AIPlatformAddMenuItemDataUS optData;
        optData.groupName = kSubgroupOptionsMenuGroup;

        optData.itemText = ai::UnicodeString(kSubgroupToggleString);
        error = sAIMenu->AddMenuItem(message->d.self, kSubgroupToggleCmd, &optData,
                                     kMenuItemWantsUpdateOption, &fToggleMenu);
        SGLog("menu: Nest Whole Groups err=%d", (int) error);

        optData.itemText = ai::UnicodeString(kSubgroupKeepOpenString);
        error = sAIMenu->AddMenuItem(message->d.self, kSubgroupKeepOpenCmd, &optData,
                                     kMenuItemWantsUpdateOption, &fKeepOpenMenu);
        SGLog("menu: Keep New Groups Open err=%d", (int) error);

#ifdef SUBGROUP_DIAGNOSTICS
        optData.itemText = ai::UnicodeString(kSubgroupProbeString);
        error = sAIMenu->AddMenuItem(message->d.self, kSubgroupProbeCmd, &optData, 0, &fProbeMenu);
        SGLog("menu: Log Selection State err=%d", (int) error);

        optData.itemText = ai::UnicodeString(kSubgroupLogToggleString);
        error = sAIMenu->AddMenuItem(message->d.self, kSubgroupLogCmd, &optData,
                                     kMenuItemWantsUpdateOption, &fLogMenu);
        SGLog("menu: Write Diagnostic Log err=%d", (int) error);
#endif
    }

    /* --- in-group alignment ---------------------------------------------- */

    AIMenuGroup alignHostGroup;
    error = sAIMenu->AddMenuGroup(kSubgroupAlignMenuGroup, kMenuGroupSeparatorOption,
                                  kSubgroupMenuGroup, &alignHostGroup);
    SGLog("menu: align host group err=%d", (int) error);
    if (error != kNoErr) return kNoErr;

    AIPlatformAddMenuItemDataUS alignRootData;
    alignRootData.groupName = kSubgroupAlignMenuGroup;
    alignRootData.itemText  = ai::UnicodeString(kSubgroupAlignRootString);
    AIMenuItemHandle alignRootItem = nullptr;
    error = sAIMenu->AddMenuItem(message->d.self, kSubgroupAlignRootCmd, &alignRootData,
                                 0, &alignRootItem);
    SGLog("menu: Align Group To Selected root err=%d", (int) error);
    if (error != kNoErr) return kNoErr;
    AIMenuGroup alignGroup;
    error = sAIMenu->AddMenuGroupAsSubMenu("Subgroup Align Items Group", 0, alignRootItem, &alignGroup);
    SGLog("menu: align submenu err=%d", (int) error);
    if (error != kNoErr) return kNoErr;

    /* Both-axis centering sits BETWEEN the horizontal and vertical triples,
       where Adobe puts its combined center in the control bar - not tacked on
       the end. The group order is what places it, so it is built first and the
       vertical group hangs off it. */
    AIMenuGroup bothGroup;
    ASErr be = sAIMenu->AddMenuGroup(kSubgroupAlignBothMenuGroup, kMenuGroupSeparatorOption,
                                     "Subgroup Align Items Group", &bothGroup);
    SGLog("menu: align both group err=%d", (int) be);
    const char* bothGroupName = (be == kNoErr) ? kSubgroupAlignBothMenuGroup
                                               : "Subgroup Align Items Group";

    AIMenuGroup vGroup;
    error = sAIMenu->AddMenuGroup(kSubgroupAlignVMenuGroup, kMenuGroupSeparatorOption,
                                  bothGroupName, &vGroup);
    SGLog("menu: align vertical group err=%d", (int) error);
    const char* vGroupName = (error == kNoErr) ? kSubgroupAlignVMenuGroup : bothGroupName;
    /* Listed in the order they should APPEAR. Group membership alone did not
       decide the layout - Illustrator places items in the order they are added
       - so Center is inserted between the two triples rather than after them.

       The mode travels with the entry, so reordering this list can never leave
       the dispatch pointing at the wrong operation. It did exactly that once:
       a failed edit left fAlignMode unassigned and every direction performed
       Left, which looked like "only the first one works". */
    struct AlignItem { const char* cmd; const char* label; const char* group; AlignMode mode; };
    AlignItem kAligns[7] = {
        { kSubgroupAlignLeftCmd,    "Left",              "Subgroup Align Items Group", kAlignLeft    },
        { kSubgroupAlignHCenterCmd, "Horizontal Center", "Subgroup Align Items Group", kAlignHCenter },
        { kSubgroupAlignRightCmd,   "Right",             "Subgroup Align Items Group", kAlignRight   },
        { kSubgroupAlignBothCmd,    "Center",            bothGroupName,                kAlignBoth    },
        { kSubgroupAlignTopCmd,     "Top",               vGroupName,                   kAlignTop     },
        { kSubgroupAlignVCenterCmd, "Vertical Center",   vGroupName,                   kAlignVCenter },
        { kSubgroupAlignBottomCmd,  "Bottom",            vGroupName,                   kAlignBottom  },
    };
    for (int i = 0; i < 7; ++i) {
        AIPlatformAddMenuItemDataUS ad;
        ad.groupName = kAligns[i].group;
        ad.itemText  = ai::UnicodeString(kAligns[i].label);
        error = sAIMenu->AddMenuItem(message->d.self, kAligns[i].cmd, &ad,
                                     kMenuItemWantsUpdateOption, &fAlignMenu[i]);
        fAlignMode[i] = kAligns[i].mode;
        SGLog("menu: align %s err=%d", kAligns[i].label, (int) error);
    }

    return kNoErr;
}

/* ------------------------------------------------------------------ prefs -- */

AIBoolean SubgroupPlugin::GetFlag(const char* suffix) const
{
    AIBoolean value = false;
    if (sAIPreference->GetBooleanPreference(kSubgroupPrefPrefix, suffix, &value) != kNoErr)
        return false;
    return value;
}

void SubgroupPlugin::PutFlag(const char* suffix, AIBoolean value) const
{
    sAIPreference->PutBooleanPreference(kSubgroupPrefPrefix, suffix, value);
}

/* ------------------------------------------------------------------ menus -- */

AIBoolean SubgroupPlugin::IsAlignItem(AIMenuItemHandle item) const
{
    if (item == nullptr) return false;
    for (int i = 0; i < 7; ++i) if (item == fAlignMenu[i]) return true;
    return false;
}

ASErr SubgroupPlugin::GoMenuItem(AIMenuMessage* message)
{
    if (message->menuItem == fGroupCmdMenu) {
        SGLog("GoMenuItem: Subgroup Group invoked");
        this->DoGroup();
    }
    else if (message->menuItem == fToggleMenu) {
        AIBoolean nowOn = !AugmentedNestingOn();          /* flipping */
        PutFlag(kSubgroupPrefNestingOff, !nowOn);         /* stored inverted */
        SGLog("GoMenuItem: augmented nesting -> %d", (int) nowOn);
    }
    else if (message->menuItem == fWrapMenu) {
        SGLog("GoMenuItem: Wrap in New Group invoked");
        this->DoWrap();
    }
    else if (message->menuItem == fKeepOpenMenu) {
        AIBoolean next = !KeepNewGroupsOpen();
        PutFlag(kSubgroupPrefKeepOpen, next);
        SGLog("GoMenuItem: keep new groups open -> %d", (int) next);
    }
    else if (IsAlignItem(message->menuItem)) {
        for (int i = 0; i < 7; ++i)
            if (message->menuItem == fAlignMenu[i]) { this->DoAlign(fAlignMode[i]); break; }
    }
#ifdef SUBGROUP_DIAGNOSTICS
    else if (message->menuItem == fProbeMenu) {
        DumpSelectionState();
    }
    else if (message->menuItem == fLogMenu) {
        gSGLogEnabled = !gSGLogEnabled;
        PutFlag(kSubgroupPrefDebugLog, gSGLogEnabled);
    }
#endif
    else if (message->menuItem == fAboutPluginMenu) {
        ShowAboutBox();
    }
    return kNoErr;
}

/* Graying out. Illustrator disables Object > Group when there is nothing to
   group; our commands should behave the same rather than looking available and
   then doing nothing. Called when the menu is about to be shown. */
ASErr SubgroupPlugin::UpdateMenuItem(AIMenuMessage* message)
{
    if (message->menuItem == fGroupCmdMenu || message->menuItem == fWrapMenu) {
        std::vector<AIArtHandle> targets;
        CollectTopLevelSelection(targets);

        AIBoolean enable = !targets.empty();

        /* Nest Down on a lone group additionally needs the toggle on and the
           group to have something in it. */
        if (enable && message->menuItem == fGroupCmdMenu && targets.size() == 1) {
            short type = -99;
            sAIArt->GetArtType(targets[0], &type);
            if (type == kGroupArt) {
                AIArtHandle firstChild = nullptr;
                sAIArt->GetArtFirstChild(targets[0], &firstChild);
                enable = AugmentedNestingOn() && firstChild != nullptr;
            }
        }

        if (enable) sAIMenu->EnableItem(message->menuItem);
        else        sAIMenu->DisableItem(message->menuItem);
    }
    else if (IsAlignItem(message->menuItem)) {
        if (InGroupAlignAvailable(nullptr, nullptr)) sAIMenu->EnableItem(message->menuItem);
        else                                        sAIMenu->DisableItem(message->menuItem);
    }
    else if (message->menuItem == fToggleMenu) {
        sAIMenu->CheckItem(fToggleMenu, AugmentedNestingOn());
        sAIMenu->EnableItem(fToggleMenu);
    }
    else if (message->menuItem == fKeepOpenMenu) {
        sAIMenu->CheckItem(fKeepOpenMenu, KeepNewGroupsOpen());
        sAIMenu->EnableItem(fKeepOpenMenu);
    }
#ifdef SUBGROUP_DIAGNOSTICS
    else if (message->menuItem == fLogMenu) {
        sAIMenu->CheckItem(fLogMenu, gSGLogEnabled);
        sAIMenu->EnableItem(fLogMenu);
    }
#endif
    return kNoErr;
}
/* -------------------------------------------------------------- notifiers -- */

ASErr SubgroupPlugin::Notify(AINotifierMessage* /*message*/)
{
    return kNoErr;
}

/* The notifier path that used to live here has been removed.
 *
 * It listened around Object > Group and completed the operation when the native
 * command declined. Measured at the keyboard, Illustrator never dispatches that
 * command when the selection is already a single group, so the notifiers never
 * fire in the one case it existed to handle. The work is done by our own
 * command instead; keeping a second, unreachable path that could also modify
 * the document was only a way to hide a future bug.
 *
 * The notifiers themselves are still registered, purely so the diagnostic log
 * can show whether the native command ran.
 */
/* The objects the Group command should act on.
 *
 * GetSelectedArt reports everything carrying kArtSelected, which on a container
 * means "contains a selection" rather than "is selected" - so a partially
 * selected parent shows up alongside its selected children. The objects the
 * user actually picked are the ones that are kArtFullySelected and whose parent
 * is not, with the layer's own backing group never counting as a parent.
 *
 *   two of five children selected -> those two   (parent group is only partial)
 *   a whole group selected        -> the group   (its parent is the layer)
 *   two loose paths selected      -> both
 */
void SubgroupPlugin::CollectTopLevelSelection(std::vector<AIArtHandle>& out)
{
    out.clear();

    AIArtHandle** sel = nullptr;
    ai::int32 nSel = 0;
    if (sAIMatchingArt->GetSelectedArt(&sel, &nSel) != kNoErr || sel == nullptr)
        return;

    for (ai::int32 i = 0; i < nSel; ++i) {
        AIArtHandle art = (*sel)[i];

        ASBoolean isLayerGroup = false;
        sAIArt->IsArtLayerGroup(art, &isLayerGroup);
        if (isLayerGroup) continue;

        ai::int32 attr = 0;
        if (sAIArt->GetArtUserAttr(art, kArtFullySelected, &attr) != kNoErr) continue;
        if ((attr & kArtFullySelected) == 0) continue;   /* only partially selected */

        AIArtHandle parent = nullptr;
        AIBoolean parentFully = false;
        if (sAIArt->GetArtParent(art, &parent) == kNoErr && parent != nullptr) {
            ASBoolean parentIsLayer = false;
            sAIArt->IsArtLayerGroup(parent, &parentIsLayer);
            if (!parentIsLayer) {
                ai::int32 pAttr = 0;
                if (sAIArt->GetArtUserAttr(parent, kArtFullySelected, &pAttr) == kNoErr)
                    parentFully = (pAttr & kArtFullySelected) != 0;
            }
        }

        if (!parentFully) out.push_back(art);
    }

    sAIMdMemory->MdMemoryDisposeHandle(reinterpret_cast<AIMdMemoryHandle>(sel));
}

/* Everything Ctrl+G should do. */
ASErr SubgroupPlugin::DoGroup()
{
    std::vector<AIArtHandle> targets;
    CollectTopLevelSelection(targets);
    SGLog("DoGroup: targets=%d", (int) targets.size());
    if (targets.empty()) return kNoErr;

    if (targets.size() == 1) {
        AIArtHandle art = targets[0];
        short type = -99;
        sAIArt->GetArtType(art, &type);
        if (type == kGroupArt) {
            AIArtHandle firstChild = nullptr;
            sAIArt->GetArtFirstChild(art, &firstChild);
            SGLog("  sole target is a group, firstChild=%p augmented=%d",
                  (void*) firstChild, (int) AugmentedNestingOn());

            if (firstChild == nullptr)
                return kNoErr;                    /* empty group: nothing to nest */

            if (!AugmentedNestingOn()) {
                SGLog("  toggle off: doing nothing, as vanilla does");
                return kNoErr;
            }

            ai::int32 la = 0;
            if (sAIArt->GetArtUserAttr(art, kArtLocked | kArtHidden, &la) != kNoErr || la != 0) {
                SGLog("  locked or hidden: silent no-op");
                return kNoErr;
            }

            ASErr e = NestContents(art);
            SGLog("  NestContents err=%d", (int) e);
            return e;
        }
    }

    return GroupItems(targets);
}

/* Wrap in New Group: always adds a level ABOVE the selection, including around
   a lone group, which Object > Group refuses outright. */
ASErr SubgroupPlugin::DoWrap()
{
    std::vector<AIArtHandle> targets;
    CollectTopLevelSelection(targets);
    SGLog("DoWrap: targets=%d", (int) targets.size());
    if (targets.empty()) return kNoErr;
    return GroupItems(targets);
}

/* Reproduces native grouping. Measured against Illustrator's own Group: the new
   group takes the z-position of the topmost selected item and members keep
   their relative order, so NewArt(kPlaceAbove, topmost) then appending each in
   sibling order gives the same tree. */
ASErr SubgroupPlugin::GroupItems(const std::vector<AIArtHandle>& items)
{
    if (items.empty()) return kNoErr;

    AIArtHandle parent = nullptr;
    if (sAIArt->GetArtParent(items[0], &parent) != kNoErr) return kNoErr;
    for (size_t i = 1; i < items.size(); ++i) {
        AIArtHandle p = nullptr;
        if (sAIArt->GetArtParent(items[i], &p) != kNoErr || p != parent) {
            SGLog("  targets span different parents; leaving it alone");
            return kNoErr;
        }
    }

    std::vector<AIArtHandle> ordered;
    AIArtHandle child = nullptr;
    if (sAIArt->GetArtFirstChild(parent, &child) != kNoErr) return kNoErr;
    while (child != nullptr) {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i] == child) { ordered.push_back(child); break; }
        }
        AIArtHandle next = nullptr;
        if (sAIArt->GetArtSibling(child, &next) != kNoErr) break;
        child = next;
    }
    if (ordered.empty()) return kNoErr;

    AIArtHandle grp = nullptr;
    ASErr e = sAIArt->NewArt(kGroupArt, kPlaceAbove, ordered[0], &grp);
    if (e) { SGLog("  NewArt err=%d", (int) e); return e; }

    for (size_t i = 0; i < ordered.size(); ++i) {
        e = sAIArt->ReorderArt(ordered[i], kPlaceInsideOnBottom, grp);
        if (e) { SGLog("  ReorderArt err=%d", (int) e); return e; }
    }
    SettleDisclosure(grp);
    SGLog("  GroupItems grouped %d item(s)", (int) ordered.size());
    return kNoErr;
}

ASErr SubgroupPlugin::NestContents(AIArtHandle group)
{
    AIArtHandle inner = nullptr;
    ASErr error = sAIArt->NewArt(kGroupArt, kPlaceInsideOnTop, group, &inner);
    if (error) return error;

    /* Snapshot the children before moving any of them; the sibling chain is
       being rewritten as we go. */
    std::vector<AIArtHandle> kids;
    AIArtHandle child = nullptr;
    error = sAIArt->GetArtFirstChild(group, &child);
    if (error) return error;

    while (child != nullptr) {
        if (child != inner) kids.push_back(child);
        AIArtHandle next = nullptr;
        if (sAIArt->GetArtSibling(child, &next) != kNoErr) break;
        child = next;
    }

    /* kids is in paint order, topmost first. Appending each to the bottom of
       the new group preserves that order. */
    for (size_t i = 0; i < kids.size(); ++i) {
        error = sAIArt->ReorderArt(kids[i], kPlaceInsideOnBottom, inner);
        if (error) return error;
    }
    SettleDisclosure(inner);
    return kNoErr;
}

void SubgroupPlugin::SettleDisclosure(AIArtHandle newGroup) const
{
    if (newGroup == nullptr) return;

    /* The new group itself: collapsed by default, which is what vanilla
       grouping does. Only opened when the user has asked for that. */
    if (KeepNewGroupsOpen())
        sAIArt->SetArtUserAttr(newGroup, kArtExpanded, kArtExpanded);

    /* Ancestors: always re-opened. This is the actual defect in the native
       behavior - adding a group several levels down collapses the topmost one
       rather than just the new one, so everything disappears. Re-opening the
       path leaves only the newest group closed. */
    AIArtHandle a = nullptr;
    if (sAIArt->GetArtParent(newGroup, &a) != kNoErr) return;

    for (int guard = 0; a != nullptr && guard < 64; ++guard) {
        ASBoolean isLayerGroup = false;
        sAIArt->IsArtLayerGroup(a, &isLayerGroup);
        if (isLayerGroup) break;

        short type = -99;
        if (sAIArt->GetArtType(a, &type) != kNoErr) break;
        if (type == kGroupArt)
            sAIArt->SetArtUserAttr(a, kArtExpanded, kArtExpanded);

        AIArtHandle parent = nullptr;
        if (sAIArt->GetArtParent(a, &parent) != kNoErr) break;
        a = parent;
    }
}

/* ------------------------------------------------------------------ about -- */

/* The dialog itself is in SubgroupAbout.cpp, which knows nothing about
   Illustrator. Everything host-shaped stays here: finding our own module, and
   the fallback for when the dialog cannot be created at all.

   Not SDKAboutPluginsHelper::PopAboutBox, which is what a sample would use. It
   takes a char* and builds an ai::UnicodeString from it with the default
   encoding - the *platform* one - so an em dash or a copyright sign becomes
   mojibake wherever the code page is not Latin-1. It also ends in MessageAlert,
   a plain OS alert with one run of unstyled text and no links. */

#ifdef WIN_ENV
/* Illustrator's own dialog colours, turned into the plain struct the dialog
   takes. This is the seam: the host is asked here, so SubgroupAbout.cpp still
   compiles without a line of Illustrator in it and tools/AboutHarness can go on
   building the same file.

   Anything missing leaves the default in place, which is the system colour --
   the right fallback, because a guessed dark grey over a light host looks worse
   than not theming at all. */
static SubgroupAboutTheme AboutThemeFromHost()
{
    SubgroupAboutTheme theme;
    if (sAIUITheme == nullptr || sAIUITheme->GetUIThemeColor == nullptr) return theme;

    struct Wanted { AIUIComponentColor which; COLORREF* into; };
    const Wanted wanted[] = {
        { kAIUIComponentColorEditTextBackground, &theme.panel },
        { kAIUIComponentColorEditText,           &theme.panelText },
        { kAIUIComponentColorBackground,         &theme.band },
        { kAIUIComponentColorText,               &theme.bandText },
        { kAIUIComponentColorBorder,             &theme.rule },
        { kAIUIComponentColorFocusRing,          &theme.link },
    };

    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); ++i) {
        AIUIThemeColor c;
        if (sAIUITheme->GetUIThemeColor(kAIUIThemeSelectorDialog, wanted[i].which, c) != kNoErr)
            return SubgroupAboutTheme();   /* all or nothing, never a half-themed window */

        const AIReal comp[3] = { c.red, c.green, c.blue };
        BYTE rgb[3];
        for (int k = 0; k < 3; ++k) {
            const double v = static_cast<double>(comp[k]) * 255.0 + 0.5;
            rgb[k] = (v <= 0.0) ? 0 : ((v >= 255.0) ? 255 : static_cast<BYTE>(v));
        }
        *(wanted[i].into) = RGB(rgb[0], rgb[1], rgb[2]);
    }

    /* The host answered, so the window is drawn entirely by us from here: a
       stock OK button and a white caption would be the two pieces left behind
       in system colors. The button face is the band moved away from itself,
       in whichever direction the theme is going, so it stays distinguishable
       at every brightness rather than only at the extremes. */
    const bool dark = (sAIUITheme->IsUIThemeDark != nullptr) && sAIUITheme->IsUIThemeDark();
    const int shift = dark ? 20 : -20;
    int r = GetRValue(theme.band) + shift;
    int g = GetGValue(theme.band) + shift;
    int b = GetBValue(theme.band) + shift;
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;

    theme.ownerDrawButton = true;
    theme.button       = RGB(r, g, b);
    theme.buttonText   = theme.bandText;
    theme.buttonBorder = theme.rule;
    theme.darkTitleBar = dark;
    return theme;
}
#endif

void SubgroupPlugin::ShowAboutBox()
{
#ifdef WIN_ENV
    /* Our own module, not the host's: the dialog resource lives in the .aip.
       Taken from the address of a function in this module rather than cached
       from DllMain, which the SDK's entry point does not hand us. */
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&SubgroupPlugin::ShowAboutBox),
                           &self) && self != nullptr) {
        if (SubgroupShowAboutDialog(self, GetActiveWindow(), AboutThemeFromHost())) return;
    }
#endif

    /* Last resort: a plain alert, same words, links spelled out. Reached only if
       the dialog could not be created - an old common-controls library, or a
       resource that failed to load. */
    if (sAIUser == nullptr) return;

    const wchar_t* text =
        L"Subgroup " SG_WVERSION L"\n\n"
        L"Nest Down\n"
        L"Adds a level of grouping inside the selection, including when the "
        L"selection is an entire group " SG_EMDASH L" the case Object > Group "
        L"declines.\n\n"
        L"Nest Up\n"
        L"Adds that level around the selection instead.\n\n"
        L"Align Group To Selected\n"
        L"Aligns a group's contents to one object selected inside it, which "
        L"Illustrator's own Align cannot reach.\n\n"
        SG_REPO_URL L"\n\n"
        L"Vibecoded by Vixen420 in September 2026.\n"
        L"Copyright " SG_COPY L" 2026 Vixen420. Free software under the GNU "
        L"General Public License, version 3 or later, with an Adobe Illustrator "
        L"SDK linking exception. It comes with ABSOLUTELY NO WARRANTY.";

    sAIUser->MessageAlert(ai::UnicodeString(reinterpret_cast<const ASUnicode*>(text)));
}

/* --------------------------------------------------- in-group alignment -- */

ASErr SubgroupPlugin::BoundsOf(AIArtHandle art, AIRealRect* out)
{
    return sAIArt->GetArtTransformBounds(art, nullptr,
                                         kNoStrokeBounds | kExcludeGuideBounds, out);
}

/* Is in-group alignment applicable, and if so what moves and what holds still?
 *
 * The rule is simply: exactly ONE child of a group is selected. That child is
 * the anchor and its siblings move to meet it.
 *
 * Deliberately NOT built on CollectTopLevelSelection, which requires
 * kArtFullySelected and returns the group when everything inside it is
 * selected. Two ways that goes wrong in the hand:
 *
 *   - clicking art inside a group with the Selection tool selects the GROUP,
 *     and a whole group singles out nothing, so there is no anchor;
 *   - the Direct Selection tool can leave an object only partially selected,
 *     which kArtFullySelected rejects even though the user plainly picked it.
 *
 * So this walks the selection itself and asks a narrower question: within one
 * parent group, is there exactly one selected child? Partial selection counts,
 * because it still says which object the user picked.
 */
AIBoolean SubgroupPlugin::InGroupAlignAvailable(std::vector<AIArtHandle>* kidsOut,
                                                AIArtHandle* anchorOut) const
{
    AIArtHandle** sel = nullptr;
    ai::int32 nSel = 0;
    if (sAIMatchingArt->GetSelectedArt(&sel, &nSel) != kNoErr || sel == nullptr)
        return false;

    AIArtHandle container = nullptr;
    AIArtHandle anchor    = nullptr;
    int         picked    = 0;
    AIBoolean   ambiguous = false;

    for (ai::int32 i = 0; i < nSel; ++i) {
        AIArtHandle art = (*sel)[i];

        ASBoolean isLayerGroup = false;
        sAIArt->IsArtLayerGroup(art, &isLayerGroup);
        if (isLayerGroup) continue;

        AIArtHandle parent = nullptr;
        if (sAIArt->GetArtParent(art, &parent) != kNoErr || parent == nullptr) continue;

        ASBoolean parentIsLayer = false;
        sAIArt->IsArtLayerGroup(parent, &parentIsLayer);
        if (parentIsLayer) continue;      /* top level art: native Align works */

        /* GetSelectedArt reports containers too, because kArtSelected on a
           group only means it contains a selection. Skip any selected art that
           is itself an ancestor of other selected art - it is the container,
           not the pick. */
        AIBoolean isAncestorOfSelection = false;
        for (ai::int32 j = 0; j < nSel && !isAncestorOfSelection; ++j) {
            if (j == i) continue;
            AIArtHandle walk = (*sel)[j];
            for (int guard = 0; walk != nullptr && guard < 64; ++guard) {
                AIArtHandle wp = nullptr;
                if (sAIArt->GetArtParent(walk, &wp) != kNoErr) break;
                if (wp == art) { isAncestorOfSelection = true; break; }
                walk = wp;
            }
        }
        if (isAncestorOfSelection) continue;

        if (container == nullptr) {
            container = parent;
            anchor    = art;
            picked    = 1;
        } else if (parent == container) {
            ++picked;                     /* more than one child picked */
        } else {
            ambiguous = true;             /* picks spread across groups */
            break;
        }
    }

    sAIMdMemory->MdMemoryDisposeHandle(reinterpret_cast<AIMdMemoryHandle>(sel));

    if (ambiguous || container == nullptr || anchor == nullptr || picked != 1)
        return false;

    std::vector<AIArtHandle> kids;
    AIArtHandle child = nullptr;
    if (sAIArt->GetArtFirstChild(container, &child) != kNoErr) return false;
    while (child != nullptr) {
        kids.push_back(child);
        AIArtHandle next = nullptr;
        if (sAIArt->GetArtSibling(child, &next) != kNoErr) break;
        child = next;
    }
    if (kids.size() < 2) return false;    /* nothing for the anchor to align */

    if (kidsOut)   *kidsOut   = kids;
    if (anchorOut) *anchorOut = anchor;
    return true;
}

ASErr SubgroupPlugin::DoAlign(AlignMode mode)
{
    std::vector<AIArtHandle> kids;
    AIArtHandle anchor = nullptr;
    if (!InGroupAlignAvailable(&kids, &anchor)) {
        SGLog("DoAlign: not the in-group case; Illustrator's own Align applies");
        return kNoErr;
    }

    AIRealRect ab;
    ASErr e = BoundsOf(anchor, &ab);
    if (e != kNoErr) { SGLog("DoAlign: anchor bounds err=%d", (int) e); return e; }

    for (size_t i = 0; i < kids.size(); ++i) {
        AIArtHandle it = kids[i];
        if (it == anchor) continue;

        ai::int32 attr = 0;
        if (sAIArt->GetArtUserAttr(it, kArtLocked | kArtHidden, &attr) != kNoErr || attr != 0)
            continue;

        AIRealRect b;
        if (BoundsOf(it, &b) != kNoErr) continue;

        AIReal dx = 0, dy = 0;
        switch (mode) {
        case kAlignLeft:    dx = ab.left  - b.left;  break;
        case kAlignRight:   dx = ab.right - b.right; break;
        case kAlignHCenter: dx = (ab.left + ab.right) / 2 - (b.left + b.right) / 2; break;
        case kAlignTop:     dy = ab.top    - b.top;    break;
        case kAlignBottom:  dy = ab.bottom - b.bottom; break;
        case kAlignVCenter: dy = (ab.top + ab.bottom) / 2 - (b.top + b.bottom) / 2; break;
        case kAlignBoth:
            dx = (ab.left + ab.right) / 2 - (b.left + b.right) / 2;
            dy = (ab.top + ab.bottom) / 2 - (b.top + b.bottom) / 2;
            break;
        }
        if (dx == 0 && dy == 0) continue;

        AIRealMatrix m;
        m.a = 1; m.b = 0; m.c = 0; m.d = 1; m.tx = dx; m.ty = dy;
        sAITransformArt->TransformArt(it, &m, 1.0, kTransformObjects | kTransformChildren);
    }
    SGLog("DoAlign: mode=%d, %d object(s) in group", (int) mode, (int) kids.size());
    return kNoErr;
}

#ifdef SUBGROUP_DIAGNOSTICS

static void SGLogArt(const char* label, AIArtHandle art)
{
    ai::UnicodeString nm;
    ASBoolean isDefaultName = false;
    sAIArt->GetArtName(art, nm, &isDefaultName);
    std::string n = nm.as_UTF8();

    short type = -99;
    sAIArt->GetArtType(art, &type);

    ai::int32 sel = 0, full = 0, tgt = 0;
    sAIArt->GetArtUserAttr(art, kArtSelected,      &sel);
    sAIArt->GetArtUserAttr(art, kArtFullySelected, &full);
    sAIArt->GetArtUserAttr(art, kArtTargeted,      &tgt);

    ASBoolean isLayerGroup = false;
    sAIArt->IsArtLayerGroup(art, &isLayerGroup);

    SGLog("  %s type=%d name='%s'%s sel=%d full=%d TARGETED=%d handle=%p",
          label, (int) type, n.c_str(), isLayerGroup ? " [layer]" : "",
          (sel  & kArtSelected)      ? 1 : 0,
          (full & kArtFullySelected) ? 1 : 0,
          (tgt  & kArtTargeted)      ? 1 : 0,
          (void*) art);
}

/* Measures a gesture instead of theorising about it: what does Illustrator
   consider selected, fully selected, and targeted right now?

   This is the instrument for the alignment question. The anchor has to come
   from somewhere, and targeting is the only channel left that might carry it. */
void SubgroupPlugin::DumpSelectionState()
{
    bool saved = gSGLogEnabled;
    gSGLogEnabled = true;               /* a probe that writes nothing is useless */

    SGLog("=== selection state probe ===");

    AIArtHandle** sel = nullptr;
    ai::int32 nSel = 0;
    ASErr e = sAIMatchingArt->GetSelectedArt(&sel, &nSel);
    SGLog("GetSelectedArt err=%d count=%d", (int) e, (int) nSel);
    if (e == kNoErr && sel != nullptr) {
        for (ai::int32 i = 0; i < nSel; ++i) SGLogArt("SEL", (*sel)[i]);
        sAIMdMemory->MdMemoryDisposeHandle(reinterpret_cast<AIMdMemoryHandle>(sel));
    }

    AIMatchingArtSpec spec(kAnyArt, kArtTargeted, kArtTargeted);
    AIArtHandle** tgt = nullptr;
    ai::int32 nTgt = 0;
    e = sAIMatchingArt->GetMatchingArt(&spec, 1, &tgt, &nTgt);
    SGLog("GetMatchingArt(kArtTargeted) err=%d count=%d", (int) e, (int) nTgt);
    if (e == kNoErr && tgt != nullptr) {
        for (ai::int32 i = 0; i < nTgt; ++i) SGLogArt("TGT", (*tgt)[i]);
        sAIMdMemory->MdMemoryDisposeHandle(reinterpret_cast<AIMdMemoryHandle>(tgt));
    }

    SGLog("=== end probe ===");
    gSGLogEnabled = saved;
}

/* Where do our items actually land? Rather than guess at menu group membership,
   ask. Written to the log once at startup when diagnostics are on. */
void SubgroupPlugin::DumpMenus()
{
    ai::int32 count = 0;
    if (sAIMenu->CountMenuItems(&count) != kNoErr) return;
    SGLog("--- menu dump: %d items ---", (int) count);

    for (ai::int32 i = 0; i < count; ++i) {
        AIMenuItemHandle item = nullptr;
        if (sAIMenu->GetNthMenuItem(i, &item) != kNoErr || item == nullptr) continue;

        const char* key = nullptr;
        sAIMenu->GetMenuItemKeyboardShortcutDictionaryKey(item, &key);

        AIMenuGroup grp = nullptr;
        const char* gname = nullptr;
        if (sAIMenu->GetItemMenuGroup(item, &grp) == kNoErr && grp != nullptr)
            sAIMenu->GetMenuGroupName(grp, &gname);

        SGLog("  [%d] key='%s' group='%s'", (int) i, key ? key : "?", gname ? gname : "?");
    }
    SGLog("--- end menu dump ---");
}

#endif  /* SUBGROUP_DIAGNOSTICS */
