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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2013-2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BreakpointSetting.cpp
 * @brief Persistent description of a single user breakpoint.
 *
 * BreakpointSetting captures the symbolic identity of a user breakpoint
 * (function, source location, image-relative address) along with the
 * enabled/hidden flags and an optional condition expression. Instances are
 * round-tripped through BMessage archives by TeamSettings.
 */


#include "BreakpointSetting.h"

#include <Message.h>

#include "ArchivingUtils.h"
#include "FunctionID.h"
#include "LocatableFile.h"
#include "UserBreakpoint.h"


/**
 * @brief Construct an empty BreakpointSetting (no function, disabled).
 */
BreakpointSetting::BreakpointSetting()
	:
	fFunctionID(NULL),
	fSourceFile(),
	fSourceLocation(),
	fRelativeAddress(0),
	fEnabled(false),
	fHidden(false),
	fConditionExpression()
{
}


/**
 * @brief Copy-construct from @a other.
 *
 * Acquires a reference on the underlying FunctionID, if any.
 *
 * @param other  Source setting whose state is duplicated.
 */
BreakpointSetting::BreakpointSetting(const BreakpointSetting& other)
	:
	fFunctionID(other.fFunctionID),
	fSourceFile(other.fSourceFile),
	fSourceLocation(other.fSourceLocation),
	fRelativeAddress(other.fRelativeAddress),
	fEnabled(other.fEnabled),
	fHidden(other.fHidden),
	fConditionExpression(other.fConditionExpression)
{
	if (fFunctionID != NULL)
		fFunctionID->AcquireReference();
}


/**
 * @brief Destructor; releases the FunctionID reference.
 */
BreakpointSetting::~BreakpointSetting()
{
	_Unset();
}


/**
 * @brief Initialise from a UserBreakpointLocation and runtime flags.
 *
 * Resets any previous state, copies the function id and source-location
 * fields, and stores the enabled/hidden flags and condition expression.
 *
 * @param location             Symbolic location of the breakpoint.
 * @param enabled              Whether the breakpoint is currently active.
 * @param hidden               Whether the breakpoint is hidden from the UI.
 * @param conditionExpression  Optional condition expression text.
 * @retval B_OK  Always.
 */
status_t
BreakpointSetting::SetTo(const UserBreakpointLocation& location, bool enabled,
	bool hidden, const BString& conditionExpression)
{
	_Unset();

	fFunctionID = location.GetFunctionID();
	if (fFunctionID != NULL)
		fFunctionID->AcquireReference();

	if (LocatableFile* file = location.SourceFile())
		file->GetPath(fSourceFile);

	fSourceLocation = location.GetSourceLocation();
	fRelativeAddress = location.RelativeAddress();
	fEnabled = enabled;
	fHidden = hidden;
	fConditionExpression = conditionExpression;

	return B_OK;
}


/**
 * @brief Initialise from a BMessage archive previously produced by WriteTo().
 *
 * Resets prior state, then unarchives the FunctionID and reads the source
 * file, source line/column, image-relative address, enabled, hidden, and
 * condition fields. Missing optional fields fall back to sensible defaults.
 *
 * @param archive  Source archive.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the FunctionID could not be unarchived.
 */
status_t
BreakpointSetting::SetTo(const BMessage& archive)
{
	_Unset();

	fFunctionID = ArchivingUtils::UnarchiveChild<FunctionID>(archive,
		"function");
	if (fFunctionID == NULL)
		return B_BAD_VALUE;

	archive.FindString("sourceFile", &fSourceFile);

	int32 line;
	if (archive.FindInt32("line", &line) != B_OK)
		line = -1;

	int32 column;
	if (archive.FindInt32("column", &column) != B_OK)
		column = -1;

	fSourceLocation = SourceLocation(line, column);

	if (archive.FindUInt64("relativeAddress", &fRelativeAddress) != B_OK)
		fRelativeAddress = 0;

	if (archive.FindBool("enabled", &fEnabled) != B_OK)
		fEnabled = false;

	if (archive.FindBool("hidden", &fHidden) != B_OK)
		fHidden = false;

	if (archive.FindString("condition", &fConditionExpression) != B_OK)
		fConditionExpression.Truncate(0);

	return B_OK;
}


/**
 * @brief Serialise the breakpoint to a BMessage archive.
 *
 * @param archive  Out: emptied and repopulated with the breakpoint fields.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When no FunctionID has been set.
 * @return Otherwise the first BMessage::Add*() error encountered.
 */
status_t
BreakpointSetting::WriteTo(BMessage& archive) const
{
	if (fFunctionID == NULL)
		return B_BAD_VALUE;

	archive.MakeEmpty();

	status_t error;
	if ((error = ArchivingUtils::ArchiveChild(fFunctionID, archive, "function"))
			!= B_OK
		|| (error = archive.AddString("sourceFile", fSourceFile)) != B_OK
		|| (error = archive.AddInt32("line", fSourceLocation.Line())) != B_OK
		|| (error = archive.AddInt32("column", fSourceLocation.Column()))
			!= B_OK
		|| (error = archive.AddUInt64("relativeAddress", fRelativeAddress))
			!= B_OK
		|| (error = archive.AddBool("enabled", fEnabled)) != B_OK
		|| (error = archive.AddBool("hidden", fHidden)) != B_OK
		|| (error = archive.AddString("condition", fConditionExpression))
			!= B_OK) {
		return error;
	}

	return B_OK;
}


/**
 * @brief Copy-assignment.
 *
 * Releases any prior FunctionID, then duplicates the right-hand side.
 *
 * @param other  Source setting whose state is copied.
 * @return Reference to @c *this.
 */
BreakpointSetting&
BreakpointSetting::operator=(const BreakpointSetting& other)
{
	if (this == &other)
		return *this;

	_Unset();

	fFunctionID = other.fFunctionID;
	if (fFunctionID != NULL)
		fFunctionID->AcquireReference();

	fSourceFile = other.fSourceFile;
	fSourceLocation = other.fSourceLocation;
	fRelativeAddress = other.fRelativeAddress;
	fEnabled = other.fEnabled;
	fHidden = other.fHidden;
	fConditionExpression = other.fConditionExpression;

	return *this;
}


/**
 * @brief Resets all fields to the empty/default state.
 *
 * Drops the FunctionID reference and clears strings, location, address,
 * enabled flag, and condition expression. The hidden flag is preserved.
 */
void
BreakpointSetting::_Unset()
{
	if (fFunctionID != NULL) {
		fFunctionID->ReleaseReference();
		fFunctionID = NULL;
	}

	fSourceFile.Truncate(0);
	fSourceLocation = SourceLocation();
	fRelativeAddress = 0;
	fEnabled = false;
	fConditionExpression.Truncate(0);
}
