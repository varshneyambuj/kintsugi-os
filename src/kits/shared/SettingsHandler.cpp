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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Be Incorporated
 */

/** @file SettingsHandler.cpp
 *  @brief Text-file settings framework: ArgvParser reads a settings file into
 *         argv arrays; SettingsArgvDispatcher handles named settings tokens;
 *         Settings owns a list of dispatchers and coordinates load/save.
 */

#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <StopWatch.h>

#include <alloca.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>


#include "SettingsHandler.h"


//	#pragma mark - ArgvParser


/*! \class ArgvParser
	ArgvParser class opens a text file and passes the context in argv
	format to a specified handler
*/
/** @brief Constructs the parser and opens the named file for reading.
 *
 *  If the file cannot be opened, the parser is left in an invalid state
 *  (fFile == NULL) and subsequent calls return immediately.
 *
 *  @param name  Path of the settings file to parse.
 */
ArgvParser::ArgvParser(const char* name)
	:
	fFile(0),
	fBuffer(NULL),
	fPos(-1),
	fArgc(0),
	fCurrentArgv(0),
	fCurrentArgsPos(-1),
	fSawBackslash(false),
	fEatComment(false),
	fInDoubleQuote(false),
	fInSingleQuote(false),
	fLineNo(0),
	fFileName(name)
{
	fFile = fopen(fFileName, "r");
	if (!fFile) {
		PRINT(("Error opening %s\n", fFileName));
		return;
	}
	fBuffer = new char [kBufferSize];
	fCurrentArgv = new char * [1024];
}


/** @brief Destroys the parser, freeing all buffers and closing the file. */
ArgvParser::~ArgvParser()
{
	delete[] fBuffer;

	MakeArgvEmpty();
	delete[] fCurrentArgv;

	if (fFile)
		fclose(fFile);
}


/** @brief Frees all strings accumulated in the current argv array.
 *
 *  Resets fArgc to zero so the array is ready for the next line.
 */
void
ArgvParser::MakeArgvEmpty()
{
	// done with current argv, free it up
	for (int32 index = 0; index < fArgc; index++)
		delete[] fCurrentArgv[index];

	fArgc = 0;
}


/** @brief Dispatches the accumulated argv to @p argvHandlerFunc if non-empty.
 *
 *  NUL-terminates the argv array, calls the handler, prints any error
 *  message returned, then empties the array via MakeArgvEmpty().
 *
 *  @param argvHandlerFunc  Callback that receives (argc, argv, passThru).
 *  @param passThru         Opaque pointer forwarded to the handler.
 *  @return B_OK on success, B_ERROR if the handler returned a non-NULL message.
 */
status_t
ArgvParser::SendArgv(ArgvHandler argvHandlerFunc, void* passThru)
{
	if (fArgc) {
		NextArgv();
		fCurrentArgv[fArgc] = 0;
		const char* result = (argvHandlerFunc)(fArgc, fCurrentArgv, passThru);
		if (result != NULL) {
			printf("File %s; Line %" B_PRId32 " # %s", fFileName, fLineNo,
				result);
		}
		MakeArgvEmpty();
		if (result != NULL)
			return B_ERROR;
	}

	return B_OK;
}


/** @brief Finalises the current argument token and appends it to argv.
 *
 *  NUL-terminates fCurrentArgs, copies it into a new heap string stored in
 *  fCurrentArgv[fArgc], and increments fArgc.
 */
void
ArgvParser::NextArgv()
{
	if (fSawBackslash) {
		fCurrentArgs[++fCurrentArgsPos] = '\\';
		fSawBackslash = false;
	}
	fCurrentArgs[++fCurrentArgsPos] = '\0';
		// terminate current arg pos

	// copy it as a string to the current argv slot
	fCurrentArgv[fArgc] = new char [strlen(fCurrentArgs) + 1];
	strcpy(fCurrentArgv[fArgc], fCurrentArgs);
	fCurrentArgsPos = -1;
	fArgc++;
}


/** @brief Calls NextArgv() only when the current argument buffer is non-empty. */
void
ArgvParser::NextArgvIfNotEmpty()
{
	if (!fSawBackslash && fCurrentArgsPos < 0)
		return;

	NextArgv();
}


/** @brief Returns the next character from the input buffer, refilling as needed.
 *
 *  Uses fgets() to fill fBuffer when exhausted. Returns EOF when the file
 *  is fully read or fFile is NULL.
 *
 *  @return The next character, or EOF.
 */
