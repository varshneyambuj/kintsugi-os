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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2013-2015, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Team.cpp
 * @brief Implementation of Team, the central debugger-model object that
 *        owns the threads, images, breakpoints, watchpoints, signal
 *        dispositions, and listener subscriptions for one debugged team.
 *
 * Team is the root aggregate of the debugger's model layer. It exposes
 * collection-style accessors for child entities (threads, images,
 * breakpoints, watchpoints, user breakpoints, signal dispositions),
 * dispatches add/remove/change notifications via a rich Listener
 * interface and Event hierarchy, and resolves source-level queries by
 * delegating to TeamDebugInfo. This translation unit also defines the
 * many small inner Event classes (BreakpointEvent, ThreadEvent,
 * ImageEvent, ImageLoadEvent, ImageLoadNameEvent, signal-disposition
 * events, console output, debug report, core-file, memory-change, and
 * watchpoint events) and the default no-op implementations of
 * Team::Listener.
 */


#include "Team.h"

#include <new>

#include <AutoLocker.h>

#include "Breakpoint.h"
#include "DisassembledCode.h"
#include "FileSourceCode.h"
#include "Function.h"
#include "ImageDebugInfo.h"
#include "SignalDispositionTypes.h"
#include "SourceCode.h"
#include "SpecificImageDebugInfo.h"
#include "Statement.h"
#include "TeamDebugInfo.h"
#include "Tracing.h"
#include "Value.h"
#include "Watchpoint.h"


// #pragma mark - BreakpointByAddressPredicate


/**
 * @brief Unary predicate ordering breakpoints relative to a target address.
 *
 * Used with @c BObjectList::FindBinaryInsertionIndex() to locate the first
 * breakpoint at or above a given address.
 */
struct Team::BreakpointByAddressPredicate
	: UnaryPredicate<Breakpoint> {
	/**
	 * @brief Captures the address against which candidates are compared.
	 *
	 * @param address Reference address.
	 */
	BreakpointByAddressPredicate(target_addr_t address)
		:
		fAddress(address)
	{
	}

	/**
	 * @brief Reports the ordering between @a breakpoint and the captured address.
	 *
	 * @param breakpoint Candidate breakpoint.
	 * @return          Comparison result with sign inverted from
	 *                   @c Breakpoint::CompareAddressBreakpoint().
	 */
	virtual int operator()(const Breakpoint* breakpoint) const
	{
		return -Breakpoint::CompareAddressBreakpoint(&fAddress, breakpoint);
	}

private:
	target_addr_t	fAddress;
};


// #pragma mark - WatchpointByAddressPredicate


/**
 * @brief Unary predicate ordering watchpoints relative to a target address.
 *
 * Used with @c BObjectList::FindBinaryInsertionIndex() for ranged
 * watchpoint lookups.
 */
struct Team::WatchpointByAddressPredicate
	: UnaryPredicate<Watchpoint> {
	/**
	 * @brief Captures the address against which candidates are compared.
	 *
	 * @param address Reference address.
	 */
	WatchpointByAddressPredicate(target_addr_t address)
		:
		fAddress(address)
	{
	}

	/**
	 * @brief Reports the ordering between @a watchpoint and the captured address.
	 *
	 * @param watchpoint Candidate watchpoint.
	 * @return          Comparison result with sign inverted from
	 *                   @c Watchpoint::CompareAddressWatchpoint().
	 */
	virtual int operator()(const Watchpoint* watchpoint) const
	{
		return -Watchpoint::CompareAddressWatchpoint(&fAddress, watchpoint);
	}

private:
	target_addr_t	fAddress;
};


// #pragma mark - Team


/**
 * @brief Constructs a Team owning @a debugInfo and pointing at the given
 *        memory and architecture services.
 *
 * The Team acquires a reference to @a debugInfo. The remaining
 * collaborators are stored as raw pointers; their lifetime is managed by
 * the controller that supplied them.
 *
 * @param teamID         Kernel team identifier.
 * @param teamMemory     Memory-access service used for ReadMemory/WriteMemory.
 * @param architecture   Target-architecture description.
 * @param debugInfo      Aggregated debug-info service; reference acquired.
 * @param typeInformation Type-lookup service (currently borrowed).
 */
Team::Team(team_id teamID, TeamMemory* teamMemory, Architecture* architecture,
	TeamDebugInfo* debugInfo, TeamTypeInformation* typeInformation)
	:
	fLock("team lock"),
	fID(teamID),
	fTeamMemory(teamMemory),
	fTypeInformation(typeInformation),
	fArchitecture(architecture),
	fDebugInfo(debugInfo),
	fStopOnImageLoad(false),
	fStopImageNameListEnabled(false),
	fDefaultSignalDisposition(SIGNAL_DISPOSITION_IGNORE)
{
	fDebugInfo->AcquireReference();
}


/**
 * @brief Releases all owned children: user breakpoints, breakpoints,
 *        watchpoints, images, threads, and the debug-info reference.
 */
Team::~Team()
{
	while (UserBreakpoint* userBreakpoint = fUserBreakpoints.RemoveHead())
		userBreakpoint->ReleaseReference();

	for (int32 i = 0; Breakpoint* breakpoint = fBreakpoints.ItemAt(i); i++)
		breakpoint->ReleaseReference();

	for (int32 i = 0; Watchpoint* watchpoint = fWatchpoints.ItemAt(i); i++)
		watchpoint->ReleaseReference();

	while (Image* image = fImages.RemoveHead())
		image->ReleaseReference();

	while (Thread* thread = fThreads.RemoveHead())
		thread->ReleaseReference();

	fDebugInfo->ReleaseReference();
}


/**
 * @brief Performs deferred initialisation by checking the team lock.
 *
 * @return Result of @c BLocker::InitCheck() on the per-Team lock.
 */
status_t
Team::Init()
{
	return fLock.InitCheck();
}


/**
 * @brief Replaces the team's display name and notifies listeners.
 *
 * @param name New team name.
 */
