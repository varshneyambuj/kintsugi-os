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
 *   Copyright 2015-2018, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file Job.cpp
 *  @brief Implements the launch daemon job with process launching, port management, and dependency resolution. */


#include "Job.h"

#include <stdlib.h>

#include <Entry.h>
#include <Looper.h>
#include <Message.h>
#include <Roster.h>

#include <MessagePrivate.h>
#include <RosterPrivate.h>
#include <user_group.h>

#include "Target.h"
#include "Utility.h"


/**
 * @brief Constructs a new job with the given name and default disabled state.
 *
 * @param name The unique name identifying this job.
 */
Job::Job(const char* name)
	:
	BaseJob(name),
	fEnabled(true),
	fService(false),
	fCreateDefaultPort(false),
	fLaunching(false),
	fInitStatus(B_NO_INIT),
	fTeam(-1),
	fDefaultPort(-1),
	fToken((uint32)B_PREFERRED_TOKEN),
	fLaunchStatus(B_NO_INIT),
	fTarget(NULL),
	fPendingLaunchDataReplies(0),
	fTeamListener(NULL)
{
	mutex_init(&fLaunchStatusLock, "launch status lock");
}


/**
 * @brief Copy-constructs a job, duplicating configuration but resetting runtime state.
 *
 * Copies arguments, requirements, ports (without port IDs), conditions,
 * environment, and source files from @a other. Runtime state such as team ID,
 * default port, and launch status are reset.
 *
 * @param other The job to copy from.
 */
Job::Job(const Job& other)
	:
	BaseJob(other.Name()),
	fEnabled(other.IsEnabled()),
	fService(other.IsService()),
	fCreateDefaultPort(other.CreateDefaultPort()),
	fLaunching(other.IsLaunching()),
	fInitStatus(B_NO_INIT),
	fTeam(-1),
	fDefaultPort(-1),
	fToken((uint32)B_PREFERRED_TOKEN),
	fLaunchStatus(B_NO_INIT),
	fTarget(other.Target()),
	fPendingLaunchDataReplies(0)
{
	mutex_init(&fLaunchStatusLock, "launch status lock");

	fCondition = other.fCondition;
	// TODO: copy events
	//fEvent = other.fEvent;
	fEnvironment = other.fEnvironment;
	fSourceFiles = other.fSourceFiles;

	for (int32 i = 0; i < other.Arguments().CountStrings(); i++)
		AddArgument(other.Arguments().StringAt(i));

	for (int32 i = 0; i < other.Requirements().CountStrings(); i++)
		AddRequirement(other.Requirements().StringAt(i));

	PortMap::const_iterator constIterator = other.Ports().begin();
	for (; constIterator != other.Ports().end(); constIterator++) {
		fPortMap.insert(
			std::make_pair(constIterator->first, constIterator->second));
	}

	PortMap::iterator iterator = fPortMap.begin();
	for (; iterator != fPortMap.end(); iterator++)
		iterator->second.RemoveData("port");
}


/** @brief Destroys the job and deletes all owned ports. */
Job::~Job()
{
	_DeletePorts();
}


/**
 * @brief Returns the team listener for this job.
 *
 * @return The current TeamListener, or NULL if none is set.
 */
::TeamListener*
Job::TeamListener() const
{
	return fTeamListener;
}


/**
 * @brief Sets the listener to be notified when this job's team is launched.
 *
 * @param listener The TeamListener to set, or NULL to clear.
 */
void
Job::SetTeamListener(::TeamListener* listener)
{
	fTeamListener = listener;
}


/** @brief Returns whether this job is enabled. */
bool
Job::IsEnabled() const
{
	return fEnabled;
}


/**
 * @brief Enables or disables this job.
 *
 * @param enable @c true to enable, @c false to disable.
 */
void
Job::SetEnabled(bool enable)
{
	fEnabled = enable;
}


/** @brief Returns whether this job is a persistent service. */
bool
Job::IsService() const
{
	return fService;
}


