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
 *   (No explicit copyright header in the original file; part of the
 *   Haiku Bluetooth server private API.)
 */

/** @file debug.h
 *  @brief User-space debug tracing helpers for the Bluetooth server, providing
 *         levelled TRACE, ERROR, CALLED, and END macros. */

#ifndef _BLUETOOTH_DEBUG_SERVER_H_
#define _BLUETOOTH_DEBUG_SERVER_H_

/** @brief Compile-time debug verbosity level; defaults to 3 (full tracing) if
 *         not defined by the build system. Higher values enable more output. */
#ifndef DEBUG
  #define DEBUG 3
#endif

#include <Debug.h>
#include <stdio.h>

#undef TRACE
#undef PRINT
#if DEBUG > 0
  /** @brief Prints a formatted error message prefixed with "### ERROR: " to
   *         standard output.
   *  @param fmt printf-style format string.
   *  @param ... Variadic arguments matching @p fmt. */
  inline void ERROR(const char *fmt, ...)
  {
  	va_list ap;
  	va_start(ap, fmt);
  	printf("### ERROR: ");
  	vprintf(fmt, ap); va_end(ap);
  }
/*
  inline void PRINT(int level, const char *fmt, ...)
  {
  	va_list ap;
  	if (level > DEBUG)
  		return;
  	va_start(ap, fmt);
  	vprintf(fmt, ap);
  	va_end(ap);
  }

  inline void PRINT(const char *fmt, ...)
  {
  	va_list ap;
  	va_start(ap, fmt);
  	vprintf(fmt, ap);
  	va_end(ap);
  }*/

  #if DEBUG >= 2
	/** @brief Prints a trace line containing the current function name and the
	 *         supplied message when DEBUG >= 2; expands to nothing otherwise.
	 *  @param a... printf-style format string and arguments. */
	#define TRACE(a...)		printf("TRACE %s : %s\n", __PRETTY_FUNCTION__, a)
  #else
	/** @brief No-op when DEBUG < 2. */
	#define TRACE(a...)		((void)0)
  #endif

  #if DEBUG >= 3
	/** @brief Prints a message recording that the current function is exiting
	 *         when DEBUG >= 3; expands to nothing otherwise. */
	#define END()	 		printf("ENDING %s\n",__PRETTY_FUNCTION__)

	/** @brief Prints a message recording that the current function has been
	 *         entered when DEBUG >= 3; expands to nothing otherwise. */
	#define CALLED() 		printf("CALLED %s\n",__PRETTY_FUNCTION__)
  #else
	/** @brief No-op when DEBUG < 3. */
	#define END()			((void)0)
  	/** @brief No-op when DEBUG < 3. */
  	#define CALLED() 		((void)0)
  #endif
#else
	/** @brief No-op when DEBUG == 0. */
	#define END()			((void)0)
	/** @brief No-op when DEBUG == 0. */
	#define CALLED()		((void)0)
	/** @brief Writes a formatted error message to stderr when DEBUG == 0.
	 *  @param a... printf-style format string and arguments. */
	#define ERROR(a...)		fprintf(stderr, a)
	/** @brief No-op when DEBUG == 0. */
	#define TRACE(a...)		((void)0)
#endif

/** @brief Unconditionally prints a formatted message to standard output.
 *  @param l printf-style format string.
 *  @param a... Variadic arguments matching @p l. */
#define PRINT(l, a...)		printf(l, a)

#endif /* _BLUETOOTH_DEBUG_SERVER_H_ */
