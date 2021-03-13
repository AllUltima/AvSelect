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
#include "rapidxml\rapidxml.hpp"
#include "rapidxml\rapidxml_utils.hpp"
#include "UserConfig.h"

using namespace rapidxml;
using namespace std;

bool UserConfig::ParseBooleanAttribute(const char* pHelpContext, rapidxml::xml_attribute<>* pAttribute)
{
	if (!_strnicmp(pAttribute->value(), "TRUE", 5))
		return true;
	else if (!_strnicmp(pAttribute->value(), "FALSE", 5))
		return false;
	else
		throw ParseException(string() +
			pHelpContext + ": attribute '" + pAttribute->name() + "', requires value of: 'True' or 'False'");
}

void UserConfig::ExpectAttribute(const char* pHelpContext, rapidxml::xml_attribute<>* pAttribute, const char* pExpectedName)
{
	if (!pAttribute)
		throw ParseException(string() +
			pHelpContext + ": Expected attribute '" + pExpectedName + "'");

	if (strcmp(pAttribute->name(), pExpectedName))
		throw ParseException(string() +
			pHelpContext + ": unExpected attribute '" + pAttribute->next_attribute()->name() + 
				"', Expected: '" + pExpectedName + "'.");
}

void UserConfig::ExpectNoSiblings(const char* pHelpContext, rapidxml::xml_attribute<>* pAttribute)
{
	if (pAttribute->next_attribute())
		throw ParseException(string() + pHelpContext + ": no additional attributes Expected after '" + pAttribute->name() +
			"', therefore attribute '" + pAttribute->next_attribute()->name() + "' is unExpected.");
}

void UserConfig::ExpectNode(const char* pHelpContext, rapidxml::xml_node<>* pNode, const char* pExpectedName)
{
	if (!pNode)
		throw ParseException(string() +
			pHelpContext + ": Expected element '" + pExpectedName + "'");

	if (strcmp(pNode->name(), pExpectedName))
		throw ParseException(string() + pHelpContext + ": unExpected element <" + pNode->name() + 
			">, Expected <" + pExpectedName + ">.");
}

void UserConfig::ExpectNoSiblings(const char* pHelpContext, rapidxml::xml_node<>* pNode)
{
	if (pNode->next_sibling())
		throw ParseException(string() + pHelpContext + ": no additional elements Expected after <" +
			pNode->name() + ">, therefore <" + pNode->next_sibling()->name() + "> is unExpected.");
}

void UserConfig::ExpectNoAttributes(const char* pHelpContext, rapidxml::xml_node<>* pNode)
{
	if (pNode->first_attribute())
		throw ParseException(string() + pHelpContext + "<" + pNode->name() + ">" +
			": no attributes Expected, therefore, attribute '" + pNode->first_attribute()->name() + "' is unExpected.");
}

void UserConfig::ExpectNoValue(const char* pHelpContext, rapidxml::xml_node<>* pNode)
{
	if (pNode->value_size() > 0)
		throw ParseException(string() + pHelpContext + "<" + pNode->name() + ">" + 
			": value should not be supplied '" + pNode->value() + "'.");
}

void UserConfig::ParseFile(std::string fileName)
{
	mpDoubleClickAction = NULL;

	file<> configFile(fileName.c_str());
	xml_document<> config;
	config.parse<0>(configFile.data());

	xml_node<>* pRoot = config.first_node("AvSelectorConfig");

	const char* pContext = "";
	char* pDoubleClickAction = NULL;
	char* pSetOnExit = NULL;
	
	for (xml_attribute<>* pAtt = pRoot->first_attribute();
		pAtt;
		pAtt = pAtt->next_attribute())
	{
		if (!strcmp(pAtt->name(), "DoubleClickTray"))
			pDoubleClickAction = pAtt->value();
		else if (!strcmp(pAtt->name(), "SetOnExit"))
			pSetOnExit = pAtt->value();
		else if (!strcmp(pAtt->name(), "RestoreOnExit"))
			mRestoreOnExit = ParseBooleanAttribute(pContext, pAtt);
		else
			throw runtime_error(string() + "Attribute '" + pAtt->name() + "' not recognized.");
	}

	ExpectNoValue(pContext, pRoot);

	xml_node<>* pMenuItems = pRoot->first_node();

	pContext = "<AvSelectorConfig>";
	ExpectNode(pContext, pMenuItems, "MenuItems");
	ExpectNoSiblings(pContext, pMenuItems);
	ExpectNoAttributes(pContext, pMenuItems);
	ExpectNoValue(pContext, pMenuItems);
	
	for (xml_node<>* pMenuItemNode = pMenuItems->first_node();
		pMenuItemNode;
		pMenuItemNode = pMenuItemNode->next_sibling())
	{
		mMenuItems.push_back(MenuItem());
		mMenuItems.back().Parse(pMenuItemNode);

		if (pDoubleClickAction && mMenuItems.back().GetName() == pDoubleClickAction)
		{
			mpDoubleClickAction = &mMenuItems.back();
			pDoubleClickAction = NULL;
		}

		if (pSetOnExit && mMenuItems.back().GetName() == pSetOnExit)
		{
			mpOnTrayExitAction = &mMenuItems.back();
			pSetOnExit = NULL;
		}
	}

	if (pDoubleClickAction)
		throw runtime_error("DoubleClickTray=\"" + string(pDoubleClickAction) + "\" not found.");

	if (pSetOnExit)
		throw runtime_error("SetOnExit=\"" + string(pSetOnExit) + "\" not found.");
}

