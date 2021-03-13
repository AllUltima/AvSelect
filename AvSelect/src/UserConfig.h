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

#include <vector>
#include <map>
#include "rapidxml\rapidxml.hpp"

#define EXT_DEFINE_EXCEPTION_BEGIN(Name, BaseException) \
	class Name : public BaseException {                 \
	public:                                             \
		Name (std::string message)                      \
		  : BaseException(message)                      \
		{}                                              \
		Name (const char* pMessage)                     \
		  : BaseException(pMessage)                     \
		{}                                              \

#define EXT_DEFINE_EXCEPTION_END };

#define EXT_DEFINE_EXCEPTION(Name, BaseException) \
	EXT_DEFINE_EXCEPTION_BEGIN(Name, BaseException) \
	EXT_DEFINE_EXCEPTION_END

class UserConfig
{
public:
	class MenuItem;
	class State;

	EXT_DEFINE_EXCEPTION(ParseException, std::runtime_error);

	struct Hotkey
	{
	public:
		unsigned int mModifierFlags;
		unsigned int mVk;
		void Parse(rapidxml::xml_node<>* pHotkeyNode);
	};

	class Field
	{
		friend class UserConfig::State;
	private:
		std::string mName;
		std::map<std::string, std::string> mValues;
		void Parse(rapidxml::xml_node<>* pStateNode);
	public:
		const std::string GetName() const { return mName; }
		std::vector<std::string> GetValueList() const;
		const std::string GetValue(std::string name) const
		{
			auto it = mValues.find(name);
			return (it == mValues.end()) ? "" : it->second;
		}
		std::string ToString() const;
	};

	class State
	{
		friend class UserConfig::MenuItem;
	private:
		bool mOptional = false;
		bool mContinueOnError = false;
		std::string mType;
		std::map<std::string, Field> mFields;
		void Parse(rapidxml::xml_node<>* pStateNode);
	public:
		bool IsOptional() const { return mOptional; }
		bool ContinueOnError() const { return mContinueOnError; }
		const std::string GetType() const { return mType; }
		const Field* GetField(std::string name) const
		{ 
			auto it = mFields.find(name);
			return (it == mFields.end()) ? NULL : &it->second;
		}
	};

	class MenuItem
	{
		friend class UserConfig;
	private:
		std::string mName;
		std::vector<State> mTargetStates;
		Hotkey mHotkey;
		void Parse(rapidxml::xml_node<>* pMenuItemNode);
	public:
		const Hotkey& GetHotkey() const { return mHotkey; }
		const std::string GetName() const { return mName; }
		const std::vector<State>& GetTargetStates() const { return mTargetStates; }
	};
private:
	std::vector<MenuItem> mMenuItems;
	MenuItem* mpDoubleClickAction;
	MenuItem* mpOnTrayExitAction;
	bool mRestoreOnExit;

	static bool ParseBooleanAttribute(const char* pHelpContext, rapidxml::xml_attribute<>* pAttribute);
	static void ExpectAttribute(const char* pHelpContext, rapidxml::xml_attribute<>* pAttribute, const char* pExpectedName);
	static void ExpectNoSiblings(const char* pHelpContext, rapidxml::xml_attribute<>* pAttribute);
	static void ExpectNode(const char* pHelpContext, rapidxml::xml_node<>* pNode, const char* pExpectedName);
	static void ExpectNoSiblings(const char* pHelpContext, rapidxml::xml_node<>* pNode);
	static void ExpectNoAttributes(const char* pHelpContext, rapidxml::xml_node<>* pNode);
	static void ExpectNoValue(const char* pHelpContext, rapidxml::xml_node<>* pNode);

public:
	const std::vector<MenuItem>& GetMenuItems() const { return mMenuItems; }
	const MenuItem* GetMenuItem(std::string name) const;
	const MenuItem* GetDoubleClickAction() const { return mpDoubleClickAction; }
	const MenuItem* GetOnTrayExitAction() const { return mpOnTrayExitAction; }
	const bool GetShouldRestoreOnExit() const { return mRestoreOnExit; }
	void ParseFile(std::string fileName);
};