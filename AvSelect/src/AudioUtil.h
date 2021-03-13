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

#ifndef _defaultaudiodevice_h_
#define _defaultaudiodevice_h_

#include <Windows.h>
#include <vector>
#include <string>

HRESULT
SetDefaultAudioPlaybackDeviceById(
	_In_z_ LPCWSTR devID
	);

HRESULT
GetDefaultAudioPlaybackDevice(
	_Outptr_result_z_ PWSTR* friendlyName
	);

HRESULT 
FindAudioPlaybackDevice(
	_In_z_ LPCWSTR friendlyName,
	_Outptr_opt_result_z_ PWSTR* deviceId,
	_Inout_ std::vector<std::wstring>* pDeviceNameList
	);

#endif
