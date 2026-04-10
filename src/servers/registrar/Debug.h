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
 *   Debug - debug stuff
 *
 *   Initial version by Axel Doerfler, axeld@pinc-software.de
 *   This file may be used under the terms of the MIT License.
 */

/** @file Debug.h
 *  @brief Debug logging macros for the registrar server with configurable verbosity levels. */
#ifndef DEBUG_H
#define DEBUG_H
#ifndef DEBUG
#	define DEBUG 0
#endif

#include <stdio.h>

#include <OS.h>

#ifdef DEBUG_PRINTF
	#define __out DEBUG_PRINTF
#else
	#define __out printf
#endif

// Short overview over the debug output macros:
//	PRINT()
//		is for general messages that very unlikely should appear in a release build
//	FATAL()
//		this is for fatal messages, when something has really gone wrong
//	INFORM()
//		general information, as disk size, etc.
//	REPORT_ERROR(status_t)
//		prints out error information
//	RETURN_ERROR(status_t)
//		calls REPORT_ERROR() and return the value
//	D()
//		the statements in D() are only included if DEBUG is defined

#define DEBUG_APP "REG"
#if DEBUG
	#define PRINT(x...) { __out(DEBUG_APP ": " x); }
	#define REPORT_ERROR(status) \
		__out(DEBUG_APP ": %s:%d: %s\n", __FUNCTION__, __LINE__, \
			strerror(status));
	#define RETURN_ERROR(err) \
		{ \
			status_t _status = err; \
			if (_status < B_OK) \
				REPORT_ERROR(_status); \
			return _status; \
		}
	#define SET_ERROR(var, err) \
		{ \
			status_t _status = err; \
			if (_status < B_OK) \
				REPORT_ERROR(_status); \
			var = _status; \
		}
	#define FATAL(x...) { __out(DEBUG_APP ": " x); }
	#define ERROR(x...) { __out(DEBUG_APP ": " x); }
	#define WARNING(x...) { __out(DEBUG_APP ": " x); }
	#define INFORM(x...) { __out(DEBUG_APP ": " x); }
	#define FUNCTION(x) { __out(DEBUG_APP ": %s() ",__FUNCTION__); __out x; }
	#define FUNCTION_START() { __out(DEBUG_APP ": %s()\n",__FUNCTION__); }
	#define FUNCTION_END() { __out(DEBUG_APP ": %s() done\n",__FUNCTION__); }
	#define D(x) {x;};
#else
	#define PRINT(x...) ;
	#define REPORT_ERROR(status) ;
	#define RETURN_ERROR(status) return status;
	#define SET_ERROR(var, err) var = err;
	#define FATAL(x...) { __out(DEBUG_APP ": " x); }
	#define ERROR(x...) { __out(DEBUG_APP ": " x); }
	#define WARNING(x...) { __out(DEBUG_APP ": " x); }
	#define INFORM(x...) { __out(DEBUG_APP ": " x); }
	#define FUNCTION(x...) ;
	#define FUNCTION_START() ;
	#define FUNCTION_END() ;
	#define D(x) ;
#endif


#endif	/* DEBUG_H */
