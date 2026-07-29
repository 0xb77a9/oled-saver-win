// oledSaverWin.cpp : Defines the entry point for the application.
//

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#define NOMINMAX

#ifndef UNICODE
#define UNICODE
#endif // UNICODE

#ifndef _UNICODE
#define _UNICODE
#endif // _UNICODE

//  Windows Header Files:
#include <windows.h>
#include <shellapi.h>

#include "resource.h"
#include <powrprof.h>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif // GET_X_LPARAM

#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif // GET_Y_LPARAM

// Global Variables:
HINSTANCE hInst; // current instance

static LPCWSTR szWindowClass = L"oledSaverWinClass";
static LPCWSTR szWindowTitle = L"oledSaverWin";
static const int nAlphaValue = 240;
static const int nIdOffTimer = 42;
static const int nIdNoSleepTimer = 43;
static const int nIdIdleTimer = 44;
NOTIFYICONDATA nid = {};

struct OledSaverWinState
{
	bool isFullscreen{false};
	int noSleepCount{0};
	WINDOWPLACEMENT wpPrev{sizeof(wpPrev)};
	bool isResizing{false};
	bool isDragging{false};
	POINT dragStartPos{};
	POINT windowStartPos{};
	POINT windowStartSize{};
	int offTimeout{0};
	int idleTimeoutSeconds{300};
	int dimPercentage{95};
	bool autostart{false};
	POINT lastMousePos{};
	int displayOffTimeoutSeconds{0};
	bool displayOffTriggered{false};
};

void LoadSettings(OledSaverWinState* state) {
	HKEY hKey;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\oledSaverWin", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		DWORD dataSize = sizeof(DWORD);
		DWORD value;
		if (RegGetValue(hKey, NULL, L"IdleTimeoutSeconds", RRF_RT_REG_DWORD, NULL, &value, &dataSize) == ERROR_SUCCESS) {
			state->idleTimeoutSeconds = value;
		}
		if (RegGetValue(hKey, NULL, L"DimPercentage", RRF_RT_REG_DWORD, NULL, &value, &dataSize) == ERROR_SUCCESS) {
			state->dimPercentage = value;
		}
		if (RegGetValue(hKey, NULL, L"DisplayOffTimeoutSeconds", RRF_RT_REG_DWORD, NULL, &value, &dataSize) == ERROR_SUCCESS) {
			state->displayOffTimeoutSeconds = value;
		}
		RegCloseKey(hKey);
	}
}

bool CheckAutostart() {
	HKEY hKey;
	bool autostart = false;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		if (RegQueryValueEx(hKey, L"oledSaverWin", NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
			autostart = true;
		}
		RegCloseKey(hKey);
	}
	return autostart;
}

void UpdateAutostart(bool enable) {
	HKEY hKey;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		if (enable) {
			wchar_t exePath[MAX_PATH];
			GetModuleFileName(NULL, exePath, MAX_PATH);
			RegSetValueEx(hKey, L"oledSaverWin", 0, REG_SZ, (const BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
		} else {
			RegDeleteValue(hKey, L"oledSaverWin");
		}
		RegCloseKey(hKey);
	}
}

void SaveSettings(OledSaverWinState* state) {
	HKEY hKey;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\oledSaverWin", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		DWORD value = state->idleTimeoutSeconds;
		RegSetValueEx(hKey, L"IdleTimeoutSeconds", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
		value = state->dimPercentage;
		RegSetValueEx(hKey, L"DimPercentage", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
		value = state->displayOffTimeoutSeconds;
		RegSetValueEx(hKey, L"DisplayOffTimeoutSeconds", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
		RegCloseKey(hKey);
	}
}

void ApplyDimSettings(HWND hWnd, OledSaverWinState* state) {
	int alpha = 255 - (state->dimPercentage * 255 / 100);
	if (alpha < 0) alpha = 0;
	if (alpha > 255) alpha = 255;
	SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
}

bool IsDisplayRequiredBySystem() {
	ULONG execState = 0;
	if (CallNtPowerInformation(SystemExecutionState, NULL, 0, &execState, sizeof(execState)) == 0) { // STATUS_SUCCESS is 0
		return (execState & ES_DISPLAY_REQUIRED) != 0;
	}
	return false;
}

// Forward declarations of functions included in this code module:
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int, OledSaverWinState *);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

enum eEventHandled
{
	eehCancelled = 0
	, eehHandled
};

eEventHandled FullscreenHandler(HWND hwnd, WPARAM wParam);

int APIENTRY WinMain(HINSTANCE hInstance,
					 HINSTANCE hPrevInstance,
					 LPSTR lpCmdLine,
					 int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	MSG msg;
	OledSaverWinState osws;

	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow, &osws))
	{
		return FALSE;
	}

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_OLEDSAVERWIN));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow, OledSaverWinState *pWindowState)
{
	HWND hWnd;
	if (!pWindowState)
		return FALSE;

	hInst = hInstance; // Store instance handle in our global variable

	LoadSettings(pWindowState);
	pWindowState->autostart = CheckAutostart();

	hWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW, szWindowClass, szWindowTitle, WS_POPUP,
						  CW_USEDEFAULT, CW_USEDEFAULT, 600, 400, NULL, NULL, hInstance, NULL);

	if (!hWnd)
		return FALSE;

	SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pWindowState);

	ApplyDimSettings(hWnd, pWindowState);
	ShowWindow(hWnd, SW_HIDE);
	UpdateWindow(hWnd);

	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hWnd;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_OLEDSAVERWIN));
	wcscpy_s(nid.szTip, ARRAYSIZE(nid.szTip), L"OLED Saver");
	Shell_NotifyIcon(NIM_ADD, &nid);

	SetTimer(hWnd, nIdIdleTimer, 1000, NULL);

	DWORD style = GetWindowLong(hWnd, GWL_STYLE);
	SetWindowLong(hWnd, GWL_STYLE, style | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

	return TRUE;
}

