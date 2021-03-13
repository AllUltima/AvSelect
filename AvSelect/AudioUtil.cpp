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
#include "WinUtil.h"
#include <Mmdeviceapi.h>
#include "Util.h"

HRESULT
GetDefaultAudioPlaybackDevice(
	_Outptr_result_z_ PWSTR* friendlyName
	)
{
	IMMDeviceEnumerator *pEnumerator = NULL;
	IMMDevice *pDevice = NULL;
	BOOLEAN comIntialized = FALSE;
	BOOLEAN propVariantInitialized = FALSE;
	PWSTR friendlyNameLocal = NULL;
	IPropertyStore *pStore = NULL;
	PROPVARIANT friendlyNameProp;
	
	HRESULT hr = CoInitialize(NULL);
	ORIGINATE_HR_ERR(Out, hr, "CoInitialize");
	comIntialized = TRUE;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
		NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (LPVOID *)&pEnumerator);
	ORIGINATE_HR_ERR(Out, hr, "CoCreateInstance");

	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	ORIGINATE_HR_ERR(Out, hr, "IMMDeviceEnumerator::GetDefaultAudioEndpoint");

	hr = pDevice->OpenPropertyStore(STGM_READ, &pStore);
	ORIGINATE_HR_ERR(Out, hr, "IMMDevice::OpenPropertyStore");

	PropVariantInit(&friendlyNameProp);
	propVariantInitialized = TRUE;
	hr = pStore->GetValue(PKEY_Device_FriendlyName, &friendlyNameProp);
	ORIGINATE_HR_ERR(Out, hr, "IPropertyStore::GetValue");

	size_t len = wcslen(friendlyNameProp.pwszVal);
	friendlyNameLocal = new WCHAR[len + 1];

	UINT rc = wcscpy_s(friendlyNameLocal, len + 1, friendlyNameProp.pwszVal);
	ORIGINATE_WIN32_ERR(Out, rc, "wcscpy_s");

	*friendlyName = friendlyNameLocal;
	friendlyNameLocal = NULL;
	hr = S_OK;

	Out:

	delete friendlyNameLocal;

	if (propVariantInitialized)
		PropVariantClear(&friendlyNameProp);

	if (pDevice)
		pDevice->Release();

	if (pStore)
		pStore->Release();

	if (comIntialized)
		CoUninitialize();

	return hr;
}

HRESULT
SetDefaultAudioPlaybackDeviceById(
	_In_z_ LPCWSTR devID
	)
{
	IPolicyConfigVista *pPolicyConfig;
	ERole reserved = eConsole;
	BOOLEAN comIntialized = FALSE;

	HRESULT hr = CoInitialize(NULL);
	ORIGINATE_HR_ERR(Out, hr, "CoInitialize");
	comIntialized = TRUE;

	hr = CoCreateInstance(__uuidof(CPolicyConfigVistaClient),
		NULL, CLSCTX_ALL, __uuidof(IPolicyConfigVista), (LPVOID *)&pPolicyConfig);
	ORIGINATE_HR_ERR(Out, hr, "CoCreateInstance");

	hr = pPolicyConfig->SetDefaultEndpoint(devID, reserved);
	pPolicyConfig->Release();

	Out:

	if (comIntialized)
		CoUninitialize();

	return hr;
}

HRESULT 
FindAudioPlaybackDevice(
	_In_z_ LPCWSTR nameToFind,
	_Outptr_opt_result_z_ PWSTR* deviceId,
	_Inout_ std::vector<std::wstring>* pDeviceNameList
	)
{
	HRESULT hr;
	IMMDeviceEnumerator *pEnum = NULL;
	IMMDeviceCollection *pDevices = NULL;
	BOOLEAN comIntialized = FALSE;
	BOOLEAN propVariantInitialized = FALSE;
	PWSTR deviceIdLocal = NULL;
	UINT count;

	hr = CoInitialize(NULL);
	ORIGINATE_HR_ERR(Out, hr, "CoInitialize");
	comIntialized = TRUE;

	// Create a multimedia device enumerator.
	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
		CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
	ORIGINATE_HR_ERR(Out, hr, "CoCreateInstance");

	// Enumerate the output devices.
	hr = pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pDevices);
	ORIGINATE_HR_ERR(Out, hr, "IMMDeviceEnumerator::EnumAudioEndpoints");

	pDevices->GetCount(&count);
	ORIGINATE_HR_ERR(Out, hr, "IMMDeviceConnection::GetCount");

	for (unsigned int i = 0; i < count && !deviceIdLocal; ++i)
	{
		IMMDevice *pDevice = NULL;
		IPropertyStore *pStore = NULL;
		PROPVARIANT friendlyName;
		hr = pDevices->Item(i, &pDevice);

		ORIGINATE_HR_ERR(Loop, hr, "IMMDeviceCollection::Item");

		LPWSTR wstrID = NULL;
		hr = pDevice->GetId(&wstrID);

		hr = pDevice->OpenPropertyStore(STGM_READ, &pStore);

		ORIGINATE_HR_ERR(Loop, hr, "IMMDevice::OpenPropertyStore");

		PropVariantInit(&friendlyName);
		propVariantInitialized = TRUE;
		hr = pStore->GetValue(PKEY_Device_FriendlyName, &friendlyName);
		
		ORIGINATE_HR_ERR(Loop, hr, "IPropertyStore::GetValue");

		if (pDeviceNameList)
			pDeviceNameList->push_back(friendlyName.pwszVal);

		if (WildcardMatch(friendlyName.pwszVal, nameToFind))
		{
			size_t wstrIDLen = wcslen(wstrID);
			deviceIdLocal = new WCHAR[wstrIDLen + 1];
			UINT rc = wcscpy_s(deviceIdLocal, wstrIDLen + 1, wstrID);
			ORIGINATE_WIN32_ERR(Loop, rc, "wcscpy_s");
		}
	
		Loop:

		if (propVariantInitialized)
			PropVariantClear(&friendlyName);

		if (pDevice)
			pDevice->Release();

		if (pStore)
			pStore->Release();
	}

	if (deviceIdLocal)
	{
		if (deviceId)
		{
			*deviceId = deviceIdLocal;
			deviceIdLocal = NULL;
		}
		hr = S_OK;
	}
	else
		hr = S_FALSE;

	Out:

	delete deviceIdLocal;

	if (pDevices)
		pDevices->Release();

	if (pEnum)
		pEnum->Release();

	if (comIntialized)
		CoUninitialize();

	return hr;
}