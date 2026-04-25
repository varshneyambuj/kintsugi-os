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
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TargetHostInterface.cpp
 * @brief Per-host coordination layer that owns the active TeamDebugger objects
 *        and the listener fan-out for one debugging transport.
 *
 * Subclasses (LocalTargetHostInterface, NetworkTargetHostInterface) implement
 * the actual attach/spawn/load-core primitives; this file factors out the
 * BLooper plumbing, debugger bookkeeping, and restart handling that is the
 * same regardless of transport.
 */


#include "TargetHostInterface.h"

#include <stdio.h>

#include <AutoLocker.h>

#include "DebuggerInterface.h"
#include "MessageCodes.h"
#include "TeamDebugger.h"


// #pragma mark - TeamDebuggerOptions


/**
 * @brief Default-initializes the options struct with sentinel values.
 *
 * Callers populate the fields relevant to their request type; sentinel values
 * (-1 ids, NULL pointers, UNKNOWN request type) make it clear when a field
 * has not been set.
 */
TeamDebuggerOptions::TeamDebuggerOptions()
	:
	requestType(TEAM_DEBUGGER_REQUEST_UNKNOWN),
	commandLineArgc(0),
	commandLineArgv(NULL),
	team(-1),
	thread(-1),
	settingsManager(NULL),
	userInterface(NULL),
	coreFilePath(NULL)
{
}


// #pragma mark - TargetHostInterface


/**
 * @brief Constructs the interface as a BLooper with empty listener and debugger lists.
 */
TargetHostInterface::TargetHostInterface()
	:
	BLooper(),
	fListeners(),
	fTeamDebuggers(20)
{
}


/**
 * @brief Notifies all attached listeners that the interface is going away.
 */
TargetHostInterface::~TargetHostInterface()
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->TargetHostInterfaceQuit(this);
	}
}


/**
 * @brief Entry point that starts a debugger session described by @a options.
 *
 * Handles team creation, attach-by-thread fallback, deduplication against an
 * already-running debugger for the same team, and finally delegation to
 * _StartTeamDebugger() to spin up the back-end and the TeamDebugger looper.
 *
 * @param options  Fully-populated options struct describing the request.
 * @return B_OK on success, B_BAD_VALUE if neither a team nor a thread is
 *         specified, or any error propagated from the underlying transport.
 */
status_t
TargetHostInterface::StartTeamDebugger(const TeamDebuggerOptions& options)
{
	// we only want to stop in main for teams we're responsible for
	// creating ourselves.
	bool stopInMain = options.requestType == TEAM_DEBUGGER_REQUEST_CREATE;
	team_id team = options.team;
	thread_id thread = options.thread;

	AutoLocker<TargetHostInterface> interfaceLocker(this);
	if (options.requestType == TEAM_DEBUGGER_REQUEST_CREATE) {
		status_t error = CreateTeam(options.commandLineArgc,
			options.commandLineArgv, team);
		if (error != B_OK)
			return error;
		thread = team;
	}

	if (options.requestType != TEAM_DEBUGGER_REQUEST_LOAD_CORE) {

		if (team < 0 && thread < 0)
			return B_BAD_VALUE;

		if (team < 0) {
			status_t error = FindTeamByThread(thread, team);
			if (error != B_OK)
				return error;
		}

		TeamDebugger* debugger = FindTeamDebugger(team);
		if (debugger != NULL) {
			debugger->Activate();
			return B_OK;
		}
	}

	return _StartTeamDebugger(team, options, stopInMain);
}


/**
 * @brief Returns the number of TeamDebuggers currently owned by this interface.
 *
 * @return Count of debugger sessions.
 */
int32
TargetHostInterface::CountTeamDebuggers() const
{
	return fTeamDebuggers.CountItems();
}


/**
 * @brief Returns the TeamDebugger at @a index in the internal sorted list.
 *
 * @param index  Zero-based index into the debugger list.
 * @return Pointer to the TeamDebugger, or NULL if @a index is out of range.
 */
TeamDebugger*
TargetHostInterface::TeamDebuggerAt(int32 index) const
{
	return fTeamDebuggers.ItemAt(index);
}


/**
 * @brief Looks up a live (non-post-mortem) debugger session for @a team.
 *
 * @param team  Team id to search for.
 * @return Matching TeamDebugger or NULL if no live session exists.
 */
TeamDebugger*
TargetHostInterface::FindTeamDebugger(team_id team) const
{
	for (int32 i = 0; i < fTeamDebuggers.CountItems(); i++) {
		TeamDebugger* debugger = fTeamDebuggers.ItemAt(i);
		if (debugger->TeamID() == team && !debugger->IsPostMortem())
			return debugger;
	}

	return NULL;
}


/**
 * @brief Inserts @a debugger into the team-id-sorted debugger list.
 *
 * @param debugger  TeamDebugger to register.
 * @return B_OK on success, B_NO_MEMORY if the insert fails.
 */
