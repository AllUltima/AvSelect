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

#include <string>
#include "UserConfig.h"

EXT_DEFINE_EXCEPTION(MissingArgumentException, std::runtime_error);
EXT_DEFINE_EXCEPTION(InvalidArgumentException, std::runtime_error);

std::wstring Widen(std::string s);

typedef std::vector<std::pair<std::string, bool>> ParamList;

void CheckRequiredValues(const UserConfig::Field field, ParamList valueList);
UserConfig::Field GetRequiredField(const UserConfig::State state, std::string name);

bool ReadValue(const UserConfig::Field* pField, std::string valueName, std::string& parsedValue, bool required = false);
bool ReadValue(const UserConfig::Field* pField, std::string valueName, std::wstring& parsedValue, bool required = false);
bool ReadValue(const UserConfig::Field* pField, std::string valueName, int& parsedValue, bool required = false);
bool ReadValue(const UserConfig::Field* pField, std::string valueName, unsigned long& parsedValue, bool required = false);
bool ReadValue(const UserConfig::Field* pField, std::string valueName, unsigned long long& parsedValue, bool required = false);
bool ReadValue(const UserConfig::Field* pField, std::string valueName, bool& parsedValue, bool required = false);
bool ReadValue(const UserConfig::Field* pField, std::string valueName, DISPLAYCONFIG_RATIONAL& rational, UINT32 defaultDenominator,
	bool required = false);

inline bool ReadValue(const UserConfig::Field* pField, std::string valueName, UINT32& parsedValue, bool required = false)
{ return ReadValue(pField, valueName, (unsigned long&)parsedValue, required); }

inline bool ReadValue(const UserConfig::Field* pField, std::string valueName, LONG& parsedValue, bool required = false)
{ return ReadValue(pField, valueName, (int&)parsedValue, required); }
