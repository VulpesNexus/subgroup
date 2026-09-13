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

#ifndef __SUBGROUPSUITES_H__
#define __SUBGROUPSUITES_H__

#include "IllustratorSDK.h"
#include "Suites.hpp"

#include "AIArt.h"
#include "AIMatchingArt.h"
#include "AIMdMemory.h"
#include "AIMenu.h"
#include "AITransformArt.h"
#include "AINotifier.h"
#include "AIPreference.h"
#include "AIUser.h"
#include "AIUITheme.h"

extern "C" SPBlocksSuite*           sSPBlocks;
extern "C" AIArtSuite*              sAIArt;
extern "C" AIMatchingArtSuite*      sAIMatchingArt;
extern "C" AIMdMemorySuite*         sAIMdMemory;
extern "C" AIMenuSuite*             sAIMenu;
extern "C" AIPreferenceSuite*       sAIPreference;
extern "C" AITransformArtSuite*     sAITransformArt;

/* sAINotifier and sAIUser are declared and imported by the SDK's common
   framework (samplecode/common/source/Suites.cpp). Redeclaring either here is a
   duplicate definition at link time, so we use theirs via Suites.hpp. AIUser.h
   is still included above, for the suite's own declarations. */
extern "C" AIUnicodeStringSuite*    sAIUnicodeString;

// Optional: it tells the About dialog what colours Illustrator is drawing its
// own dialogs in. A host without it still loads the plugin, and the dialog
// falls back to the system colours.
extern "C" AIUIThemeSuite*          sAIUITheme;

#endif /* __SUBGROUPSUITES_H__ */
