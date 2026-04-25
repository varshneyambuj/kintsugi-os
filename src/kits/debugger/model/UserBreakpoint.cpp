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
 * @file UserBreakpoint.cpp
 * @brief Implementation of UserBreakpoint, UserBreakpointInstance, and
 *        UserBreakpointLocation: the user-facing breakpoint model.
 *
 * UserBreakpointLocation describes the source-level address (function id,
 * source file, source location, and offset) requested by the user.
 * UserBreakpoint owns a list of UserBreakpointInstance objects, each
 * mapped onto a low-level Breakpoint by the controller; this lets a
 * single source-level breakpoint expand into multiple machine
 * breakpoints for inlined or templated code.
 */


#include "UserBreakpoint.h"

#include "Function.h"
#include "FunctionID.h"
#include "LocatableFile.h"


// #pragma mark - UserBreakpointLocation


/**
 * @brief Constructs a UserBreakpointLocation acquiring references to its parts.
 *
 * @param functionID      Function identity the breakpoint targets; reference acquired.
 * @param sourceFile      Source file containing the breakpoint, or NULL.
 * @param sourceLocation  Line/column inside @a sourceFile.
 * @param relativeAddress Function-relative byte offset of the breakpoint.
 */
UserBreakpointLocation::UserBreakpointLocation(FunctionID* functionID,
	LocatableFile* sourceFile, const SourceLocation& sourceLocation,
	target_addr_t relativeAddress)
	:
	fFunctionID(functionID),
	fSourceFile(sourceFile),
	fSourceLocation(sourceLocation),
	fRelativeAddress(relativeAddress)
{
	fFunctionID->AcquireReference();
	if (fSourceFile != NULL)
		fSourceFile->AcquireReference();
}


/**
 * @brief Copy-constructs by acquiring fresh references to the source's parts.
 *
 * @param other Source location to copy.
 */
UserBreakpointLocation::UserBreakpointLocation(
	const UserBreakpointLocation& other)
	:
	fFunctionID(other.fFunctionID),
	fSourceFile(other.fSourceFile),
	fSourceLocation(other.fSourceLocation),
	fRelativeAddress(other.fRelativeAddress)
{
	fFunctionID->AcquireReference();
	if (fSourceFile != NULL)
		fSourceFile->AcquireReference();
}


/**
 * @brief Releases the function-id and source-file references.
 */
UserBreakpointLocation::~UserBreakpointLocation()
{
	fFunctionID->ReleaseReference();
	if (fSourceFile != NULL)
		fSourceFile->ReleaseReference();
}


/**
 * @brief Assigns from @a other; safe under self-assignment due to acquire-before-release.
 *
 * @param other Source location.
 * @return     Reference to *this.
 */
UserBreakpointLocation&
UserBreakpointLocation::operator=(
	const UserBreakpointLocation& other)
{
	other.fFunctionID->AcquireReference();
	if (other.fSourceFile != NULL)
		other.fSourceFile->AcquireReference();

	fFunctionID->ReleaseReference();
	if (fSourceFile != NULL)
		fSourceFile->ReleaseReference();

	fFunctionID = other.fFunctionID;
	fSourceFile = other.fSourceFile;
	fSourceLocation = other.fSourceLocation;
	fRelativeAddress = other.fRelativeAddress;

	return *this;
}


// #pragma mark - UserBreakpointInstance


/**
 * @brief Constructs an unbound UserBreakpointInstance at @a address.
 *
 * The instance is created in the not-yet-installed state; @c SetBreakpoint()
 * is called by the controller once a low-level Breakpoint has been
 * provisioned for it.
 *
 * @param userBreakpoint Owning UserBreakpoint.
 * @param address        Resolved target-space address for this instance.
 */
UserBreakpointInstance::UserBreakpointInstance(UserBreakpoint* userBreakpoint,
	target_addr_t address)
	:
	fAddress(address),
	fUserBreakpoint(userBreakpoint),
	fBreakpoint(NULL)
{
}


/**
 * @brief Records the low-level Breakpoint backing this instance.
 *
 * @param breakpoint Backing Breakpoint, or NULL on detach.
 */
void
UserBreakpointInstance::SetBreakpoint(Breakpoint* breakpoint)
{
	fBreakpoint = breakpoint;
}


// #pragma mark - UserBreakpoint


/**
 * @brief Constructs a fresh, disabled UserBreakpoint at @a location.
 *
 * @param location Source-level location captured by the breakpoint.
 */
UserBreakpoint::UserBreakpoint(const UserBreakpointLocation& location)
	:
	fLocation(location),
	fValid(false),
	fEnabled(false),
	fHidden(false),
	fConditionExpression()
{
}


/**
 * @brief Deletes every owned UserBreakpointInstance.
 */
UserBreakpoint::~UserBreakpoint()
{
	for (int32 i = 0; UserBreakpointInstance* instance = fInstances.ItemAt(i);
			i++) {
		delete instance;
	}
}


/**
 * @brief Returns the number of instances backing this user breakpoint.
 *
 * @return Instance count.
 */
int32
UserBreakpoint::CountInstances() const
{
	return fInstances.CountItems();
}


/**
 * @brief Returns the instance at @a index, or NULL if out of range.
 *
 * @param index Zero-based instance index.
 * @return     The instance, or NULL.
 */
UserBreakpointInstance*
UserBreakpoint::InstanceAt(int32 index) const
{
	return fInstances.ItemAt(index);
}


/**
 * @brief Appends a UserBreakpointInstance to the breakpoint.
 *
 * @param instance Instance to add; ownership transfers to UserBreakpoint.
 * @return        True on success, false on allocation failure.
 */
bool
UserBreakpoint::AddInstance(UserBreakpointInstance* instance)
{
	return fInstances.AddItem(instance);
}


/**
 * @brief Removes @a instance without deleting it.
 *
 * @param instance Instance to detach.
 */
void
UserBreakpoint::RemoveInstance(UserBreakpointInstance* instance)
{
	fInstances.RemoveItem(instance);
}


/**
 * @brief Removes and returns the instance at @a index.
 *
 * @param index Zero-based instance index.
 * @return     The detached instance, or NULL if out of range.
 */
UserBreakpointInstance*
UserBreakpoint::RemoveInstanceAt(int32 index)
{
	return fInstances.RemoveItemAt(index);
}


/**
 * @brief Marks the breakpoint as having (or losing) a resolvable location.
 *
 * @param valid True after the location was successfully resolved.
 */
void
UserBreakpoint::SetValid(bool valid)
{
	fValid = valid;
}


/**
 * @brief Enables or disables the breakpoint.
 *
 * @param enabled True to enable, false to disable without removing it.
 */
void
UserBreakpoint::SetEnabled(bool enabled)
{
	fEnabled = enabled;
}


/**
 * @brief Marks the breakpoint hidden from the user-visible breakpoint list.
 *
 * Used for internal/controlled breakpoints (e.g. step-out helpers).
 *
 * @param hidden True to hide, false to show.
 */
void
UserBreakpoint::SetHidden(bool hidden)
{
	fHidden = hidden;
}


/**
 * @brief Sets the conditional-breakpoint expression text.
 *
 * Empty string disables the condition.
 *
 * @param conditionExpression Expression evaluated each time the breakpoint hits.
 */
void
UserBreakpoint::SetCondition(const BString& conditionExpression)
{
	fConditionExpression = conditionExpression;
}