void
Team::SetName(const BString& name)
{
	fName = name;
	_NotifyTeamRenamed();
}


/**
 * @brief Adds an externally constructed Thread to the team.
 *
 * @param thread Thread to add; ownership transfers to the Team's list.
 */
void
Team::AddThread(Thread* thread)
{
	fThreads.Add(thread);
	_NotifyThreadAdded(thread);
}



/**
 * @brief Constructs a Thread from @a threadInfo, registers it, and returns it.
 *
 * @param threadInfo Lightweight descriptor for the new thread.
 * @param _thread    Optional: receives the newly constructed Thread on success.
 * @return          @c B_OK on success, @c B_NO_MEMORY if allocation fails,
 *                   or the @c Thread::Init() error code.
 */
status_t
Team::AddThread(const ThreadInfo& threadInfo, Thread** _thread)
{
	Thread* thread = new(std::nothrow) Thread(this, threadInfo.ThreadID());
	if (thread == NULL)
		return B_NO_MEMORY;

	status_t error = thread->Init();
	if (error != B_OK) {
		delete thread;
		return error;
	}

	thread->SetName(threadInfo.Name());
	AddThread(thread);

	if (_thread != NULL)
		*_thread = thread;

	return B_OK;
}


/**
 * @brief Removes @a thread from the team's thread list and notifies listeners.
 *
 * Does not release the caller's reference.
 *
 * @param thread Thread to detach.
 */
void
Team::RemoveThread(Thread* thread)
{
	fThreads.Remove(thread);
	_NotifyThreadRemoved(thread);
}


/**
 * @brief Removes the thread with id @a threadID, releasing the held reference.
 *
 * @param threadID Identifier of the thread to remove.
 * @return        True if a matching thread was found and removed.
 */
bool
Team::RemoveThread(thread_id threadID)
{
	Thread* thread = ThreadByID(threadID);
	if (thread == NULL)
		return false;

	RemoveThread(thread);
	thread->ReleaseReference();
	return true;
}


/**
 * @brief Looks up a thread by id via linear search.
 *
 * @param threadID Identifier to search for.
 * @return        The matching Thread, or NULL if absent.
 */
Thread*
Team::ThreadByID(thread_id threadID) const
{
	for (ThreadList::ConstIterator it = fThreads.GetIterator();
			Thread* thread = it.Next();) {
		if (thread->ID() == threadID)
			return thread;
	}

	return NULL;
}


/**
 * @brief Returns a const reference to the team's thread list.
 *
 * @return The list of all Threads currently registered with the team.
 */
const ThreadList&
Team::Threads() const
{
	return fThreads;
}


/**
 * @brief Constructs an Image from @a imageInfo and registers it with the team.
 *
 * If the new image is the application image (@c B_APP_IMAGE), the team's
 * display name is updated to match.
 *
 * @param imageInfo Snapshot describing the loaded image.
 * @param imageFile Optional on-disk file backing the image.
 * @param _image    Optional: receives the new Image on success.
 * @return         @c B_OK on success; @c B_NO_MEMORY on allocation failure;
 *                  the @c Image::Init() error code otherwise.
 */
status_t
Team::AddImage(const ImageInfo& imageInfo, LocatableFile* imageFile,
	Image** _image)
{
	Image* image = new(std::nothrow) Image(this, imageInfo, imageFile);
	if (image == NULL)
		return B_NO_MEMORY;

	status_t error = image->Init();
	if (error != B_OK) {
		delete image;
		return error;
	}

	if (image->Type() == B_APP_IMAGE)
		SetName(image->Name());

	fImages.Add(image);
	_NotifyImageAdded(image);

	if (_image != NULL)
		*_image = image;

	return B_OK;
}


/**
 * @brief Removes @a image from the team and notifies listeners.
 *
 * Does not release the caller's reference.
 *
 * @param image Image to detach.
 */
void
Team::RemoveImage(Image* image)
{
	fImages.Remove(image);
	_NotifyImageRemoved(image);
}


/**
 * @brief Removes the image with id @a imageID, releasing the held reference.
 *
 * @param imageID Identifier of the image to remove.
 * @return       True if a matching image was found and removed.
 */
bool
Team::RemoveImage(image_id imageID)
{
	Image* image = ImageByID(imageID);
	if (image == NULL)
		return false;

	RemoveImage(image);
	image->ReleaseReference();
	return true;
}


/**
 * @brief Looks up an image by id via linear search.
 *
 * @param imageID Identifier to search for.
 * @return       The matching Image, or NULL.
 */
Image*
Team::ImageByID(image_id imageID) const
{
	for (ImageList::ConstIterator it = fImages.GetIterator();
			Image* image = it.Next();) {
		if (image->ID() == imageID)
			return image;
	}

	return NULL;
}


/**
 * @brief Returns the Image whose mapped range contains @a address.
 *
 * @param address Target-space address.
 * @return       The owning Image, or NULL if @a address falls outside all
 *                mapped images.
 */
Image*
Team::ImageByAddress(target_addr_t address) const
{
	for (ImageList::ConstIterator it = fImages.GetIterator();
			Image* image = it.Next();) {
		if (image->ContainsAddress(address))
			return image;
	}

	return NULL;
}


/**
 * @brief Returns a const reference to the team's image list.
 *
 * @return The list of all Images currently registered with the team.
 */
const ImageList&
Team::Images() const
{
	return fImages;
}


/**
 * @brief Removes every image from the team, dispatching removal notifications.
 */
void
Team::ClearImages()
{
	while (!fImages.IsEmpty())
		RemoveImage(fImages.First());
}


/**
 * @brief Adds @a name to the stop-on-image-load name list, kept sorted.
 *
 * @param name Image name pattern to add.
 * @return    True on success, false on allocation failure.
 */
bool
Team::AddStopImageName(const BString& name)
{
	if (!fStopImageNames.Add(name))
		return false;

	fStopImageNames.Sort();

	NotifyStopImageNameAdded(name);
	return true;
}


