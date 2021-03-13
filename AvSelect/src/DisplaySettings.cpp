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

#include <stdafx.h>
#include "DisplaySettings.h"
#include "WinUtil.h"
#include "Util.h"
#include <algorithm>
#include <numeric>
#include <iomanip>

using namespace std;

BOOLEAN RationalsEqual(const DISPLAYCONFIG_RATIONAL& a, const DISPLAYCONFIG_RATIONAL& b)
{
	UINT64 commonDenom = std::lcm((UINT64)a.Denominator, (UINT64)b.Denominator);
	UINT64 numA = commonDenom / a.Denominator * a.Numerator;
	UINT64 numB = commonDenom / b.Denominator * b.Numerator;
	return numA == numB;
}

DisplayConfig::DisplayConfig()
{
	RefreshFromSystemDisplayConfig();
}

DisplayConfig::DisplayConfig(const DisplayConfig& config)
:
mPathInfoArray(new DISPLAYCONFIG_PATH_INFO[config.mNumPathArrayElements]),
mModeInfoArray(new DISPLAYCONFIG_MODE_INFO[config.mNumModeArrayElements]),
mNumPathArrayElements(config.mNumPathArrayElements),
mNumModeArrayElements(config.mNumModeArrayElements),
mTargetInfo(config.mTargetInfo),
mDirty(TRUE)
{
	memcpy(mPathInfoArray.get(), config.mModeInfoArray.get(), 
		sizeof(DISPLAYCONFIG_PATH_INFO) * mNumPathArrayElements);
	memcpy(mModeInfoArray.get(), config.mModeInfoArray.get(),
		sizeof(DISPLAYCONFIG_MODE_INFO) * mNumModeArrayElements);
}

/* Initialize self from QueryDisplayConfig
*/
void DisplayConfig::RefreshFromSystemDisplayConfig()
{
	mDirty = TRUE;
	mChangesWillEnableDisplay = FALSE;
	for (UINT32 tryBufferSize = 32;; tryBufferSize <<= 1)
	{
		std::unique_ptr<DISPLAYCONFIG_PATH_INFO[]> pathInfoArray(new DISPLAYCONFIG_PATH_INFO[tryBufferSize]);
		std::unique_ptr<DISPLAYCONFIG_MODE_INFO[]> modeInfoArray(new DISPLAYCONFIG_MODE_INFO[tryBufferSize]);
		mNumPathArrayElements = mNumModeArrayElements = tryBufferSize;

		ULONG rc = QueryDisplayConfig(
			QDC_ALL_PATHS,
			&mNumPathArrayElements,
			pathInfoArray.get(),
			&mNumModeArrayElements,
			modeInfoArray.get(),
			NULL);

		if (rc == ERROR_SUCCESS &&
			mNumPathArrayElements < tryBufferSize - 1) // must have 2 extra slots for new modes)
		{
			mPathInfoArray = std::move(pathInfoArray);
			mModeInfoArray = std::move(modeInfoArray);
			break;
		}

		if (rc != ERROR_INSUFFICIENT_BUFFER)
			throw std::runtime_error(string("Unexpected QueryDisplayConfig return code: ") 
				+ std::to_string(rc));

		if (tryBufferSize > 8192)
			throw std::runtime_error("QueryDisplayConfig returned over 8192 elements");
	}

	map<DeviceId, DISPLAYCONFIG_PATH_INFO*> targetMap;

	for (UINT32 i = 0; i < mNumPathArrayElements; ++i)
	{
		DeviceId targetId = mPathInfoArray.get()[i].targetInfo;

		if ((mPathInfoArray.get()[i].flags & DISPLAYCONFIG_PATH_ACTIVE) ||
			targetMap.find(targetId) == targetMap.end() &&
			mPathInfoArray.get()[i].targetInfo.targetAvailable)
		{
			targetMap[targetId] = &mPathInfoArray.get()[i];
		}
	}

	mTargetInfo.clear();
	for (auto it = targetMap.begin(); it != targetMap.end(); ++it)
	{
		mTargetInfo.push_back(TargetAuxInfo());
		DISPLAYCONFIG_PATH_INFO& current = *it->second;
		TargetAuxInfo& currentDst = mTargetInfo.back();

		union {
			DISPLAYCONFIG_SOURCE_DEVICE_NAME source;
			DISPLAYCONFIG_TARGET_DEVICE_NAME target;
		} queryInfo;

		if (current.flags & DISPLAYCONFIG_PATH_ACTIVE)
		{
			ZeroMemory(&queryInfo, sizeof(queryInfo));
			queryInfo.source.header.size = sizeof(queryInfo.source);
			queryInfo.source.header.adapterId = current.sourceInfo.adapterId;
			queryInfo.source.header.id = current.sourceInfo.id;
			queryInfo.source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			ULONG rc = DisplayConfigGetDeviceInfo(&queryInfo.source.header);

			if (rc == ERROR_SUCCESS)
				currentDst.mAttachedSourceGdiDeviceName = queryInfo.source.viewGdiDeviceName;
		}

		currentDst.mOutputTech = current.targetInfo.outputTechnology;
		currentDst.mId = current.targetInfo;

		ZeroMemory(&queryInfo, sizeof(queryInfo));
		queryInfo.target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		queryInfo.target.header.size = sizeof(queryInfo.target);
		queryInfo.target.header.adapterId = current.targetInfo.adapterId;
		queryInfo.target.header.id = current.targetInfo.id;
		ULONG rc = DisplayConfigGetDeviceInfo(&queryInfo.target.header);

		if (rc == ERROR_SUCCESS)
			currentDst.mFriendlyName = queryInfo.target.monitorFriendlyDeviceName;
	}

	// std::sort(mTargetInfo.begin(), mTargetInfo.end(), TargetAuxInfoCmp);
	// for (UINT32 i = 0; i < mTargetInfo.size(); ++i)
	//	mTargetInfo[i].mUiIndex = i;

	mDirty = false;
}

