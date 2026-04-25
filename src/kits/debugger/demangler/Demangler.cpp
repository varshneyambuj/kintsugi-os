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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Demangler.cpp
 * @brief Thin wrapper around the system demangle helpers used by the debugger.
 *
 * Tries gcc3+ Itanium-ABI demangling first when the symbol begins with
 * "_Z", then falls back to gcc2 demangling (still needed for legacy
 * binaries). When the gcc2 demangler succeeds it walks each argument and
 * synthesizes a friendly type name even when the upstream helper could
 * not recover one.
 */

#include "Demangler.h"

#include <TypeConstants.h>

#include "demangle.h"


/**
 * @brief Demangle a C++ symbol name into a human-readable form.
 *
 * Falls back to the original @a mangledName when the symbol is not mangled.
 *
 * @param mangledName  Possibly mangled C++ symbol.
 * @return The demangled name (with parameter list if available), or the
 *         original input when no demangler recognizes it.
 */
/*static*/ BString
Demangler::Demangle(const BString& mangledName)
{
	char buffer[1024];

	if (mangledName.Compare("_Z", 2) == 0) {
		// probably a gcc3+ mangled symbol
		const char* demangled = demangle_name_gcc3(mangledName.String(), buffer,
			sizeof(buffer));
		if (demangled != NULL)
			return demangled;
	}

	// try gcc 2 demangling
	const char* demangled = demangle_symbol_gcc2(mangledName.String(), buffer,
		sizeof(buffer), NULL);

	if (demangled == NULL) {
		// name not mangled
		return mangledName;
	}

	BString demangledName(demangled);
	demangledName << "(";

	size_t length;
	int32 type;
	int32 i = 0;
	uint32 cookie = 0;
	while (get_next_argument_gcc2(&cookie, mangledName.String(), buffer,
			sizeof(buffer), &type, &length) == B_OK) {
		if (i++ > 0)
			demangledName << ", ";

		if (buffer[0]) {
			demangledName << buffer;
			continue;
		}

		// unnamed argument: fallback to known type
		switch (type) {
			case B_ANY_TYPE:
				break;
			case B_INT64_TYPE:
				demangledName << "int64";
				break;
			case B_INT32_TYPE:
				demangledName << "int32";
				break;
			case B_INT16_TYPE:
				demangledName << "int16";
				break;
			case B_INT8_TYPE:
				demangledName << "int8";
				break;
			case B_UINT64_TYPE:
				demangledName << "uint64";
				break;
			case B_UINT32_TYPE:
				demangledName << "uint32";
				break;
			case B_UINT16_TYPE:
				demangledName << "uint16";
				break;
			case B_UINT8_TYPE:
				demangledName << "uint8";
				break;
			case B_BOOL_TYPE:
				demangledName << "bool";
				break;
			case B_CHAR_TYPE:
				demangledName << "char";
				break;
			case B_FLOAT_TYPE:
				demangledName << "float";
				break;
			case B_DOUBLE_TYPE:
				demangledName << "double";
				break;
			case B_POINTER_TYPE:
				// TODO: use length as hint on pointer type
				demangledName << "void*";
				break;
			case B_REF_TYPE:
			case B_NODE_REF_TYPE:
				// TODO: use length as hint on reference type
				demangledName << "&";
				break;
			case B_STRING_TYPE:
				demangledName << "char*";
				break;
			default:
				demangledName << "?";
				break;
		}
	}

	demangledName << ")";
	return demangledName;
}
