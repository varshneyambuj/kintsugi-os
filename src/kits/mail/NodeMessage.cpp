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
 *   NodeMessage - BMessage/BNode attribute bridge operators
 *   (Original file lacked a formal copyright notice.)
 */


/**
 * @file NodeMessage.cpp
 * @brief Stream operators for copying BFS node attributes to and from BMessages.
 *
 * Provides operator<<(BNode&, const BMessage&) to write all message fields as
 * node attributes, and operator>>(BNode&, BMessage&) to read all node attributes
 * into a message. Used internally by BAttributedMailAttachment to serialise
 * and restore BFS extended attributes when a mail is sent or received.
 *
 * @see BAttributedMailAttachment, BNode, BMessage
 */


#include "NodeMessage.h"
#include <StorageKit.h>
#include <fs_attr.h>
#include <stdlib.h>

/*
   These functions gives a nice BMessage interface to node attributes,
   by letting you transfer attributes to and from BMessages.  It makes
   it so you can use all the convenient Find...() and Add...() functions
   provided by BMessage for attributes too.  You use it as follows:

   BMessage m;
   BNode n(path);
   if (reading) { n>>m; printf("woohoo=%s\n",m.FindString("woohoo")) }
   else { m.AddString("woohoo","it's howdy doody time"); n<<m; }

   If there is more than one data item with a given name, the first
   item is the one writen to the node.
*/


/**
 * @brief Writes all fields from a BMessage as BFS extended attributes on a BNode.
 *
 * Iterates over all entries in \a m (of any type) and writes each one as a
 * named attribute on \a n. Only the first item for each name is written. If
 * an attribute already exists on the node it is overwritten.
 *
 * @param n  Target BNode to receive the attributes.
 * @param m  Source BMessage whose fields are written as node attributes.
 * @return Reference to \a n for chaining.
 */
_EXPORT BNode& operator<<(BNode& n, const BMessage& m)
{
	#if defined(HAIKU_TARGET_PLATFORM_DANO)
	const
	#endif
	char *name;
	type_code   type;
	ssize_t     bytes;
	const void *data;

	for (int32 i = 0;
		m.GetInfo(B_ANY_TYPE, i, &name, &type) == 0;
		i++) {
		m.FindData (name,type,0,&data,&bytes);
		n.WriteAttr(name,type,0, data, bytes);
	}

	return n;
}


/**
 * @brief Reads all BFS extended attributes from a BNode into a BMessage.
 *
 * Rewinds the attribute cursor on \a n, then reads each attribute's type and
 * data, appending it as a field in \a m. A realloc-managed buffer is used
 * to handle attributes of varying sizes efficiently.
 *
 * @param n  Source BNode whose attributes are read.
 * @param m  Destination BMessage to receive the attribute data.
 * @return Reference to \a n for chaining.
 */
_EXPORT BNode& operator>>(BNode& n, BMessage& m)
{
	char        name[B_ATTR_NAME_LENGTH];
	attr_info	info;
	char *buf = NULL;

	n.RewindAttrs();
	while (n.GetNextAttrName(name) == B_OK) {
		if (n.GetAttrInfo(name,&info) != B_OK)
			continue;

		// resize the buffer
		if (char *newBuffer = (char*)realloc(buf, info.size))
			buf = newBuffer;
		else
			continue;

		info.size=n.ReadAttr(name,info.type,0,buf,info.size);
		if (info.size >= 0)
			m.AddData(name,info.type,buf,info.size);
	}
	n.RewindAttrs();

	free(buf);

	return n;
}