/**
 * @brief Marks this job as a service (auto-restartable) or a one-shot job.
 *
 * @param service @c true to mark as a service, @c false for a one-shot job.
 */
void
Job::SetService(bool service)
{
	fService = service;
}


/** @brief Returns whether a default port should be created when this job launches. */
bool
Job::CreateDefaultPort() const
{
	return fCreateDefaultPort;
}


/**
 * @brief Sets whether a default port should be created for this job.
 *
 * @param createPort @c true to create a default port on launch.
 */
void
Job::SetCreateDefaultPort(bool createPort)
{
	fCreateDefaultPort = createPort;
}


/**
 * @brief Adds a named port definition to this job's port map.
 *
 * @param data A BMessage containing the port configuration, including a "name" string.
 */
void
Job::AddPort(BMessage& data)
{
	const char* name = data.GetString("name");
	fPortMap.insert(std::pair<BString, BMessage>(BString(name), data));
}


/** @brief Returns the command-line argument list (const version). */
const BStringList&
Job::Arguments() const
{
	return fArguments;
}


/** @brief Returns the command-line argument list (mutable version). */
BStringList&
Job::Arguments()
{
	return fArguments;
}


/**
 * @brief Appends a command-line argument for this job's launch.
 *
 * @param argument The argument string to add.
 */
void
Job::AddArgument(const char* argument)
{
	fArguments.Add(argument);
}


/** @brief Returns the launch target this job belongs to, or NULL. */
::Target*
Job::Target() const
{
	return fTarget;
}


/**
 * @brief Assigns this job to a launch target.
 *
 * @param target The target to associate with, or NULL to clear.
 */
void
Job::SetTarget(::Target* target)
{
	fTarget = target;
}


/** @brief Returns the list of dependency names for this job (const version). */
const BStringList&
Job::Requirements() const
{
	return fRequirements;
}


/** @brief Returns the list of dependency names for this job (mutable version). */
BStringList&
Job::Requirements()
{
	return fRequirements;
}


/**
 * @brief Adds a named dependency requirement for this job.
 *
 * @param requirement The name of the job or target that must be satisfied first.
 */
void
Job::AddRequirement(const char* requirement)
{
	fRequirements.Add(requirement);
}


/** @brief Returns the list of jobs pending on this one (const version). */
const BStringList&
Job::Pending() const
{
	return fPendingJobs;
}


/** @brief Returns the list of jobs pending on this one (mutable version). */
BStringList&
Job::Pending()
{
	return fPendingJobs;
}


/**
 * @brief Adds a pending job name that will be launched after this one completes.
 *
 * @param pending The name of the job to launch after this one.
 */
void
Job::AddPending(const char* pending)
{
	fPendingJobs.Add(pending);
}


/**
 * @brief Tests whether this job's conditions are met, including target launch state.
 *
 * Returns @c false if this job has a target that has not yet launched.
 * Otherwise delegates to BaseJob::CheckCondition().
 *
 * @param context The condition evaluation context.
 * @return @c true if conditions are met, @c false otherwise.
 */
bool
Job::CheckCondition(ConditionContext& context) const
{
	if (Target() != NULL && !Target()->HasLaunched())
		return false;

	return BaseJob::CheckCondition(context);
}


/**
 * @brief Initializes the job by resolving its dependency graph.
 *
 * Recursively initializes all required dependencies, detecting cyclic
 * dependencies and registering BJob-level dependencies so the job queue
 * respects the ordering.
 *
 * @param finder       The resolver used to look up jobs and targets by name.
 * @param dependencies Accumulator set for detecting cyclic dependencies.
 * @return B_OK on success, or an error code if initialization fails.
 */