int
ArgvParser::GetCh()
{
	if (fPos < 0 || fBuffer[fPos] == 0) {
		if (fFile == 0)
			return EOF;
		if (fgets(fBuffer, kBufferSize, fFile) == 0)
			return EOF;
		fPos = 0;
	}

	return fBuffer[fPos++];
}


/** @brief Static helper that creates an ArgvParser for @p name and drives it.
 *
 *  @param name              Path to the settings file.
 *  @param argvHandlerFunc   Callback invoked for each argv line.
 *  @param passThru          Opaque pointer forwarded to the callback.
 *  @return B_OK on success, B_ERROR on the first line that the handler rejects.
 */
status_t
ArgvParser::EachArgv(const char* name, ArgvHandler argvHandlerFunc,
	void* passThru)
{
	ArgvParser parser(name);

	return parser.EachArgvPrivate(name, argvHandlerFunc, passThru);
}


/** @brief Drives the character-by-character parse loop.
 *
 *  Reads characters one at a time, accumulating them into argument tokens.
 *  Handles quoting (single and double), backslash escaping, comment
 *  characters (#), and semicolon command separators (;). Calls SendArgv()
 *  at end-of-line and end-of-file.
 *
 *  @param name              Path of the file (used in error messages).
 *  @param argvHandlerFunc   Callback invoked per logical line.
 *  @param passThru          Opaque pointer forwarded to the callback.
 *  @return B_OK on success, B_ERROR if a parse error or handler error occurs.
 */
status_t
ArgvParser::EachArgvPrivate(const char* name, ArgvHandler argvHandlerFunc,
	void* passThru)
{
	status_t result;

	for (;;) {
		int ch = GetCh();
		if (ch == EOF) {
			// done with fFile
			if (fInDoubleQuote || fInSingleQuote) {
				printf("File %s # unterminated quote at end of file\n", name);
				result = B_ERROR;
				break;
			}
			result = SendArgv(argvHandlerFunc, passThru);
			break;
		}

		if (ch == '\n' || ch == '\r') {
			// handle new line
			fEatComment = false;
			if (!fSawBackslash && (fInDoubleQuote || fInSingleQuote)) {
				printf("File %s ; Line %" B_PRId32 " # unterminated quote\n",
					name, fLineNo);
				result = B_ERROR;
				break;
			}

			fLineNo++;
			if (fSawBackslash) {
				fSawBackslash = false;
				continue;
			}

			// end of line, flush all argv
			result = SendArgv(argvHandlerFunc, passThru);

			continue;
		}

		if (fEatComment)
			continue;

		if (!fSawBackslash) {
			if (!fInDoubleQuote && !fInSingleQuote) {
				if (ch == ';') {
					// semicolon is a command separator, pass on
					// the whole argv
					result = SendArgv(argvHandlerFunc, passThru);
					if (result != B_OK)
						break;
					continue;
				} else if (ch == '#') {
					// ignore everything on this line after this character
					fEatComment = true;
					continue;
				} else if (ch == ' ' || ch == '\t') {
					// space or tab separates the individual arg strings
					NextArgvIfNotEmpty();
					continue;
				} else if (!fSawBackslash && ch == '\\') {
					// the next character is escaped
					fSawBackslash = true;
					continue;
				}
			}
			if (!fInSingleQuote && ch == '"') {
				// enter/exit double quote handling
				fInDoubleQuote = !fInDoubleQuote;
				continue;
			}
			if (!fInDoubleQuote && ch == '\'') {
				// enter/exit single quote handling
				fInSingleQuote = !fInSingleQuote;
				continue;
			}
		} else {
			// we just pass through the escape sequence as is
			fCurrentArgs[++fCurrentArgsPos] = '\\';
			fSawBackslash = false;
		}
		fCurrentArgs[++fCurrentArgsPos] = ch;
	}

	return result;
}


//	#pragma mark - SettingsArgvDispatcher


/** @brief Constructs a dispatcher that responds to the token @p name.
 *  @param name  The settings token this dispatcher owns (e.g. "window_frame").
 */
SettingsArgvDispatcher::SettingsArgvDispatcher(const char* name)
	:
	fName(name)
{
}