/**
 * @brief Removes @a name from the stop-on-image-load list and notifies listeners.
 *
 * @param name Image name to remove.
 */
void
Team::RemoveStopImageName(const BString& name)
{
	fStopImageNames.Remove(name);
	NotifyStopImageNameRemoved(name);
}


/**
 * @brief Configures whether image loads halt the team and how the name list applies.
 *
 * @param enabled          True to halt the team on image load.
 * @param useImageNameList True to filter halts by the stop-image-name list.
 */
void
Team::SetStopOnImageLoad(bool enabled, bool useImageNameList)
{
	fStopOnImageLoad = enabled;
	fStopImageNameListEnabled = useImageNameList;
	NotifyStopOnImageLoadChanged(enabled, useImageNameList);
}


/**
 * @brief Returns the stop-on-image-load name filter list.
 *
 * @return Reference to the sorted filter list.
 */
const BStringList&
Team::StopImageNames() const
{
	return fStopImageNames;
}


/**
 * @brief Sets the team's default signal disposition.
 *
 * No notification is dispatched if the disposition is unchanged.
 *
 * @param disposition One of the @c SIGNAL_DISPOSITION_* constants.
 */
void
Team::SetDefaultSignalDisposition(int32 disposition)
{
	if (disposition != fDefaultSignalDisposition) {
		fDefaultSignalDisposition = disposition;
		NotifyDefaultSignalDispositionChanged(disposition);
	}
}


/**
 * @brief Sets a per-signal override for the team's signal disposition.
 *
 * @param signal      POSIX signal number.
 * @param disposition One of the @c SIGNAL_DISPOSITION_* constants.
 * @return           True on success, false on map insert failure.
 */
bool
Team::SetCustomSignalDisposition(int32 signal, int32 disposition)
{
	SignalDispositionMappings::iterator it = fCustomSignalDispositions.find(
		signal);
	if (it != fCustomSignalDispositions.end() && it->second == disposition)
		return true;

	try {
		fCustomSignalDispositions[signal] = disposition;
	} catch (...) {
		return false;
	}

	NotifyCustomSignalDispositionChanged(signal, disposition);

	return true;
}


/**
 * @brief Removes the custom override for @a signal, if any, and notifies listeners.
 *
 * @param signal POSIX signal number whose override is to be cleared.
 */
void
Team::RemoveCustomSignalDisposition(int32 signal)
{
	SignalDispositionMappings::iterator it = fCustomSignalDispositions.find(
		signal);
	if (it == fCustomSignalDispositions.end())
		return;

	fCustomSignalDispositions.erase(it);

	NotifyCustomSignalDispositionRemoved(signal);
}


/**
 * @brief Returns the effective disposition for @a signal.
 *
 * @param signal POSIX signal number.
 * @return      The custom disposition if present, otherwise the default.
 */
int32
Team::SignalDispositionFor(int32 signal) const
{
	SignalDispositionMappings::const_iterator it
		= fCustomSignalDispositions.find(signal);
	if (it != fCustomSignalDispositions.end())
		return it->second;

	return fDefaultSignalDisposition;
}


/**
 * @brief Returns the full custom-signal-disposition map.
 *
 * @return Reference to the (signal -> disposition) map.
 */
const SignalDispositionMappings&
Team::GetSignalDispositionMappings() const
{
	return fCustomSignalDispositions;
}


/**
 * @brief Clears every custom signal-disposition override.
 *
 * No notifications are dispatched per entry.
 */
void
Team::ClearSignalDispositionMappings()
{
	fCustomSignalDispositions.clear();
}


/**
 * @brief Inserts a Breakpoint into the team's address-sorted list.
 *
 * On insertion failure the caller's reference is released.
 *
 * @param breakpoint Breakpoint to insert; ownership transfers to the Team
 *                    (one reference) on success.
 * @return          True on success; false on insert failure.
 */
bool
Team::AddBreakpoint(Breakpoint* breakpoint)
{
	if (fBreakpoints.BinaryInsert(breakpoint, &Breakpoint::CompareBreakpoints))
		return true;

	breakpoint->ReleaseReference();
	return false;
}


/**
 * @brief Removes @a breakpoint from the team and releases the held reference.
 *
 * Silently does nothing if no matching breakpoint is found.
 *
 * @param breakpoint Breakpoint to remove.
 */
void
Team::RemoveBreakpoint(Breakpoint* breakpoint)
{
	int32 index = fBreakpoints.BinarySearchIndex(*breakpoint,
		&Breakpoint::CompareBreakpoints);
	if (index < 0)
		return;

	fBreakpoints.RemoveItemAt(index);
	breakpoint->ReleaseReference();
}


/**
 * @brief Returns the number of low-level breakpoints registered.
 *
 * @return Breakpoint count.
 */
int32
Team::CountBreakpoints() const
{
	return fBreakpoints.CountItems();
}


/**
 * @brief Returns the breakpoint at @a index in address-sorted order.
 *
 * @param index Zero-based index.
 * @return     The breakpoint, or NULL if out of range.
 */
Breakpoint*
Team::BreakpointAt(int32 index) const
{
	return fBreakpoints.ItemAt(index);
}


/**
 * @brief Looks up a breakpoint by exact address via binary search.
 *
 * @param address Target-space address.
 * @return       The matching Breakpoint, or NULL if absent.
 */
Breakpoint*
Team::BreakpointAtAddress(target_addr_t address) const
{
	return fBreakpoints.BinarySearchByKey(address,
		&Breakpoint::CompareAddressBreakpoint);
}


/**
 * @brief Collects every UserBreakpoint mapped onto a breakpoint within @a range.
 *
 * @param range       Inclusive target-address range.
 * @param breakpoints Output list; matching UserBreakpoints are appended.
 *
 * @todo Avoid duplicates when one user breakpoint maps onto multiple
 *       low-level breakpoints inside the range.
 */
