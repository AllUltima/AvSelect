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

enum
{
	INSIDE_SINGLE_QUOTES = (1 << 0),
	INSIDE_DOUBLE_QUOTES = (1 << 0),
};

size_t FindOutsideQuotes(wstring string, wstring substring)
{
	ULONG blocked = 0;
	PCWSTR pSource = string.c_str();
	PCWSTR pSubString = substring.c_str();
	size_t substringLength = substring.length();
	int scanLength = (int)string.length() - (int)substringLength;

	for (int i = 0; i < scanLength; ++i)
	{
		if (pSource[i] == '\'')
			blocked ^= INSIDE_SINGLE_QUOTES;
		else if (pSource[i] == '\"')
			blocked ^= INSIDE_DOUBLE_QUOTES;
		else if (!blocked && !wcsncmp(pSource + i, pSubString, substringLength))
			return i;
	}

	return string::npos;
}

//////////////////////////////////////////////////////////////////////////
//    https://www.codeproject.com/Articles/188256/A-Simple-Wildcard-Matching-Function
//    Under License: https://www.codeproject.com/info/cpol10.aspx
//      (Modified to this codebase)
//    WildcardMatch
//        pszString    - Input string to match
//        pszMatch    - Match mask that may contain wildcards like ? and *
//    
//        A ? sign matches any character, except an empty string.
//        A * sign matches any string inclusive an empty string.
//        Characters are compared caseless.
bool WildcardMatch(const WCHAR *pszString, const WCHAR *pszMatch)
{
	// We have a special case where string is empty ("") and the mask is "*".
	// We need to handle this too. So we can't test on !*pszString here.
	// The loop breaks when the match string is exhausted.
	while (*pszMatch)
	{
		// Single wildcard character
		if (*pszMatch == L'?')
		{
			// Matches any character except empty string
			if (!*pszString)
				return false;

			// OK next
			++pszString;
			++pszMatch;
		}
		else if (*pszMatch == L'*')
		{
			// Need to do some tricks.

			// 1. The wildcard * is ignored. 
			//    So just an empty string matches. This is done by recursion.
			//      Because we eat one character from the match string, the
			//      recursion will stop.
			if (WildcardMatch(pszString, pszMatch + 1))
				// we have a match and the * replaces no other character
				return true;

			// 2. Chance we eat the next character and try it again, with a
			//    wildcard * match. This is done by recursion. Because we eat
			//      one character from the string, the recursion will stop.
			if (*pszString && WildcardMatch(pszString + 1, pszMatch))
				return true;

			// Nothing worked with this wildcard.
			return false;
		}
		else
		{
			// Standard compare of 2 chars. Note that *pszSring might be 0
			// here, but then we never get a match on *pszMask that has always
			// a value while inside this loop.
			if (::CharUpper(MAKEINTRESOURCE(MAKELONG(*pszString++, 0)))
				!= ::CharUpper(MAKEINTRESOURCE(MAKELONG(*pszMatch++, 0))))
				return false;
		}
	}

	// Have a match? Only if both are at the end...
	return !*pszString && !*pszMatch;
}
