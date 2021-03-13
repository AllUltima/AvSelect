/* Copyright (c) 2014 Nicholas Ver Hoeve
*
* This software is published under the MIT License:
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE. */

#include "stdafx.h"
#include "resource.h"
#include "DisplaySettings.h"
#include "Util.h"
#include "AvSelect.h"
#include <list>
#include <fstream>

#define TRAYICONID	1                  // ID number for the Notify Icon
#define MIN_SETTLE_TIME 200
#define MIN_DISPLAY_CHANGE_SETTLE_TIME 1000
#define ENABLE_DISPLAY_SETTLE_TIME 3000

using namespace std;

enum StaticMenuId {
	MenuId_First = WM_APP,

	MenuId_Exit = MenuId_First,
	MenuId_About,

	StaticMenuId_Max,
	StaticMenuId_Count = StaticMenuId_Max - MenuId_First
};

#define DYNAMIC_MENU_ENTRY_START StaticMenuId_Max
#define MAX_ALLOWABLE_MENU_ENTRIES (0xBFFF - WM_APP - StaticMenuId_Count)

wstring	g_UpdateXmlMsg = L"Please correct your config.xml file.";

HINSTANCE  g_Instance;  // current instance
NOTIFYICONDATA g_NotifIconData; // notify icon data
UserConfig g_Config;
wfstream g_log;
BOOLEAN g_AboutBoxVisible = FALSE;
HANDLE g_Started = NULL;
HANDLE g_AbortAudioThreadEvent = NULL;
bool g_enableMessageBoxErrors = true;

struct {
	DisplayConfig* pInitialDisplayConfig;
	PWSTR pDefaultDevice;
	bool errorMessageBoxes;
} g_RestoreState = {0};

BOOL OnInitDialog(HWND hWnd);
void ShowContextMenu(HWND hWnd);

INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK About(HWND, UINT, WPARAM, LPARAM);

void LogMessage(wstring msg)
{
	if (g_log.is_open())
		g_log << msg << std::endl;
}

void ErrorMsg(wstring msg)
{
	LogMessage(msg);
	if (g_enableMessageBoxErrors)
	{
		MessageBox(NULL, msg.c_str(), L"AvSelector Tray", MB_OK | MB_ICONERROR);
	}
}

void XmlConfigErrorMsg(wstring msg)
{
	msg += L"\n" + g_UpdateXmlMsg;
	ErrorMsg(msg);
}

bool ChangeDefaultAudioDevice(wstring name, bool suppressError)
{
	if (name == L"") 
		XmlConfigErrorMsg(L"Error updating state DefaultAudioDevice: AudioDeviceFriendlyName must be supplied.");
		
	vector<wstring> deviceList;
	PWSTR deviceId = NULL;

	HRESULT hr = FindAudioPlaybackDevice(name.c_str(), &deviceId, &deviceList);

	if (SUCCEEDED(hr) && hr != S_FALSE)
		hr = SetDefaultAudioPlaybackDeviceById(deviceId);

	delete deviceId;

	if (!suppressError && (FAILED(hr) || hr == S_FALSE))
	{
		wstringstream errorMsg;

		errorMsg << "Could not set default audio device." << std::endl;
		errorMsg << "HRESULT: 0x" << std::hex << hr << std::endl;

		if (hr == S_FALSE)
			errorMsg << "(No devices containing '" << name << "') were found." << std::endl;

		errorMsg << std::endl << "Found these audio devices: " << std::endl;

		for (wstring device : deviceList)
		{
			errorMsg << "  " << device << std::endl;
		}

		XmlConfigErrorMsg(errorMsg.str().c_str());
		return false;
	}

	return true;
}