status_t
TargetHostInterface::AddTeamDebugger(TeamDebugger* debugger)
{
	if (!fTeamDebuggers.BinaryInsert(debugger, &_CompareDebuggers))
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Removes @a debugger from the internal list (no-op if absent).
 *
 * @param debugger  TeamDebugger to unregister.
 */
void
TargetHostInterface::RemoveTeamDebugger(TeamDebugger* debugger)
{
	for (int32 i = 0; i < fTeamDebuggers.CountItems(); i++) {
		if (fTeamDebuggers.ItemAt(i) == debugger) {
			fTeamDebuggers.RemoveItemAt(i);
			break;
		}
	}
}


/**
 * @brief Registers @a listener for interface-lifecycle and team-debugger events.
 *
 * @param listener  Listener instance owned by the caller.
 */
void
TargetHostInterface::AddListener(Listener* listener)
{
	AutoLocker<TargetHostInterface> interfaceLocker(this);
	fListeners.Add(listener);
}


/**
 * @brief Unregisters a previously-added listener.
 *
 * @param listener  Listener to remove.
 */
void
TargetHostInterface::RemoveListener(Listener* listener)
{
	AutoLocker<TargetHostInterface> interfaceLocker(this);
	fListeners.Remove(listener);
}


/**
 * @brief Quits the BLooper only if no debugger sessions remain.
 *
 * @note  Suppresses Quit() while there are still active debuggers so they get
 *        a chance to finish unwinding.
 */
void
TargetHostInterface::Quit()
{
	if (fTeamDebuggers.CountItems() == 0)
		BLooper::Quit();
}


/**
 * @brief Routes incoming messages for debugger-quit and team-restart requests.
 *
 * Handles two main internal codes:
 *  - MSG_TEAM_DEBUGGER_QUIT: blocks on the now-defunct debugger thread so
 *    its resources are reclaimed before the looper continues.
 *  - MSG_TEAM_RESTART_REQUESTED: clones the originating user interface and
 *    spawns a fresh TeamDebugger for the same arguments, then asks the old
 *    debugger to quit.
 * Anything else is forwarded to BLooper::MessageReceived().
 *
 * @param message  Incoming message; must be non-NULL.
 */
void
TargetHostInterface::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case MSG_TEAM_DEBUGGER_QUIT:
	{
		thread_id thread;
		if (message->FindInt32("thread", &thread) == B_OK)
			wait_for_thread(thread, NULL);
		break;
	}
	case MSG_TEAM_RESTART_REQUESTED:
	{
		int32 teamID;
		if (message->FindInt32("team", &teamID) != B_OK)
			break;

		TeamDebugger* debugger = FindTeamDebugger(teamID);

		UserInterface* userInterface = debugger->GetUserInterface()->Clone();
		if (userInterface == NULL)
			break;

		BReference<UserInterface> userInterfaceReference(userInterface, true);

		TeamDebuggerOptions options;
		options.requestType = TEAM_DEBUGGER_REQUEST_CREATE;
		options.commandLineArgc = debugger->ArgumentCount();
		options.commandLineArgv = debugger->Arguments();
		options.settingsManager = debugger->GetSettingsManager();
		options.userInterface = userInterface;
		status_t result = StartTeamDebugger(options);
		if (result == B_OK) {
			userInterfaceReference.Detach();
			debugger->PostMessage(B_QUIT_REQUESTED);
		}
		break;
	}
	default:
		BLooper::MessageReceived(message);
		break;
	}
}


/**
 * @brief Records a freshly-started TeamDebugger and notifies listeners.
 *
 * @param debugger  Newly-running TeamDebugger.
 */
void
TargetHostInterface::TeamDebuggerStarted(TeamDebugger* debugger)
{
	AutoLocker<TargetHostInterface> locker(this);
	AddTeamDebugger(debugger);
	_NotifyTeamDebuggerStarted(debugger);
}


/**
 * @brief Posts a restart message to the looper for asynchronous handling.
 *
 * @param debugger  TeamDebugger requesting a restart.
 */
void
TargetHostInterface::TeamDebuggerRestartRequested(TeamDebugger* debugger)
{
	BMessage message(MSG_TEAM_RESTART_REQUESTED);
	message.AddInt32("team", debugger->TeamID());
	PostMessage(&message);
}


/**
 * @brief Removes @a debugger from the list and queues a join on its thread.
 *
 * @param debugger  TeamDebugger that has finished.
 */
void
TargetHostInterface::TeamDebuggerQuit(TeamDebugger* debugger)
{
	AutoLocker<TargetHostInterface> interfaceLocker(this);
	RemoveTeamDebugger(debugger);

	if (debugger->Thread() >= 0) {
		_NotifyTeamDebuggerQuit(debugger);
		BMessage message(MSG_TEAM_DEBUGGER_QUIT);
		message.AddInt32("thread", debugger->Thread());
		PostMessage(&message);
	}
}


