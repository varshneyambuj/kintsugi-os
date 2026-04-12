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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TeamDebugger.cpp
 * @brief High-level team debugger that owns the debugger port and nub context.
 *
 * BTeamDebugger extends BDebugContext to provide the full lifecycle for
 * attaching a debugger to a running team or launching a new program under
 * debug control. It creates and owns the debugger port, installs the team
 * debugger, and exposes ReadDebugMessage() to synchronously receive the next
 * debug event from the port.
 *
 * @see BDebugContext, BDebugLooper, BDebugMessageHandler
 */


#include <TeamDebugger.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <string.h>

#include <Path.h>
#include <String.h>

#include <libroot_private.h>
#include <syscalls.h>
#include <syscall_load_image.h>


/**
 * @brief Default constructor — initialises the debugger port to an invalid value.
 */
BTeamDebugger::BTeamDebugger()
	:
	fDebuggerPort(-1)
{
}


/**
 * @brief Destructor — calls Uninstall() to remove the debugger and free the port.
 */
BTeamDebugger::~BTeamDebugger()
{
	Uninstall();
}


/**
 * @brief Attach this debugger to an existing team.
 *
 * Creates a debugger port, calls install_team_debugger() to redirect the
 * team's debug events to that port, and initialises the BDebugContext base.
 * On any failure the port is deleted and all state is rolled back.
 *
 * @param team  ID of the team to attach to.
 * @return B_OK on success; the negative port or install error on failure.
 */
status_t
BTeamDebugger::Install(team_id team)
{
	Uninstall();

	// create a debugger port
	char name[B_OS_NAME_LENGTH];
	snprintf(name, sizeof(name), "debugger for team %" B_PRId32, team);
	fDebuggerPort = create_port(100, name);
	if (fDebuggerPort < 0)
		return fDebuggerPort;

	port_id nubPort = install_team_debugger(team, fDebuggerPort);
	if (nubPort < 0) {
		delete_port(fDebuggerPort);
		fDebuggerPort = -1;
		return nubPort;
	}

	status_t error = BDebugContext::Init(team, nubPort);
	if (error != B_OK) {
		remove_team_debugger(team);
		delete_port(fDebuggerPort);
		fDebuggerPort = -1;
		return error;
	}

	return B_OK;
}


/**
 * @brief Detach from the currently debugged team and release all resources.
 *
 * Calls remove_team_debugger(), deletes the debugger port, and uninitialises
 * the BDebugContext base. Safe to call when not installed (returns B_BAD_VALUE).
 *
 * @return B_OK on success, B_BAD_VALUE if no team is currently being debugged.
 */
status_t
BTeamDebugger::Uninstall()
{
	if (Team() < 0)
		return B_BAD_VALUE;

	remove_team_debugger(Team());

	delete_port(fDebuggerPort);

	BDebugContext::Uninit();

	fDebuggerPort = -1;

	return B_OK;
}


/**
 * @brief Launch a program under debugger control.
 *
 * Loads the program using _LoadProgram() (which resolves the executable path
 * through PATH if necessary) and then calls Install() on the resulting team.
 * If Install() fails the newly spawned team is killed.
 *
 * @param args         NULL-terminated array of argument strings; args[0] is
 *                     the program name, resolved via PATH if not absolute.
 * @param argCount     Number of entries in @a args.
 * @param traceLoading When true, the program is loaded without waiting for it
 *                     to fully initialise (B_WAIT_TILL_LOADED is not passed).
 * @return B_OK on success, or an error from _LoadProgram() or Install().
 */
status_t
BTeamDebugger::LoadProgram(const char* const* args, int32 argCount,
	bool traceLoading)
{
	// load the program
	thread_id thread = _LoadProgram(args, argCount, traceLoading);
	if (thread < 0)
		return thread;

	// install the debugger
	status_t error = Install(thread);
	if (error != B_OK) {
		kill_team(thread);
		return error;
	}

	return B_OK;
}


/**
 * @brief Block until the next debug message is available on the debugger port.
 *
 * Reads one message from fDebuggerPort into @a messageBuffer and stores the
 * message code in @a _messageCode.
 *
 * @param _messageCode    Output — receives the B_DEBUGGER_MESSAGE_* code.
 * @param messageBuffer   Output — receives the full message data union.
 * @return B_OK on success, or the negative read_port error code.
 */
