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
 *   Copyright 2007 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ramshankar, v.ramshankar@gmail.com
 *       Stephan Aßmus <superstippi@gmx.de>
 */

/** @file CommandPipe.cpp
 *  @brief Implementation of BCommandPipe, a utility class for executing
 *         shell commands and capturing their stdout/stderr output.
 */

//! BCommandPipe class to handle reading shell output
//  (stdout/stderr) of other programs into memory.
#include "CommandPipe.h"

#include <stdlib.h>
#include <unistd.h>

#include <image.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>


/** @brief Default constructor. Initializes the pipe with no arguments and
 *         closed stdout/stderr descriptors.
 */
BCommandPipe::BCommandPipe()
	:
	fStdOutOpen(false),
	fStdErrOpen(false)
{
}


/** @brief Destructor. Flushes all queued arguments and closes any open
 *         pipe file descriptors.
 */
BCommandPipe::~BCommandPipe()
{
	FlushArgs();
}


/** @brief Appends a command-line argument to the internal argument list.
 *
 *  @param arg  A null-terminated string representing the argument to add.
 *              Must not be NULL or empty.
 *  @return B_OK on success, B_BAD_VALUE if @p arg is NULL or empty,
 *          or B_NO_MEMORY if memory allocation fails.
 */
status_t
BCommandPipe::AddArg(const char* arg)
{
	if (arg == NULL || arg[0] == '\0')
		return B_BAD_VALUE;

	char* argCopy = strdup(arg);
	if (argCopy == NULL)
		return B_NO_MEMORY;

	if (!fArgList.AddItem(reinterpret_cast<void*>(argCopy))) {
		free(argCopy);
		return B_NO_MEMORY;
	}

	return B_OK;
}


/** @brief Prints all queued arguments to standard output, separated by
 *         spaces, followed by a newline.
 */
void
BCommandPipe::PrintToStream() const
{
	for (int32 i = 0L; i < fArgList.CountItems(); i++)
		printf("%s ", reinterpret_cast<char*>(fArgList.ItemAtFast(i)));

	printf("\n");
}


/** @brief Clears all queued arguments and closes any open pipe descriptors.
 *         Frees all memory associated with stored argument strings.
 */
void
BCommandPipe::FlushArgs()
{
	// Delete all arguments from the list
	for (int32 i = fArgList.CountItems() - 1; i >= 0; i--)
		free(fArgList.ItemAtFast(i));
	fArgList.MakeEmpty();

	Close();
}


/** @brief Closes any open stdout/stderr pipe read-end file descriptors that
 *         were opened by a previous PipeInto() call.
 */
void
BCommandPipe::Close()
{
	if (fStdErrOpen) {
		close(fStdErr[0]);
		fStdErrOpen = false;
	}

	if (fStdOutOpen) {
		close(fStdOut[0]);
		fStdOutOpen = false;
	}
}


/** @brief Builds a NULL-terminated argv array from the current argument list.
 *
 *  The caller is responsible for freeing the returned array with free().
 *  The strings pointed to by the array elements are owned by this object
 *  and must not be freed by the caller.
 *
 *  @param argc  Output parameter set to the number of arguments.
 *  @return A heap-allocated, NULL-terminated array of C-string pointers.
 */
const char**
BCommandPipe::Argv(int32& argc) const
{
	// NOTE: Freeing is left to caller as indicated in the header!
	argc = fArgList.CountItems();
	const char** argv = reinterpret_cast<const char**>(
		malloc((argc + 1) * sizeof(char*)));
	for (int32 i = 0; i < argc; i++)
		argv[i] = reinterpret_cast<const char*>(fArgList.ItemAtFast(i));

	argv[argc] = NULL;
	return argv;
}


// #pragma mark -


/** @brief Launches the command with both stdout and stderr merged into a
 *         single pipe descriptor pair.
 *
 *  Both STDOUT_FILENO and STDERR_FILENO are redirected to the write end of
 *  the same pipe (@p stdOutAndErr[1]) before spawning the image.
 *
 *  @param stdOutAndErr  A two-element array that receives the pipe's read
 *                       ([0]) and write ([1]) file descriptors.
 *  @return The thread_id of the spawned application thread, or a negative
 *          error code on failure.
 */