BOOLEAN DisplayConfig::AreDisplaySettingsCurrent(
	const DeviceId& target, const DisplaySettings& settings) const
{
	const DISPLAYCONFIG_PATH_INFO* pPathWithSource = FindActivePath(target);

	if (!pPathWithSource)
		return !settings.mEnabled || !*settings.mEnabled;

	if (settings.mEnabled && !*settings.mEnabled)
		return FALSE;

	if (settings.mCloneOf)
	{
		const DISPLAYCONFIG_PATH_INFO* pClonePath = FindActivePath(*settings.mCloneOf);
		if (!pClonePath)
			return false;
		DeviceId sourceId = pPathWithSource->sourceInfo;
		if (!(sourceId == pClonePath->sourceInfo))
			return false;
	}
	else if (IsCloned(pPathWithSource))
	{
		return FALSE;
	}

	DISPLAYCONFIG_SOURCE_MODE& sourceMode = 
		mModeInfoArray.get()[pPathWithSource->sourceInfo.modeInfoIdx].sourceMode;

	if (settings.mResolution && (
		settings.mResolution->first != sourceMode.width || 
		settings.mResolution->second != sourceMode.height))
	{
		return FALSE;
	}

	if (settings.mPosition)
	{
		POINTL desiredPosition = *settings.mPosition;

		if (settings.mPositionAnchor)
		{
			const DISPLAYCONFIG_PATH_INFO* pAnchorPath = FindActivePath(*settings.mPositionAnchor);

			if (!pAnchorPath) 
				return false;

			DISPLAYCONFIG_MODE_INFO& mode =
				mModeInfoArray.get()[pAnchorPath->sourceInfo.modeInfoIdx];

			assert(mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);
			desiredPosition.x += mode.sourceMode.position.x;
			desiredPosition.y += mode.sourceMode.position.y;
		}
		
		if (memcmp(&desiredPosition, &sourceMode.position, sizeof(POINTL)))
			return FALSE;
	}

	if (settings.mPixelFormat && *settings.mPixelFormat != sourceMode.pixelFormat)
		return FALSE;

	const DISPLAYCONFIG_PATH_TARGET_INFO& targetInfo = pPathWithSource->targetInfo;

	if (settings.mRefreshInfo && (
		!RationalsEqual(settings.mRefreshInfo->first, targetInfo.refreshRate) ||
		settings.mRefreshInfo->second != targetInfo.scanLineOrdering))
	{
		return FALSE;
	}
	
	DISPLAYCONFIG_TARGET_MODE& targetMode =
		mModeInfoArray.get()[pPathWithSource->targetInfo.modeInfoIdx].targetMode;

	if (settings.mTargetMode && 
		memcmp(&*settings.mTargetMode, &targetMode, sizeof(targetMode)))
	{
		return FALSE;
	}

	return TRUE;
}

