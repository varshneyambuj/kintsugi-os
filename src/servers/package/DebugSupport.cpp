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
   /*
    * Copyright 2003-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
    * Distributed under the terms of the MIT License.
    */
 */

/** @file DebugSupport.cpp
 *  @brief Implements thread-safe debug logging with file output support */



#include "DebugSupport.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>


/*!
	\file Debug.cpp
	\brief Defines debug output function with printf() signature printing
		   into a file.

	\note The initialization is not thread safe!
*/


// locking support

/** @brief Reference count for init_debugging()/exit_debugging() calls. */
static int32 init_counter = 0;

/** @brief Semaphore used to serialize debug output across threads. */
static sem_id dbg_printf_sem = -1;

/** @brief Thread currently holding the debug output lock. */
static thread_id dbg_printf_thread = -1;

/** @brief Recursive lock nesting depth for the debug output lock. */
static int dbg_printf_nesting = 0;


#if DEBUG_PRINT
/** @brief File descriptor for the debug output file. */
static int out = -1;
#endif


/**
 * @brief Initializes the debug output subsystem.
 *
 * On the first call, opens the debug output file (if DEBUG_PRINT is
 * enabled) and creates the serialization semaphore. Subsequent calls
 * increment a reference counter. Paired with exit_debugging().
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
init_debugging()
{
	status_t error = B_OK;
	if (init_counter++ == 0) {
		// open the file
		#if DEBUG_PRINT
			out = open(DEBUG_PRINT_FILE, O_RDWR | O_CREAT | O_TRUNC);
			if (out < 0) {
				error = errno;
				init_counter--;
			}
		#endif	// DEBUG_PRINT
		// allocate the semaphore
		if (error == B_OK) {
			dbg_printf_sem = create_sem(1, "dbg_printf");
			if (dbg_printf_sem < 0)
				error = dbg_printf_sem;
		}
		if (error == B_OK) {
			#if DEBUG
				__out("##################################################\n");
			#endif
		} else
			exit_debugging();
	}
	return error;
}


/**
 * @brief Tears down the debug output subsystem.
 *
 * Decrements the reference counter. When it reaches zero, closes the
 * debug output file and deletes the semaphore.
 *
 * @return B_OK on success, B_NO_INIT if the counter was already zero.
 */
status_t
exit_debugging()
{
	status_t error = B_OK;
	if (--init_counter == 0) {
		#if DEBUG_PRINT
			close(out);
			out = -1;
		#endif	// DEBUG_PRINT
		delete_sem(dbg_printf_sem);
	} else
		error = B_NO_INIT;
	return error;
}


/**
 * @brief Acquires the recursive debug output lock for the calling thread.
 *
 * If the calling thread already holds the lock, the nesting count is
 * incremented. Otherwise the semaphore is acquired.
 *
 * @return @c true if the lock was acquired, @c false on failure.
 */
static inline bool
dbg_printf_lock()
{
	thread_id thread = find_thread(NULL);
	if (thread != dbg_printf_thread) {
		if (acquire_sem(dbg_printf_sem) != B_OK)
			return false;
		dbg_printf_thread = thread;
	}
	dbg_printf_nesting++;
	return true;
}


/**
 * @brief Releases one level of the recursive debug output lock.
 *
 * When the nesting count reaches zero, the semaphore is released so
 * other threads may print.
 */
static inline void
dbg_printf_unlock()
{
	thread_id thread = find_thread(NULL);
	if (thread != dbg_printf_thread)
		return;
	dbg_printf_nesting--;
	if (dbg_printf_nesting == 0) {
		dbg_printf_thread = -1;
		release_sem(dbg_printf_sem);
	}
}


/** @brief Acquires the debug output lock for a multi-statement debug block. */
void
dbg_printf_begin()
{
	dbg_printf_lock();
}


/** @brief Releases the debug output lock after a multi-statement debug block. */
void
dbg_printf_end()
{
	dbg_printf_unlock();
}


#if DEBUG_PRINT

/**
 * @brief Prints a formatted debug message to the debug output file.
 *
 * Acquires the debug lock, formats the message with vsnprintf/vsprintf,
 * writes it to the output file descriptor, then releases the lock.
 *
 * @param format A printf-style format string, followed by variadic arguments.
 */
void
dbg_printf(const char *format,...)
{
	if (!dbg_printf_lock())
		return;
	char buffer[1024];
	va_list args;
	va_start(args, format);
	// no vsnprintf() on PPC and in kernel
	#if defined(__i386__) && USER
		vsnprintf(buffer, sizeof(buffer) - 1, format, args);
	#else
		vsprintf(buffer, format, args);
	#endif
	va_end(args);
	buffer[sizeof(buffer) - 1] = '\0';
	write(out, buffer, strlen(buffer));
	dbg_printf_unlock();
}

#endif	// DEBUG_PRINT