/** @brief Writes this setting to @p settings if it needs saving.
 *
 *  Calls SaveSettingValue() to produce the value text, wrapping it in the
 *  setting name and a trailing newline.
 *
 *  @param settings          The Settings object to write to.
 *  @param onlyIfNonDefault  When true, only writes if NeedsSaving() returns true.
 */
void
SettingsArgvDispatcher::SaveSettings(Settings* settings,
	bool onlyIfNonDefault)
{
	if (!onlyIfNonDefault || NeedsSaving()) {
		settings->Write("%s ", Name());
		SaveSettingValue(settings);
		settings->Write("\n");
	}
}


/** @brief Parses a BRect from sequential argv strings.
 *
 *  Expects four integers (left, top, right, bottom) starting at @p argv.
 *  Prints an error message to stdout if @p printError is true and a value
 *  is missing.
 *
 *  @param result      Output BRect populated on success.
 *  @param argv        Pointer into an argv array; must point to the left value.
 *  @param printError  If true, print a human-readable error when a value is absent.
 *  @return true on success, false if any component is missing.
 */
bool
SettingsArgvDispatcher::HandleRectValue(BRect &result,
	const char* const* argv, bool printError)
{
	if (!*argv) {
		if (printError)
			printf("rect left expected");
		return false;
	}
	result.left = atoi(*argv);

	if (!*++argv) {
		if (printError)
			printf("rect top expected");
		return false;
	}
	result.top = atoi(*argv);

	if (!*++argv) {
		if (printError)
			printf("rect right expected");
		return false;
	}
	result.right = atoi(*argv);

	if (!*++argv) {
		if (printError)
			printf("rect bottom expected");
		return false;
	}
	result.bottom = atoi(*argv);

	return true;
}


/** @brief Writes a BRect as four space-separated integers via @p setting.
 *
 *  @param setting  The Settings object whose Write() method is used.
 *  @param rect     The rectangle to serialise (components truncated to int32).
 */
void
SettingsArgvDispatcher::WriteRectValue(Settings* setting, BRect rect)
{
	setting->Write("%d %d %d %d", (int32)rect.left, (int32)rect.top,
		(int32)rect.right, (int32)rect.bottom);
}


/*!	\class Settings
	this class represents a list of all the settings handlers, reads and
	saves the settings file
*/
/** @brief Constructs the Settings manager.
 *
 *  Allocates the initial dispatcher list. Does not load the file; call
 *  TryReadingSettings() explicitly.
 *
 *  @param filename        Base filename of the settings file (e.g. "Tracker").
 *  @param settingsDirName Sub-directory under B_USER_SETTINGS_DIRECTORY.
 */
Settings::Settings(const char* filename, const char* settingsDirName)
	:
	fFileName(filename),
	fSettingsDir(settingsDirName),
	fList(0),
	fCount(0),
	fListSize(30),
	fCurrentSettings(0)
{
	fList = (SettingsArgvDispatcher**)calloc((size_t)fListSize,
		sizeof(SettingsArgvDispatcher*));
}


/** @brief Destroys the Settings manager and all registered dispatchers. */
Settings::~Settings()
{
	for (int32 index = 0; index < fCount; index++)
		delete fList[index];

	free(fList);
}


/** @brief Static ArgvHandler callback: dispatches an argv line to the named handler.
 *
 *  Looks up @p argv[0] in the Settings list. Returns an error string if the
 *  token is unknown or if the handler itself reports an error.
 *
 *  @param argc         Number of arguments (unused; present for callback signature).
 *  @param argv         The argv array; argv[0] is the settings token name.
 *  @param castToThis   Pointer to the Settings instance.
 *  @return NULL on success, or a human-readable error string.
 */
const char*
Settings::ParseUserSettings(int, const char* const* argv, void* castToThis)
{
	if (!*argv)
		return 0;

	SettingsArgvDispatcher* handler = ((Settings*)castToThis)->Find(*argv);
	if (!handler)
		return "unknown command";

	return handler->Handle(argv);
}


/*!
	Returns false if argv dispatcher with the same name already
	registered
*/
/** @brief Registers a SettingsArgvDispatcher with this Settings manager.
 *
 *  Duplicate names are rejected. The list grows automatically in increments
 *  of 30 entries.
 *
 *  @param setting  The dispatcher to register. Ownership is transferred.
 *  @return true on success, false if a dispatcher with the same name exists.
 */
