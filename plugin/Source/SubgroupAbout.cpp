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

/* The About dialog.
 *
 * Three dialogs were tried before this one, and each was abandoned for a
 * concrete reason worth recording, because the reasons are not obvious and the
 * temptation to go back to the simpler thing is real.
 *
 *   AIUserSuite::MessageAlert, which SDKAboutPluginsHelper::PopAboutBox ends in,
 *   is a plain OS alert: one run of unstyled text, no emphasis, no links. It
 *   also takes a char* and decodes it in the *platform* encoding, so an em dash
 *   or a copyright sign turns to mojibake anywhere the code page is not
 *   Latin-1 - which is how a bug got within one restart of shipping.
 *
 *   Windows' task dialog looks the part and can hold links, but only in its
 *   content and footer: the main instruction, the one piece of text with any
 *   visual weight, cannot be one. And its markup is links and nothing else -
 *   no bold, and no italics anywhere at all.
 *
 *   A dialog of static controls gets bold, because a static can be given a bold
 *   font. It cannot get italics *within a sentence*, because a static control
 *   has exactly one font.
 *
 * So: a dialog whose prose lives in a rich edit control, which is the one
 * common control that can change font mid-sentence. Command names are bold
 * where they head a paragraph and italic where they appear inside one; links
 * are SysLink controls, which handle their own hit-testing and keyboard focus.
 *
 * The two-band background - white above, the button face below, divided by a
 * rule - is the task dialog's own layout, reproduced because it reads better
 * than a flat gray sheet. That appearance never depended on the task dialog.
 *
 * There are no Illustrator types below. That is what lets tools/AboutHarness
 * build this same file and show it, which is the only way to actually look at a
 * modal dialog that otherwise appears only inside a host application.
 */

#ifdef _WIN32

#include "SubgroupAbout.h"
#include "SubgroupID.h"
#include "Resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <richedit.h>
#include <cstring>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

/* SG_WVERSION, SG_EMDASH, SG_COPY and the two URLs come from SubgroupID.h, so
   the dialog and the plain-alert fallback in SubgroupPlugin.cpp cannot drift. */

namespace {

/* The prose, as RTF.
 *
 * Command names are bold where they head a paragraph and italic where they are
 * mentioned inside one, which is the distinction a reader actually needs: the
 * heading says "this is the command", the italic says "this names something in
 * the interface".
 *
 * The em dash is written as an RTF Unicode escape: backslash-u, the decimal
 * code point, then a replacement character for readers that cannot cope, so
 * the trailing "?" is that fallback rather than a typo. Keeping it in this
 * form leaves the file plain ASCII, the same reason the wide literals
 * elsewhere use \x escapes.
 *
 * \fs18 is 9pt: RTF measures in half-points. */
const char* const kBodyRtf =
    "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil Segoe UI;}}"
    /* \sa140 is 7pt of space after each paragraph. Without it the sections run
       together: \par alone starts a new paragraph but adds no gap, which reads
       as one wall of text. */
    "\\fs18\\sa140 "

    "{\\b Nest Down}\\line "
    "Adds a level of grouping inside the selection, including when that "
    "selection is an entire group \\u8212? the case {\\i Object > Group} "
    "declines.\\par "

    "{\\b Nest Up}\\line "
    "Adds that level around the selection instead, rather than inside it.\\par "

