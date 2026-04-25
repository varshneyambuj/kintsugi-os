/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2013-2014, Rene
 * Gollent.
 */

/** @file BreakpointSetting.h
    @brief Persisted description of a single user breakpoint. */

#ifndef BREAKPOINT_SETTING_H
#define BREAKPOINT_SETTING_H


#include <String.h>

#include <ObjectList.h>

#include "SourceLocation.h"
#include "Types.h"


class BMessage;
class FunctionID;
class UserBreakpointLocation;


/**
 * @brief Round-trippable description of a user breakpoint.
 *
 * Captures the symbolic identity (function id, source file/line, image-
 * relative address) and the runtime flags (enabled, hidden, condition) so a
 * breakpoint can be recreated across debugger sessions.
 */
class BreakpointSetting {
public:
								BreakpointSetting();
								BreakpointSetting(
									const BreakpointSetting& other);
								~BreakpointSetting();

			status_t			SetTo(const UserBreakpointLocation& location,
									bool enabled, bool hidden,
									const BString& conditionExpression);
			status_t			SetTo(const BMessage& archive);
			status_t			WriteTo(BMessage& archive) const;

			/** @brief Returns the FunctionID identifying the breakpoint
			 *         function, or @c NULL when unset. */
			FunctionID*			GetFunctionID() const	{ return fFunctionID; }
			/** @brief Returns the recorded source file path. */
			const BString&		SourceFile() const		{ return fSourceFile; }
			/** @brief Returns the recorded line/column source location. */
			SourceLocation		GetSourceLocation() const
									{ return fSourceLocation; }
			/** @brief Returns the image-relative address, or 0 if unset. */
			target_addr_t		RelativeAddress() const
									{ return fRelativeAddress; }

			/** @brief Returns @c true when the breakpoint is enabled. */
			bool				IsEnabled() const	{ return fEnabled; }
			/** @brief Returns @c true when the breakpoint is hidden from UI. */
			bool				IsHidden() const	{ return fHidden; }

			/** @brief Returns the optional condition expression text. */
			const BString&		Condition() const
									{ return fConditionExpression; }

			BreakpointSetting&	operator=(const BreakpointSetting& other);

private:
			void				_Unset();

private:
			FunctionID*			fFunctionID;
			BString				fSourceFile;
			SourceLocation		fSourceLocation;
			target_addr_t		fRelativeAddress;
			bool				fEnabled;
			bool				fHidden;
			BString				fConditionExpression;
};


#endif	// BREAKPOINT_SETTING_H