DisplayConfig::DeviceId FindTarget(const DisplayConfig& config, UserConfig::Field field, 
	ParamList additionalArgs = ParamList())
{
	ParamList paramList = {
		{ "FriendlyName", false },
		{ "UiIndex", false },
		{ "AdapterLuid", false },
		{ "Id", false }
	};

	paramList.insert(paramList.end(), additionalArgs.begin(), additionalArgs.end());

	CheckRequiredValues(field, paramList);

	wstring name;
	unsigned long long adapterLuidRaw = 0;
	unsigned long id = ULONG_MAX;
	unsigned long uiIndex = 0;

	ReadValue(&field, "FriendlyName", name);
	ReadValue(&field, "AdapterLuid", adapterLuidRaw);
	ReadValue(&field, "Id", id);
	ReadValue(&field, "UiIndex", uiIndex);

	LUID adapterLuid;
	adapterLuid.LowPart = adapterLuidRaw & ULONG_MAX;
	adapterLuid.HighPart = adapterLuidRaw >> 32;

	list<const DisplayConfig::TargetAuxInfo*> candidates;
	for (const DisplayConfig::TargetAuxInfo& target : config.GetAuxInfo())
	{
		if ((name == L"" || WildcardMatch(target.mFriendlyName.c_str(), name.c_str())) &&
			(adapterLuidRaw == 0 || !memcmp(&target.mId.mAdapterId, &adapterLuid, sizeof(adapterLuid))) &&
			(id == ULONG_MAX || target.mId.mId == id))
			//(uiIndex == 0 || target.mUiIndex == uiIndex))
		{
			candidates.emplace_front(&target);
		}
	}

	string pAcceptedElements = "";

	for (auto item : paramList)
		pAcceptedElements += item.first + " ";

	if (field.GetValueList().size() == 0)
		throw InvalidArgumentException(string() + "Error in " + field.ToString() +
			"; Target must be specified. Select a target using one or more of these attributes: " + 
			pAcceptedElements + ".");

	switch(candidates.size())
	{
	case 0:
		return DisplayConfig::DeviceId{};
	case 1:
		return candidates.back()->mId;
	default:
		throw InvalidArgumentException(string() + "Error in " + field.ToString() +
			"; Target is ambiguous. Multiple targets on this system match. " +
			"The list of targets must be narrowed to exactly 1 using these attributes " + 
			pAcceptedElements + ".");
	}
}

DisplayConfig::DeviceId FindRequiredTarget(const DisplayConfig& config, UserConfig::Field field,
	ParamList additionalArgs = ParamList())
{
	DisplayConfig::DeviceId returnVal = FindTarget(config, field, additionalArgs);
	if (!returnVal.IsValid())
	{
		throw InvalidArgumentException(string() + "Error in " + field.ToString() +
			"; Target monitor device not found.");
	}
	return returnVal;
}

DisplayConfig::DisplaySettings ParseDisplaySettings(DisplayConfig& config, const UserConfig::State& state)
{
	DisplayConfig::DisplaySettings settings;
	int bitsPerPixel = 0;

	bool enabled;
	if (ReadValue(state.GetField("Enabled"), "Value", enabled, false))
		settings.mEnabled = enabled;

	const UserConfig::Field* pResolutionField = state.GetField("Resolution");
	if (pResolutionField)
	{
		static const vector<std::pair<string, bool>> requiredFields = {
			{ "Width", true },
			{ "Height", true },
			{ "BitsPerPixel", false },
			{ "RefreshRate", false },
			{ "ScanLineOrdering", false }
		};

		CheckRequiredValues(*pResolutionField, requiredFields);
		settings.mResolution = std::pair<UINT32, UINT32>();
		ReadValue(pResolutionField, "Width", settings.mResolution->first, true);
		ReadValue(pResolutionField, "Height", settings.mResolution->second, true);

		int bitsPerPixel = 0;
		if (ReadValue(pResolutionField, "BitsPerPixel", bitsPerPixel, true))
		{
			switch (bitsPerPixel)
			{
			case 0:
				settings.mPixelFormat = DISPLAYCONFIG_PIXELFORMAT_NONGDI;
				break;
			case 8:
				settings.mPixelFormat = DISPLAYCONFIG_PIXELFORMAT_8BPP;
				break;
			case 16:
				settings.mPixelFormat = DISPLAYCONFIG_PIXELFORMAT_16BPP;
				break;
			case 24:
				settings.mPixelFormat = DISPLAYCONFIG_PIXELFORMAT_24BPP;
				break;
			case 32:
				settings.mPixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
				break;
			default:
				throw runtime_error(std::to_string(bitsPerPixel) + " is not a valid BitsPerPixel setting");
			}
		}

		DISPLAYCONFIG_RATIONAL refreshRate;
		if (ReadValue(pResolutionField, "RefreshRate", refreshRate, 1, false))
		{
			string scanLineOrdering;
			DISPLAYCONFIG_SCANLINE_ORDERING ordering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;

			if (ReadValue(pResolutionField, "ScanLineOrdering", scanLineOrdering, false))
			{
				if (!_stricmp(scanLineOrdering.c_str(), "Progressive"))
					ordering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
				else if (!_stricmp(scanLineOrdering.c_str(), "Interlaced"))
					ordering = DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED;
				else if (!_stricmp(scanLineOrdering.c_str(), "InterlacedLowerFirst"))
					ordering = DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED_LOWERFIELDFIRST;
				else
					throw runtime_error(scanLineOrdering + " is not a valid ScanLineOrdering setting");
			}

			settings.mRefreshInfo = DisplayConfig::RefreshInfo(refreshRate, ordering);
		}
	}

	const UserConfig::Field* pLocationRelativeToTarget = state.GetField("LocationRelativeToTarget");
	if (pLocationRelativeToTarget)
	{
		ParamList additionalParams = { { "X", true }, { "Y", true } };
		settings.mPositionAnchor = FindRequiredTarget(config, *pLocationRelativeToTarget, additionalParams);
		settings.mPosition = POINTL();
		ReadValue(pLocationRelativeToTarget, "X", settings.mPosition->x, true);
		ReadValue(pLocationRelativeToTarget, "Y", settings.mPosition->y, true);
	}

	const UserConfig::Field* pCloneTarget = state.GetField("CloneTarget");
	if (pCloneTarget)
		settings.mCloneOf = FindRequiredTarget(config, *pCloneTarget);

	return settings;
}