status_t
Job::Init(const Finder& finder, std::set<BString>& dependencies)
{
	// Only initialize the jobs once
	if (fInitStatus != B_NO_INIT)
		return fInitStatus;

	fInitStatus = B_OK;

	if (fTarget != NULL)
		fTarget->AddDependency(this);

	// Check dependencies

	for (int32 index = 0; index < Requirements().CountStrings(); index++) {
		const BString& requirement = Requirements().StringAt(index);
		if (dependencies.find(requirement) != dependencies.end()) {
			// Found a cyclic dependency
			// TODO: log error
			return fInitStatus = B_ERROR;
		}
		dependencies.insert(requirement);

		Job* dependency = finder.FindJob(requirement);
		if (dependency != NULL) {
			std::set<BString> subDependencies = dependencies;

			fInitStatus = dependency->Init(finder, subDependencies);
			if (fInitStatus != B_OK) {
				// TODO: log error
				return fInitStatus;
			}

			fInitStatus = _AddRequirement(dependency);
		} else {
			::Target* target = finder.FindTarget(requirement);
			if (target != NULL)
				fInitStatus = _AddRequirement(dependency);
			else {
				// Could not find dependency
				fInitStatus = B_NAME_NOT_FOUND;
			}
		}
		if (fInitStatus != B_OK) {
			// TODO: log error
			return fInitStatus;
		}
	}

	return fInitStatus;
}


/**
 * @brief Returns the initialization status of this job.
 *
 * @return B_OK if Init() succeeded, B_NO_INIT if not yet initialized,
 *         or another error code.
 */
status_t
Job::InitCheck() const
{
	return fInitStatus;
}


/** @brief Returns the team ID of the launched process, or -1 if not running. */
team_id
Job::Team() const
{
	return fTeam;
}


/** @brief Returns the port map containing all named ports for this job. */
const PortMap&
Job::Ports() const
{
	return fPortMap;
}


/**
 * @brief Looks up a named port ID from this job's port map.
 *
 * @param name The name of the port to find.
 * @return The port ID, or B_NAME_NOT_FOUND if no such port exists.
 */
port_id
Job::Port(const char* name) const
{
	PortMap::const_iterator found = fPortMap.find(name);
	if (found != fPortMap.end())
		return found->second.GetInt32("port", -1);

	return B_NAME_NOT_FOUND;
}


/** @brief Returns the default (unnamed) port ID, or -1 if none. */
port_id
Job::DefaultPort() const
{
	return fDefaultPort;
}


/**
 * @brief Sets the default port and updates the first unnamed entry in the port map.
 *
 * @param port The port ID to set as the default.
 */
void
Job::SetDefaultPort(port_id port)
{
	fDefaultPort = port;

	PortMap::iterator iterator = fPortMap.begin();
	for (; iterator != fPortMap.end(); iterator++) {
		BString name;
		if (iterator->second.HasString("name"))
			continue;

		iterator->second.SetInt32("port", (int32)port);
		break;
	}
}


/**
 * @brief Launches the job's process, building its environment and argument vector.
 *
 * Constructs the process environment by merging the system environ, target
 * environment, job environment, and source-file environment. If no arguments
 * are configured, launches by application signature; otherwise resolves the
 * first argument as an entry_ref and launches directly.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
Job::Launch()
{
	// Build environment

	std::vector<const char*> environment;
	for (const char** variable = (const char**)environ; variable[0] != NULL;
			variable++) {
		environment.push_back(variable[0]);
	}

	if (Target() != NULL)
		_AddStringList(environment, Target()->Environment());
	_AddStringList(environment, Environment());

	// Resolve source files
	BStringList sourceFilesEnvironment;
	GetSourceFilesEnvironment(sourceFilesEnvironment);
	_AddStringList(environment, sourceFilesEnvironment);

	environment.push_back(NULL);

	if (fArguments.IsEmpty()) {
		// Launch by signature
		BString signature("application/");
		signature << Name();

		return _Launch(signature.String(), NULL, 0, NULL, &environment[0]);
	}

	// Build argument vector

	entry_ref ref;
	status_t status = get_ref_for_path(
		Utility::TranslatePath(fArguments.StringAt(0).String()), &ref);
	if (status != B_OK) {
		_SetLaunchStatus(status);
		return status;
	}

	std::vector<BString> strings;
	std::vector<const char*> args;

	size_t count = fArguments.CountStrings() - 1;
	if (count > 0) {
		for (int32 i = 1; i < fArguments.CountStrings(); i++) {
			strings.push_back(Utility::TranslatePath(fArguments.StringAt(i)));
			args.push_back(strings.back());
		}
		args.push_back(NULL);
	}

	// Launch via entry_ref
	return _Launch(NULL, &ref, count, &args[0], &environment[0]);
}


/** @brief Returns whether this job has been launched (successfully or not). */
bool
Job::IsLaunched() const
{
	return fLaunchStatus != B_NO_INIT;
}


