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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MimeSnifferAddon.cpp
 * @brief Default implementations for the BMimeSnifferAddon plug-in interface.
 *
 * BMimeSnifferAddon is the base class that third-party MIME sniffer add-ons
 * must subclass.  This file provides the constructor, destructor, and no-op
 * default implementations of MinimalBufferSize() and both GuessMimeType()
 * overloads so that subclasses only need to override the methods they care
 * about.
 *
 * @see BMimeSnifferAddon
 */


#include <MimeSnifferAddon.h>


/**
 * @brief Constructs a BMimeSnifferAddon instance.
 */
BMimeSnifferAddon::BMimeSnifferAddon()
{
}

/**
 * @brief Destroys the BMimeSnifferAddon instance.
 */
BMimeSnifferAddon::~BMimeSnifferAddon()
{
}

/**
 * @brief Returns the minimum number of bytes the sniffer needs to examine.
 *
 * Subclasses should override this to return the number of leading file bytes
 * required by their GuessMimeType(BFile*, const void*, int32, BMimeType*)
 * implementation.  The default returns 0.
 *
 * @return Minimum buffer size in bytes; 0 by default.
 */
size_t
BMimeSnifferAddon::MinimalBufferSize()
{
	return 0;
}

/**
 * @brief Guesses a MIME type from a filename.
 *
 * The default implementation performs no guessing and always returns -1.
 * Subclasses should override this to inspect the filename (e.g. extension)
 * and populate \a type when a match is found.
 *
 * @param fileName The name of the file to examine.
 * @param type     Pointer to a BMimeType object to receive the guessed type.
 * @return A priority value in [0, 1] on success, or -1 if no type could be guessed.
 */
float
BMimeSnifferAddon::GuessMimeType(const char* fileName, BMimeType* type)
{
	return -1;
}

/**
 * @brief Guesses a MIME type from a file handle and its leading byte buffer.
 *
 * The default implementation performs no guessing and always returns -1.
 * Subclasses should override this to inspect the raw buffer content and
 * populate \a type when a match is found.
 *
 * @param file   Pointer to the open BFile being examined (may be NULL).
 * @param buffer Pointer to the leading bytes of the file's content.
 * @param length Number of bytes available in \a buffer.
 * @param type   Pointer to a BMimeType object to receive the guessed type.
 * @return A priority value in [0, 1] on success, or -1 if no type could be guessed.
 */
float
BMimeSnifferAddon::GuessMimeType(BFile* file, const void* buffer, int32 length,
	BMimeType* type)
{
	return -1;
}