void
Team::GetBreakpointsInAddressRange(TargetAddressRange range,
	BObjectList<UserBreakpoint>& breakpoints) const
{
	int32 index = fBreakpoints.FindBinaryInsertionIndex(
		BreakpointByAddressPredicate(range.Start()));
	for (; Breakpoint* breakpoint = fBreakpoints.ItemAt(index); index++) {
		if (breakpoint->Address() > range.End())
			break;

		for (UserBreakpointInstanceList::ConstIterator it
				= breakpoint->UserBreakpoints().GetIterator();
			UserBreakpointInstance* instance = it.Next();) {
			breakpoints.AddItem(instance->GetUserBreakpoint());
		}
	}

	// TODO: Avoid duplicates!
}


/**
 * @brief Collects every UserBreakpoint visible in @a sourceCode.
 *
 * For DisassembledCode the search uses the address range; for file-backed
 * source the search filters by source file path.
 *
 * @param sourceCode  Source view in which to find breakpoints.
 * @param breakpoints Output list; matching UserBreakpoints are appended.
 *
 * @todo Source-file lookup is linear; a per-source-file index would speed it up.
 */
void
Team::GetBreakpointsForSourceCode(SourceCode* sourceCode,
	BObjectList<UserBreakpoint>& breakpoints) const
{
	if (DisassembledCode* disassembledCode
			= dynamic_cast<DisassembledCode*>(sourceCode)) {
		GetBreakpointsInAddressRange(disassembledCode->StatementAddressRange(),
			breakpoints);
		return;
	}

	LocatableFile* sourceFile = sourceCode->GetSourceFile();
	if (sourceFile == NULL)
		return;

	// TODO: This can probably be optimized. Maybe by registering the user
	// breakpoints with the team and sorting them by source code.
	for (int32 i = 0; Breakpoint* breakpoint = fBreakpoints.ItemAt(i); i++) {
		UserBreakpointInstance* userBreakpointInstance
			= breakpoint->FirstUserBreakpoint();
		if (userBreakpointInstance == NULL)
			continue;

		UserBreakpoint* userBreakpoint
			= userBreakpointInstance->GetUserBreakpoint();
		if (userBreakpoint->Location().SourceFile() == sourceFile)
			breakpoints.AddItem(userBreakpoint);
	}
}


/**
 * @brief Registers a UserBreakpoint with the team and acquires a reference.
 *
 * @param userBreakpoint Source-level breakpoint to track.
 */
void
Team::AddUserBreakpoint(UserBreakpoint* userBreakpoint)
{
	fUserBreakpoints.Add(userBreakpoint);
	userBreakpoint->AcquireReference();
}


/**
 * @brief Detaches a previously registered UserBreakpoint and releases the reference.
 *
 * @param userBreakpoint Source-level breakpoint to detach.
 */
void
Team::RemoveUserBreakpoint(UserBreakpoint* userBreakpoint)
{
	fUserBreakpoints.Remove(userBreakpoint);
	userBreakpoint->ReleaseReference();
}


/**
 * @brief Inserts a Watchpoint into the team's address-sorted list.
 *
 * On insertion failure the caller's reference is released.
 *
 * @param watchpoint Watchpoint to insert; ownership transfers on success.
 * @return          True on success; false on insert failure.
 */
bool
Team::AddWatchpoint(Watchpoint* watchpoint)
{
	if (fWatchpoints.BinaryInsert(watchpoint, &Watchpoint::CompareWatchpoints))
		return true;

	watchpoint->ReleaseReference();
	return false;
}


/**
 * @brief Removes @a watchpoint from the team and releases the held reference.
 *
 * Silently does nothing if no matching watchpoint is found.
 *
 * @param watchpoint Watchpoint to remove.
 */
void
Team::RemoveWatchpoint(Watchpoint* watchpoint)
{
	int32 index = fWatchpoints.BinarySearchIndex(*watchpoint,
		&Watchpoint::CompareWatchpoints);
	if (index < 0)
		return;

	fWatchpoints.RemoveItemAt(index);
	watchpoint->ReleaseReference();
}


/**
 * @brief Returns the number of watchpoints registered.
 *
 * @return Watchpoint count.
 */
int32
Team::CountWatchpoints() const
{
	return fWatchpoints.CountItems();
}


/**
 * @brief Returns the watchpoint at @a index in address-sorted order.
 *
 * @param index Zero-based index.
 * @return     The watchpoint, or NULL if out of range.
 */
Watchpoint*
Team::WatchpointAt(int32 index) const
{
	return fWatchpoints.ItemAt(index);
}


/**
 * @brief Looks up a watchpoint by exact address via binary search.
 *
 * @param address Target-space address.
 * @return       The matching Watchpoint, or NULL if absent.
 */
Watchpoint*
Team::WatchpointAtAddress(target_addr_t address) const
{
	return fWatchpoints.BinarySearchByKey(address,
		&Watchpoint::CompareAddressWatchpoint);
}


/**
 * @brief Collects every watchpoint with a base address inside @a range.
 *
 * @param range       Inclusive target-address range.
 * @param watchpoints Output list; matching Watchpoints are appended.
 */
void
Team::GetWatchpointsInAddressRange(TargetAddressRange range,
	BObjectList<Watchpoint>& watchpoints) const
{
	int32 index = fWatchpoints.FindBinaryInsertionIndex(
		WatchpointByAddressPredicate(range.Start()));
	for (; Watchpoint* watchpoint = fWatchpoints.ItemAt(index); index++) {
		if (watchpoint->Address() > range.End())
			break;

		watchpoints.AddItem(watchpoint);
	}
}