wstring GetPcConfigurationText()
{
	wstringstream ss;
	DisplayConfig config;
	wstring endl = L"\r\n";

	ss << "Targets (Display Devices):" << endl;

	for (const auto& target : config.GetAuxInfo())
	{
		UINT64 luid = target.mId.mAdapterId.LowPart | ((UINT64)target.mId.mAdapterId.HighPart << 32);
		ss << "FriendlyName: " << target.mFriendlyName << "  ";
		//ss << "UiIndex: " << target.mUiIndex << "  ";
		ss << "AdapterLuid: " << hex << luid << "  ";
		ss << "Id: " << target.mId.mId << " ";

		DisplayConfig::RefreshInfo refreshInfo = config.GetRefreshInfo(target.mId);

		ss << "\r\n     RefreshRate: " << dec << refreshInfo.first.Numerator <<
			"/" << refreshInfo.first.Denominator << " ";

		ss << "ScanLineOrdering: ";
		switch (refreshInfo.second)
		{
		case DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE:
			ss << "Progressive";
			break;
		case DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED:
			ss << "Interlaced";
			break;
		case DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED_LOWERFIELDFIRST:
			ss << "InterlacedLowerFirst";
			break;
		default:
			ss << "?";
		}

		ss << endl;
	}

	ss << endl;

	ss << "Audio Devices:" << endl;

	vector<wstring> deviceNameList;
	FindAudioPlaybackDevice(L"", NULL, &deviceNameList);
	for (wstring device : deviceNameList)
	{
		ss << "FriendlyName: " << device << endl;
	}
	LogMessage(ss.str());

	return ss.str();
}

void ApplyDisplayConfig(DisplayConfig& config)
{
	LONG rc = config.Apply(false);

	if (rc != ERROR_SUCCESS)
		ErrorMsg(L"Failed to update display configuration. Error Code:" + std::to_wstring(rc));
}

struct SetDefaultAudioDeviceParam
{
	int mDelayMs = 0;
	std::wstring mAudioDeviceName;
	bool mBeep = false;
	bool mHideErrors = false;
};

DWORD WINAPI SetDefaultAudioDevice(LPVOID lpParam)
{
	SetDefaultAudioDeviceParam* pParam = (SetDefaultAudioDeviceParam*)lpParam;

	if (pParam->mDelayMs)
	{
		switch (WaitForSingleObject(g_AbortAudioThreadEvent, pParam->mDelayMs))
		{
		case WAIT_OBJECT_0:
			goto Out; // Abandon this task
		case WAIT_FAILED:
			Sleep(pParam->mDelayMs);
		}
	}

	if (ChangeDefaultAudioDevice(pParam->mAudioDeviceName, pParam->mHideErrors) &&
		pParam->mBeep && 
		g_Started)
	{
		PlaySoundW((LPCWSTR)SND_ALIAS_SYSTEMDEFAULT, NULL, SND_ALIAS_ID);
	}

	Out:

	delete pParam;
	return 0;
}