bool
Settings::Add(SettingsArgvDispatcher* setting)
{
	// check for uniqueness
	if (Find(setting->Name()))
		return false;

	if (fCount >= fListSize) {
		fListSize += 30;
		fList = (SettingsArgvDispatcher**)realloc(fList,
			fListSize * sizeof(SettingsArgvDispatcher*));
	}
	fList[fCount++] = setting;
	return true;
}


/** @brief Finds a registered dispatcher by name.
 *
 *  @param name  The token name to search for.
 *  @return Pointer to the matching dispatcher, or NULL if not found.
 */
SettingsArgvDispatcher*
Settings::Find(const char* name)
{
	for (int32 index = 0; index < fCount; index++)
		if (strcmp(name, fList[index]->Name()) == 0)
			return fList[index];

	return NULL;
}


/** @brief Attempts to read the settings file from the user's settings directory.
 *
 *  Resolves B_USER_SETTINGS_DIRECTORY / fSettingsDir / fFileName and parses
 *  it via ArgvParser::EachArgv(). Silently does nothing if the directory or
 *  file does not exist.
 */
void
Settings::TryReadingSettings()
{
	BPath prefsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &prefsPath, true) == B_OK) {
		prefsPath.Append(fSettingsDir);

		BPath path(prefsPath);
		path.Append(fFileName);
		ArgvParser::EachArgv(path.Path(), Settings::ParseUserSettings, this);
	}
}


/** @brief Saves all registered settings.
 *  @param onlyIfNonDefault  When true, only dispatchers reporting NeedsSaving()
 *                           will write their values.
 */
void
Settings::SaveSettings(bool onlyIfNonDefault)
{
	SaveCurrentSettings(onlyIfNonDefault);
}


/** @brief Creates the settings directory hierarchy (mkdir -p equivalent).
 *
 *  Splits the path at each '/' and calls mkdir() at each level. The final
 *  BDirectory is returned via @p resultingSettingsDir.
 *
 *  @param resultingSettingsDir  Output parameter set to the settings directory.
 */
void
Settings::MakeSettingsDirectory(BDirectory* resultingSettingsDir)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return;

	// make sure there is a directory
	// mkdir() will only make one leaf at a time, unfortunately
	path.Append(fSettingsDir);
	char* ptr = (char *)alloca(strlen(path.Path()) + 1);
	strcpy(ptr, path.Path());
	char* end = ptr+strlen(ptr);
	char* mid = ptr+1;
	while (mid < end) {
		mid = strchr(mid, '/');
		if (!mid) break;
		*mid = 0;
		mkdir(ptr, 0777);
		*mid = '/';
		mid++;
	}
	mkdir(ptr, 0777);
	resultingSettingsDir->SetTo(path.Path());
}


/** @brief Writes all settings to the settings file.
 *
 *  Removes any pre-existing file, creates a fresh one, and iterates over all
 *  registered dispatchers, calling their SaveSettings() method.
 *
 *  @param onlyIfNonDefault  When true only non-default values are written.
 */
void
Settings::SaveCurrentSettings(bool onlyIfNonDefault)
{
	BDirectory settingsDir;
	MakeSettingsDirectory(&settingsDir);

	if (settingsDir.InitCheck() != B_OK)
		return;

	// nuke old settings
	BEntry entry(&settingsDir, fFileName);
	entry.Remove();

	BFile prefs(&entry, O_RDWR | O_CREAT);
	if (prefs.InitCheck() != B_OK)
		return;

	fCurrentSettings = &prefs;
	for (int32 index = 0; index < fCount; index++)
		fList[index]->SaveSettings(this, onlyIfNonDefault);

	fCurrentSettings = NULL;
}


/** @brief Formats a string and writes it to the current settings file.
 *
 *  Must only be called while a save operation is active (i.e., from within
 *  a SaveSettings() call chain).
 *
 *  @param format  printf-style format string.
 *  @param ...     Arguments for the format string.
 */
void
Settings::Write(const char* format, ...)
{
	va_list args;

	va_start(args, format);
	VSWrite(format, args);
	va_end(args);
}


/** @brief va_list variant of Write(); formats and writes to the settings file.
 *
 *  @param format  printf-style format string.
 *  @param arg     Argument list corresponding to @p format.
 */
void
Settings::VSWrite(const char* format, va_list arg)
{
	char buffer[2048];
	vsprintf(buffer, format, arg);
	ASSERT(fCurrentSettings && fCurrentSettings->InitCheck() == B_OK);
	fCurrentSettings->Write(buffer, strlen(buffer));
}