/**
 * @brief Resolves the statement covering @a address and the owning function.
 *
 * The lookup walks images, then per-image debug info, then the function's
 * disassembled-code cache (if any). If the function lacks cached
 * disassembled code, the statement is fetched directly from the
 * specific-image debug info.
 *
 * @param address    Target-space address to resolve.
 * @param _function  On success, receives the FunctionInstance owning the
 *                    address (no reference acquired here; the caller already
 *                    holds an effective reference via the returned statement).
 * @param _statement On success, receives the Statement; reference acquired.
 * @return          @c B_OK on success, @c B_ENTRY_NOT_FOUND if any step
 *                   fails to locate a candidate, or the propagated debug-info
 *                   error code.
 */
status_t
Team::GetStatementAtAddress(target_addr_t address, FunctionInstance*& _function,
	Statement*& _statement)
{
	TRACE_CODE("Team::GetStatementAtAddress(%#" B_PRIx64 ")\n", address);

	// get the image at the address
	Image* image = ImageByAddress(address);
	if (image == NULL) {
		TRACE_CODE("  -> no image\n");
		return B_ENTRY_NOT_FOUND;
	}

	ImageDebugInfo* imageDebugInfo = image->GetImageDebugInfo();
	if (imageDebugInfo == NULL) {
		TRACE_CODE("  -> no image debug info\n");
		return B_ENTRY_NOT_FOUND;
	}

	// get the function
	FunctionInstance* functionInstance
		= imageDebugInfo->FunctionAtAddress(address);
	if (functionInstance == NULL) {
		TRACE_CODE("  -> no function instance\n");
		return B_ENTRY_NOT_FOUND;
	}

	// If the function instance has disassembled code attached, we can get the
	// statement directly.
	if (DisassembledCode* code = functionInstance->GetSourceCode()) {
		Statement* statement = code->StatementAtAddress(address);
		if (statement == NULL)
			return B_ENTRY_NOT_FOUND;

		statement->AcquireReference();
		_statement = statement;
		_function = functionInstance;
		return B_OK;
	}

	// get the statement from the image debug info
	FunctionDebugInfo* functionDebugInfo
		= functionInstance->GetFunctionDebugInfo();
	status_t error = functionDebugInfo->GetSpecificImageDebugInfo()
		->GetStatement(functionDebugInfo, address, _statement);
	if (error != B_OK) {
		TRACE_CODE("  -> no statement from the specific image debug info\n");
		return error;
	}

	_function = functionInstance;
	return B_OK;
}


/**
 * @brief Resolves the statement at @a location inside @a sourceCode.
 *
 * For DisassembledCode the lookup is direct; for file-backed source the
 * function at the location is found via TeamDebugInfo, then its first
 * instance's image debug info is consulted.
 *
 * @param sourceCode Source view containing the location.
 * @param location   Source-level (line, column) coordinates.
 * @param _statement On success, receives the Statement; reference acquired.
 * @return          @c B_OK on success or @c B_ENTRY_NOT_FOUND if no
 *                   covering statement could be located.
 */
status_t
Team::GetStatementAtSourceLocation(SourceCode* sourceCode,
	const SourceLocation& location, Statement*& _statement)
{
	TRACE_CODE("Team::GetStatementAtSourceLocation(%p, (%" B_PRId32 ", %"
		B_PRId32 "))\n", sourceCode, location.Line(), location.Column());

	// If we're lucky the source code can provide us with a statement.
	if (DisassembledCode* code = dynamic_cast<DisassembledCode*>(sourceCode)) {
		Statement* statement = code->StatementAtLocation(location);
		if (statement == NULL)
			return B_ENTRY_NOT_FOUND;

		statement->AcquireReference();
		_statement = statement;
		return B_OK;
	}

	// Go the long and stony way over the source file and the team debug info.
	// get the source file for the source code
	LocatableFile* sourceFile = sourceCode->GetSourceFile();
	if (sourceFile == NULL)
		return B_ENTRY_NOT_FOUND;

	// get the function at the source location
	Function* function = fDebugInfo->FunctionAtSourceLocation(sourceFile,
		location);
	if (function == NULL)
		return B_ENTRY_NOT_FOUND;

	// Get some function instance and ask its image debug info to provide us
	// with a statement.
	FunctionInstance* functionInstance = function->FirstInstance();
	if (functionInstance == NULL)
		return B_ENTRY_NOT_FOUND;

	FunctionDebugInfo* functionDebugInfo
		= functionInstance->GetFunctionDebugInfo();
	return functionDebugInfo->GetSpecificImageDebugInfo()
		->GetStatementAtSourceLocation(functionDebugInfo, location, _statement);
}


/**
 * @brief Resolves a function id to its Function object via TeamDebugInfo.
 *
 * @param functionID Function identity to look up.
 * @return          The Function, or NULL if no matching debug info exists.
 */
Function*
Team::FunctionByID(FunctionID* functionID) const
{
	return fDebugInfo->FunctionByID(functionID);
}


/**
 * @brief Subscribes @a listener for team events.
 *
 * The team is locked during subscription to keep notification dispatch
 * stable.
 *
 * @param listener Listener to add; caller retains ownership.
 */
void
Team::AddListener(Listener* listener)
{
	AutoLocker<Team> locker(this);
	fListeners.Add(listener);
}


/**
 * @brief Unsubscribes a previously registered listener.
 *
 * @param listener Listener previously passed to @c AddListener().
 */
void
Team::RemoveListener(Listener* listener)
{
	AutoLocker<Team> locker(this);
	fListeners.Remove(listener);
}


/**
 * @brief Dispatches @c ThreadStateChanged to every listener.
 *
 * @param thread Thread whose state changed.
 */
void
Team::NotifyThreadStateChanged(Thread* thread)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ThreadStateChanged(
			ThreadEvent(TEAM_EVENT_THREAD_STATE_CHANGED, thread));
	}
}


/**
 * @brief Dispatches @c ThreadCpuStateChanged to every listener.
 *
 * @param thread Thread whose CpuState changed.
 */
void
Team::NotifyThreadCpuStateChanged(Thread* thread)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ThreadCpuStateChanged(
			ThreadEvent(TEAM_EVENT_THREAD_CPU_STATE_CHANGED, thread));
	}
}