void HandleUserConfigMenuItemPicked(const UserConfig::MenuItem& menuItem)
{
	std::unique_ptr<DisplayConfig> pDisplayConfig;

	try {
		pDisplayConfig.reset(new DisplayConfig);
	} catch (const std::exception& e) {
		XmlConfigErrorMsg(Widen(e.what()));
	}

	LogMessage(L"Option chosen: " + Widen(menuItem.GetName()));

	if (!SetEvent(g_AbortAudioThreadEvent))
		ErrorMsg(L"SetEvent failed: " + to_wstring(GetLastError()));
	else if (!ResetEvent(g_AbortAudioThreadEvent))
		ErrorMsg(L"ResetEvent failed: " + to_wstring(GetLastError()));

	if (g_log.is_open() && pDisplayConfig)
		pDisplayConfig->LogState(g_log);

	for (UserConfig::State state : menuItem.GetTargetStates())
	{
		try 
		{
			LogMessage(L"State transition: " + Widen(state.GetType()));

			if (state.GetType() == "DefaultAudioDevice")
			{
				SetDefaultAudioDeviceParam* pParam = new SetDefaultAudioDeviceParam;
				ZeroMemory(pParam, sizeof(*pParam));

				bool applyDisplayChanges = false;
				const UserConfig::Field* pDependantOnDisplay = state.GetField("WaitUntilDisplayEnabledComplete");
				if (pDisplayConfig && pDisplayConfig->ChangesWillEnableDisplay() &&
					ReadValue(pDependantOnDisplay, "Value", applyDisplayChanges) &&
					applyDisplayChanges)
				{
					ApplyDisplayConfig(*pDisplayConfig);
					const UserConfig::Field* pDependantOnDisplay = state.GetField("WaitUntilDisplayEnabledComplete");
					pParam->mDelayMs = ENABLE_DISPLAY_SETTLE_TIME;
					ReadValue(pDependantOnDisplay, "DelayMs", pParam->mDelayMs);
				}

				pParam->mBeep = true;
				const UserConfig::Field* pBeep = state.GetField("PlayTestSound");
				ReadValue(pBeep, "Value", pParam->mBeep, true);

				UserConfig::Field device = GetRequiredField(state, "AudioDevice");
				string name;
				ReadValue(&device, "FriendlyName", name, true);
				pParam->mAudioDeviceName = Widen(name);
				pParam->mHideErrors = state.IsOptional();

				HANDLE h = CreateThread(NULL, 0, SetDefaultAudioDevice, pParam, 0, NULL);

				if (h)
					CloseHandle(h);
				else
				{
					ErrorMsg(L"Failed to Create thread for updating DefaultAudioDevice");
					delete pParam;
				}
			}
			else if (state.GetType() == "PrimaryDisplay")
			{
				if (!pDisplayConfig) continue;
				UserConfig::Field target = GetRequiredField(state, "Target");

				DisplayConfig::DeviceId targetDeviceId =
					state.IsOptional() ?
					FindTarget(*pDisplayConfig, target) :
					FindRequiredTarget(*pDisplayConfig, target);
				pDisplayConfig->SetPrimaryTarget(targetDeviceId);
			}
			else if (state.GetType() == "DisplaySettings")
			{
				if (!pDisplayConfig) continue;

				UserConfig::Field target = GetRequiredField(state, "Target");
				DisplayConfig::DeviceId targetDeviceId = 
					state.IsOptional() ?
						FindTarget(*pDisplayConfig, target) :
						FindRequiredTarget(*pDisplayConfig, target);
				DisplayConfig::DisplaySettings& settings = ParseDisplaySettings(*pDisplayConfig, state);
				pDisplayConfig->UpdateDisplaySettings(targetDeviceId, settings);
			}
			else
			{
				string msg = "Don't know how to update the state type called '";
				msg += state.GetType() + "'\n";
				msg += "Recognized state types: DefaultAudioDevice, PrimaryMonitor.\n";
				XmlConfigErrorMsg(Widen(msg));
			}
		} catch (const std::exception& e)
		{
			XmlConfigErrorMsg(Widen(e.what()));
			if (!state.ContinueOnError())
			{
				break;
			}
		}
	}

	bool wait = false;
	bool waitForEnable = false;

	if (pDisplayConfig) {
		bool wait = pDisplayConfig->HasChanged();
		bool waitForEnable = pDisplayConfig->ChangesWillEnableDisplay();
		ApplyDisplayConfig(*pDisplayConfig);

		if (g_log.is_open())
			pDisplayConfig->LogState(g_log);
	}

	LogMessage(L"Finished Option: " + Widen(menuItem.GetName()));

	if (wait)
		Sleep(MIN_DISPLAY_CHANGE_SETTLE_TIME);
	if (waitForEnable)
		Sleep(ENABLE_DISPLAY_SETTLE_TIME);
}

