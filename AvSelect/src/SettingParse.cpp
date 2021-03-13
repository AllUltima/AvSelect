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

using namespace std;

wstring Widen(string s)
{
	return wstring(s.begin(), s.end());
}

UserConfig::Field GetRequiredField(const UserConfig::State state, std::string name)
{
	const UserConfig::Field* pState = state.GetField(name);
	if (!pState)
		throw runtime_error("<" + name + "> required.");

	return *pState;
}

void CheckRequiredValues(UserConfig::Field field, vector<std::pair<std::string, bool>> recognizedValues)
{
	auto valueList = field.GetValueList();
	for (auto pair : recognizedValues)
	{
		auto item = std::find(valueList.begin(), valueList.end(), pair.first);

		if (item == valueList.end() && pair.second)
			throw runtime_error("Error parsing <" + field.GetName() + ">. Required attribute " + pair.first + " not found.");

		if (item != valueList.end())
			valueList.erase(item);
	}

	if (!valueList.empty())
		throw runtime_error("Error parsing <" + field.GetName() + ">. Unrecognized attribute " + valueList[0] + ".");
}

bool ReadValue(const UserConfig::Field* pField, string valueName, string& parsedValue, bool required)
{
	if (!pField) return false;
	string value = pField->GetValue(valueName);
	if (value == "") return false;
	parsedValue = value;
	return true;
}

bool ReadValue(const UserConfig::Field* pField, string valueName, wstring& parsedValue, bool required)
{
	if (!pField) return false;
	string value = pField->GetValue(valueName);
	if (value == "") return false;
	parsedValue = Widen(value);
	return true;
}

bool ReadValue(const UserConfig::Field* pField, string valueName, int& parsedValue, bool required)
{
	if (!pField) return false;
	string value = pField->GetValue(valueName);
	if (value == "") return false;

	try
	{
		parsedValue = std::stol(value);
		return true;
	}
	catch (...)
	{
		throw "'" + valueName + "' must be a valid int. Cannot parse '" + value + "'.";
	}
}

bool ReadValue(const UserConfig::Field* pField, string valueName, unsigned long& parsedValue, bool required)
{
	if (!pField) return false;
	string value = pField->GetValue(valueName);
	if (value == "") return false;

	try
	{
		parsedValue = std::stoul(value);
		return true;
	}
	catch (...)
	{
		throw "'" + valueName + "' must be a valid unsigned long. Cannot parse '" + value + "'.";
	}
}

bool ReadValue(const UserConfig::Field* pField, string valueName, unsigned long long& parsedValue, bool required)
{
	if (!pField) return false;
	string value = pField->GetValue(valueName);
	if (value == "") return false;

	try
	{
		parsedValue = std::stoull(value);
		return true;
	}
	catch (...)
	{
		throw "'" + valueName + "' must be a valid unsigned long long. Cannot parse '" + value + "'.";
	}
}

bool ReadValue(const UserConfig::Field* pField, std::string valueName, bool& parsedValue, bool required)
{
	if (!pField) return false;
	string value = pField->GetValue(valueName);
	if (value == "") return false;

	if (!_stricmp(value.c_str(), "true"))
		parsedValue = true;
	else if (!_stricmp(value.c_str(), "false"))
		parsedValue = false;
	else throw runtime_error("'" + valueName + "' must be either \"true\" or \"false\". Cannot parse '" 
		+ value + "'.");

	return true;
}

bool ReadValue(const UserConfig::Field* pField, std::string valueName, DISPLAYCONFIG_RATIONAL& parsedValue, UINT32 defaultDenominator,
	bool required)
{
	if (!pField) return false;
	string value = pField->GetValue(valueName);
	if (value == "") return false;

	try
	{
		std::size_t splitIndex = value.find("/");

		if (splitIndex == string::npos)
		{
			parsedValue.Denominator = defaultDenominator;
			parsedValue.Numerator = std::stoul(value) * defaultDenominator;
		}
		else
		{
			parsedValue.Numerator = std::stoul(value.substr(0, splitIndex));
			parsedValue.Denominator = std::stoul(value.substr(splitIndex + 1));
		}

		return true;
	}
	catch (...)
	{
		throw "'" + valueName + "' must be a valid unsigned long or rational (ulong/ulong). Cannot parse '" + value + "'.";
	}

	return true;
}