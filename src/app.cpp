/**
 * Mahtab GUI: plain Win32 (no Qt, no resources). Dark, flat, custom-drawn look.
 *
 * Behaviour:
 *   - sliders apply live (preview) but are NOT saved
 *   - "Save to tray" (also the window's X button) saves and hides the window
 *   - "Exit" quits without saving; the original gamma is restored on exit
 *
 * GPLv3 (see LICENSE).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <string>

#include "appinfo.h"
#include "config.h"
#include "engine.h"
#include "win_overlay.h"

namespace {

constexpr wchar_t   kClass[]   = L"BrightnessToolMainWindow";
constexpr wchar_t   kRunKey[]  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr UINT      WM_TRAY    = WM_APP + 1;
constexpr UINT      WM_SHOWAPP = WM_APP + 2;
constexpr UINT_PTR  ID_TIMER   = 1;

// IDI_APPLICATION / IDC_ARROW expand to ANSI strings unless UNICODE is defined, so spell out the wide ones.
const wchar_t *const kStdIcon     = MAKEINTRESOURCEW(32512);   // IDI_APPLICATION
const wchar_t *const kArrowCursor = MAKEINTRESOURCEW(32512);   // IDC_ARROW

// ---- palette
const COLORREF kBg      = RGB(27, 29, 34);
const COLORREF kText    = RGB(232, 234, 237);
const COLORREF kMuted   = RGB(142, 148, 160);
const COLORREF kAccent  = RGB(245, 183, 59);
const COLORREF kPanel   = RGB(44, 47, 54);
const COLORREF kPanelHi = RGB(58, 62, 71);
const COLORREF kTrack   = RGB(58, 62, 71);
const COLORREF kThumb   = RGB(244, 245, 247);

enum {
	ID_STATIC_MUTED = 301, ID_STATIC_VALUE = 302, ID_STATIC_TITLE = 303,
	ID_TOGGLE_STARTUP = 201, ID_BTN_RESET = 202, ID_BTN_ABOUT = 203, ID_BTN_EXIT = 204, ID_BTN_TRAY = 205,
	ID_BTN_DONATE = 206,
	ID_TB_BASE = 210
};
enum { CMD_TOGGLE = 1, CMD_STARTUP, CMD_ABOUT, CMD_DONATE, CMD_QUIT };

enum Slider { S_BRT, S_TEMP, S_COUNT };

struct SliderDef { const wchar_t *name; int lo, hi; };

const SliderDef kDefs[S_COUNT] = {
	{ L"Brightness",        10, 100 },   // percent
	{ L"Color temperature", 20,  65 },   // x 100 K
};

HINSTANCE g_inst = nullptr;
HWND      g_wnd = nullptr, g_toggle = nullptr;
HWND      g_tb[S_COUNT] {}, g_val[S_COUNT] {};
HFONT     g_fBody = nullptr, g_fBold = nullptr, g_fTitle = nullptr, g_fSmall = nullptr;
HBRUSH    g_bgBrush = nullptr;
HICON     g_icon = nullptr;
int       g_dpi = 96;
UINT      g_taskbarMsg = 0;
bool      g_startup = false;

AppConfig g_cfg;
Engine    g_engine;
Overlay   g_overlay;

int S(int v) { return MulDiv(v, g_dpi, 96); }

// ---------------------------------------------------------------- startup (registry)

bool startupEnabled()
{
	return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kAppId, RRF_RT_REG_SZ,
	                    nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

void setStartup(bool on)
{
	if (on) {
		wchar_t path[MAX_PATH] {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		const std::wstring v = L"\"" + std::wstring(path) + L"\" --tray";
		RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kAppId, REG_SZ,
		                v.c_str(), DWORD((v.size() + 1) * sizeof(wchar_t)));
	} else {
		RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kAppId);
	}
}

// ---------------------------------------------------------------- config <-> slider positions

int cfgToPos(int s) { return s == S_BRT ? g_cfg.brightnessPct : g_cfg.kelvin / 100; }

void posToCfg(int s, int p)
{
	if (s == S_BRT) g_cfg.brightnessPct = p;
	else            g_cfg.kelvin = p * 100;
}

std::wstring fmt(int s, int pos)
{
	wchar_t b[32] {};
	if (s == S_TEMP)
		_snwprintf(b, 31, L"%d K", pos * 100);
	else
		_snwprintf(b, 31, L"%d %%", pos);
	return b;
}

void refreshLabels()
{
	for (int i = 0; i < S_COUNT; ++i) {
		const int pos = int(SendMessageW(g_tb[i], TBM_GETPOS, 0, 0));
		SetWindowTextW(g_val[i], fmt(i, pos).c_str());
	}
}

void syncControls()
{
	g_startup = startupEnabled();
	InvalidateRect(g_toggle, nullptr, FALSE);

	for (int i = 0; i < S_COUNT; ++i) {
		SendMessageW(g_tb[i], TBM_SETPOS, TRUE, cfgToPos(i));
		InvalidateRect(g_tb[i], nullptr, FALSE);
	}

	refreshLabels();
}

void applyConfig()
{
	g_engine.set(g_cfg.brightnessPct, g_cfg.kelvin);
	g_overlay.set(g_engine.overlayAlpha());   // the part of the dimming Windows' gamma range can't do
}

// ---------------------------------------------------------------- tray, about, helpers

void trayAdd()
{
	NOTIFYICONDATAW nid {};
	nid.cbSize           = sizeof(nid);
	nid.hWnd             = g_wnd;
	nid.uID              = 1;
	nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_TRAY;
	nid.hIcon            = g_icon;
	wcsncpy(nid.szTip, kAppName, 127);
	Shell_NotifyIconW(NIM_ADD, &nid);
}

void trayRemove()
{
	NOTIFYICONDATAW nid {};
	nid.cbSize = sizeof(nid);
	nid.hWnd   = g_wnd;
	nid.uID    = 1;
	Shell_NotifyIconW(NIM_DELETE, &nid);
}

void showWindow()
{
	syncControls();
	ShowWindow(g_wnd, SW_SHOW);
	ShowWindow(g_wnd, SW_RESTORE);
	SetForegroundWindow(g_wnd);
}

// Keep the current settings (save them) and hide the window; the app keeps running in the tray.
void saveAndHide()
{
	saveConfig(g_cfg);
	ShowWindow(g_wnd, SW_HIDE);
}

void openUrl(const wchar_t *url)
{
	if (url && *url)
		ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void showAbout()
{
	std::wstring t = std::wstring(kAppName) + L"  " + kAppVersion + L"\n\n"
	               + L"Simple screen brightness and color temperature control for Windows.\n\n"
	               + L"Developer: " + kAuthor + L"\n"
	               + L"License: " + kLicense + L"\n\n";

	const bool hasSite = kWebsite[0] != L'\0';
	if (hasSite)
		t += std::wstring(L"\n\n") + kWebsite + L"\n\nOpen the project page?";

	const int r = MessageBoxW(g_wnd, t.c_str(), (std::wstring(L"About ") + kAppName).c_str(),
	                          (hasSite ? MB_YESNO : MB_OK) | MB_ICONINFORMATION);
	if (hasSite && r == IDYES)
		openUrl(kWebsite);
}

bool copyToClipboard(const wchar_t *text)
{
	if (!OpenClipboard(g_wnd))
		return false;

	EmptyClipboard();
	const size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);

	bool ok = false;
	if (mem) {
		std::memcpy(GlobalLock(mem), text, bytes);
		GlobalUnlock(mem);
		ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
		if (!ok)
			GlobalFree(mem);
	}

	CloseClipboard();
	return ok;
}

void showDonate()
{
	if (kBtcAddress[0] == L'\0') {
		MessageBoxW(g_wnd, L"No Bitcoin address is set yet.\n\nOpen src/appinfo.h, put your address in kBtcAddress and rebuild.",
		            kAppName, MB_OK | MB_ICONINFORMATION);
		return;
	}

	const std::wstring t = std::wstring(L"Thank you for supporting ") + kAppName + L"!\n\n"
	                     + L"Bitcoin (BTC) address:\n\n" + kBtcAddress
	                     + L"\n\nCopy the address to the clipboard?";

	if (MessageBoxW(g_wnd, t.c_str(), L"Support this project", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
		if (copyToClipboard(kBtcAddress))
			MessageBoxW(g_wnd, L"Address copied.", kAppName, MB_OK | MB_ICONINFORMATION);
	}
}

void trayMenu()
{
	HMENU m = CreatePopupMenu();
	AppendMenuW(m, MF_STRING, CMD_TOGGLE, IsWindowVisible(g_wnd) ? L"Hide window" : L"Show window");
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING | (startupEnabled() ? MF_CHECKED : 0), CMD_STARTUP, L"Run at startup");
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING, CMD_ABOUT, L"About");
	AppendMenuW(m, MF_STRING, CMD_DONATE, L"Donate (Bitcoin)...");
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING, CMD_QUIT, L"Quit");

	POINT pt;
	GetCursorPos(&pt);
	SetForegroundWindow(g_wnd);   // required, or the menu won't close when clicking elsewhere
	const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, nullptr);
	PostMessageW(g_wnd, WM_NULL, 0, 0);
	DestroyMenu(m);

	switch (cmd) {
	case CMD_TOGGLE:
		if (IsWindowVisible(g_wnd)) saveAndHide(); else showWindow();
		break;
	case CMD_STARTUP: setStartup(!startupEnabled()); syncControls(); break;
	case CMD_ABOUT:   showAbout(); break;
	case CMD_DONATE:  showDonate(); break;
	case CMD_QUIT:    DestroyWindow(g_wnd); break;   // unsaved changes are discarded
	}
}

// ---------------------------------------------------------------- icon (drawn at runtime, no resources)

HICON makeIcon(int size)
{
	BITMAPV5HEADER bi {};
	bi.bV5Size        = sizeof(bi);
	bi.bV5Width       = size;
	bi.bV5Height      = size;
	bi.bV5Planes      = 1;
	bi.bV5BitCount    = 32;
	bi.bV5Compression = BI_BITFIELDS;
	bi.bV5RedMask     = 0x00FF0000;
	bi.bV5GreenMask   = 0x0000FF00;
	bi.bV5BlueMask    = 0x000000FF;
	bi.bV5AlphaMask   = 0xFF000000;

	void *bits = nullptr;
	HDC   dc   = GetDC(nullptr);
	HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
	ReleaseDC(nullptr, dc);

	if (!color || !bits)
		return LoadIconW(nullptr, kStdIcon);

	auto *px = static_cast<DWORD *>(bits);
	const double c = size / 2.0, rad = size / 2.0 - 1.5;

	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const double dx = x + 0.5 - c, dy = y + 0.5 - c;
			const double r  = std::sqrt(dx * dx + dy * dy);

			double a = rad + 0.5 - r;                    // 1 inside, soft edge, 0 outside
			a = a < 0 ? 0 : (a > 1 ? 1 : a);

			const bool right = dx > 0;                   // half bright, half dimmed
			const DWORD R = right ? 70 : 255, G = right ? 70 : 196, B = right ? 85 : 40;
			const DWORD A = DWORD(a * 255);

			px[y * size + x] = (A << 24) | (R << 16) | (G << 8) | B;
		}
	}

	HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
	ICONINFO ii {};
	ii.fIcon    = TRUE;
	ii.hbmMask  = mask;
	ii.hbmColor = color;
	HICON icon = CreateIconIndirect(&ii);

	DeleteObject(mask);
	DeleteObject(color);
	return icon ? icon : LoadIconW(nullptr, kStdIcon);
}

// ---------------------------------------------------------------- custom drawing

void fillRound(HDC dc, const RECT &r, int radius, COLORREF c)
{
	HBRUSH b  = CreateSolidBrush(c);
	HGDIOBJ ob = SelectObject(dc, b);
	HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
	RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
	SelectObject(dc, op);
	SelectObject(dc, ob);
	DeleteObject(b);
}

void fillEllipse(HDC dc, const RECT &r, COLORREF c)
{
	HBRUSH b  = CreateSolidBrush(c);
	HGDIOBJ ob = SelectObject(dc, b);
	HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
	Ellipse(dc, r.left, r.top, r.right + 1, r.bottom + 1);
	SelectObject(dc, op);
	SelectObject(dc, ob);
	DeleteObject(b);
}

void drawButton(const DRAWITEMSTRUCT *d)
{
	HDC  dc = d->hDC;
	RECT r  = d->rcItem;
	const bool pressed = (d->itemState & ODS_SELECTED) != 0;

	FillRect(dc, &r, g_bgBrush);   // clear the corners first
	SetBkMode(dc, TRANSPARENT);

	wchar_t text[96] {};
	GetWindowTextW(d->hwndItem, text, 95);

	if (d->CtlID == ID_TOGGLE_STARTUP) {
		// a full-width row: label on the left, switch on the right
		RECT lr = r;
		SelectObject(dc, g_fBody);
		SetTextColor(dc, kText);
		DrawTextW(dc, text, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

		const int w = S(40), h = S(22), cy = (r.top + r.bottom) / 2;
		RECT sw = { r.right - w, cy - h / 2, r.right, cy - h / 2 + h };
		fillRound(dc, sw, h, g_startup ? kAccent : kPanelHi);

		const int k = h - S(6);
		const int kx = g_startup ? sw.right - S(3) - k : sw.left + S(3);
		RECT knob = { kx, sw.top + S(3), kx + k, sw.top + S(3) + k };
		fillEllipse(dc, knob, g_startup ? RGB(40, 32, 8) : kThumb);
		return;
	}

	if (d->CtlID == ID_BTN_DONATE) {
		const int n  = S(22);
		const int ox = (r.left + r.right - n) / 2, oy = (r.top + r.bottom - n) / 2;
		const COLORREF hc = pressed ? RGB(180, 50, 56) : RGB(235, 76, 86);

		for (int py = 0; py < n; ++py) {
			for (int px = 0; px < n; ++px) {
				const double c = core::heartCoverage(px, py, n);
				if (c <= 0.0)
					continue;
				const int R = int(GetRValue(kBg) * (1 - c) + GetRValue(hc) * c + 0.5);
				const int G = int(GetGValue(kBg) * (1 - c) + GetGValue(hc) * c + 0.5);
				const int B = int(GetBValue(kBg) * (1 - c) + GetBValue(hc) * c + 0.5);
				SetPixelV(dc, ox + px, oy + py, RGB(R, G, B));
			}
		}
		return;
	}

	const bool primary = d->CtlID == ID_BTN_TRAY;
	COLORREF fill = primary ? kAccent : kPanel;
	if (pressed)
		fill = primary ? RGB(214, 154, 36) : kPanelHi;

	fillRound(dc, r, S(10), fill);

	SelectObject(dc, primary ? g_fBold : g_fBody);
	SetTextColor(dc, primary ? RGB(32, 26, 6) : kText);
	if (pressed)
		OffsetRect(&r, 0, 1);
	DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// Slider: rounded track, brightness = amber fill up to the thumb, temperature = warm->cool gradient.
void drawChannel(int idx, NMCUSTOMDRAW *cd)
{
	HDC  dc = cd->hdc;
	RECT cr {};
	GetClientRect(cd->hdr.hwndFrom, &cr);

	RECT th {};
	SendMessageW(cd->hdr.hwndFrom, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&th));
	const int thumbX = (th.left + th.right) / 2;

	const int cy = (cr.top + cr.bottom) / 2, h = S(6);
	RECT tr = { cr.left + S(4), cy - h / 2, cr.right - S(4), cy - h / 2 + h };

	HRGN rgn = CreateRoundRectRgn(tr.left, tr.top, tr.right + 1, tr.bottom + 1, h, h);
	SelectClipRgn(dc, rgn);

	if (idx == S_TEMP) {
		const int n = tr.right - tr.left;
		for (int x = 0; x < n; ++x) {
			const double t = n > 1 ? double(x) / (n - 1) : 0.0;   // 0 = warm, 1 = cool
			const int R = int(255 + (205 - 255) * t);
			const int G = int(150 + (225 - 150) * t);
			const int B = int(50  + (255 - 50)  * t);
			RECT s = { tr.left + x, tr.top, tr.left + x + 1, tr.bottom + 1 };
			HBRUSH b = CreateSolidBrush(RGB(R, G, B));
			FillRect(dc, &s, b);
			DeleteObject(b);
		}
	} else {
		HBRUSH bt = CreateSolidBrush(kTrack);
		RECT all = { tr.left, tr.top, tr.right + 1, tr.bottom + 1 };
		FillRect(dc, &all, bt);
		DeleteObject(bt);

		RECT fillR = { tr.left, tr.top, thumbX, tr.bottom + 1 };
		HBRUSH ba = CreateSolidBrush(kAccent);
		FillRect(dc, &fillR, ba);
		DeleteObject(ba);
	}

	SelectClipRgn(dc, nullptr);
	DeleteObject(rgn);
}

void drawThumb(NMCUSTOMDRAW *cd)
{
	RECT r = cd->rc;
	InflateRect(&r, 0, -S(2));
	fillRound(cd->hdc, r, r.right - r.left, kThumb);
}

LRESULT sliderCustomDraw(int idx, NMCUSTOMDRAW *cd)
{
	switch (cd->dwDrawStage) {
	case CDDS_PREPAINT:
		FillRect(cd->hdc, &cd->rc, g_bgBrush);
		return CDRF_NOTIFYITEMDRAW;

	case CDDS_ITEMPREPAINT:
		if (cd->dwItemSpec == TBCD_CHANNEL)
			drawChannel(idx, cd);
		else if (cd->dwItemSpec == TBCD_THUMB)
			drawThumb(cd);
		return CDRF_SKIPDEFAULT;   // also skips the tick marks
	}
	return CDRF_DODEFAULT;
}

void enableDarkTitleBar(HWND hwnd)
{
	using DwmSetFn = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

	if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
		const auto fn = reinterpret_cast<DwmSetFn>(reinterpret_cast<void *>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
		if (fn) {
			const BOOL on = TRUE;
			fn(hwnd, 20, &on, sizeof(on));   // DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 2004+ / 11)
			fn(hwnd, 19, &on, sizeof(on));   // same attribute id on older Windows 10 builds
		}
		FreeLibrary(dwm);
	}
}

// ---------------------------------------------------------------- window

HWND mk(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h,
        HWND parent, int id, HFONT font)
{
	HWND h2 = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
	                          parent, reinterpret_cast<HMENU>(INT_PTR(id)), g_inst, nullptr);
	SendMessageW(h2, WM_SETFONT, WPARAM(font), TRUE);
	return h2;
}

constexpr int kClientW = 420;
constexpr int kClientH = 394;

void createControls(HWND hwnd)
{
	// header (the sun icon is painted in WM_PAINT)
	mk(L"STATIC", kAppName, SS_LEFT, 66, 16, 290, 30, hwnd, ID_STATIC_TITLE, g_fTitle);
	mk(L"STATIC", L"Brightness and color temperature", SS_LEFT, 66, 46, 290, 18, hwnd, ID_STATIC_MUTED, g_fSmall);

	mk(L"BUTTON", L"Donate", WS_TABSTOP | BS_OWNERDRAW, 366, 18, 30, 30, hwnd, ID_BTN_DONATE, g_fBody);

	for (int i = 0; i < S_COUNT; ++i) {
		const int y = 84 + i * 68;

		mk(L"STATIC", kDefs[i].name, SS_LEFT, 24, y, 240, 20, hwnd, 0, g_fBody);
		g_val[i] = mk(L"STATIC", L"", SS_RIGHT, 296, y, 100, 20, hwnd, ID_STATIC_VALUE, g_fBold);
		g_tb[i]  = mk(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
		              24, y + 24, 372, 30, hwnd, ID_TB_BASE + i, g_fBody);

		// the warmest selectable temperature is whatever this PC's gamma limit allows (see engine.h)
		const int lo = i == S_TEMP ? g_engine.minKelvin() / 100 : kDefs[i].lo;
		SendMessageW(g_tb[i], TBM_SETRANGE, TRUE, MAKELONG(lo, kDefs[i].hi));
		SendMessageW(g_tb[i], TBM_SETLINESIZE, 0, 1);
		SendMessageW(g_tb[i], TBM_SETPAGESIZE, 0, 5);
		SendMessageW(g_tb[i], WM_CHANGEUISTATE, MAKELONG(UIS_SET, UISF_HIDEFOCUS), 0);
	}

	mk(L"STATIC", L"Lower = warmer light with less blue.", SS_LEFT, 24, 84 + 68 + 56, 372, 18, hwnd, ID_STATIC_MUTED, g_fSmall);

	g_toggle = mk(L"BUTTON", L"Run at Windows startup", WS_TABSTOP | BS_OWNERDRAW,
	              24, 244, 372, 36, hwnd, ID_TOGGLE_STARTUP, g_fBody);

	mk(L"STATIC", L"Save to tray keeps your settings. Exit closes the app and discards unsaved changes. Reset restores the defaults.",
	   SS_LEFT, 24, 292, 372, 34, hwnd, ID_STATIC_MUTED, g_fSmall);

	mk(L"BUTTON", L"Reset",        WS_TABSTOP | BS_OWNERDRAW, 24,  336, 72,  34, hwnd, ID_BTN_RESET, g_fBody);
	mk(L"BUTTON", L"About",        WS_TABSTOP | BS_OWNERDRAW, 102, 336, 72,  34, hwnd, ID_BTN_ABOUT, g_fBody);
	mk(L"BUTTON", L"Exit",         WS_TABSTOP | BS_OWNERDRAW, 206, 336, 80,  34, hwnd, ID_BTN_EXIT,  g_fBody);
	mk(L"BUTTON", L"Save to tray", WS_TABSTOP | BS_OWNERDRAW, 292, 336, 104, 34, hwnd, ID_BTN_TRAY,  g_fBold);

	syncControls();
}

void onScroll(HWND tb, WORD code)
{
	int idx = -1;
	for (int i = 0; i < S_COUNT; ++i)
		if (g_tb[i] == tb) idx = i;
	if (idx < 0)
		return;

	posToCfg(idx, int(SendMessageW(tb, TBM_GETPOS, 0, 0)));
	applyConfig();
	refreshLabels();
	InvalidateRect(tb, nullptr, FALSE);   // repaint the whole slider so the fill never leaves stale pixels
	(void)code;                           // changes are previewed live, saved only via "Save to tray"
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == g_taskbarMsg && g_taskbarMsg != 0) {   // Explorer restarted
		trayAdd();
		return 0;
	}

	switch (msg) {
	case WM_CREATE:
		g_wnd = hwnd;
		enableDarkTitleBar(hwnd);
		createControls(hwnd);
		return 0;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		DrawIconEx(dc, S(24), S(16), g_icon, S(32), S(32), 0, nullptr, DI_NORMAL);
		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_CTLCOLORSTATIC: {
		HDC  dc = reinterpret_cast<HDC>(wp);
		const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
		SetBkColor(dc, kBg);
		SetTextColor(dc, id == ID_STATIC_MUTED ? kMuted : (id == ID_STATIC_VALUE ? kAccent : kText));
		return reinterpret_cast<LRESULT>(g_bgBrush);
	}

	case WM_DRAWITEM:
		drawButton(reinterpret_cast<const DRAWITEMSTRUCT *>(lp));
		return TRUE;

	case WM_NOTIFY: {
		const NMHDR *h = reinterpret_cast<const NMHDR *>(lp);
		if (h->code == NM_CUSTOMDRAW)
			for (int i = 0; i < S_COUNT; ++i)
				if (h->hwndFrom == g_tb[i])
					return sliderCustomDraw(i, reinterpret_cast<NMCUSTOMDRAW *>(lp));
		break;
	}

	case WM_COMMAND:
		if (HIWORD(wp) == BN_CLICKED) {
			switch (LOWORD(wp)) {
			case ID_TOGGLE_STARTUP:
				g_startup = !g_startup;
				setStartup(g_startup);
				InvalidateRect(g_toggle, nullptr, FALSE);
				break;
			case ID_BTN_RESET:
				g_cfg = AppConfig();       // defaults, applied and saved right away
				applyConfig();
				syncControls();
				saveConfig(g_cfg);
				break;
			case ID_BTN_ABOUT:
				showAbout();
				break;
			case ID_BTN_DONATE:
				showDonate();
				break;
			case ID_BTN_TRAY:
				saveAndHide();
				break;
			case ID_BTN_EXIT:
				DestroyWindow(hwnd);        // no save; original gamma is restored in WM_DESTROY
				break;
			}
		}
		return 0;

	case WM_HSCROLL:
		onScroll(reinterpret_cast<HWND>(lp), LOWORD(wp));
		return 0;

	case WM_TIMER:
		if (wp == ID_TIMER) {
			g_engine.reapply();   // Windows (UAC, fullscreen apps, drivers) can silently reset the ramp
			g_overlay.refresh();  // stay on top of the taskbar and other topmost windows
		}
		return 0;

	case WM_TRAY:
		if (lp == WM_LBUTTONUP) {
			if (IsWindowVisible(hwnd)) saveAndHide(); else showWindow();
		} else if (lp == WM_RBUTTONUP) {
			trayMenu();
		}
		return 0;

	case WM_SHOWAPP:
		showWindow();
		return 0;

	case WM_DISPLAYCHANGE:
		g_engine.reapply();
		g_overlay.refresh();
		return 0;

	case WM_POWERBROADCAST:
		if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND)
			g_engine.reapply();
		return TRUE;

	case WM_CLOSE:                 // the X button behaves like "Save to tray"
		saveAndHide();
		return 0;

	case WM_ENDSESSION:
		if (wp)
			g_engine.restore();
		return 0;

	case WM_DESTROY:
		KillTimer(hwnd, ID_TIMER);
		trayRemove();
		g_overlay.destroy();
		g_engine.restore();
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wp, lp);
}

HFONT makeFont(int px, int weight)
{
	return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
	                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
	                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

} // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int)
{
	g_inst = inst;
	SetProcessDPIAware();

	// Single instance: a second launch just brings up the first one's window.
	CreateMutexW(nullptr, FALSE, (std::wstring(L"Local\\") + kAppId + L"-single-instance").c_str());
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		if (HWND other = FindWindowW(kClass, nullptr))
			PostMessageW(other, WM_SHOWAPP, 0, 0);
		return 0;
	}

	INITCOMMONCONTROLSEX icc { sizeof(icc), ICC_BAR_CLASSES };
	InitCommonControlsEx(&icc);

	HDC sdc = GetDC(nullptr);
	g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
	ReleaseDC(nullptr, sdc);

	g_fBody  = makeFont(14, FW_NORMAL);
	g_fBold  = makeFont(14, FW_SEMIBOLD);
	g_fTitle = makeFont(22, FW_SEMIBOLD);
	g_fSmall = makeFont(12, FW_NORMAL);
	g_bgBrush = CreateSolidBrush(kBg);

	if (!g_engine.init()) {
		MessageBoxW(nullptr, L"Could not open the primary display.", kAppName, MB_ICONERROR);
		return 1;
	}

	g_cfg = loadConfig();
	g_cfg.kelvin = std::max(g_cfg.kelvin, g_engine.minKelvin());   // saved value may be warmer than this PC allows
	g_overlay.create(inst);

	g_icon       = makeIcon(32);
	g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");

	WNDCLASSEXW wc {};
	wc.cbSize        = sizeof(wc);
	wc.lpfnWndProc   = wndProc;
	wc.hInstance     = inst;
	wc.hIcon         = g_icon;
	wc.hIconSm       = g_icon;
	wc.hCursor       = LoadCursorW(nullptr, kArrowCursor);
	wc.hbrBackground = g_bgBrush;
	wc.lpszClassName = kClass;
	RegisterClassExW(&wc);

	const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	RECT rc = { 0, 0, S(kClientW), S(kClientH) };
	AdjustWindowRect(&rc, style, FALSE);

	CreateWindowExW(0, kClass, kAppName, style, CW_USEDEFAULT, CW_USEDEFAULT,
	                rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);

	if (!g_wnd)
		return 1;

	applyConfig();       // restore the brightness / temperature saved last time
	trayAdd();
	SetTimer(g_wnd, ID_TIMER, 5000, nullptr);

	if (!wcsstr(GetCommandLineW(), L"--tray"))   // started by Windows at login: stay in the tray
		showWindow();

	MSG m;
	while (GetMessageW(&m, nullptr, 0, 0) > 0) {
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}

	DestroyIcon(g_icon);
	for (HFONT f : { g_fBody, g_fBold, g_fTitle, g_fSmall })
		DeleteObject(f);
	DeleteObject(g_bgBrush);
	return int(m.wParam);
}