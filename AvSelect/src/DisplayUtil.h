#pragma once

#include <windows.h>
#include <string>
#include <optional>

struct DeviceId
{
	DeviceId() {}
	DeviceId(const LUID& luid, UINT32 id) : mAdapterId(luid), mId(id) {}
	DeviceId(const DISPLAYCONFIG_PATH_SOURCE_INFO& info) : mAdapterId(info.adapterId), mId(info.id) {}
	DeviceId(const DISPLAYCONFIG_PATH_TARGET_INFO& info) : mAdapterId(info.adapterId), mId(info.id) {}

	LUID mAdapterId;
	UINT32 mId;

	bool operator==(const DeviceId& other) const { return !memcmp(this, &other, sizeof(other)); }
	bool operator<(const DeviceId& other) const { return 0 > memcmp(this, &other, sizeof(other)); }
};

struct SourceInfo
{
	DeviceId mId;
	std::wstring mGdiDeviceName; // e.g.\\.\Display1
	ULONG mResolutionX;
	ULONG mResolutionY;
	ULONG mVirtualDesktopX;
	ULONG mVirtualDesktopY;
	DISPLAYCONFIG_PIXELFORMAT mPixelFormat;

	bool operator==(const SourceInfo& other) const { return !memcmp(this, &other, sizeof(other)); }
};

struct TargetInfo
{
	ULONG mIndex;
	DeviceId mId;
	std::wstring mFriendlyName; // Name of the attached display device (monitor)
	DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY mOutputTech;
	BOOLEAN mPrimaryMonitor;
	std::optional<SourceInfo> mAttachedSource;

	bool operator==(const TargetInfo& other) const { return mId == other.mId; }
};


std::vector<TargetInfo> GenerateTargetArray();

HRESULT DisableTarget(const TargetInfo&);

HRESULT ChangePrimaryMonitor(std::wstring gdiDeviceName);

HRESULT EnableTarget(
	const TargetInfo& target,
	const std::optional<TargetInfo> toClone,
	const std::optional<std::pair<UINT32, UINT32>> resolution,
	const std::optional<POINTL> vDesktopPosition,
	const std::optional<DISPLAYCONFIG_TARGET_MODE> targetMode,
	const std::optional<DISPLAYCONFIG_SCANLINE_ORDERING> scanLineOrdering,
	std::optional<UINT32> pRefreshRate);