// if pMenuItemName is provided, limit the scope of saving state to things affected by pMenuItemName
void SaveState(wstring menuItemName = L"")
{
	if (menuItemName != L"")
	{
#pragma warning( disable : 4244 )
		string menuItemNameA(menuItemName.begin(), menuItemName.end());
		const UserConfig::MenuItem* pMenuItem = g_Config.GetMenuItem(menuItemNameA);

		for (auto& state : pMenuItem->GetTargetStates())
		{
			if (!g_RestoreState.pInitialDisplayConfig && (
				Widen(state.GetType()) == L"PrimaryDisplay" ||
				Widen(state.GetType()) == L"DisplaySettings"))
			{
				g_RestoreState.pInitialDisplayConfig = new DisplayConfig();
			}

			if (!g_RestoreState.pDefaultDevice && (
				Widen(state.GetType()) == L"DefaultAudioDevice"))
			{
				if (FAILED(GetDefaultAudioPlaybackDevice(&g_RestoreState.pDefaultDevice)))
					throw runtime_error("Cannot get default audio device.");
			}
		}
	}
	else
	{
		g_RestoreState.pInitialDisplayConfig = new DisplayConfig();
		GetDefaultAudioPlaybackDevice(&g_RestoreState.pDefaultDevice);
	}
}

void RestoreInitialState()
{
	if (g_RestoreState.pDefaultDevice)
	{
		ChangeDefaultAudioDevice(g_RestoreState.pDefaultDevice, false);
		delete g_RestoreState.pDefaultDevice;
		g_RestoreState.pDefaultDevice = NULL;
	}

	if (g_RestoreState.pInitialDisplayConfig)
	{
		g_RestoreState.pInitialDisplayConfig->Apply(true);
		delete g_RestoreState.pInitialDisplayConfig;
		g_RestoreState.pInitialDisplayConfig = NULL;
	}
}

void Apply(wstring menuItemName)
{
	string menuItemNameA(menuItemName.begin(), menuItemName.end());
	const UserConfig::MenuItem* pMenuItem = g_Config.GetMenuItem(menuItemNameA);

	if (!pMenuItem)
	{
		string narrow(menuItemName.begin(), menuItemName.end());
		throw runtime_error(string() + "Menu item name: " + narrow + " not found.");
		return;
	}

	HandleUserConfigMenuItemPicked(*pMenuItem);
	Sleep(MIN_SETTLE_TIME);
}

