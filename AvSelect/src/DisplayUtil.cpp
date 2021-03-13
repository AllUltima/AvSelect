#include <stdafx.h>
#include "WinUtil.h"
#include <map>
#include <algorithm>
#include "DisplayUtil.h"
#include <assert.h>

using namespace std;
using namespace boost;

int g_rgnOutputTechPriority[] =
{
	9,   // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15                
	12,  // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SVIDEO              
	15,  // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPOSITE_VIDEO     
	10,  // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPONENT_VIDEO     
	6,   // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI                 
	5,   // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI                
	3,   // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS                
	256, // 
	13,  // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_D_JPN               
	8,   // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDI                 
	4,   // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL
	1,   // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED
	7,   // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EXTERNAL        
	2,   // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED        
	14,  // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDTVDONGLE          
};

int OutputTechPriority(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY dvot)
{
	int nPriority;
	int nTech = (dvot & (~DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL));
	if ((nTech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER) ||
		(nTech < 0) ||
		(nTech >= ARRAYSIZE(g_rgnOutputTechPriority)))
	{
		nPriority = 11; // See order above.
	}
	else
	{
		nPriority = g_rgnOutputTechPriority[nTech];
	}

	// If this is not 'internal', bump priority down a bit.
	if (dvot & ~DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL)
	{
		nPriority += 20;
	}

	return nPriority;
}

bool TargetCmp(TargetInfo& a, TargetInfo& b)
{
	if (a.mId.mAdapterId.HighPart < b.mId.mAdapterId.HighPart)
		return true;

	if (a.mId.mAdapterId.HighPart > b.mId.mAdapterId.HighPart)
		return false;

	if (a.mId.mAdapterId.LowPart < b.mId.mAdapterId.LowPart)
		return true;

	if (a.mId.mAdapterId.LowPart > b.mId.mAdapterId.LowPart)
		return false;

	if (OutputTechPriority(a.mOutputTech) < OutputTechPriority(b.mOutputTech))
		return true;

	if (OutputTechPriority(a.mOutputTech) > OutputTechPriority(b.mOutputTech))
		return false;

	return a.mId.mId < b.mId.mId;
};