/**
 * @brief Dispatches @c ThreadStackTraceChanged to every listener.
 *
 * @param thread Thread whose StackTrace changed.
 */
void
Team::NotifyThreadStackTraceChanged(Thread* thread)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ThreadStackTraceChanged(
			ThreadEvent(TEAM_EVENT_THREAD_STACK_TRACE_CHANGED, thread));
	}
}


/**
 * @brief Dispatches @c ImageDebugInfoChanged to every listener.
 *
 * @param image Image whose ImageDebugInfo state changed.
 */
void
Team::NotifyImageDebugInfoChanged(Image* image)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ImageDebugInfoChanged(
			ImageEvent(TEAM_EVENT_IMAGE_DEBUG_INFO_CHANGED, image));
	}
}


/**
 * @brief Dispatches @c StopOnImageLoadSettingsChanged to every listener.
 *
 * @param enabled          Updated halt-on-image-load flag.
 * @param useImageNameList Updated name-list-filter flag.
 */
void
Team::NotifyStopOnImageLoadChanged(bool enabled, bool useImageNameList)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->StopOnImageLoadSettingsChanged(
			ImageLoadEvent(TEAM_EVENT_IMAGE_LOAD_SETTINGS_CHANGED, this,
				enabled, useImageNameList));
	}
}


/**
 * @brief Dispatches @c StopOnImageLoadNameAdded to every listener.
 *
 * @param name Image name newly added to the stop list.
 */
void
Team::NotifyStopImageNameAdded(const BString& name)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->StopOnImageLoadNameAdded(
			ImageLoadNameEvent(TEAM_EVENT_IMAGE_LOAD_NAME_ADDED, this, name));
	}
}


/**
 * @brief Dispatches @c StopOnImageLoadNameRemoved to every listener.
 *
 * @param name Image name removed from the stop list.
 */
void
Team::NotifyStopImageNameRemoved(const BString& name)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->StopOnImageLoadNameRemoved(
			ImageLoadNameEvent(TEAM_EVENT_IMAGE_LOAD_NAME_REMOVED, this,
				name));
	}
}


/**
 * @brief Dispatches @c DefaultSignalDispositionChanged to every listener.
 *
 * @param disposition New default signal disposition.
 */
void
Team::NotifyDefaultSignalDispositionChanged(int32 disposition)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->DefaultSignalDispositionChanged(
			DefaultSignalDispositionEvent(
				TEAM_EVENT_DEFAULT_SIGNAL_DISPOSITION_CHANGED, this,
				disposition));
	}
}


/**
 * @brief Dispatches @c CustomSignalDispositionChanged to every listener.
 *
 * @param signal      Signal whose override changed.
 * @param disposition New disposition value.
 */
void
Team::NotifyCustomSignalDispositionChanged(int32 signal, int32 disposition)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->CustomSignalDispositionChanged(
			CustomSignalDispositionEvent(
				TEAM_EVENT_CUSTOM_SIGNAL_DISPOSITION_CHANGED, this,
				signal, disposition));
	}
}


/**
 * @brief Dispatches @c CustomSignalDispositionRemoved to every listener.
 *
 * @param signal Signal whose override was cleared.
 */
void
Team::NotifyCustomSignalDispositionRemoved(int32 signal)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->CustomSignalDispositionRemoved(
			CustomSignalDispositionEvent(
				TEAM_EVENT_CUSTOM_SIGNAL_DISPOSITION_REMOVED, this,
				signal, SIGNAL_DISPOSITION_IGNORE));
	}
}


/**
 * @brief Dispatches @c ConsoleOutputReceived to every listener.
 *
 * @param fd     Descriptor on which @a output appeared (1 = stdout, 2 = stderr).
 * @param output Captured text.
 */
void
Team::NotifyConsoleOutputReceived(int32 fd, const BString& output)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ConsoleOutputReceived(
			ConsoleOutputEvent(TEAM_EVENT_CONSOLE_OUTPUT_RECEIVED, this,
				fd, output));
	}
}


/**
 * @brief Dispatches @c UserBreakpointChanged to every listener.
 *
 * @param breakpoint UserBreakpoint whose state changed.
 */
void
Team::NotifyUserBreakpointChanged(UserBreakpoint* breakpoint)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->UserBreakpointChanged(UserBreakpointEvent(
			TEAM_EVENT_USER_BREAKPOINT_CHANGED, this, breakpoint));
	}
}


/**
 * @brief Dispatches @c WatchpointChanged to every listener.
 *
 * @param watchpoint Watchpoint whose state changed.
 */
void
Team::NotifyWatchpointChanged(Watchpoint* watchpoint)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->WatchpointChanged(WatchpointEvent(
			TEAM_EVENT_WATCHPOINT_CHANGED, this, watchpoint));
	}
}


/**
 * @brief Dispatches @c DebugReportChanged to every listener.
 *
 * @param reportPath Path to the on-disk debug-report file.
 * @param result     Result code of the report-generation operation.
 */
void
Team::NotifyDebugReportChanged(const char* reportPath, status_t result)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->DebugReportChanged(DebugReportEvent(
			TEAM_EVENT_DEBUG_REPORT_CHANGED, this, reportPath, result));
	}
}


/**
 * @brief Dispatches @c CoreFileChanged to every listener.
 *
 * @param targetPath On-disk path to the core file just produced or loaded.
 */
void
Team::NotifyCoreFileChanged(const char* targetPath)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->CoreFileChanged(CoreFileChangedEvent(
			TEAM_EVENT_CORE_FILE_CHANGED, this, targetPath));
	}
}


/**
 * @brief Dispatches @c MemoryChanged to every listener.
 *
 * @param address Base address of the modified memory range.
 * @param size    Size of the modified range in bytes.
 */
void
Team::NotifyMemoryChanged(target_addr_t address, target_size_t size)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->MemoryChanged(MemoryChangedEvent(
			TEAM_EVENT_MEMORY_CHANGED, this, address, size));
	}
}