void ShowContextMenu(HWND hWnd)
{
	std::unique_ptr<DisplayConfig> pDisplayConfig;

	try {
		pDisplayConfig.reset(new DisplayConfig);
	} catch (...) {}

	POINT pt;
	GetCursorPos(&pt);
	HMENU hMenu = CreatePopupMenu();
	if(!hMenu)
		return;

	int dynamicCommandIndex = 0;
	for (const UserConfig::MenuItem& item : g_Config.GetMenuItems())
	{
		ULONG flags = MF_BYPOSITION | MF_CHECKED;

		for (const UserConfig::State& state : item.GetTargetStates())
		{
			if (state.IsOptional())
			{
				continue;
			}

			try
			{
				if (state.GetType() == "DefaultAudioDevice")
				{
					if (!pDisplayConfig) throw std::runtime_error("");
					UserConfig::Field device = GetRequiredField(state, "AudioDevice");
					string name;
					ReadValue(&device, "FriendlyName", name, true);

					PWSTR defaultDevice = NULL;
					HRESULT hr = GetDefaultAudioPlaybackDevice(&defaultDevice);
					if (SUCCEEDED(hr))
					{
						if (!WildcardMatch(defaultDevice, Widen(name).c_str()))
						{
							flags &= ~MF_CHECKED;
							break;
						}
					}
				}
				else if (state.GetType() == "PrimaryDisplay")
				{
					if (!pDisplayConfig) throw std::runtime_error("");
					UserConfig::Field target = GetRequiredField(state, "Target");
					DisplayConfig::DeviceId targetDeviceId = FindRequiredTarget(*pDisplayConfig, target);
					
					if (targetDeviceId != pDisplayConfig->GetPrimaryTarget())
					{
						flags &= ~MF_CHECKED;
						break;
					}
				}
				else if (state.GetType() == "DisplaySettings")
				{
					if (!pDisplayConfig) throw std::runtime_error("");
					UserConfig::Field target = GetRequiredField(state, "Target");
					DisplayConfig::DeviceId targetDeviceId = FindRequiredTarget(*pDisplayConfig, target);
					DisplayConfig::DisplaySettings& settings = ParseDisplaySettings(*pDisplayConfig, state);

					if (!pDisplayConfig->AreDisplaySettingsCurrent(targetDeviceId, settings))
					{
						flags &= ~MF_CHECKED;
						break;
					}
				}
			} catch (const std::exception&) { flags &= ~MF_CHECKED; }
		}

		InsertMenuA(hMenu, -1, flags, StaticMenuId_Max + dynamicCommandIndex++, item.GetName().c_str());
	}

	InsertMenu(hMenu, -1, MF_MENUBARBREAK, 0, 0);
	InsertMenu(hMenu, -1, MF_BYPOSITION, IDC_ABOUTBOX, L"About");
	InsertMenu(hMenu, -1, MF_BYPOSITION, MenuId_Exit, L"Exit");

	// must set window to the foreground or the menu won't disappear when it should
	SetForegroundWindow(hWnd);

	TrackPopupMenu(hMenu, TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
	DestroyMenu(hMenu);
}

// Message handler for the app, and PC Config window
INT_PTR CALLBACK DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	unsigned int wmId, wmEvent;

	switch (message) 
	{
	case WM_SHOWWINDOW:
		if (wParam)
		{
			HWND hText = GetDlgItem(hWnd, IDC_CONFIGTEXT);
			assert(hText);
			SendMessage(hText, EM_SETSEL, -1, -1);
			SendMessage(hText, EM_REPLACESEL, TRUE, (LPARAM)GetPcConfigurationText().c_str());
		}
		break;
	case WM_HOTKEY:
		wmEvent = LOWORD(wParam);

		if (wmEvent < g_Config.GetMenuItems().size())
			HandleUserConfigMenuItemPicked(g_Config.GetMenuItems()[wmEvent]);
		break;
	case WM_APP:
		switch(lParam)
		{
		case WM_LBUTTONDBLCLK:
			if (g_Config.GetDoubleClickAction())
				HandleUserConfigMenuItemPicked(*g_Config.GetDoubleClickAction());
			break;
		case WM_RBUTTONDOWN:
		case WM_CONTEXTMENU:
			ShowContextMenu(hWnd);
		}
		break;
	case WM_SYSCOMMAND:
		if((wParam & 0xFFF0) == SC_MINIMIZE)
		{
			ShowWindow(hWnd, SW_HIDE);
			return 1;
		}
		else if(wParam == IDC_ABOUTBOX) 
		{
			DialogBox(g_Instance, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
		}
		break;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam); 

		switch (wmId)
		{
		case MenuId_Exit:
			DestroyWindow(hWnd);
			break;
		case IDC_ABOUTBOX:
			if (!g_AboutBoxVisible)
				DialogBox(g_Instance, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
			break;
		default:
			if (wmId >= StaticMenuId_Max) {
				unsigned int dynamicChoice = wmId - StaticMenuId_Max;

				if (dynamicChoice < g_Config.GetMenuItems().size()) {
					HandleUserConfigMenuItemPicked(g_Config.GetMenuItems()[dynamicChoice]);
				}
			}
		}
		return 1;
	case WM_SIZE:
	{
		HWND hText = GetDlgItem(hWnd, IDC_CONFIGTEXT);
		assert(hText);
		SetWindowPos(hText, 0, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOMOVE);
		break;
	}
	case WM_CLOSE:
		ShowWindow(hWnd, SW_HIDE);
		break;
	case WM_DESTROY:
		g_NotifIconData.uFlags = 0;
		Shell_NotifyIcon(NIM_DELETE,&g_NotifIconData);
		PostQuitMessage(0);
		break;
	}
	return 0;
}

// Message handler for about box.
LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	unsigned int wmId, wmEvent;

	switch (message)
	{
	case WM_INITDIALOG:
		g_AboutBoxVisible = TRUE;
		return TRUE;

	case WM_COMMAND:
		wmId = LOWORD(wParam);
		wmEvent = HIWORD(wParam);

		switch (wmId)
		{
		case IDC_PCCONFIG:
			ShowWindow(g_NotifIconData.hWnd, SW_SHOW);
			// fallthrough
		case IDOK:
		case IDCANCEL:
			g_AboutBoxVisible = FALSE;
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		break;
	}
	return FALSE;
}


BOOLEAN CheckOneInstance()
{
	g_Started = CreateEvent(NULL, TRUE, FALSE, L"AvSelectRunning-001");
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CloseHandle(g_Started);
		g_Started = NULL;
		return FALSE;
	}

	return TRUE;
}