vector<TargetInfo> GenerateTargetArray()
{
	HRESULT hr;
	vector<TargetInfo> targets;
	map<DeviceId, DISPLAYCONFIG_PATH_INFO*> sourceMap;
	map<DeviceId, DISPLAYCONFIG_PATH_INFO*> targetMap;

	DISPLAYCONFIG_PATH_INFO* pPathInfoArray = NULL;
	DISPLAYCONFIG_MODE_INFO* pModeInfoArray = NULL;
	UINT32 numPathArrayElements;
	UINT32 numModeInfoArrayElements;
	for (UINT32 tryBufferSize = 32; ; tryBufferSize <<= 1)
	{
		pPathInfoArray = new DISPLAYCONFIG_PATH_INFO[tryBufferSize];
		pModeInfoArray = new DISPLAYCONFIG_MODE_INFO[tryBufferSize];
		numPathArrayElements = numModeInfoArrayElements = tryBufferSize;

		ULONG rc = QueryDisplayConfig(
			QDC_ALL_PATHS,
			&numPathArrayElements,
			pPathInfoArray,
			&numModeInfoArrayElements,
			pModeInfoArray,
			NULL);

		if (rc == ERROR_SUCCESS)
			break;

		if (rc != ERROR_INSUFFICIENT_BUFFER || tryBufferSize > 1024)
			ORIGINATE_WIN32_ERR(Out, rc, "QueryDisplayConfig");
	}

	// Compress the targets of targets to those which can actually be used
	// Since there are many potential targets for a source, find the one that 
	// is actually in use or available.
	for (UINT32 i = 0; i < numPathArrayElements; ++i)
	{
		DeviceId targetId = { pPathInfoArray[i].targetInfo.adapterId, pPathInfoArray[i].targetInfo.id };

		if ((pPathInfoArray[i].flags & DISPLAYCONFIG_PATH_ACTIVE) ||
			targetMap.find(targetId) == targetMap.end() &&
			pPathInfoArray[i].targetInfo.targetAvailable)
		{
			targetMap[targetId] = &pPathInfoArray[i];
		}
	}

	for (auto it = targetMap.begin(); it != targetMap.end(); ++it)
	{
		targets.push_back(TargetInfo());
		ZeroMemory(&targets.back(), sizeof(targets.back()));
		DISPLAYCONFIG_PATH_INFO& current = *it->second;
		TargetInfo& currentDst = targets.back();

		union {
			DISPLAYCONFIG_SOURCE_DEVICE_NAME source;
			DISPLAYCONFIG_TARGET_DEVICE_NAME target;
		} queryInfo;

		if (current.flags & DISPLAYCONFIG_PATH_ACTIVE)
		{
			currentDst.mAttachedSource = SourceInfo();
			DISPLAYCONFIG_MODE_INFO* pModeInfo = &pModeInfoArray[current.sourceInfo.modeInfoIdx];
			assert(pModeInfo->infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);

			currentDst.mAttachedSource->mId = 
				{ current.sourceInfo.adapterId, current.sourceInfo.id };
			currentDst.mAttachedSource->mResolutionX = pModeInfo->sourceMode.width;
			currentDst.mAttachedSource->mResolutionY = pModeInfo->sourceMode.height;
			currentDst.mAttachedSource->mVirtualDesktopX = pModeInfo->sourceMode.position.x;
			currentDst.mAttachedSource->mVirtualDesktopY = pModeInfo->sourceMode.position.y;
			currentDst.mAttachedSource->mPixelFormat = pModeInfo->sourceMode.pixelFormat;

			ZeroMemory(&queryInfo, sizeof(queryInfo));
			queryInfo.source.header.usize = sizeof(queryInfo.source);
			queryInfo.source.header.adapterId = current.sourceInfo.adapterId;
			queryInfo.source.header.id = current.sourceInfo.id;
			queryInfo.source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			ULONG rc = DisplayConfigGetDeviceInfo(&queryInfo.source.header);

			if (rc == ERROR_SUCCESS)
				currentDst.mAttachedSource->mGdiDeviceName = queryInfo.source.viewGdiDeviceName;
		}

		currentDst.mOutputTech = current.targetInfo.outputTechnology;
		currentDst.mId = { current.targetInfo.adapterId, current.targetInfo.id, };

		ZeroMemory(&queryInfo, sizeof(queryInfo));
		queryInfo.target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		queryInfo.target.header.usize = sizeof(queryInfo.target);
		queryInfo.target.header.adapterId = current.targetInfo.adapterId;
		queryInfo.target.header.id = current.targetInfo.id;
		ULONG rc = DisplayConfigGetDeviceInfo(&queryInfo.target.header);

		if (rc == ERROR_SUCCESS)
			currentDst.mFriendlyName = queryInfo.target.monitorFriendlyDeviceName;
	}

	std::sort(targets.begin(), targets.end(), TargetCmp);

	for (ULONG i = 0; i < targets.size(); ++i)
		targets[i].mIndex = i;

	hr = S_OK;

	Out:

	delete pPathInfoArray;
	delete pModeInfoArray;

	return targets;
}