enum DragResizeEvent
{
	dreStart = 0,
	dreStop,
	dreMove,
	//	dreFullscreenOn,
	//	dreFullscreenOff,
};

eEventHandled DragHandler(HWND hwnd, DragResizeEvent event)
{
	OledSaverWinState *me = (OledSaverWinState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (!me)
		return eehCancelled;

	if (dreStop == event)
	{
		me->isDragging = false;
		return eehHandled;
	}
	else if (dreStart == event)
	{
		if (me->isFullscreen)
			return eehCancelled;

		short shiftState = GetKeyState(VK_SHIFT);
		short controlState = GetKeyState(VK_CONTROL);
		if ((shiftState & 0x8000) || (controlState & 0x8000))
			return eehCancelled; // shift or control -> resizing, not dragging

		me->isDragging = true;
		GetCursorPos(&me->dragStartPos);
		RECT rect;
		GetWindowRect(hwnd, &rect);
		me->windowStartPos.x = rect.left;
		me->windowStartPos.y = rect.top;
		return eehHandled;
	}
	else if (dreMove == event)
	{
		if (!me->isDragging)
			return eehCancelled;

		POINT dragPos = {};
		GetCursorPos(&dragPos);
		// move window
		int newX = dragPos.x - me->dragStartPos.x + me->windowStartPos.x;
		int newY = dragPos.y - me->dragStartPos.y + me->windowStartPos.y;
		SetWindowPos(hwnd, NULL, newX, newY, 0, 0, SWP_NOSIZE);
		return eehHandled;
	}

	return eehCancelled;
}

void ResizeHandler(HWND hwnd, DragResizeEvent event)
{
	OledSaverWinState *me = (OledSaverWinState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (!me)
		return;

	if (dreStop == event)
	{
		static int cnt = 0;
		me->isResizing = false;
		return;
	}
	else if (dreStart == event)
	{
		if (me->isFullscreen)
			return;

		me->isResizing = true;
		GetCursorPos(&me->dragStartPos);
		RECT rect;
		GetWindowRect(hwnd, &rect);
		me->windowStartSize.x = rect.right - rect.left;
		me->windowStartSize.y = rect.bottom - rect.top;
		return;
	}
	else if (dreMove == event)
	{
		if (!me->isResizing)
			return;

		POINT dragPos = {};
		GetCursorPos(&dragPos);
		// resize window
		int newWidth = dragPos.x - me->dragStartPos.x + me->windowStartSize.x;
		if (newWidth < 100)
			newWidth = 100;
		int newHeight = dragPos.y - me->dragStartPos.y + me->windowStartSize.y;
		if (newHeight < 100)
			newHeight = 100;
		SetWindowPos(hwnd, HWND_TOP, 0, 0, newWidth, newHeight, SWP_NOZORDER | SWP_NOMOVE);
	}
}

eEventHandled FullscreenHandler(HWND hwnd, WPARAM wParam)
{
	OledSaverWinState *me = (OledSaverWinState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (!me)
		return eehCancelled;

	// if number key (upper row, or numpad) is pressed, then monitor off
	int delaySeconds = 0;
	if (wParam >= '0' && wParam <= '9') //|| (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9))
		delaySeconds = (wParam - '0') + 1;

	if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
		delaySeconds = (wParam - VK_NUMPAD0) + 1;

	// PgDn or arrow down, then monitor off
	if ((VK_NEXT == wParam) || (VK_DOWN == wParam))
		delaySeconds = 1;

	if (0 != delaySeconds)
	{
		SetTimer(hwnd, nIdOffTimer, delaySeconds * 1000, NULL);
		if (!me->isFullscreen)
			ShowWindow(hwnd, SW_HIDE);
		return eehHandled;
	}

	// handle escape and enter keys only
	if ((VK_RETURN != wParam) && (VK_ESCAPE != wParam))
		return eehCancelled;

	if ((VK_ESCAPE == wParam) && (!me->isFullscreen))
	{
		// if not full screen, then minimize window to taskbar
		ShowWindow(hwnd, SW_HIDE);
		return eehHandled;
	}

	// otherwise, toggle
	if (!me->isFullscreen)
	{
		MONITORINFO mi = {sizeof(mi)};
		if (!GetWindowPlacement(hwnd, &me->wpPrev) || !GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi))
			return eehHandled;

		SetWindowPos(hwnd, HWND_TOPMOST,
					 mi.rcMonitor.left, mi.rcMonitor.top,
					 mi.rcMonitor.right - mi.rcMonitor.left,
					 mi.rcMonitor.bottom - mi.rcMonitor.top,
					 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

		// and hide mouse cursor
		ShowCursor(false);
		ShowWindow(hwnd, SW_SHOW);
		GetCursorPos(&me->lastMousePos);
		me->isFullscreen = true;
	}
	else
	{
		SetWindowPlacement(hwnd, &me->wpPrev);
		SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
						 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		ShowCursor(true);
		ShowWindow(hwnd, SW_HIDE);
		me->isFullscreen = false;
	}

	return eehHandled;
}

INT_PTR CALLBACK SettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	static OledSaverWinState* state = nullptr;
	switch (message) {
	case WM_INITDIALOG:
		state = (OledSaverWinState*)lParam;
		if (state) {
			SetDlgItemInt(hDlg, IDC_IDLE_TIME, state->idleTimeoutSeconds, FALSE);
			SetDlgItemInt(hDlg, IDC_DIM_PERCENT, state->dimPercentage, FALSE);
			SetDlgItemInt(hDlg, IDC_DISPLAYOFF_TIME, state->displayOffTimeoutSeconds, FALSE);
			CheckDlgButton(hDlg, IDC_AUTOSTART, state->autostart ? BST_CHECKED : BST_UNCHECKED);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK && state) {
			state->idleTimeoutSeconds = GetDlgItemInt(hDlg, IDC_IDLE_TIME, NULL, FALSE);
			state->dimPercentage = GetDlgItemInt(hDlg, IDC_DIM_PERCENT, NULL, FALSE);
			state->displayOffTimeoutSeconds = GetDlgItemInt(hDlg, IDC_DISPLAYOFF_TIME, NULL, FALSE);
			state->autostart = (IsDlgButtonChecked(hDlg, IDC_AUTOSTART) == BST_CHECKED);
			SaveSettings(state);
			UpdateAutostart(state->autostart);
			ApplyDimSettings(GetParent(hDlg), state);
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		} else if (LOWORD(wParam) == IDCANCEL) {
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

void ContextMenuHandler(HWND hWnd, LPARAM lParam)
{
	ShowCursor(true);
	HMENU hMenu = CreatePopupMenu();
	if (!hMenu)
		return;

	OledSaverWinState *me = (OledSaverWinState *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	bool isFullscreen = me && me->isFullscreen;
	AppendMenu(hMenu, MF_STRING | (isFullscreen ? MF_CHECKED : MF_UNCHECKED), IDM_FULLSCREEN, L"Fullscreen\tEnter");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MF_STRING, IDM_SETTINGS, L"Settings...");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MF_STRING, IDM_DISPLAYOFF, L"Display Off\tPgDn");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MF_STRING, IDM_NO_SLEEP, L"No Sleep Timeout 30m");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"Exit\tAlt+F4");

	// Get cursor position for the context menu
	POINT pt;
	if (GET_X_LPARAM(lParam) == -1 && GET_Y_LPARAM(lParam) == -1)
	{
		// Menu was triggered via keyboard - center in window
		RECT rc;
		GetClientRect(hWnd, &rc);
		pt.x = (rc.right - rc.left) / 2;
		pt.y = (rc.bottom - rc.top) / 2;
		ClientToScreen(hWnd, &pt);
	}
	else
	{
		// Menu was triggered via mouse - use cursor position
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
	}

	// Show the context menu
	int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
							 pt.x, pt.y, 0, hWnd, NULL);
	ShowCursor(false);

	// Process the selected command
	switch (cmd)
	{
	case IDM_FULLSCREEN:
		// Handle fullscreen toggle
		FullscreenHandler(hWnd, VK_RETURN);
		break;

	case IDM_SETTINGS:
		DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, SettingsDialogProc, (LPARAM)me);
		break;

	case IDM_DISPLAYOFF:
		// Turn off display
		SetTimer(hWnd, nIdOffTimer, 1 * 1000, NULL);
		break;

	case IDM_NO_SLEEP:
		// Toggle no sleep mode
		me->noSleepCount = 30 * 60;
		SetTimer(hWnd, nIdNoSleepTimer, 1 * 1000, NULL);
		break;

	case IDM_EXIT:
		// Exit application
		PostMessage(hWnd, WM_CLOSE, 0, 0);
		break;
	}

	// Clean up
	DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
	case WM_TRAYICON:
		if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
			POINT pt;
			GetCursorPos(&pt);
			SetForegroundWindow(hWnd);
			ContextMenuHandler(hWnd, MAKELPARAM(pt.x, pt.y));
			PostMessage(hWnd, WM_NULL, 0, 0);
		}
		break;
	case WM_LBUTTONDBLCLK:
		FullscreenHandler(hWnd, VK_RETURN); // emulate enter hit
		break;
	case WM_KEYDOWN:
	{
		OledSaverWinState *me = (OledSaverWinState *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (me && me->isFullscreen && wParam != VK_ESCAPE && wParam != VK_RETURN) {
			FullscreenHandler(hWnd, VK_RETURN);
		}
		if (eehHandled == FullscreenHandler(hWnd, wParam))
		{
			DragHandler(hWnd, dreStop);
			ResizeHandler(hWnd, dreStop);
		}
		break;
	}
	case WM_LBUTTONDOWN:
		SetCapture(hWnd);
		if(eehHandled != DragHandler(hWnd, dreStart))
			ResizeHandler(hWnd, dreStart);
		break;
	case WM_LBUTTONUP:
		ReleaseCapture();
		DragHandler(hWnd, dreStop);
		ResizeHandler(hWnd, dreStop);
		break;
	case WM_MBUTTONDOWN:
		SetCapture(hWnd);
		ResizeHandler(hWnd, dreStart);
		break;
	case WM_MBUTTONUP:
		ReleaseCapture();
		ResizeHandler(hWnd, dreStop);
		break;
	case WM_MOUSEMOVE:
	{
		OledSaverWinState *me = (OledSaverWinState *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (me && me->isFullscreen) {
			POINT pt;
			GetCursorPos(&pt);
			int dx = pt.x - me->lastMousePos.x;
			int dy = pt.y - me->lastMousePos.y;
			if (dx*dx + dy*dy > 25) { // move more than 5 pixels
				FullscreenHandler(hWnd, VK_RETURN);
			}
		} else {
			DragHandler(hWnd, dreMove);
			ResizeHandler(hWnd, dreMove);
		}
		break;
	}
	case WM_CONTEXTMENU:
		ContextMenuHandler(hWnd, lParam);
		break;
	case WM_TIMER:
	{
		if (wParam == nIdOffTimer)
		{
			PostMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
			KillTimer(hWnd, nIdOffTimer);
		}
		else if (wParam == nIdNoSleepTimer)
		{
			OledSaverWinState *me = (OledSaverWinState *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if (me)
			{
				me->noSleepCount--;
				if (me->noSleepCount <= 0)
				{
					KillTimer(hWnd, nIdNoSleepTimer);
					SetThreadExecutionState(0);
				}
				else
				{
					SetThreadExecutionState(ES_DISPLAY_REQUIRED);
				}
			}
		}
		else if (wParam == nIdIdleTimer)
		{
			OledSaverWinState *me = (OledSaverWinState *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if (me)
			{
				LASTINPUTINFO lii;
				lii.cbSize = sizeof(LASTINPUTINFO);
				if (GetLastInputInfo(&lii))
				{
					DWORD idleTimeMs = GetTickCount() - lii.dwTime;
					
					if (idleTimeMs < 1000) {
						me->displayOffTriggered = false;
						if (me->isFullscreen) {
							FullscreenHandler(hWnd, VK_RETURN);
						}
					}

					if (me->idleTimeoutSeconds > 0 && !me->isFullscreen && idleTimeMs > (DWORD)(me->idleTimeoutSeconds * 1000))
					{
						if (!IsDisplayRequiredBySystem()) {
							FullscreenHandler(hWnd, VK_RETURN);
						}
					}

					if (me->displayOffTimeoutSeconds > 0 && !me->displayOffTriggered && idleTimeMs > (DWORD)(me->displayOffTimeoutSeconds * 1000))
					{
						if (!IsDisplayRequiredBySystem()) {
							PostMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
							me->displayOffTriggered = true;
						}
					}
				}
			}
		}
	}
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		// TODO: Add any drawing code here...
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		Shell_NotifyIcon(NIM_DELETE, &nid);
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}