//	Initialize the window and tray icon
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	if (!CheckOneInstance())
		return FALSE;

	if (g_Config.GetShouldRestoreOnExit())
		SaveState();

	// prepare for XP style controls
	InitCommonControls();

	// store instance handle and create dialog
	g_Instance = hInstance;
	HWND hWnd = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_PCCONFIG), NULL, (DLGPROC)DlgProc);
	if (!hWnd)
		return FALSE;

	for (int hotkeyNumber = 0; hotkeyNumber < (int)g_Config.GetMenuItems().size(); hotkeyNumber++)
	{
		const UserConfig::MenuItem& Item = g_Config.GetMenuItems()[hotkeyNumber];

		if (Item.GetHotkey().mVk)
		{
			if (!RegisterHotKey(hWnd, hotkeyNumber, Item.GetHotkey().mModifierFlags, Item.GetHotkey().mVk))
			{
				wstring msg = L"Warning: hotkey for ";
				msg += Widen(Item.GetName());
				msg += L" failed to register.";
				ErrorMsg(msg.c_str());
			}
		}
	}

	ZeroMemory(&g_NotifIconData, sizeof(NOTIFYICONDATA));
	g_NotifIconData.cbSize = sizeof(NOTIFYICONDATA);

	// the ID number can be anything you choose
	g_NotifIconData.uID = TRAYICONID;

	// state which structure members are valid
	g_NotifIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;

	// load the icon
	g_NotifIconData.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(ICON_TV),
		IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
		LR_DEFAULTCOLOR);

	// the window to send messages to and the message to send
	//    note:	the message value should be in the
	//     range of WM_APP through 0xBFFF
	g_NotifIconData.hWnd = hWnd;
	g_NotifIconData.uCallbackMessage = WM_APP;

	// tooltip message
	lstrcpyn(g_NotifIconData.szTip, L"AV Selector", ARRAYSIZE(g_NotifIconData.szTip));

	Shell_NotifyIcon(NIM_ADD, &g_NotifIconData);

	// free icon handle
	HICON icon = g_NotifIconData.hIcon;
	g_NotifIconData.hIcon = NULL;
	icon && DestroyIcon(icon);

	return TRUE;
}

void HostProcess(wstring command, wstring workingDirectory)
{
	PWCHAR pBuffer = new WCHAR[command.length() + 1];
	wcscpy_s(pBuffer, command.length() + 1, command.c_str());

	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb = sizeof(si);

	if (CreateProcess(NULL, pBuffer, NULL, NULL, FALSE, 0, NULL,
		workingDirectory.c_str(), &si, &pi))
	{
		CloseHandle(pi.hThread);
		 
		if (WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED)
			ErrorMsg(L"Wait for hosted Process failed, exiting...");

		CloseHandle(pi.hProcess);
	}
	else
	{
		wstring error;

		switch (GetLastError())
		{
		case ERROR_FILE_NOT_FOUND:
			error = L"executable file not found.";
		default:
			error = to_wstring(GetLastError());
		}

		ErrorMsg(wstring() + L"CreateProcess on: \r\n" + pBuffer +
			L"\r\nFailed With Error:" + error);
	}

	delete pBuffer;
}