/**
 * @brief Internal helper dispatching @c TeamRenamed to every listener.
 */
void
Team::_NotifyTeamRenamed()
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->TeamRenamed(Event(TEAM_EVENT_TEAM_RENAMED, this));
	}
}


/**
 * @brief Internal helper dispatching @c ThreadAdded to every listener.
 *
 * @param thread Newly added thread.
 */
void
Team::_NotifyThreadAdded(Thread* thread)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ThreadAdded(ThreadEvent(TEAM_EVENT_THREAD_ADDED, thread));
	}
}


/**
 * @brief Internal helper dispatching @c ThreadRemoved to every listener.
 *
 * @param thread Thread being removed.
 */
void
Team::_NotifyThreadRemoved(Thread* thread)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ThreadRemoved(ThreadEvent(TEAM_EVENT_THREAD_REMOVED, thread));
	}
}


/**
 * @brief Internal helper dispatching @c ImageAdded to every listener.
 *
 * @param image Newly added image.
 */
void
Team::_NotifyImageAdded(Image* image)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ImageAdded(ImageEvent(TEAM_EVENT_IMAGE_ADDED, image));
	}
}


/**
 * @brief Internal helper dispatching @c ImageRemoved to every listener.
 *
 * @param image Image being removed.
 */
void
Team::_NotifyImageRemoved(Image* image)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ImageRemoved(ImageEvent(TEAM_EVENT_IMAGE_REMOVED, image));
	}
}


// #pragma mark - Event


/**
 * @brief Constructs the base Event with an event type and owning team.
 *
 * @param type Event-type identifier (one of @c TEAM_EVENT_*).
 * @param team Owning Team.
 */
Team::Event::Event(uint32 type, Team* team)
	:
	fEventType(type),
	fTeam(team)
{
}


// #pragma mark - ThreadEvent


/**
 * @brief Constructs a ThreadEvent inferring its team from @a thread.
 *
 * @param type   Event-type identifier.
 * @param thread Thread the event concerns.
 */
Team::ThreadEvent::ThreadEvent(uint32 type, Thread* thread)
	:
	Event(type, thread->GetTeam()),
	fThread(thread)
{
}


// #pragma mark - ImageEvent


/**
 * @brief Constructs an ImageEvent inferring its team from @a image.
 *
 * @param type  Event-type identifier.
 * @param image Image the event concerns.
 */
Team::ImageEvent::ImageEvent(uint32 type, Image* image)
	:
	Event(type, image->GetTeam()),
	fImage(image)
{
}


// #pragma mark - ImageLoadEvent


/**
 * @brief Constructs an ImageLoadEvent carrying stop-on-image-load settings.
 *
 * @param type                     Event-type identifier.
 * @param team                     Team the event concerns.
 * @param stopOnImageLoad          New stop-on-image-load flag.
 * @param stopImageNameListEnabled New name-list-filter flag.
 */
Team::ImageLoadEvent::ImageLoadEvent(uint32 type, Team* team,
	bool stopOnImageLoad, bool stopImageNameListEnabled)
	:
	Event(type, team),
	fStopOnImageLoad(stopOnImageLoad),
	fStopImageNameListEnabled(stopImageNameListEnabled)
{
}


// #pragma mark - ImageLoadNameEvent


/**
 * @brief Constructs an ImageLoadNameEvent for a stop-list name change.
 *
 * @param type Event-type identifier.
 * @param team Team the event concerns.
 * @param name Image name added or removed.
 */
Team::ImageLoadNameEvent::ImageLoadNameEvent(uint32 type, Team* team,
	const BString& name)
	:
	Event(type, team),
	fImageName(name)
{
}


// #pragma mark - DefaultSignalDispositionEvent


/**
 * @brief Constructs a DefaultSignalDispositionEvent.
 *
 * @param type        Event-type identifier.
 * @param team        Team the event concerns.
 * @param disposition New default signal disposition.
 */
Team::DefaultSignalDispositionEvent::DefaultSignalDispositionEvent(uint32 type,
	Team* team, int32 disposition)
	:
	Event(type, team),
	fDefaultDisposition(disposition)
{
}


// #pragma mark - CustomSignalDispositionEvent


/**
 * @brief Constructs a CustomSignalDispositionEvent for one signal.
 *
 * @param type        Event-type identifier.
 * @param team        Team the event concerns.
 * @param signal      Signal whose override changed.
 * @param disposition New disposition value.
 */
Team::CustomSignalDispositionEvent::CustomSignalDispositionEvent(uint32 type,
	Team* team, int32 signal, int32 disposition)
	:
	Event(type, team),
	fSignal(signal),
	fDisposition(disposition)
{
}


// #pragma mark - BreakpointEvent


/**
 * @brief Constructs a BreakpointEvent for a low-level breakpoint change.
 *
 * @param type       Event-type identifier.
 * @param team       Team the event concerns.
 * @param breakpoint Breakpoint touched by the event.
 */
Team::BreakpointEvent::BreakpointEvent(uint32 type, Team* team,
	Breakpoint* breakpoint)
	:
	Event(type, team),
	fBreakpoint(breakpoint)
{
}


// #pragma mark - ConsoleOutputEvent


/**
 * @brief Constructs a ConsoleOutputEvent capturing one chunk of console text.
 *
 * @param type   Event-type identifier.
 * @param team   Team that produced the output.
 * @param fd     Descriptor on which the output appeared.
 * @param output Captured text.
 */
Team::ConsoleOutputEvent::ConsoleOutputEvent(uint32 type, Team* team,
	int32 fd, const BString& output)
	:
	Event(type, team),
	fDescriptor(fd),
	fOutput(output)
{
}


// #pragma mark - DebugReportEvent


/**
 * @brief Constructs a DebugReportEvent.
 *
 * @param type        Event-type identifier.
 * @param team        Team the report covers.
 * @param reportPath  On-disk path of the report.
 * @param finalStatus Final status of the report-generation operation.
 */