status_t
BTeamDebugger::ReadDebugMessage(int32& _messageCode,
	debug_debugger_message_data& messageBuffer)
{
	ssize_t bytesRead = read_port(fDebuggerPort, &_messageCode, &messageBuffer,
		sizeof(messageBuffer));
	if (bytesRead < 0)
		return bytesRead;

	return B_OK;
}


/**
 * @brief Internal helper — spawn the program and return its main thread ID.
 *
 * Resolves args[0] via _FindProgram(), flattens the argument vector and
 * environment into the format expected by _kern_load_image(), and launches
 * the program.
 *
 * @param args         NULL-terminated argument vector; args[0] is the program.
 * @param argCount     Number of entries in @a args.
 * @param traceLoading When true, does not wait for the image to finish loading.
 * @return The main thread_id of the new team on success, or a negative error.
 */
/*static*/ thread_id
BTeamDebugger::_LoadProgram(const char* const* args, int32 argCount,
	bool traceLoading)
{
	// clone the argument vector so that we can change it
	const char** mutableArgs = new const char*[argCount];
	for (int i = 0; i < argCount; i++)
		mutableArgs[i] = args[i];

	// resolve the program path
	BPath programPath;
	status_t error = _FindProgram(args[0], programPath);
	if (error != B_OK) {
		delete[] mutableArgs;
		return error;
	}
	mutableArgs[0] = programPath.Path();

	// count environment variables
	int32 envCount = 0;
	while (environ[envCount] != NULL)
		envCount++;

	// flatten the program args and environment
	char** flatArgs = NULL;
	size_t flatArgsSize;
	error = __flatten_process_args(mutableArgs, argCount, environ, &envCount,
		mutableArgs[0], &flatArgs, &flatArgsSize);

	// load the program
	thread_id thread;
	if (error == B_OK) {
		thread = _kern_load_image(flatArgs, flatArgsSize, argCount, envCount,
			B_NORMAL_PRIORITY, (traceLoading ? 0 : B_WAIT_TILL_LOADED), -1, 0);

		free(flatArgs);
	} else
		thread = error;

	delete[] mutableArgs;

	return thread;
}


/**
 * @brief Search PATH for the executable named @a programName.
 *
 * If @a programName is an absolute path or contains a '/', it is used
 * directly via BPath::SetTo(). Otherwise each colon-separated component of
 * the PATH environment variable is tried in order until a regular file is
 * found.
 *
 * @param programName   The bare program name or a path to resolve.
 * @param resolvedPath  Output — receives the full path of the found executable.
 * @return B_OK when the executable is located; B_ENTRY_NOT_FOUND if PATH is
 *         exhausted; B_NO_MEMORY if a BString allocation fails.
 */
/*static*/ status_t
BTeamDebugger::_FindProgram(const char* programName, BPath& resolvedPath)
{
    // If the program name is absolute, then there's nothing to do.
    // If the program name consists of more than one path element, then we
    // consider it a relative path and don't search in PATH either.
    if (*programName == '/' || strchr(programName, '/'))
        return resolvedPath.SetTo(programName);

    // get the PATH environment variable
    const char* paths = getenv("PATH");
    if (!paths)
        return B_ENTRY_NOT_FOUND;

    // iterate through the paths
    do {
        const char* pathEnd = strchr(paths, ':');
        int pathLen = (pathEnd ? pathEnd - paths : strlen(paths));

        // We skip empty paths.
        if (pathLen > 0) {
            // get the program path
			BString directory(paths, pathLen);
			if (directory.Length() == 0)
				return B_NO_MEMORY;

			BPath path;
			status_t error = path.SetTo(directory, programName);
			if (error != B_OK)
				continue;

            // stat() the path to be sure, there is a file
            struct stat st;
            if (stat(path.Path(), &st) == 0 && S_ISREG(st.st_mode)) {
            	resolvedPath = path;
                return B_OK;
            }
        }

        paths = (pathEnd ? pathEnd + 1 : NULL);
    } while (paths);

    // not found in PATH
    return B_ENTRY_NOT_FOUND;
}