BOOLEAN ParseCommandLine(LPWSTR commandLine)
{
	BOOLEAN rval = TRUE;

	try
	{
		LPWSTR *szArgList;
		int argCount;
		int currentArg = 1;
		szArgList = CommandLineToArgvW(GetCommandLine(), &argCount);
		wstring runCommand;
		wstring runWorkingDir;
		wstring stateToApply;
		bool saveState = false;

		if (argCount > 1 && !_wcsicmp(szArgList[1], L"-log"))
		{
			g_log.open("log.txt", fstream::out);
			if (!g_log.is_open())
			{
				CHAR Buffer[MAX_PATH];
				strerror_s(&Buffer[0], ARRAYSIZE(Buffer), errno);
				ErrorMsg(L"Could not open log file: " + Widen(&Buffer[0]));
			}
			++currentArg;
		}

		if (argCount > currentArg + 2 && !_wcsicmp(szArgList[1], L"-nomessageboxes"))
		{
			g_enableMessageBoxErrors = true;
			++currentArg;
		}

		if (argCount >= currentArg + 2 && !_wcsicmp(szArgList[currentArg], L"-set"))
		{
			stateToApply = szArgList[currentArg + 1];
			if (stateToApply == L"") throw runtime_error("-set must be followed by <statename>");
			rval = FALSE;
			currentArg += 2;
		}
		else if (argCount >= currentArg + 2 && !_wcsicmp(szArgList[currentArg], L"-setuntilexit"))
		{
			stateToApply = szArgList[currentArg + 1];
			if (stateToApply == L"") throw runtime_error("-set must be followed by <statename>");
			saveState = true;
			currentArg += 2;
		}

		if (argCount >= currentArg + 2 && !_wcsicmp(szArgList[currentArg], L"-runworkingdir"))
		{
			runWorkingDir = szArgList[currentArg + 1];
			currentArg += 2;
		}

		wstring fullCommandLine = commandLine;
		PCWSTR pRunSwitch = L"-run ";
		size_t index = FindOutsideQuotes(fullCommandLine, pRunSwitch);
		
		if (index != string::npos)
		{
			index += wcslen(pRunSwitch);
			runCommand = fullCommandLine.substr(index);

			runCommand.erase(runCommand.begin(), std::find_if(runCommand.begin(), runCommand.end(),
				[](WCHAR c) { return !iswspace(c); }));

			wstring moduleName = runCommand;
			size_t firstSpace = FindOutsideQuotes(runCommand, L" ");
			if (firstSpace != wstring::npos)
				moduleName = moduleName.substr(0, firstSpace);

			if (runWorkingDir == L"")
			{
				size_t lastWhack = moduleName.rfind(L"\\");
				wstring moduleDir = runCommand.substr(0, lastWhack);
				moduleDir.erase(moduleDir.begin(), std::find_if(moduleDir.begin(), moduleDir.end(),
					[](WCHAR c) { return c != '\'' && c != '\"' && !iswspace(c);; }));

				runWorkingDir = moduleDir;
			}

			rval = FALSE;
		}
		else if (currentArg < argCount)
		{
			wstring unrecognized;
			for (int i = currentArg; i < argCount; ++i)
				unrecognized += szArgList[i] + wstring(L" ");
			string unrecognizedNarrow(unrecognized.begin(), unrecognized.end());

			throw runtime_error("command line arguments not accepted: " + unrecognizedNarrow);
		}

		LocalFree(szArgList);

		if (saveState)
			SaveState(stateToApply);

		if (stateToApply != L"")
			Apply(stateToApply);

		if (runCommand != L"")
			HostProcess(runCommand, runWorkingDir);
	}
	catch (const runtime_error& ex)
	{
		ErrorMsg(Widen(ex.what()));
		rval = FALSE;
	}

	return rval;
}

BOOLEAN ParseConfig()
{
	try
	{
		g_Config.ParseFile("config.xml");
		return TRUE;
	}
	catch (const std::exception& ex)
	{
		std::wstring msg = L"Failure to initialize. Could not parse config.xml.\n";
		msg += Widen(ex.what());
		ErrorMsg(msg.c_str());
		return FALSE;
	}
}

int APIENTRY wWinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPWSTR    lpCmdLine,
	int       nCmdShow)
{
	MSG msg = {0};
	HACCEL hAccelTable;

	ParseConfig();

	g_AbortAudioThreadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	assert(g_AbortAudioThreadEvent);

	if (!ParseCommandLine(lpCmdLine))
		goto Out;

	LogMessage(L"Starting up...");

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow))
		goto Out;

	hAccelTable = LoadAccelerators(hInstance, (LPCTSTR)ICON_TV);

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg) ||
			!IsDialogMessage(msg.hwnd, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	Out:

	LogMessage(L"Shutting down...");

	if (g_Config.GetOnTrayExitAction())
		Apply(Widen(g_Config.GetOnTrayExitAction()->GetName()));

	RestoreInitialState();

	if (g_Started)
		CloseHandle(g_Started);

	if (g_AbortAudioThreadEvent)
		CloseHandle(g_AbortAudioThreadEvent);

	if (g_log.is_open())
		g_log.close();

	return (int)msg.wParam;
}