/** @brief Returns whether this job's process is currently running. */
bool
Job::IsRunning() const
{
	return fTeam >= 0;
}


/**
 * @brief Resets runtime state after the job's team has been deleted.
 *
 * Clears the team ID and default port. For services, resets the job
 * state to waiting so it can be relaunched.
 */
void
Job::TeamDeleted()
{
	fTeam = -1;
	fDefaultPort = -1;

	if (IsService())
		SetState(B_JOB_STATE_WAITING_TO_RUN);

	MutexLocker locker(fLaunchStatusLock);
	fLaunchStatus = B_NO_INIT;
}


/**
 * @brief Returns whether this job is eligible to be launched right now.
 *
 * A job can be launched if it is enabled, not already launching, and
 * (for services) not already running.
 *
 * @return @c true if the job can be launched.
 */
bool
Job::CanBeLaunched() const
{
	// Services cannot be launched while they are running
	return IsEnabled() && !IsLaunching() && (!IsService() || !IsRunning());
}


/** @brief Returns whether this job is currently in the process of launching. */
bool
Job::IsLaunching() const
{
	return fLaunching;
}


/**
 * @brief Sets the launching flag for this job.
 *
 * @param launching @c true to mark as launching, @c false to clear.
 */
void
Job::SetLaunching(bool launching)
{
	fLaunching = launching;
}


/**
 * @brief Handles a B_GET_LAUNCH_DATA request for this job's port information.
 *
 * If the job is already launched, replies immediately with port data.
 * Otherwise, queues the message for a deferred reply once the job launches.
 *
 * @param message The incoming request message (takes ownership on deferral).
 * @return B_OK on success, B_NOT_ALLOWED if the job is disabled, or B_NO_MEMORY.
 */
