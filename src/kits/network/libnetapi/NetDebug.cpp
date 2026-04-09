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
 *   Copyright (c) 2002, The Haiku project.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the "Software"),
 *   to deal in the Software without restriction, including without limitation
 *   the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *   and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *   DEALINGS IN THE SOFTWARE.
 *
 *   Written by S.T. Mansfield (thephantom@mac.com)
 */

/** @file NetDebug.cpp
 *  @brief Implementation of the BNetDebug helper class, a process-wide
 *         switch for printing debug messages and hex dumps of network traffic. */


#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <NetDebug.h>
#include <SupportDefs.h>


// Off by default cuz the BeBook sez so.
static bool g_NetDebugEnabled = false;


/** @brief Enables or disables debug output for the entire process.
 *         The flag is global and static — it affects all users of BNetDebug.
 *  @param Enable true to enable Print()/Dump() output, false to silence it. */
void BNetDebug::Enable( bool Enable )
{
    g_NetDebugEnabled = Enable;
}


/** @brief Reports whether debug output is currently enabled.
 *  @return true if Enable(true) is in effect. */
bool BNetDebug::IsEnabled( void )
{
    return g_NetDebugEnabled;
}


/** @brief Prints a debug message to stderr, prefixed with "debug: ".
 *         Output is suppressed unless debugging has been Enable()'d.
 *  @param msg Message text, or NULL (treated as "(null)"). */
void BNetDebug::Print( const char* msg )
{
	if ( !g_NetDebugEnabled )
    	return;

	if (msg == NULL)
		msg = "(null)";

	fprintf( stderr, "debug: %s\n", msg );
}


/** @brief Prints a combined hex + ASCII dump of a byte region to stderr.
 *         Each line shows up to 16 bytes in hex followed by their printable
 *         ASCII representation. Output is suppressed unless debugging has
 *         been Enable()'d. stderr is flushed when the dump completes.
 *  @param data  Pointer to the bytes to dump, or NULL.
 *  @param size  Number of bytes to dump.
 *  @param title Optional header label, or NULL for "(untitled)". */
void BNetDebug::Dump(const char* data, size_t size, const char* title)
{

    if ( ! g_NetDebugEnabled)
        return;

    fprintf( stderr, "----------\n%s\n(dumping %ld bytes)\n",
    	title ? title : "(untitled)", size );

    if (! data)
    	fprintf(stderr, "NULL data!\n");
    else {
		uint32	i,j;
	  	char text[96];	// only 3*16 + 3 + 16 max by line needed
		uint8 *byte = (uint8 *) data;
		char *ptr;

		for ( i = 0; i < size; i += 16 )	{
			ptr = text;

	      	for ( j = i; j < i + 16 ; j++ ) {
				if ( j < size )
					sprintf(ptr, "%02x ", byte[j]);
				else
					sprintf(ptr, "   ");
				ptr += 3;
			};

			strcat(ptr, "| ");
			ptr += 2;

			for (j = i; j < size && j < i + 16;j++) {
				if ( byte[j] >= 0x20 && byte[j] < 0x7e )
					*ptr = byte[j];
				else
					*ptr = '.';
				ptr++;
			};

			ptr[0] = '\n';
			ptr[1] = '\0';
			fputs(text, stderr);
		};
	};
    fputs("----------\n", stderr);
    fflush( stderr );
}


/*=------------------------------------------------------------------- End -=*/