void DisplayConfig::DisableDisplay(const DeviceId& target)
{
	const DISPLAYCONFIG_PATH_INFO* pPath = FindActivePath(target);

	if (!pPath)
		return;

	for (UINT32 i = 0; i < mNumPathArrayElements; ++i)
	{
		if (target == mPathInfoArray.get()[i].targetInfo)
		{
			mDirty |= (mPathInfoArray.get()[i].targetInfo.statusFlags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
			mPathInfoArray.get()[i].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
			mPathInfoArray.get()[i].targetInfo.statusFlags &= ~DISPLAYCONFIG_TARGET_IN_USE;
		}

		//if (DeviceId(pPath->sourceInfo) == mPathInfoArray.get()[i].sourceInfo)
		//{
		//	mPathInfoArray.get()[i].sourceInfo.statusFlags &= ~DISPLAYCONFIG_SOURCE_IN_USE;
		//}
	}
}

#pragma warning( disable : 4244 )
void DisplayConfig::UpdateDisplaySettings(const DeviceId& target, const DisplaySettings& settings)
{
	if (AreDisplaySettingsCurrent(target, settings))
		return;

	if (settings.mEnabled && !*settings.mEnabled)
	{
		DisableDisplay(target);
		return;
	}

	UINT32 sourceModeIndex = 0;
	UINT32 newTargetModeIndex = 0;
	if (settings.mTargetMode) newTargetModeIndex = mNumModeArrayElements++;
	
	const DISPLAYCONFIG_PATH_INFO* pClonePath = NULL;
	const DISPLAYCONFIG_PATH_INFO* pAnchorPath = NULL;

	if (settings.mCloneOf)
	{
		// TODO: validate removal DisableDisplay(target);
		pClonePath = FindActivePath(*settings.mCloneOf);

		if (!pClonePath)
		{
			auto related = GetAuxInfo(*settings.mCloneOf);
			string relatedName(related->mFriendlyName.begin(), related->mFriendlyName.end());
			throw std::runtime_error(string("Cannot clone ") + relatedName + 
				" because it is disabled.");
		}
	}

	if (settings.mPositionAnchor)
	{
		pAnchorPath = FindActivePath(*settings.mPositionAnchor);

		if (!pAnchorPath)
		{
			auto related = GetAuxInfo(*settings.mCloneOf);
			string relatedName(related->mFriendlyName.begin(), related->mFriendlyName.end());
			throw std::runtime_error(string("Cannot position relative to ") + relatedName + 
				" because it is disabled.");
		}
	}
    
	DISPLAYCONFIG_PATH_INFO* pPathWithSource = NULL;

	for (UINT32 i = 0; i < mNumPathArrayElements; ++i)
	{
		if (target == mPathInfoArray.get()[i].targetInfo)
		{
			mPathInfoArray.get()[i].targetInfo.statusFlags |= DISPLAYCONFIG_TARGET_IN_USE;

			if (pClonePath)
			{
				if (DeviceId(pClonePath->sourceInfo) == mPathInfoArray.get()[i].sourceInfo)
					pPathWithSource = mPathInfoArray.get() + i;
				else
					mPathInfoArray.get()[i].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
			}
			else
			{
				if (mPathInfoArray.get()[i].flags & DISPLAYCONFIG_PATH_ACTIVE)
					pPathWithSource = mPathInfoArray.get() + i;
			}
		}
	}

    if (!pPathWithSource && !pClonePath)
    {
        // We need to find one not in use.

        for (UINT32 i = 0; i < mNumPathArrayElements; ++i)
        {
            if (target == mPathInfoArray.get()[i].targetInfo)
            {
                if (!pPathWithSource && !(mPathInfoArray.get()[i].sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE))
                    pPathWithSource = mPathInfoArray.get() + i;
            }
        }
    }

	if (!pPathWithSource)
	{
		throw std::runtime_error(string("This video card cannot render to any additional sources."));
	}

	if (settings.mRefreshInfo)
	{
		pPathWithSource->targetInfo.refreshRate = settings.mRefreshInfo->first;
		pPathWithSource->targetInfo.scanLineOrdering = settings.mRefreshInfo->second;
	}

	BOOLEAN newModeRequired =
		(pPathWithSource->sourceInfo.modeInfoIdx == DISPLAYCONFIG_PATH_MODE_IDX_INVALID);

	if (newModeRequired && !settings.mResolution)
		throw std::runtime_error(string("Resolution must be provided when enabling a monitor."));

	if (newModeRequired && !settings.mPosition && pClonePath)
		throw std::runtime_error(string("Position must be provided when enabling a monitor."));

	if (settings.mResolution || settings.mPosition || settings.mPixelFormat || newModeRequired)
	{
		sourceModeIndex = pPathWithSource->sourceInfo.modeInfoIdx;

		if (newModeRequired)
		{
			sourceModeIndex = mNumModeArrayElements++;
			ZeroMemory(mModeInfoArray.get() + sourceModeIndex, sizeof (DISPLAYCONFIG_MODE_INFO));
			mModeInfoArray.get()[sourceModeIndex].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
		}

		pPathWithSource->sourceInfo.modeInfoIdx = sourceModeIndex;
		DISPLAYCONFIG_MODE_INFO* pModeInfo = &mModeInfoArray.get()[sourceModeIndex];
		pModeInfo->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
		pModeInfo->adapterId = pPathWithSource->sourceInfo.adapterId;
		pModeInfo->id = pPathWithSource->sourceInfo.id;
		
		if (settings.mResolution)
		{
			pModeInfo->sourceMode.width = settings.mResolution->first;
			pModeInfo->sourceMode.height = settings.mResolution->second;
		}

		if (settings.mPosition)
		{
			pModeInfo->sourceMode.position = *settings.mPosition;

			if (pAnchorPath)
			{
				auto mode = mModeInfoArray.get()[pAnchorPath->sourceInfo.modeInfoIdx];
				assert(mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);
				pModeInfo->sourceMode.position.x += mode.sourceMode.position.x;
				pModeInfo->sourceMode.position.y += mode.sourceMode.position.y;
			}
		}

		if (settings.mPixelFormat)
			pModeInfo->sourceMode.pixelFormat = *settings.mPixelFormat;
	}

	if (settings.mTargetMode)
	{
		DISPLAYCONFIG_MODE_INFO& modeInfo = mModeInfoArray.get()[newTargetModeIndex];
		modeInfo.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
		modeInfo.adapterId = target.mAdapterId;
		modeInfo.id = target.mId;
		memcpy(&modeInfo.targetMode, &*settings.mTargetMode, sizeof(*settings.mTargetMode));
	}

	if (!(pPathWithSource->flags & DISPLAYCONFIG_PATH_ACTIVE))
		mChangesWillEnableDisplay = TRUE;

	pPathWithSource->sourceInfo.statusFlags |= DISPLAYCONFIG_SOURCE_IN_USE;
	pPathWithSource->flags |= DISPLAYCONFIG_PATH_ACTIVE;
	mDirty = TRUE;
}

DisplayConfig::DeviceId DisplayConfig::GetPrimaryTarget() const
{
	for (UINT32 i = 0; i < mNumPathArrayElements; ++i)
	{
		if ((mPathInfoArray.get()[i].flags & DISPLAYCONFIG_PATH_ACTIVE))
		{
			DISPLAYCONFIG_MODE_INFO& mode =
				mModeInfoArray.get()[mPathInfoArray.get()[i].sourceInfo.modeInfoIdx];

			assert(mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);
			if (mode.sourceMode.position.x == 0 && mode.sourceMode.position.y == 0)
				return mPathInfoArray.get()[i].targetInfo;
		}
	}

	assert(false); // no primary target is an invalid state
	throw runtime_error("No primary monitor?");
}

void DisplayConfig::SetPrimaryTarget(const DeviceId& target)
{
	const DISPLAYCONFIG_PATH_INFO* pPathWithSource = FindActivePath(target);

	if (!pPathWithSource)
		throw runtime_error("Target must be enabled before it can be set to primary.");

	if (pPathWithSource->sourceInfo.modeInfoIdx == DISPLAYCONFIG_PATH_MODE_IDX_INVALID)
		throw runtime_error("Target unexpectedly has no source mode.");

	// COPY this so the old x and y are held
	DISPLAYCONFIG_SOURCE_MODE newPrimarySourceMode =
		mModeInfoArray.get()[pPathWithSource->sourceInfo.modeInfoIdx].sourceMode;

	if (newPrimarySourceMode.position.x == 0 && newPrimarySourceMode.position.y == 0)
		return; // already primary!

	mDirty = TRUE;

	// shuffle all active source modes to reorient newPrimarySourceMode to (0, 0)
	for (UINT32 i = 0; i < mNumModeArrayElements; ++i)
	{
		if (mModeInfoArray.get()[i].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
		{
			mModeInfoArray.get()[i].sourceMode.position.x -= newPrimarySourceMode.position.x;
			mModeInfoArray.get()[i].sourceMode.position.y -= newPrimarySourceMode.position.y;
		}
	}

	return;
}

LONG DisplayConfig::Apply(bool force)
{
	if (!mDirty && !force)
		return 0;

	LONG rc = SetDisplayConfig(
		mNumPathArrayElements,
		mPathInfoArray.get(),
		mNumModeArrayElements,
		mModeInfoArray.get(),
		SDC_APPLY | SDC_SAVE_TO_DATABASE | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES);

	if (rc == ERROR_SUCCESS)
	{
		mDirty = FALSE;
		mChangesWillEnableDisplay = FALSE;
	}

	return rc;
}

const DISPLAYCONFIG_PATH_INFO* DisplayConfig::FindActivePath(const DeviceId& target) const
{
	DISPLAYCONFIG_PATH_INFO* pPath = NULL;
	for (UINT32 i = 0; i < mNumPathArrayElements; ++i)
	{
		if (!(mPathInfoArray.get()[i].flags & DISPLAYCONFIG_PATH_ACTIVE))
			continue;

		if (target == mPathInfoArray.get()[i].targetInfo)
			pPath = mPathInfoArray.get() + i;
	}

	return pPath;
}

bool DisplayConfig::IsCloned(const DISPLAYCONFIG_PATH_INFO* pInfo) const
{
	for (UINT32 i = 0; i < mNumPathArrayElements; ++i)
	{
		if (!(mPathInfoArray.get()[i].flags & DISPLAYCONFIG_PATH_ACTIVE) || &mPathInfoArray.get()[i] == pInfo)
			continue;

		if (pInfo->sourceInfo.id == mPathInfoArray.get()[i].sourceInfo.id)
			return true;
	}

	return false;
}

const DisplayConfig::TargetAuxInfo* DisplayConfig::GetAuxInfo(const DeviceId& id) const
{
	for (const TargetAuxInfo& info : mTargetInfo)
	{
		if (info.mId == id)
			return &info;
	}

	return NULL;
}

DisplayConfig::TargetAuxInfo* DisplayConfig::GetAuxInfo(const DeviceId& id)
{
	for (TargetAuxInfo& info : mTargetInfo)
	{
		if (info.mId == id)
			return &info;
	}

	return NULL;
}

std::wstring DisplayConfig::DeviceId::ToWString()
{
	wstringstream ss;
	UINT64 luid = mAdapterId.LowPart | ((UINT64)mAdapterId.HighPart << 32);
	ss << L"{ " << std::hex << luid << L":" << std::dec << mId << L" }";
	return ss.str();
}

void DisplayConfig::LogState(std::wfstream& stream, LogStateFlags flags)
{
	map<DeviceId, DISPLAYCONFIG_PATH_SOURCE_INFO*> sourceMap;
	map<DeviceId, DISPLAYCONFIG_PATH_TARGET_INFO*> targetMap;

	for (UINT32 i = 0; i < mNumPathArrayElements; ++i)
	{
		sourceMap[mPathInfoArray.get()[i].sourceInfo] = &mPathInfoArray.get()[i].sourceInfo;
		targetMap[mPathInfoArray.get()[i].targetInfo] = &mPathInfoArray.get()[i].targetInfo;
	}

	stream << "Display Configuration:" << std::endl;

	for (auto pair : sourceMap)
	{
		stream << " Source: " << DeviceId(*pair.second).ToWString();
		if (pair.second->modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID)
		{
			DISPLAYCONFIG_MODE_INFO& mode =
				mModeInfoArray.get()[pair.second->modeInfoIdx];
			assert(mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);
			DISPLAYCONFIG_SOURCE_MODE& sourceMode = mode.sourceMode;

			stream << " [Resolution: " << std::setw(4) << sourceMode.width << ", " 
				<< std::setw(4) << sourceMode.height << " ";
			stream << "Position: " << std::setw(4) << sourceMode.position.x << ", " 
				<< std::setw(4) <<sourceMode.position.y << " ";
			stream << "PixelFormat: " << sourceMode.pixelFormat;
			stream << "]" << " Status: " << pair.second->statusFlags;
		}

		stream << std::endl;
	}

	for (auto pair : targetMap)
	{
		if (pair.second->targetAvailable || (flags & ALL_TARGETS))
		{
			stream << " Target: " << DeviceId(*pair.second).ToWString();

			if (pair.second->modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID)
			{
				DISPLAYCONFIG_MODE_INFO& mode =
					mModeInfoArray.get()[pair.second->modeInfoIdx];
				assert(mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET);
				DISPLAYCONFIG_TARGET_MODE& targetMode = mode.targetMode;
				const TargetAuxInfo* pAuxInfo = GetAuxInfo(*pair.second);
		
				stream << " [OutputTechnology: " << std::setw(2) << pair.second->outputTechnology;
				//stream << " Index: " << pAuxInfo->mUiIndex;
				RefreshInfo info = GetRefreshInfo(pAuxInfo->mId);
				stream << " RefreshRate: " << info.first.Numerator << "/" << info.first.Denominator;
				stream << " ScanLineOrdering: " << info.second;
				stream << "]" << " TargetAvailable: " << pair.second->targetAvailable;
				stream << " Status: " << pair.second->statusFlags;
				stream << " FriendlyName: " << pAuxInfo->mFriendlyName ;
			}
			
			stream << std::endl;
		}
	}

	for (UINT32 i = 0; i < mNumPathArrayElements; ++i)
	{
		if (!(mPathInfoArray.get()[i].flags & DISPLAYCONFIG_PATH_ACTIVE) && !(flags & ALL_PATHS))
			continue;

		stream << " Active Path: " << DeviceId(mPathInfoArray.get()[i].sourceInfo).ToWString();
		stream << " -> " << DeviceId(mPathInfoArray.get()[i].targetInfo).ToWString() << std::endl;
	}

	stream.flush();
}

DisplayConfig::RefreshInfo DisplayConfig::GetRefreshInfo(const DeviceId& target) const
{
	RefreshInfo ret;
	ZeroMemory(&ret, sizeof (ret));

	const DISPLAYCONFIG_PATH_INFO* pPathWithSource = FindActivePath(target);

	if (pPathWithSource)
	{
		ret.first = pPathWithSource->targetInfo.refreshRate;
		ret.second = pPathWithSource->targetInfo.scanLineOrdering;
	}

	return ret;
}