status_t
Job::HandleGetLaunchData(BMessage* message)
{
	MutexLocker launchLocker(fLaunchStatusLock);
	if (IsLaunched())
		return _SendLaunchDataReply(message);

	if (!IsEnabled())
		return B_NOT_ALLOWED;

	return fPendingLaunchDataReplies.AddItem(message) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Constructs a BMessenger targeting this job's running application.
 *
 * Looks up the running app info via BRoster and verifies it is not
 * pre-registered before constructing the messenger.
 *
 * @param messenger Output BMessenger that will target this job's team.
 * @return B_OK on success, or B_NAME_NOT_FOUND if the job is not accessible.
 */
status_t
Job::GetMessenger(BMessenger& messenger)
{
	if (fDefaultPort < 0)
		return B_NAME_NOT_FOUND;

	app_info info;
	status_t status = be_roster->GetRunningAppInfo(fTeam, &info);
	if (status != B_OK)
		return B_NAME_NOT_FOUND;

	bool preRegistered = false;
	status = BRoster::Private().IsAppRegistered(&info.ref, info.team, fToken, &preRegistered, NULL);
	if (status != B_OK || preRegistered)
		return B_NAME_NOT_FOUND;

	BMessenger::Private(messenger).SetTo(fTeam, info.port, fToken);
	return B_OK;
}


/**
 * @brief Runs this job via the BJob framework and resets non-service jobs for potential relaunch.
 *
 * @return The status code from BJob::Run().
 */
status_t
Job::Run()
{
	status_t status = BJob::Run();

	// Jobs can be relaunched at any time
	if (!IsService())
		SetState(B_JOB_STATE_WAITING_TO_RUN);

	return status;
}


/**
 * @brief Executes the job by launching its process (unless it is an already-running service).
 *
 * @return B_OK on success, or an error code from Launch().
 */
status_t
Job::Execute()
{
	status_t status = B_OK;
	if (!IsRunning() || !IsService())
		status = Launch();
	else
		debug_printf("Ignore launching %s\n", Name());

	fLaunching = false;
	return status;
}


/**
 * @brief Deletes all kernel ports owned by this job.
 */
void
Job::_DeletePorts()
{
	PortMap::const_iterator iterator = Ports().begin();
	for (; iterator != Ports().end(); iterator++) {
		port_id port = iterator->second.GetInt32("port", -1);
		if (port >= 0)
			delete_port(port);
	}
}


/**
 * @brief Registers a BJob-level dependency based on the dependency's current state.
 *
 * If the dependency is still pending, adds it as a direct dependency.
 * If it has already succeeded, does nothing. If it failed or was aborted,
 * returns an error.
 *
 * @param dependency The dependency job (may be NULL, which is a no-op).
 * @return B_OK on success, or B_BAD_VALUE if the dependency failed.
 */
status_t
Job::_AddRequirement(BJob* dependency)
{
	if (dependency == NULL)
		return B_OK;

	switch (dependency->State()) {
		case B_JOB_STATE_WAITING_TO_RUN:
		case B_JOB_STATE_STARTED:
		case B_JOB_STATE_IN_PROGRESS:
			AddDependency(dependency);
			break;

		case B_JOB_STATE_SUCCEEDED:
			// Just queue it without any dependencies
			break;

		case B_JOB_STATE_FAILED:
		case B_JOB_STATE_ABORTED:
			// TODO: return appropriate error
			return B_BAD_VALUE;
	}

	return B_OK;
}


/**
 * @brief Appends all strings from a BStringList to a raw C-string array.
 *
 * @param array The target vector to append to.
 * @param list  The source string list.
 */
void
Job::_AddStringList(std::vector<const char*>& array, const BStringList& list)
{
	int32 count = list.CountStrings();
	for (int32 index = 0; index < count; index++) {
		array.push_back(list.StringAt(index).String());
	}
}


/**
 * @brief Records the final launch status and sends any pending launch data replies.
 *
 * @param launchStatus The result of the launch attempt.
 */
void
Job::_SetLaunchStatus(status_t launchStatus)
{
	MutexLocker launchLocker(fLaunchStatusLock);
	fLaunchStatus = launchStatus != B_NO_INIT ? launchStatus : B_ERROR;
	launchLocker.Unlock();

	_SendPendingLaunchDataReplies();
}


/**
 * @brief Sends a launch data reply containing team ID and port information.
 *
 * Constructs a reply BMessage with the team ID and all port IDs, sends
 * it, and deletes the original request message.
 *
 * @param message The original request message to reply to (takes ownership).
 * @return B_OK always.
 */
status_t
Job::_SendLaunchDataReply(BMessage* message)
{
	BMessage reply(fTeam < 0 ? fTeam : (uint32)B_OK);
	if (reply.what == B_OK) {
		reply.AddInt32("team", fTeam);

		PortMap::const_iterator iterator = fPortMap.begin();
		for (; iterator != fPortMap.end(); iterator++) {
			BString name;
			if (iterator->second.HasString("name"))
				name << iterator->second.GetString("name") << "_";
			name << "port";

			reply.AddInt32(name.String(),
				iterator->second.GetInt32("port", -1));
		}
	}

	message->SendReply(&reply);
	delete message;
	return B_OK;
}


/**
 * @brief Sends launch data replies to all deferred requestors and clears the pending list.
 */
void
Job::_SendPendingLaunchDataReplies()
{
	for (int32 i = 0; i < fPendingLaunchDataReplies.CountItems(); i++)
		_SendLaunchDataReply(fPendingLaunchDataReplies.ItemAt(i));

	fPendingLaunchDataReplies.MakeEmpty();
}


/*!	Creates the ports for a newly launched job. If the registrar already
	pre-registered the application, \c fDefaultPort will already be set, and
	honored when filling the ports message.
*/
status_t
Job::_CreateAndTransferPorts()
{
	// TODO: prefix system ports with "system:"

	bool defaultPort = false;

	for (PortMap::iterator iterator = fPortMap.begin();
			iterator != fPortMap.end(); iterator++) {
		BString name(Name());
		const char* suffix = iterator->second.GetString("name");
		if (suffix != NULL)
			name << ':' << suffix;
		else
			defaultPort = true;

		const int32 capacity = iterator->second.GetInt32("capacity",
			B_LOOPER_PORT_DEFAULT_CAPACITY);

		port_id port = -1;
		if (suffix != NULL || fDefaultPort < 0) {
			port = _CreateAndTransferPort(name.String(), capacity);
			if (port < 0)
				return port;

			if (suffix == NULL)
				fDefaultPort = port;
		} else if (suffix == NULL)
			port = fDefaultPort;

		iterator->second.SetInt32("port", port);

		if (name == "x-vnd.haiku-registrar:auth") {
			// Allow the launch_daemon to access the registrar authentication
			BPrivate::set_registrar_authentication_port(port);
		}
	}

	if (fCreateDefaultPort && !defaultPort) {
		BMessage data;
		data.AddInt32("capacity", B_LOOPER_PORT_DEFAULT_CAPACITY);

		port_id port = -1;
		if (fDefaultPort < 0) {
			port = _CreateAndTransferPort(Name(),
				B_LOOPER_PORT_DEFAULT_CAPACITY);
			if (port < 0)
				return port;

			fDefaultPort = port;
		} else
			port = fDefaultPort;

		data.SetInt32("port", port);
		AddPort(data);
	}

	return B_OK;
}


/**
 * @brief Creates a kernel port and transfers ownership to this job's team.
 *
 * @param name     The human-readable name for the port.
 * @param capacity The message capacity of the port.
 * @return The new port ID on success, or a negative error code.
 */
port_id
Job::_CreateAndTransferPort(const char* name, int32 capacity)
{
	port_id port = create_port(B_LOOPER_PORT_DEFAULT_CAPACITY, Name());
	if (port < 0)
		return port;

	status_t status = set_port_owner(port, fTeam);
	if (status != B_OK) {
		delete_port(port);
		return status;
	}

	return port;
}


/**
 * @brief Performs the actual process launch via BRoster::Private::Launch().
 *
 * Creates and transfers ports, resumes the main thread on success, or
 * kills it on port-creation failure. Notifies the team listener and
 * records the launch status.
 *
 * @param signature   Application signature (used if @a ref is NULL).
 * @param ref         Entry reference to the executable (may be NULL).
 * @param argCount    Number of command-line arguments.
 * @param args        Array of argument strings.
 * @param environment NULL-terminated array of environment strings.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Job::_Launch(const char* signature, entry_ref* ref, int argCount,
	const char* const* args, const char** environment)
{
	thread_id mainThread = -1;
	status_t result = BRoster::Private().Launch(signature, ref, NULL, argCount,
		args, environment, &fTeam, &mainThread, &fDefaultPort, NULL, true);
	if (result == B_OK) {
		result = _CreateAndTransferPorts();

		if (result == B_OK) {
			resume_thread(mainThread);

			if (fTeamListener != NULL)
				fTeamListener->TeamLaunched(this, result);
		} else
			kill_thread(mainThread);
	}

	_SetLaunchStatus(result);
	return result;
}
