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

#pragma once

#include <Windows.h>
#include <optional>

class DisplayConfig
{
public:
	typedef std::pair<DISPLAYCONFIG_RATIONAL, DISPLAYCONFIG_SCANLINE_ORDERING> RefreshInfo;

	struct DeviceId
	{
		DeviceId() {}
		DeviceId(const LUID& luid, UINT32 id) : mAdapterId(luid), mId(id) {}
		DeviceId(const DISPLAYCONFIG_PATH_SOURCE_INFO& info) : mAdapterId(info.adapterId), mId(info.id) {}
		DeviceId(const DISPLAYCONFIG_PATH_TARGET_INFO& info) : mAdapterId(info.adapterId), mId(info.id) {}

		LUID mAdapterId = { 0 };
		UINT32 mId = MAXDWORD32;

		bool IsValid() const { return mId != MAXDWORD32 || mAdapterId.LowPart != 0 || mAdapterId.HighPart != 0; }

		bool operator==(const DeviceId& other) const { return !memcmp(this, &other, sizeof(other)); }
		bool operator!=(const DeviceId& other) const { return !(*this == other); }
		bool operator<(const DeviceId& other) const { return 0 > memcmp(this, &other, sizeof(other)); }
		std::wstring ToWString();
	};

	struct DisplaySettings
	{
		std::optional<bool> mEnabled;
		std::optional<DeviceId> mCloneOf;
		std::optional<std::pair<UINT32, UINT32>> mResolution;
		std::optional<DISPLAYCONFIG_PIXELFORMAT> mPixelFormat;
		std::optional<DeviceId> mPositionAnchor;
		std::optional<POINTL> mPosition;
		std::optional<RefreshInfo> mRefreshInfo;
		std::optional<DISPLAYCONFIG_TARGET_MODE> mTargetMode;
	};

	struct TargetAuxInfo
	{
		DeviceId mId = {};
		DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY mOutputTech = {};;
		std::wstring mFriendlyName;
		std::wstring mAttachedSourceGdiDeviceName;
	};

	DisplayConfig();
	DisplayConfig(const DisplayConfig& config);

	void RefreshFromSystemDisplayConfig();

	BOOLEAN AreDisplaySettingsCurrent(const DeviceId& target, const DisplaySettings& settings) const;
	void UpdateDisplaySettings(const DeviceId& target, const DisplaySettings& settings);
	DeviceId GetPrimaryTarget() const;
	void SetPrimaryTarget(const DeviceId& target);

	enum LogStateFlags
	{
		NONE = 0,
		ALL_PATHS = (1 << 1),
		ALL_TARGETS = (1 << 2)
	};

	void LogState(std::wfstream& stream, LogStateFlags allPaths = NONE);
	
	LONG Apply(bool force = true);

private:
	BOOLEAN mDirty;
	BOOLEAN mChangesWillEnableDisplay;
	std::unique_ptr<DISPLAYCONFIG_PATH_INFO[]> mPathInfoArray;
	std::unique_ptr<DISPLAYCONFIG_MODE_INFO[]> mModeInfoArray;
	UINT32 mNumPathArrayElements;
	UINT32 mNumModeArrayElements;
	std::vector<TargetAuxInfo> mTargetInfo;

	void DisableDisplay(const DeviceId& target);
	const DISPLAYCONFIG_PATH_INFO* FindActivePath(const DeviceId& target) const;
	bool IsCloned(const DISPLAYCONFIG_PATH_INFO* pInfo) const;
	static bool TargetAuxInfoCmp(const TargetAuxInfo&, const TargetAuxInfo&);

	DisplayConfig::TargetAuxInfo* DisplayConfig::GetAuxInfo(const DeviceId& id);
	
public:
	inline bool HasChanged() const { return mDirty != 0; }
	inline bool ChangesWillEnableDisplay() const { return mChangesWillEnableDisplay != 0; }
	inline const std::vector<TargetAuxInfo>& GetAuxInfo() const { return mTargetInfo; }
	const TargetAuxInfo* GetAuxInfo(const DeviceId&) const;
	RefreshInfo GetRefreshInfo(const DeviceId&) const;
};