void UserConfig::MenuItem::Parse(rapidxml::xml_node<>* pMenuItemNode)
{
	const char* pContext = "<AvSelectorConfig><MenuItems>";
	ExpectNode(pContext, pMenuItemNode, "MenuItem");
	ExpectNoValue(pContext, pMenuItemNode);

	pContext = "<AvSelectorConfig><MenuItems><MenuItem>";
	xml_attribute<>* pName = pMenuItemNode->first_attribute("Name");
	mName = pName->value();

	xml_node<>* pHotkeyNode = pMenuItemNode->first_node();
	xml_node<>* pTargetStatesNode;

	ZeroMemory(&mHotkey, sizeof (mHotkey));
	if (!strcmp(pHotkeyNode->name(), "Hotkey"))
	{
		mHotkey.Parse(pHotkeyNode);
		pTargetStatesNode = pHotkeyNode->next_sibling();
	}
	else
	{
		pTargetStatesNode = pHotkeyNode; // it was a target states node after all
	}

	ExpectNode(pContext, pTargetStatesNode, "TargetStates");
	ExpectNoSiblings(pContext, pTargetStatesNode);
	ExpectNoAttributes(pContext, pTargetStatesNode);
	ExpectNoValue(pContext, pTargetStatesNode);

	for (xml_node<>* pState = pTargetStatesNode->first_node();
		pState;
		pState = pState->next_sibling())
	{
		mTargetStates.push_back(State());
		mTargetStates.back().Parse(pState);
	}
}

void UserConfig::State::Parse(rapidxml::xml_node<>* pStateNode)
{
	const char* pContext = "<AvSelectorConfig><MenuItems><MenuItem><TargetStates>";
	ExpectNode(pContext, pStateNode, "State");
	ExpectNoValue(pContext, pStateNode);

	pContext = "<AvSelectorConfig><MenuItems><MenuItem><TargetStates><State>";

	for (xml_attribute<>* pAtt = pStateNode->first_attribute();
		pAtt;
		pAtt = pAtt->next_attribute())
	{
		if (!strcmp(pAtt->name(), "Type"))
			mType = pAtt->value();
		else if (!strcmp(pAtt->name(), "Optional"))
			mOptional = _stricmp(pAtt->value(), "false");
		else if (!strcmp(pAtt->name(), "ContinueOnError"))
			mContinueOnError = _stricmp(pAtt->value(), "false");
		else
			throw ParseException(string() +
				pContext + ": unExpected attribute '" + pAtt->next_attribute()->name() +
				"', Expected: 'Type','Optional','ContinueOnError'.");
	}

	if (mType.empty())
	{
		UserConfig::ExpectAttribute(pContext, nullptr, "Type");
	}

	for (xml_node<>* pStateField = pStateNode->first_node();
		pStateField;
		pStateField = pStateField->next_sibling())
	{
		Field& field = mFields[pStateField->name()];

		for (xml_attribute<>* pValue = pStateField->first_attribute();
			pValue;
			pValue = pValue->next_attribute())
		{
			field.mValues[pValue->name()] = pValue->value();
		}

		field.mName = pStateField->name();
	}
}

void UserConfig::Hotkey::Parse(rapidxml::xml_node<>* pHotkeyNode)
{
	mModifierFlags = MOD_NOREPEAT;
	bool VkFound = false;
	const char* pContext = "<AvSelectorConfig><MenuItems><MenuItem><Hotkey>";

	for (xml_attribute<>* pAtt = pHotkeyNode->first_attribute();
		pAtt;
		pAtt = pAtt->next_attribute())
	{
		if (!strcmp(pAtt->name(), "VkHex"))
		{
			if (VkFound)
				throw ParseException(string() + pContext + 
					"Hotkey respecifies the key. Use VkHex or Char, not both.");

			VkFound = true;
			mVk = strtoul(pAtt->value(), NULL, 16);
		} 
		else if (!strcmp(pAtt->name(), "Char"))
		{
			if (VkFound)
				throw ParseException(string() + pContext + 
					"Hotkey respecifies the key. Use VkHex or Char, not both.");

			VkFound = true;
			mVk = toupper(pAtt->value()[0]);
		}
		else if (!strcmp(pAtt->name(), "ModAlt"))
		{
			if (ParseBooleanAttribute(pContext, pAtt))
				mModifierFlags |= MOD_ALT;
		}
		else if (!strcmp(pAtt->name(), "ModShift"))
		{
			if (ParseBooleanAttribute(pContext, pAtt))
				mModifierFlags |= MOD_SHIFT;
		}
		else if (!strcmp(pAtt->name(), "ModCtrl"))
		{
			if (ParseBooleanAttribute(pContext, pAtt))
				mModifierFlags |= MOD_CONTROL;
		}
		else
		{
			throw ParseException(string() + pContext + "Attribute '" + pAtt->name() + "' unrecognized.");
		}
	}

	if ((mModifierFlags & (MOD_ALT | MOD_SHIFT | MOD_CONTROL)) == 0)
		throw ParseException(string() + pContext +
			"Hotkey requires ModAlt, ModShift, or ModCtrl.");

	if (!VkFound)
		throw ParseException(string() + pContext +
			"Hotkey requires either VkHex or Char attribute, otherwise no key is specified.");
}

vector<string> UserConfig::Field::GetValueList() const
{
	vector<string> v;
	for (auto it : mValues) {
		v.push_back(it.first);
	}
	return v;
}

string UserConfig::Field::ToString() const
{
	string s = "<" + GetName();
	vector<string> v;
	for (auto value : mValues) {
		s += string(" ") + value.first + "=" + "\"" + value.second + "\"";
	}
	return s + "/>";
}

const UserConfig::MenuItem* UserConfig::GetMenuItem(std::string name) const
{
	const UserConfig::MenuItem* pMenuItem = NULL;
	for (auto& item : GetMenuItems())
	{
		if (name == item.GetName())
			pMenuItem = &item;
	}

	return pMenuItem;
}