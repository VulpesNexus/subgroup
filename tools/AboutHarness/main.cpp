// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vixen420
//
// Subgroup is free software: you may redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. It comes with ABSOLUTELY NO WARRANTY. See the file LICENSE, or
// <https://www.gnu.org/licenses/>, for the full text.

/* Shows the plugin's About dialog on its own, outside Illustrator.
 *
 * A modal dialog inside a host application is close to untestable: you cannot
 * drive it, screenshot it, or measure it without a person sitting there. This
 * builds the very same SubgroupAbout.cpp and the very same dialog resource into
 * a two-hundred-line executable, so the layout can be looked at, and looked at
 * again after every edit.
 *
 * Build it with tools/AboutHarness/build.cmd. It is a development tool and is
 * not part of the plugin.
 */

#include <windows.h>
#include "SubgroupAbout.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR cmdLine, int)
{
    /* /exit<ms> closes the dialog by itself, so a screenshot can be taken
       without anything hanging around waiting for a click. */
    int autoCloseMs = 0;
    if (cmdLine != nullptr && wcsstr(cmdLine, L"/exit") != nullptr)
        autoCloseMs = _wtoi(wcsstr(cmdLine, L"/exit") + 5);

    /* /dark shows it in Illustrator's darkest interface colours rather than the
       system's. The plugin reads the real numbers from the host; these are the
       ones Illustrator 30.7 reported at its darkest setting, hard-coded here so
       the dialog can be looked at in that theme without a copy of Illustrator
       to ask. That is the whole point of the harness. */
    SubgroupAboutTheme theme;
    if (cmdLine != nullptr && wcsstr(cmdLine, L"/dark") != nullptr) {
        theme.panel     = RGB(0x32, 0x32, 0x32);
        theme.panelText = RGB(0xCD, 0xCD, 0xCD);
        theme.band      = RGB(0x32, 0x32, 0x32);
        theme.bandText  = RGB(0xCD, 0xCD, 0xCD);
        theme.rule      = RGB(0x1E, 0x1E, 0x1E);
        theme.link      = RGB(0x5A, 0xA9, 0xE6);
        theme.ownerDrawButton = true;
        theme.button       = RGB(0x46, 0x46, 0x46);
        theme.buttonText   = RGB(0xCD, 0xCD, 0xCD);
        theme.buttonBorder = RGB(0x1E, 0x1E, 0x1E);
        theme.darkTitleBar = true;
    }

    if (autoCloseMs > 0) {
        struct Closer {
            static DWORD WINAPI Run(LPVOID ms) {
                Sleep(static_cast<DWORD>(reinterpret_cast<UINT_PTR>(ms)));
                HWND dlg = FindWindowW(nullptr, L"About Subgroup");
                if (dlg != nullptr) PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
                return 0;
            }
        };
        CloseHandle(CreateThread(nullptr, 0, Closer::Run,
                    reinterpret_cast<LPVOID>(static_cast<UINT_PTR>(autoCloseMs)),
                    0, nullptr));
    }

    if (!SubgroupShowAboutDialog(instance, nullptr, theme)) {
        MessageBoxW(nullptr, L"SubgroupShowAboutDialog returned false: the "
                             L"dialog could not be created.",
                    L"About harness", MB_ICONERROR | MB_OK);
        return 1;
    }
    return 0;
}