HRESULT DisableTarget(const TargetInfo& target)
{
	HRESULT hr;

	DISPLAYCONFIG_PATH_INFO* pPathInfoArray = NULL;
	DISPLAYCONFIG_MODE_INFO* pModeInfoArray = NULL;
	UINT32 numPathArrayElements;
	UINT32 numModeInfoArrayElements;

	for (UINT32 tryBufferSize = 32;; tryBufferSize <<= 1)
	{
		pPathInfoArray = new DISPLAYCONFIG_PATH_INFO[tryBufferSize];
		pModeInfoArray = new DISPLAYCONFIG_MODE_INFO[tryBufferSize];
		numPathArrayElements = numModeInfoArrayElements = tryBufferSize;

		ULONG rc = QueryDisplayConfig(
			QDC_ALL_PATHS,
			&numPathArrayElements,
			pPathInfoArray,
			&numModeInfoArrayElements,
			pModeInfoArray,
			NULL);

		if (rc == ERROR_SUCCESS)
			break;

		if (rc != ERROR_INSUFFICIENT_BUFFER || tryBufferSize > 1024)
			ORIGINATE_WIN32_ERR(Out, rc, "QueryDisplayConfig");
	}

	DISPLAYCONFIG_PATH_SOURCE_INFO* pSource = NULL;
	DISPLAYCONFIG_PATH_TARGET_INFO* pTarget = NULL;

	for (UINT32 i = 0; i < numPathArrayElements; ++i)
	{
		if (pPathInfoArray[i].targetInfo.targetAvailable && 
			target.mId == pPathInfoArray[i].targetInfo)
		{
			pPathInfoArray[i].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
			pSource = &pPathInfoArray[i].sourceInfo;
			pTarget = &pPathInfoArray[i].targetInfo;
		}
	}

	for (UINT32 i = 0; i < numPathArrayElements; ++i)
	{
		if (!memcmp(&pPathInfoArray[i].sourceInfo.adapterId, &pSource->adapterId, sizeof (LUID)) &&
			pPathInfoArray[i].sourceInfo.id == pSource->id
			)
		{
			pPathInfoArray[i].targetInfo.statusFlags &= ~DISPLAYCONFIG_SOURCE_IN_USE;
		}

		if (!memcmp(&pPathInfoArray[i].targetInfo.adapterId, &pTarget->adapterId, sizeof (LUID)) &&
			pPathInfoArray[i].targetInfo.id == pTarget->id
			)
		{
			pPathInfoArray[i].targetInfo.statusFlags &= ~DISPLAYCONFIG_TARGET_IN_USE;
		}
	}

	LONG rc = SetDisplayConfig(
		numPathArrayElements,
		pPathInfoArray,
		numModeInfoArrayElements,
		pModeInfoArray,
		SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES);

	ORIGINATE_WIN32_ERR(Out, rc, "SetDisplayConfig");

	hr = S_OK;

	Out:

	return hr;
}

HRESULT EnableTarget(
	const TargetInfo& target,
	const optional<TargetInfo> toClone,
	const optional<pair<UINT32, UINT32>> resolution,
	const optional<POINTL> vDesktopPosition,
	const optional<DISPLAYCONFIG_TARGET_MODE> targetMode,
	const optional<DISPLAYCONFIG_SCANLINE_ORDERING> scanLineOrdering,
	optional<UINT32> refreshRate)
{
	HRESULT hr;

	DISPLAYCONFIG_PATH_INFO* pPathInfoArray = NULL;
	DISPLAYCONFIG_MODE_INFO* pModeInfoArray = NULL;
	UINT32 numPathArrayElements;
	UINT32 numModeInfoArrayElements;

	for (UINT32 tryBufferSize = 32;; tryBufferSize <<= 1)
	{
		pPathInfoArray = new DISPLAYCONFIG_PATH_INFO[tryBufferSize];
		pModeInfoArray = new DISPLAYCONFIG_MODE_INFO[tryBufferSize];
		numPathArrayElements = numModeInfoArrayElements = tryBufferSize;

		ULONG rc = QueryDisplayConfig(
			QDC_ALL_PATHS,
			&numPathArrayElements,
			pPathInfoArray,
			&numModeInfoArrayElements,
			pModeInfoArray,
			NULL);

		if (rc == ERROR_SUCCESS && 
			numModeInfoArrayElements < tryBufferSize - 1) // must have 2 extra slots for new modes)
			break;

		if (rc != ERROR_INSUFFICIENT_BUFFER && rc != ERROR_SUCCESS || tryBufferSize > 1024)
			ORIGINATE_WIN32_ERR(Out, ERROR_INSUFFICIENT_BUFFER, "QueryDisplayConfig");
	}

	UINT32 newSourceModeIndex = 0;
	UINT32 newTargetModeIndex = 0;
	if (targetMode) newTargetModeIndex = numModeInfoArrayElements++;
	DISPLAYCONFIG_PATH_INFO* pPathFound = NULL;

	for (UINT32 i = 0, j = 0; i < numPathArrayElements; ++i)
	{
		if (target.mId == pPathInfoArray[i].targetInfo)
		{
			if ((pPathInfoArray[i].flags & DISPLAYCONFIG_PATH_ACTIVE) ||
				!pPathFound && ((toClone && toClone->mId == pPathInfoArray[i].sourceInfo) ||
				!(pPathInfoArray[i].sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE)))
			{
				pPathFound = pPathInfoArray + i;
			}

			pPathInfoArray[i].targetInfo.statusFlags |= DISPLAYCONFIG_TARGET_IN_USE;

			if (targetMode)
				pPathInfoArray[i].targetInfo.modeInfoIdx = newTargetModeIndex;
				
			if (refreshRate)
			{
				pPathInfoArray[i].targetInfo.refreshRate.Numerator = *refreshRate * 1000;
				pPathInfoArray[i].targetInfo.refreshRate.Denominator = 1000;
			}
			if (scanLineOrdering) 
				pPathInfoArray[i].targetInfo.scanLineOrdering = *scanLineOrdering;
		}
	}

	if (!pPathFound)
		ORIGINATE_HR_ERR(Out, E_NOTFOUND, "0");
	
	if (sourceMode)
	{
		newSourceModeIndex = (pPathFound->flags & DISPLAYCONFIG_PATH_ACTIVE) ?
			pPathFound->sourceInfo.modeInfoIdx :
			numModeInfoArrayElements++;

		pPathFound->sourceInfo.modeInfoIdx = newSourceModeIndex;
		DISPLAYCONFIG_MODE_INFO* pModeInfo = &pModeInfoArray[newSourceModeIndex];
		pModeInfo->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
		pModeInfo->adapterId = pPathFound->sourceInfo.adapterId;
		pModeInfo->id = pPathFound->sourceInfo.id;
		memcpy(&pModeInfo->sourceMode, &*sourceMode, sizeof(*sourceMode));
	}

	if (targetMode)
	{
		DISPLAYCONFIG_MODE_INFO& modeInfo = pModeInfoArray[newTargetModeIndex];
		modeInfo.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
		modeInfo.adapterId = target.mId.mAdapterId;
		modeInfo.id = target.mId.mId;
		memcpy(&modeInfo.targetMode, &*targetMode, sizeof(*targetMode));
	}

	pPathFound->sourceInfo.statusFlags |= DISPLAYCONFIG_SOURCE_IN_USE;
	pPathFound->flags |= DISPLAYCONFIG_PATH_ACTIVE;

	LONG rc = SetDisplayConfig(
		numPathArrayElements,
		pPathInfoArray,
		numModeInfoArrayElements,
		pModeInfoArray,
		SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES);

	ORIGINATE_WIN32_ERR(Out, rc, "SetDisplayConfig");

	hr = S_OK;

Out:

	return hr;
}

