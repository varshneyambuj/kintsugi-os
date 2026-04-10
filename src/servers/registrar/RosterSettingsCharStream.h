/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
   Copyright (c) 2001-2002, Haiku
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
   File Name:		RecentApps.cpp
   Author:			Tyler Dauwalder (tyler@dauwalder.net)
   Description:	Recently launched apps list
 */

/** @file RosterSettingsCharStream.h
 *  @brief Character stream parser for reading roster settings files with quoted strings and escapes. */
#ifndef _ROSTER_SETTINGS_CHAR_STREAM_H
#define _ROSTER_SETTINGS_CHAR_STREAM_H

#include <sniffer/CharStream.h>
#include <SupportDefs.h>

#include <string>

/** @brief Parses roster settings files line by line, handling escapes, quotes, and comments. */
class RosterSettingsCharStream : public BPrivate::Storage::Sniffer::CharStream {
public:
	RosterSettingsCharStream(const std::string &string);
	RosterSettingsCharStream();
	virtual ~RosterSettingsCharStream();
	
	/** @brief Reads and returns the next string token from the current line. */
	status_t GetString(char *result);
	/** @brief Advances the stream position past the current line. */
	status_t SkipLine();
	
	static const status_t kEndOfLine				= B_ERRORS_END+1;
	static const status_t kEndOfStream				= B_ERRORS_END+2;
	static const status_t kInvalidEscape			= B_ERRORS_END+3;
	static const status_t kUnterminatedQuotedString	= B_ERRORS_END+4;
	static const status_t kComment					= B_ERRORS_END+5;
	static const status_t kUnexpectedState			= B_ERRORS_END+6;
	static const status_t kStringTooLong			= B_ERRORS_END+7;
private:
	RosterSettingsCharStream(const RosterSettingsCharStream &ref);
	RosterSettingsCharStream& operator=(const RosterSettingsCharStream &ref);
};

#endif // _ROSTER_SETTINGS_CHAR_STREAM_H