/**
 * @brief Performs the back-end attach (or core load) and constructs the TeamDebugger.
 *
 * @param teamID      Resolved team identifier.
 * @param options     Caller's options struct (already vetted by StartTeamDebugger).
 * @param stopInMain  True to ask the new debugger to halt at main(); only used
 *                    for teams the debugger spawned itself.
 * @return B_OK on success, B_BAD_VALUE if no user interface was supplied, or
 *         any error from the underlying transport or TeamDebugger::Init().
 */
status_t
TargetHostInterface::_StartTeamDebugger(team_id teamID,
	const TeamDebuggerOptions& options, bool stopInMain)
{
	UserInterface* userInterface = options.userInterface;
	if (userInterface == NULL) {
		fprintf(stderr, "Error: Requested team debugger start without "
			"valid user interface!\n");
		return B_BAD_VALUE;
	}

	thread_id threadID = options.thread;
	if (options.commandLineArgv != NULL)
		threadID = teamID;

	DebuggerInterface* interface = NULL;
	TeamDebugger* debugger = NULL;
	status_t error = B_OK;
	if (options.requestType != TEAM_DEBUGGER_REQUEST_LOAD_CORE) {
		error = Attach(teamID, options.thread, interface);
		if (error != B_OK) {
			fprintf(stderr, "Error: Failed to attach to team %" B_PRId32
				": %s!\n", teamID, strerror(error));
			return error;
		}
	} else {
		error = LoadCore(options.coreFilePath, interface, threadID);
		if (error != B_OK) {
			fprintf(stderr, "Error: Failed to load core file '%s': %s!\n",
				options.coreFilePath, strerror(error));
			return error;
		}
	}

	BReference<DebuggerInterface> debuggerInterfaceReference(interface,
		true);
	debugger = new(std::nothrow) TeamDebugger(this, userInterface,
		options.settingsManager);
	if (debugger != NULL) {
		error = debugger->Init(interface, threadID,
			options.commandLineArgc, options.commandLineArgv, stopInMain);
	}

	if (error != B_OK) {
		printf("Error: debugger for team %" B_PRId32 " on interface %s failed"
			" to init: %s!\n", interface->TeamID(), Name(), strerror(error));
		delete debugger;
		debugger = NULL;
	} else {
		printf("debugger for team %" B_PRId32 " on interface %s created and"
			" initialized successfully!\n", interface->TeamID(), Name());
	}

	return error;
}


/**
 * @brief Notifies every registered listener that a debugger session has started.
 *
 * @param debugger  Newly-started TeamDebugger.
 */
void
TargetHostInterface::_NotifyTeamDebuggerStarted(TeamDebugger* debugger)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->TeamDebuggerStarted(debugger);
	}
}


/**
 * @brief Notifies every registered listener that a debugger session has ended.
 *
 * @param debugger  TeamDebugger that has finished.
 */
void
TargetHostInterface::_NotifyTeamDebuggerQuit(TeamDebugger* debugger)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->TeamDebuggerQuit(debugger);
	}
}


/**
 * @brief Comparison callback for keeping the debugger list sorted by team id.
 *
 * @param a  Left-hand TeamDebugger.
 * @param b  Right-hand TeamDebugger.
 * @return -1 if @a a precedes @a b, 1 otherwise.
 * @note Equal team ids cannot occur because FindTeamDebugger() rejects
 *       duplicates earlier; the binary `< -1 : 1` form is intentional.
 */
/*static*/ int
TargetHostInterface::_CompareDebuggers(const TeamDebugger* a,
	const TeamDebugger* b)
{
	return a->TeamID() < b->TeamID() ? -1 : 1;
}


// #pragma mark - TargetHostInterface::Listener


/**
 * @brief Virtual destructor for the listener interface.
 */
TargetHostInterface::Listener::~Listener()
{
}


/**
 * @brief Default no-op hook called when a TeamDebugger starts on this interface.
 *
 * @param debugger  Newly-running TeamDebugger; ignored.
 */
void
TargetHostInterface::Listener::TeamDebuggerStarted(TeamDebugger* debugger)
{
}


/**
 * @brief Default no-op hook called when a TeamDebugger ends on this interface.
 *
 * @param debugger  TeamDebugger that has quit; ignored.
 */
void
TargetHostInterface::Listener::TeamDebuggerQuit(TeamDebugger* debugger)
{
}


/**
 * @brief Default no-op hook called when the host interface itself is shutting down.
 *
 * @param interface  Interface that is quitting; ignored.
 */
void
TargetHostInterface::Listener::TargetHostInterfaceQuit(
	TargetHostInterface* interface)
{
}