Team::DebugReportEvent::DebugReportEvent(uint32 type, Team* team,
	const char* reportPath, status_t finalStatus)
	:
	Event(type, team),
	fReportPath(reportPath),
	fFinalStatus(finalStatus)
{
}


// #pragma mark - CoreFileChangedEvent


/**
 * @brief Constructs a CoreFileChangedEvent.
 *
 * @param type       Event-type identifier.
 * @param team       Team the core file describes.
 * @param targetPath On-disk path of the core file.
 */
Team::CoreFileChangedEvent::CoreFileChangedEvent(uint32 type, Team* team,
	const char* targetPath)
	:
	Event(type, team),
	fTargetPath(targetPath)
{
}


// #pragma mark - MemoryChangedEvent


/**
 * @brief Constructs a MemoryChangedEvent for a modified memory range.
 *
 * @param type    Event-type identifier.
 * @param team    Team whose memory changed.
 * @param address Base address of the modified range.
 * @param size    Size of the modified range in bytes.
 */
Team::MemoryChangedEvent::MemoryChangedEvent(uint32 type, Team* team,
	target_addr_t address, target_size_t size)
	:
	Event(type, team),
	fTargetAddress(address),
	fSize(size)
{
}


// #pragma mark - WatchpointEvent


/**
 * @brief Constructs a WatchpointEvent for a watchpoint state change.
 *
 * @param type       Event-type identifier.
 * @param team       Team owning the watchpoint.
 * @param watchpoint Watchpoint that changed.
 */
Team::WatchpointEvent::WatchpointEvent(uint32 type, Team* team,
	Watchpoint* watchpoint)
	:
	Event(type, team),
	fWatchpoint(watchpoint)
{
}


// #pragma mark - UserBreakpointEvent


/**
 * @brief Constructs a UserBreakpointEvent.
 *
 * @param type       Event-type identifier.
 * @param team       Team owning the user breakpoint.
 * @param breakpoint UserBreakpoint that changed.
 */
Team::UserBreakpointEvent::UserBreakpointEvent(uint32 type, Team* team,
	UserBreakpoint* breakpoint)
	:
	Event(type, team),
	fBreakpoint(breakpoint)
{
}


// #pragma mark - Listener


/**
 * @brief Virtual destructor anchor for Team::Listener.
 */
Team::Listener::~Listener()
{
}


/**
 * @brief Default no-op for the team-renamed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::TeamRenamed(const Team::Event& event)
{
}


/**
 * @brief Default no-op for the thread-added callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::ThreadAdded(const Team::ThreadEvent& event)
{
}


/**
 * @brief Default no-op for the thread-removed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::ThreadRemoved(const Team::ThreadEvent& event)
{
}


/**
 * @brief Default no-op for the image-added callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::ImageAdded(const Team::ImageEvent& event)
{
}


/**
 * @brief Default no-op for the image-removed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::ImageRemoved(const Team::ImageEvent& event)
{
}


/**
 * @brief Default no-op for the thread-state-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::ThreadStateChanged(const Team::ThreadEvent& event)
{
}


/**
 * @brief Default no-op for the thread-CPU-state-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::ThreadCpuStateChanged(const Team::ThreadEvent& event)
{
}


/**
 * @brief Default no-op for the thread-stack-trace-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::ThreadStackTraceChanged(const Team::ThreadEvent& event)
{
}


/**
 * @brief Default no-op for the image-debug-info-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::ImageDebugInfoChanged(const Team::ImageEvent& event)
{
}


/**
 * @brief Default no-op for the stop-on-image-load settings callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::StopOnImageLoadSettingsChanged(
	const Team::ImageLoadEvent& event)
{
}


/**
 * @brief Default no-op for the stop-image-name-added callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::StopOnImageLoadNameAdded(const Team::ImageLoadNameEvent& event)
{
}


/**
 * @brief Default no-op for the stop-image-name-removed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::StopOnImageLoadNameRemoved(
	const Team::ImageLoadNameEvent& event)
{
}


/**
 * @brief Default no-op for the default-signal-disposition-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::DefaultSignalDispositionChanged(
	const Team::DefaultSignalDispositionEvent& event)
{
}


/**
 * @brief Default no-op for the custom-signal-disposition-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::CustomSignalDispositionChanged(
	const Team::CustomSignalDispositionEvent& event)
{
}


/**
 * @brief Default no-op for the custom-signal-disposition-removed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::CustomSignalDispositionRemoved(
	const Team::CustomSignalDispositionEvent& event)
{
}


/**
 * @brief Default no-op for the console-output-received callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::ConsoleOutputReceived(const Team::ConsoleOutputEvent& event)
{
}


/**
 * @brief Default no-op for the breakpoint-added callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::BreakpointAdded(const Team::BreakpointEvent& event)
{
}


/**
 * @brief Default no-op for the breakpoint-removed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::BreakpointRemoved(const Team::BreakpointEvent& event)
{
}


/**
 * @brief Default no-op for the user-breakpoint-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::UserBreakpointChanged(const Team::UserBreakpointEvent& event)
{
}


/**
 * @brief Default no-op for the watchpoint-added callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::WatchpointAdded(const Team::WatchpointEvent& event)
{
}


/**
 * @brief Default no-op for the watchpoint-removed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::WatchpointRemoved(const Team::WatchpointEvent& event)
{
}


/**
 * @brief Default no-op for the watchpoint-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::WatchpointChanged(const Team::WatchpointEvent& event)
{
}


/**
 * @brief Default no-op for the debug-report-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::DebugReportChanged(const Team::DebugReportEvent& event)
{
}


/**
 * @brief Default no-op for the core-file-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::CoreFileChanged(const Team::CoreFileChangedEvent& event)
{
}


/**
 * @brief Default no-op for the memory-changed callback.
 *
 * @param event Event payload (unused in the default implementation).
 */
void
Team::Listener::MemoryChanged(const Team::MemoryChangedEvent& event)
{
}