    "{\\b Align Group To Selected}\\line "
    "Aligns a group's contents to one object selected inside it, which "
    "{\\i Align} cannot otherwise reach: selecting every object in a group "
    "{\\i is} selecting the group, leaving nothing to align to."
    "}";

struct AboutState {
    HFONT  title;
    HFONT  legal;
    HBRUSH panel;       /* upper band */
    HBRUSH band;        /* lower band */
    int    splitY;      /* pixel row where the upper panel ends */
    HMODULE richEdit;
    SubgroupAboutTheme theme;
};

/* Feeds a fixed buffer to EM_STREAMIN. */
struct RtfSource { const char* text; size_t left; };

DWORD CALLBACK RtfReader(DWORD_PTR cookie, LPBYTE buffer, LONG wanted, LONG* done)
{
    RtfSource* src = reinterpret_cast<RtfSource*>(cookie);
    LONG n = (static_cast<LONG>(src->left) < wanted) ? static_cast<LONG>(src->left) : wanted;
    if (n > 0) {
        memcpy(buffer, src->text, static_cast<size_t>(n));
        src->text += n;
        src->left -= static_cast<size_t>(n);
    }
    *done = n;
    return 0;
}

/* Derives a font from the dialog's own, so it follows whatever the user's shell
   font and DPI actually are instead of hard-coding a face and a pixel size. */
HFONT DeriveFont(HWND dlg, int percentOfHeight, bool bold)
{
    HFONT base = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
    if (base == nullptr) return nullptr;

    LOGFONTW lf;
    if (GetObjectW(base, sizeof(lf), &lf) == 0) return nullptr;

    LONG h = lf.lfHeight;                     /* negative for character height */
    lf.lfHeight = (h < 0) ? -MulDiv(-h, percentOfHeight, 100)
                          :  MulDiv( h, percentOfHeight, 100);
    if (bold) lf.lfWeight = FW_BOLD;
    return CreateFontIndirectW(&lf);
}

/* Anything a SysLink hands back came from the strings below, but check the
   scheme anyway: ShellExecute will happily launch things that are not URLs, and
   feeding it unchecked strings is how that becomes a bug later. */
void OpenLink(HWND parent, const wchar_t* url)
{
    if (url == nullptr) return;
    if (wcsncmp(url, L"https://", 8) != 0) return;
    ShellExecuteW(parent, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void FillBody(HWND dlg, const SubgroupAboutTheme& theme)
{
    HWND body = GetDlgItem(dlg, IDC_ABOUT_BODY);
    if (body == nullptr) return;

    SendMessageW(body, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(theme.panel));
    SendMessageW(body, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SendMessageW(body, EM_EXLIMITTEXT, 0, 64 * 1024);

    RtfSource src = { kBodyRtf, strlen(kBodyRtf) };
    EDITSTREAM es = { 0 };
    es.dwCookie    = reinterpret_cast<DWORD_PTR>(&src);
    es.pfnCallback = RtfReader;
    SendMessageW(body, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));

    /* The RTF above names no colours, so every run came in as the control's
       default black -- which is unreadable once the panel behind it is dark.
       Applying one character format over the whole text recolours it without
       putting a colour table in the prose, where it would have to be kept in
       step with the theme by hand. */
    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize      = sizeof(cf);
    cf.dwMask      = CFM_COLOR;
    cf.crTextColor = theme.panelText;
    SendMessageW(body, EM_SETSEL, 0, static_cast<LPARAM>(-1));
    SendMessageW(body, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    /* Read-only edits still show a selection and a caret when focused. Neither
       belongs in a paragraph of prose, so keep the control out of the tab order
       entirely - it is set WS_DISABLED-free but never focusable in the
       template. */
    SendMessageW(body, EM_SETSEL, static_cast<WPARAM>(-1), 0);
    SendMessageW(body, EM_HIDESELECTION, TRUE, 0);
}

/* Loaded by name rather than linked, so the dialog still depends on nothing a
   machine running Windows does not already have. Attribute 20 is the
   documented one; builds between 17763 and 18985 used 19 for the same thing
   and ignore 20, so both are offered. */
void ApplyDarkTitleBar(HWND dlg)
{
    typedef HRESULT (WINAPI *SetAttrProc)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm == nullptr) return;
    SetAttrProc setAttr = reinterpret_cast<SetAttrProc>(
        reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
    if (setAttr != nullptr) {
        BOOL on = TRUE;
        setAttr(dlg, 20, &on, sizeof(on));
        setAttr(dlg, 19, &on, sizeof(on));
    }
    FreeLibrary(dwm);
}

/* The OK button, when the theme asks for it. Flat face, one-pixel border, and
   a focus ring drawn inside it -- the same shape Illustrator's own buttons
   have, and the same one the Shear dialog uses. */
void DrawOkButton(const AboutState* st, const DRAWITEMSTRUCT* di)
{
    const bool pressed = (di->itemState & ODS_SELECTED) != 0;
    const bool focused = (di->itemState & ODS_FOCUS) != 0;

    HBRUSH face = CreateSolidBrush(pressed ? st->theme.buttonBorder : st->theme.button);
    if (face != nullptr) { FillRect(di->hDC, &di->rcItem, face); DeleteObject(face); }

    HBRUSH edge = CreateSolidBrush(focused ? st->theme.link : st->theme.buttonBorder);
    if (edge != nullptr) { FrameRect(di->hDC, &di->rcItem, edge); DeleteObject(edge); }

    wchar_t text[32];
    text[0] = L'\0';
    GetWindowTextW(di->hwndItem, text, 32);

    HFONT base = reinterpret_cast<HFONT>(SendMessageW(di->hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ old = (base != nullptr) ? SelectObject(di->hDC, base) : nullptr;
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, st->theme.buttonText);
    RECT box = di->rcItem;
    DrawTextW(di->hDC, text, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old != nullptr) SelectObject(di->hDC, old);
}

INT_PTR CALLBACK AboutProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AboutState* st = reinterpret_cast<AboutState*>(GetWindowLongPtrW(dlg, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        st = new AboutState();
        /* lParam is the theme, handed down from DialogBoxParamW. */
        const SubgroupAboutTheme* passed =
            reinterpret_cast<const SubgroupAboutTheme*>(lParam);
        st->theme    = (passed != nullptr) ? *passed : SubgroupAboutTheme();
        st->title    = DeriveFont(dlg, 150, false);
        st->legal    = DeriveFont(dlg,  92, false);
        st->panel    = CreateSolidBrush(st->theme.panel);
        st->band     = CreateSolidBrush(st->theme.band);
        st->richEdit = nullptr;
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));

        /* Where the white panel ends, in pixels. Expressed in dialog units so
           it tracks the font and DPI like everything else. */
        RECT split = { 0, kAboutSplitDlgY, 1, kAboutSplitDlgY + 1 };
        MapDialogRect(dlg, &split);
        st->splitY = split.top;

        if (st->title) SendDlgItemMessageW(dlg, IDC_ABOUT_TITLE, WM_SETFONT,
                                           reinterpret_cast<WPARAM>(st->title), TRUE);
        if (st->legal) SendDlgItemMessageW(dlg, IDC_ABOUT_ATTRIB, WM_SETFONT,
                                           reinterpret_cast<WPARAM>(st->legal), TRUE);
        if (st->legal) SendDlgItemMessageW(dlg, IDC_ABOUT_LEGAL, WM_SETFONT,
                                           reinterpret_cast<WPARAM>(st->legal), TRUE);

        SetDlgItemTextW(dlg, IDC_ABOUT_TITLE,
            L"<a href=\"" SG_REPO_URL L"\">Subgroup " SG_WVERSION L"</a>");

        FillBody(dlg, st->theme);

        /* The author link lives on this name rather than on the one in the
           copyright below. The notice is a legal statement and reads better
           without a link in the middle of it, and the same address twice in one
           block would be the noise the source-code line was removed for. */
        SetDlgItemTextW(dlg, IDC_ABOUT_ATTRIB,
            L"Vibecoded by <a href=\"" SG_AUTHOR_URL L"\">Vixen420</a> in September 2026.");

        /* The GPL asks an interactive program to show this where the user can
           find it. For a plugin with no window of its own, that is here. */
        SetDlgItemTextW(dlg, IDC_ABOUT_LEGAL,
            L"Copyright " SG_COPY L" 2026 Vixen420. "
            L"Free software under the GNU General Public License, version 3 or "
            L"later, with an Adobe Illustrator SDK linking exception. It comes "
            L"with ABSOLUTELY NO WARRANTY.");

        RECT dr;
        GetWindowRect(dlg, &dr);
        HWND owner = GetWindow(dlg, GW_OWNER);
        RECT pr;
        if (owner != nullptr && IsWindowVisible(owner)) GetWindowRect(owner, &pr);
        else SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
        SetWindowPos(dlg, nullptr,
                     pr.left + ((pr.right - pr.left) - (dr.right - dr.left)) / 2,
                     pr.top  + ((pr.bottom - pr.top) - (dr.bottom - dr.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);

        /* A stock button paints itself from the visual styles, which no message
           reaches. Turning it owner-drawn is the only way it follows the rest
           of the window. BS_OWNERDRAW replaces BS_DEFPUSHBUTTON -- they share
           the same style bits -- but the dialog manager still sends IDOK for
           Enter when no control claims to be the default, which is what the
           WM_COMMAND below already handles. */
        if (st->theme.ownerDrawButton) {
            HWND ok = GetDlgItem(dlg, IDOK);
            if (ok != nullptr) {
                LONG_PTR style = GetWindowLongPtrW(ok, GWL_STYLE);
                style = (style & ~static_cast<LONG_PTR>(BS_TYPEMASK)) | BS_OWNERDRAW;
                SetWindowLongPtrW(ok, GWL_STYLE, style);
            }
        }

        if (st->theme.darkTitleBar) ApplyDarkTitleBar(dlg);

        /* Focus OK rather than letting the dialog manager give it to the first
           tab stop, which is the title link: the box would open with a focus
           rectangle drawn around its heading, and Enter would open a browser
           instead of closing it. Returning FALSE says focus is already set. */
        SetFocus(GetDlgItem(dlg, IDOK));
        return FALSE;
    }

    case WM_ERASEBKGND: {
        if (st == nullptr) break;
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(dlg, &rc);

        RECT upper = rc; upper.bottom = st->splitY;
        FillRect(dc, &upper, st->panel);

        RECT lower = rc; lower.top = st->splitY;
        FillRect(dc, &lower, st->band);

        /* The rule between the panels. */
        RECT rule = { rc.left, st->splitY, rc.right, st->splitY + 1 };
        HBRUSH ruleBrush = CreateSolidBrush(st->theme.rule);
        if (ruleBrush != nullptr) { FillRect(dc, &rule, ruleBrush); DeleteObject(ruleBrush); }
        return TRUE;
    }

    /* SysLink and static controls paint their own background, so they have to
       be told which panel they are sitting on. */
    case WM_CTLCOLORSTATIC: {
        if (st == nullptr) break;
        HWND ctl = reinterpret_cast<HWND>(lParam);
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        if (GetDlgCtrlID(ctl) == IDC_ABOUT_TITLE) {
            SetTextColor(dc, st->theme.panelText);
            return reinterpret_cast<INT_PTR>(st->panel);
        }
        SetTextColor(dc, st->theme.bandText);
        return reinterpret_cast<INT_PTR>(st->band);
    }

    case WM_DRAWITEM: {
        if (st == nullptr || !st->theme.ownerDrawButton) break;
        const DRAWITEMSTRUCT* di = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (di->CtlType == ODT_BUTTON && di->CtlID == IDOK) {
            DrawOkButton(st, di);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY: {
        const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lParam);

        /* A SysLink draws its link text in a colour of its own choosing, which
           no WM_CTLCOLORSTATIC reaches: the default is a blue picked to sit on
           white, and on a dark panel it is barely legible. Custom draw is the
           only hook that recolours it. */
        if (hdr != nullptr && hdr->code == NM_CUSTOMDRAW && st != nullptr &&
            (hdr->idFrom == IDC_ABOUT_TITLE || hdr->idFrom == IDC_ABOUT_ATTRIB ||
             hdr->idFrom == IDC_ABOUT_LEGAL)) {
            NMCUSTOMDRAW* cd = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (cd->dwDrawStage == CDDS_PREPAINT) {
                SetWindowLongPtrW(dlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                return TRUE;
            }
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(cd->hdc, st->theme.link);
                SetWindowLongPtrW(dlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
                return TRUE;
            }
        }

        if (hdr != nullptr && (hdr->code == NM_CLICK || hdr->code == NM_RETURN) &&
            (hdr->idFrom == IDC_ABOUT_TITLE || hdr->idFrom == IDC_ABOUT_ATTRIB ||
             hdr->idFrom == IDC_ABOUT_LEGAL)) {
            OpenLink(dlg, reinterpret_cast<const NMLINK*>(lParam)->item.szUrl);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dlg, LOWORD(wParam));
            return TRUE;
        }
        break;

    case WM_DESTROY:
        if (st != nullptr) {
            if (st->title) DeleteObject(st->title);
            if (st->legal) DeleteObject(st->legal);
            if (st->panel) DeleteObject(st->panel);
            if (st->band)  DeleteObject(st->band);
            delete st;
            SetWindowLongPtrW(dlg, DWLP_USER, 0);
        }
        break;

    default:
        break;
    }
    return FALSE;
}

} /* anonymous namespace */

bool SubgroupShowAboutDialog(HINSTANCE instance, HWND parent,
                             const SubgroupAboutTheme& theme)
{
    /* SysLink lives in version 6 of the common controls. Whether a host process
       has that in its activation context is the host's business, so ask rather
       than assume, and let the caller fall back if the answer is no. */
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_LINK_CLASS | ICC_STANDARD_CLASSES;
    if (!InitCommonControlsEx(&icc)) return false;

    /* Loading Msftedit is what registers the RICHEDIT50W window class the
       dialog template names. Without it the control silently fails to create
       and the dialog comes up with a hole in it, so treat it as required. */
    HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
    if (richEdit == nullptr) return false;

    INT_PTR r = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SUBGROUP_ABOUT),
                                parent, AboutProc,
                                reinterpret_cast<LPARAM>(&theme));

    FreeLibrary(richEdit);
    return r != -1 && r != 0;
}

#endif /* _WIN32 */
