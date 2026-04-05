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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2005, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */

/* A BMallocIO similar structure but with less overhead */

#ifndef _SIMPLE_MALLOC_IO_H_
#define _SIMPLE_MALLOC_IO_H_

#include <malloc.h>

namespace BPrivate {

class BSimpleMallocIO {
public:
						BSimpleMallocIO(size_t size)
							:	fSize(size)
						{
							fBuffer = (char *)malloc(size);
						}

						~BSimpleMallocIO()
						{
							free(fBuffer);
						}

		void			Read(void *buffer)
						{
							memcpy(buffer, fBuffer, fSize);
						}

		void			Read(void *buffer, size_t size)
						{
							memcpy(buffer, fBuffer, size);
						}

		void			ReadAt(off_t pos, void *buffer, size_t size)
						{
							memcpy(buffer, fBuffer + pos, size);
						}

		void			Write(const void *buffer)
						{
							memcpy(fBuffer, buffer, fSize);
						}

		void			Write(const void *buffer, size_t size)
						{
							memcpy(fBuffer, buffer, size);
						}

		void			WriteAt(off_t pos, const void *buffer, size_t size)
						{
							memcpy(fBuffer + pos, buffer, size);
						}

		status_t		SetSize(off_t size)
						{
							fBuffer = (char *)realloc(fBuffer, size);
							if (!fBuffer)
								return B_NO_MEMORY;

							fSize = size;
							return B_OK;
						}

		char			*Buffer()
						{
							return fBuffer;
						}

		size_t			BufferLength()
						{
							return fSize;
						}

private:
		char			*fBuffer;
		size_t			fSize;
};

} // namespace BPivate

#endif // _SIMPLE_MALLOC_IO_H_