thread_id
BCommandPipe::PipeAll(int* stdOutAndErr) const
{
	// This function pipes both stdout and stderr to the same filedescriptor
	// (stdOut)
	int oldStdOut;
	int oldStdErr;
	pipe(stdOutAndErr);
	oldStdOut = dup(STDOUT_FILENO);
	oldStdErr = dup(STDERR_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	// TODO: This looks broken, using "stdOutAndErr[1]" twice!
	dup2(stdOutAndErr[1], STDOUT_FILENO);
	dup2(stdOutAndErr[1], STDERR_FILENO);

	// Construct the argv vector
	int32 argc;
	const char** argv = Argv(argc);

	// Load the app image... and pass the args
	thread_id appThread = load_image((int)argc, argv,
		const_cast<const char**>(environ));

	dup2(oldStdOut, STDOUT_FILENO);
	dup2(oldStdErr, STDERR_FILENO);
	close(oldStdOut);
	close(oldStdErr);

	free(argv);

	return appThread;
}


/** @brief Launches the command with stdout and stderr redirected into two
 *         separate pipe descriptor pairs.
 *
 *  @param stdOut  A two-element array receiving the stdout pipe fds.
 *  @param stdErr  A two-element array receiving the stderr pipe fds.
 *  @return The thread_id of the spawned application thread, or a negative
 *          error code on failure.
 */
thread_id
BCommandPipe::Pipe(int* stdOut, int* stdErr) const
{
	int oldStdOut;
	int oldStdErr;
	pipe(stdOut);
	pipe(stdErr);
	oldStdOut = dup(STDOUT_FILENO);
	oldStdErr = dup(STDERR_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	dup2(stdOut[1], STDOUT_FILENO);
	dup2(stdErr[1], STDERR_FILENO);

	// Construct the argv vector
	int32 argc;
	const char** argv = Argv(argc);

	// Load the app image... and pass the args
	thread_id appThread = load_image((int)argc, argv, const_cast<
		const char**>(environ));

	dup2(oldStdOut, STDOUT_FILENO);
	dup2(oldStdErr, STDERR_FILENO);
	close(oldStdOut);
	close(oldStdErr);

	free(argv);

	return appThread;
}


/** @brief Launches the command redirecting only stdout; stderr is discarded.
 *
 *  @param stdOut  A two-element array receiving the stdout pipe fds.
 *  @return The thread_id of the spawned application thread, or a negative
 *          error code on failure.
 */
thread_id
BCommandPipe::Pipe(int* stdOut) const
{
	// Redirects only output (stdout) to caller, stderr is closed
	int stdErr[2];
	thread_id tid = Pipe(stdOut, stdErr);
	close(stdErr[0]);
	close(stdErr[1]);
	return tid;
}


/** @brief Launches the command and returns FILE* streams for both stdout
 *         and stderr.
 *
 *  Resumes the spawned thread immediately. The caller is responsible for
 *  closing the returned FILE* handles when done reading.
 *
 *  @param _out  Output parameter set to a readable FILE* for the command's
 *               stdout.
 *  @param _err  Output parameter set to a readable FILE* for the command's
 *               stderr.
 *  @return The thread_id of the spawned application thread, or a negative
 *          error code on failure.
 */
thread_id
BCommandPipe::PipeInto(FILE** _out, FILE** _err)
{
	Close();

	thread_id tid = Pipe(fStdOut, fStdErr);
	if (tid >= 0)
		resume_thread(tid);

	close(fStdErr[1]);
	close(fStdOut[1]);

	fStdOutOpen = true;
	fStdErrOpen = true;

	*_out = fdopen(fStdOut[0], "r");
	*_err = fdopen(fStdErr[0], "r");

	return tid;
}


/** @brief Launches the command and returns a single FILE* stream containing
 *         both stdout and stderr merged together.
 *
 *  Resumes the spawned thread immediately. The caller is responsible for
 *  closing the returned FILE* handle when done reading.
 *
 *  @param _outAndErr  Output parameter set to a readable FILE* carrying
 *                     both stdout and stderr.
 *  @return The thread_id of the spawned application thread, or a negative
 *          error code on failure.
 */
thread_id
BCommandPipe::PipeInto(FILE** _outAndErr)
{
	Close();

	thread_id tid = PipeAll(fStdOut);
	if (tid >= 0)
		resume_thread(tid);

	close(fStdOut[1]);
	fStdOutOpen = true;

	*_outAndErr = fdopen(fStdOut[0], "r");
	return tid;
}


// #pragma mark -


/** @brief Executes the command synchronously, discarding all output.
 *
 *  Behaves similarly to system() but uses explicit pipes and
 *  wait_for_thread(). Blocks until the command completes.
 */
void
BCommandPipe::Run()
{
	// Runs the command without bothering to redirect streams, this is similar
	// to system() but uses pipes and wait_for_thread.... Synchronous.
	int stdOut[2], stdErr[2];
	status_t exitCode;
	wait_for_thread(Pipe(stdOut, stdErr), &exitCode);

	close(stdOut[0]);
	close(stdErr[0]);
	close(stdOut[1]);
	close(stdErr[1]);
}


/** @brief Executes the command asynchronously, discarding all output.
 *
 *  Behaves similarly to system() but uses explicit pipes without waiting
 *  for the thread to complete.
 */
void
BCommandPipe::RunAsync()
{
	// Runs the command without bothering to redirect streams, this is similar
	// to system() but uses pipes.... Asynchronous.
	Close();
	FILE* f = NULL;
	PipeInto(&f);
	fclose(f);
}


// #pragma mark -


/** @brief Reads lines from @p file and dispatches each complete line to
 *         @p lineReader.
 *
 *  Reads until EOF or until lineReader->IsCanceled() returns true.
 *  Each newline-terminated line (including the newline) is passed to
 *  lineReader->ReadLine().
 *
 *  @param file        A readable FILE* to read from. Must not be NULL.
 *  @param lineReader  A LineReader callback object. Must not be NULL.
 *  @return B_OK on success, B_BAD_VALUE if either argument is NULL,
 *          B_CANCELED if the lineReader requested cancellation, or any
 *          error code returned by lineReader->ReadLine().
 */
status_t
BCommandPipe::ReadLines(FILE* file, LineReader* lineReader)
{
	// Reads output of file, line by line. Each line is passed to lineReader
	// for inspection, and the IsCanceled() method is repeatedly called.

	if (file == NULL || lineReader == NULL)
		return B_BAD_VALUE;

	BString line;

	while (!feof(file)) {
		if (lineReader->IsCanceled())
			return B_CANCELED;

		unsigned char c = fgetc(file);
			// TODO: fgetc() blocks, is there a way to make it timeout?

		if (c != 255)
			line << (char)c;

		if (c == '\n') {
			status_t ret = lineReader->ReadLine(line);
			if (ret != B_OK)
				return ret;
			line = "";
		}
	}

	return B_OK;
}


/** @brief Reads all lines from @p file and returns them concatenated as a
 *         single BString.
 *
 *  @param file  A readable FILE* to read from.
 *  @return A BString containing all output read from @p file.
 */
BString
BCommandPipe::ReadLines(FILE* file)
{
	class AllLinesReader : public LineReader {
	public:
		AllLinesReader()
			:
			fResult("")
		{
		}

		virtual bool IsCanceled()
		{
			return false;
		}

		virtual status_t ReadLine(const BString& line)
		{
			int lineLength = line.Length();
			int resultLength = fResult.Length();
			fResult << line;
			if (fResult.Length() != lineLength + resultLength)
				return B_NO_MEMORY;
			return B_OK;
		}

		BString Result() const
		{
			return fResult;
		}

	private:
		BString fResult;
	} lineReader;

	ReadLines(file, &lineReader);

	return lineReader.Result();
}


// #pragma mark -


/** @brief Stream-insertion operator that appends a C-string argument.
 *
 *  @param arg  A null-terminated C-string to append to the argument list.
 *  @return A reference to this object, enabling chaining.
 */
BCommandPipe&
BCommandPipe::operator<<(const char* arg)
{
	AddArg(arg);
	return *this;
}


/** @brief Stream-insertion operator that appends a BString argument.
 *
 *  @param arg  A BString whose content is appended to the argument list.
 *  @return A reference to this object, enabling chaining.
 */
BCommandPipe&
BCommandPipe::operator<<(const BString& arg)
{
	AddArg(arg.String());
	return *this;
}


/** @brief Stream-insertion operator that appends all arguments from another
 *         BCommandPipe.
 *
 *  @param arg  Another BCommandPipe whose argument list is appended to this
 *              object's argument list.
 *  @return A reference to this object, enabling chaining.
 */
BCommandPipe&
BCommandPipe::operator<<(const BCommandPipe& arg)
{
	int32 argc;
	const char** argv = arg.Argv(argc);
	for (int32 i = 0; i < argc; i++)
		AddArg(argv[i]);

	free(argv);
	return *this;
}