HRESULT ChangePrimaryMonitor(wstring gdiDeviceName)
{
	HRESULT hr;
	wstring lastPrimaryDisplay = L"";
	bool shouldRefresh = false;

	DEVMODE newPrimaryDeviceMode;
	newPrimaryDeviceMode.dmSize = sizeof(newPrimaryDeviceMode);
	if (!EnumDisplaySettings(gdiDeviceName.c_str(), ENUM_CURRENT_SETTINGS, &newPrimaryDeviceMode))
		ORIGINATE_HR_ERR(Out, E_FAIL, "EnumDisplaySettings");

	for (int i = 0;; ++i)
	{
		ULONG flags = CDS_UPDATEREGISTRY | CDS_NORESET;
		DISPLAY_DEVICE device;
		device.cb = sizeof(device);
		if (!EnumDisplayDevices(NULL, i, &device, EDD_GET_DEVICE_INTERFACE_NAME))
			break;

		if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
			continue;

		if (!wcscmp(device.DeviceName, gdiDeviceName.c_str()))
			flags |= CDS_SET_PRIMARY;

		DEVMODE deviceMode;
		newPrimaryDeviceMode.dmSize = sizeof(deviceMode);
		if (!EnumDisplaySettings(device.DeviceName, ENUM_CURRENT_SETTINGS, &deviceMode))
			ORIGINATE_HR_ERR(Out, E_FAIL, "EnumDisplaySettings");

		deviceMode.dmPosition.x -= newPrimaryDeviceMode.dmPosition.x;
		deviceMode.dmPosition.y -= newPrimaryDeviceMode.dmPosition.y;
		deviceMode.dmFields |= DM_POSITION;

		LONG rc = ChangeDisplaySettingsEx(device.DeviceName, &deviceMode, NULL,
			flags, NULL);

		if (rc != DISP_CHANGE_SUCCESSFUL) {
			ORIGINATE_WIN32_ERR(Out, rc, "ChangeDisplaySettingsEx");
		}

		shouldRefresh = true;
	}

	hr = S_OK;

	Out:

	if (shouldRefresh)
		ChangeDisplaySettingsEx(NULL, NULL, NULL, CDS_RESET, NULL);

	return hr;
}
