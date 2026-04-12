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
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 *   AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file AttributeStream.cpp
 * @brief Pipeline-style stream classes for copying BFS extended attributes between nodes.
 *
 * Provides a composable set of nodes (file, memory, template, filter, transformer)
 * that can be chained with the << operator to stream extended-attribute data from a
 * source to a sink. Nodes are wired into a directed pipeline; the head node drives
 * iteration while the tail node persists the data.
 *
 * @see AttributeStreamNode, AttributeStreamFileNode, AttributeStreamMemoryNode
 */


#include "AttributeStream.h"

#include <Debug.h>
#include <Node.h>

#include "Utilities.h"


// ToDo:
// lazy Rewind from Drive, only if data is available
// BMessage node
// partial feeding (part, not the whole buffer)


//	#pragma mark - AttributeInfo


/**
 * @brief Default constructor; initialises to an empty raw-type attribute.
 */
AttributeInfo::AttributeInfo()
	:
	fName("")
{
	fInfo.type = B_RAW_TYPE;
	fInfo.size = 0;
}


/**
 * @brief Copy constructor.
 *
 * @param other  The AttributeInfo to copy from.
 */
AttributeInfo::AttributeInfo(const AttributeInfo& other)
	:
	fName(other.fName),
	fInfo(other.fInfo)

{
}


/**
 * @brief Construct from a name and a kernel attr_info descriptor.
 *
 * @param name  Attribute name string.
 * @param info  Kernel attr_info struct containing type and size.
 */
AttributeInfo::AttributeInfo(const char* name, attr_info info)
	:
	fName(name),
	fInfo(info)
{
}


/**
 * @brief Construct from a name, type code, and explicit size.
 *
 * @param name  Attribute name string.
 * @param type  Attribute type code (e.g. B_STRING_TYPE).
 * @param size  Size of the attribute data in bytes.
 */
AttributeInfo::AttributeInfo(const char* name, uint32 type, off_t size)
	:
	fName(name)
{
	fInfo.type = type;
	fInfo.size = size;
}


/**
 * @brief Return the attribute name as a C string.
 *
 * @return Pointer to the null-terminated attribute name.
 */
const char*
AttributeInfo::Name() const
{
	return fName.String();
}


/**
 * @brief Return the attribute type code.
 *
 * @return The type_code stored in the attr_info descriptor.
 */
uint32
AttributeInfo::Type() const
{
	return fInfo.type;
}


/**
 * @brief Return the attribute data size in bytes.
 *
 * @return The size stored in the attr_info descriptor.
 */
off_t
AttributeInfo::Size() const
{
	return fInfo.size;
}


/**
 * @brief Replace this object's contents with a copy of @a other.
 *
 * @param other  Source AttributeInfo to copy from.
 */
void
AttributeInfo::SetTo(const AttributeInfo& other)
{
	fName = other.fName;
	fInfo = other.fInfo;
}


/**
 * @brief Set from a name and a kernel attr_info struct.
 *
 * @param name  Attribute name string.
 * @param info  Kernel attr_info struct containing type and size.
 */
void
AttributeInfo::SetTo(const char* name, attr_info info)
{
	fName = name;
	fInfo = info;
}


/**
 * @brief Set from a name, type code, and explicit size.
 *
 * @param name  Attribute name string.
 * @param type  Attribute type code.
 * @param size  Size of the attribute data in bytes.
 */
void
AttributeInfo::SetTo(const char* name, uint32 type, off_t size)
{
	fName = name;
	fInfo.type = type;
	fInfo.size = size;
}


//	#pragma mark - AttributeStreamNode


/**
 * @brief Default constructor; both upstream and downstream pointers are NULL.
 */
AttributeStreamNode::AttributeStreamNode()
	:
	fReadFrom(NULL),
	fWriteTo(NULL)
{
}


/**
 * @brief Destructor; detaches this node from any connected stream pipeline.
 */
AttributeStreamNode::~AttributeStreamNode()
{
	Detach();
}


/**
 * @brief Wire @a source as the upstream provider for this node.
 *
 * Sets up the pipeline link so that this node reads from @a source.
 * If @a source can feed data immediately, Start() is called to begin
 * pumping attributes.
 *
 * @param source  The upstream AttributeStreamNode to read from.
 * @return A reference to @a source, enabling chained << expressions.
 */
AttributeStreamNode&
AttributeStreamNode::operator<<(AttributeStreamNode &source)
{
	fReadFrom = &source;
	fReadFrom->fWriteTo = this;
	if (fReadFrom->CanFeed())
		fReadFrom->Start();

	return source;
}


/**
 * @brief Propagate a rewind request upstream through the pipeline.
 */
void
AttributeStreamNode::Rewind()
{
	if (fReadFrom != NULL)
		fReadFrom->Rewind();
}


/**
 * @brief Stub MakeEmpty that asserts; file nodes must override this.
 */
void
AttributeStreamFileNode::MakeEmpty()
{
	TRESPASS();
}


/**
 * @brief Query whether the upstream node contains an attribute with the given name and type.
 *
 * @param name  Attribute name to look up.
 * @param type  Expected attribute type code.
 * @return Size of the attribute in bytes, or 0 if not found.
 */
off_t
AttributeStreamNode::Contains(const char* name, uint32 type)
{
	if (fReadFrom == NULL)
		return 0;

	return fReadFrom->Contains(name, type);
}


/**
 * @brief Read an attribute value from the upstream node into @a buffer.
 *
 * Tries the native @a name first; on failure tries @a foreignName and applies
 * @a swapFunc to byte-swap the data into native endianness.
 *
 * @param name         Native attribute name.
 * @param foreignName  Foreign (alternate byte-order) attribute name, or NULL.
 * @param type         Expected attribute type code.
 * @param size         Size of the buffer in bytes.
 * @param buffer       Destination buffer for the attribute data.
 * @param swapFunc     Optional byte-swap function applied when foreign name is used.
 * @return Number of bytes read, or 0 on failure.
 */
off_t
AttributeStreamNode::Read(const char* name, const char* foreignName,
	uint32 type, off_t size, void* buffer, void (*swapFunc)(void*))
{
	if (fReadFrom == NULL)
		return 0;

	return fReadFrom->Read(name, foreignName, type, size, buffer, swapFunc);
}


/**
 * @brief Write an attribute value to the downstream node.
 *
 * @param name         Native attribute name.
 * @param foreignName  Foreign attribute name to remove after successful write.
 * @param type         Attribute type code.
 * @param size         Size of the data in bytes.
 * @param buffer       Source buffer containing the attribute data.
 * @return Number of bytes written, or 0 if no downstream node is connected.
 */
off_t
AttributeStreamNode::Write(const char* name, const char* foreignName,
	uint32 type, off_t size, const void* buffer)
{
	if (fWriteTo == NULL)
		return 0;

	return fWriteTo->Write(name, foreignName, type, size, buffer);
}


/**
 * @brief Begin driving attribute iteration from this node.
 *
 * Called on nodes that can feed data. Rewinds the upstream source and
 * returns true if the upstream pipeline is valid.
 *
 * @return true if driving can proceed, false if no upstream node is connected.
 */
bool
AttributeStreamNode::Drive()
{
	ASSERT(CanFeed());
	if (fReadFrom == NULL)
		return false;

	Rewind();
	return true;
}


/**
 * @brief Return the next AttributeInfo from the upstream source.
 *
 * @return Pointer to the next AttributeInfo, or NULL at end of stream.
 */
const AttributeInfo*
AttributeStreamNode::Next()
{
	if (fReadFrom != NULL)
		return fReadFrom->Next();

	return NULL;
}


/**
 * @brief Return a pointer to the raw data for the current attribute.
 *
 * @return Pointer to the attribute data buffer owned by the upstream node.
 */
const char*
AttributeStreamNode::Get()
{
	ASSERT(fReadFrom != NULL);

	return fReadFrom->Get();
}


/**
 * @brief Copy the current attribute's raw data into @a buffer.
 *
 * @param buffer  Caller-allocated buffer; must be at least AttributeInfo::Size() bytes.
 * @return true on success, false on failure.
 */
bool
AttributeStreamNode::Fill(char* buffer) const
{
	ASSERT(fReadFrom != NULL);

	return fReadFrom->Fill(buffer);
}


/**
 * @brief Start streaming attributes through the pipeline.
 *
 * If this node is at the head (no downstream), calls Drive() directly.
 * Otherwise delegates to the downstream node's Start().
 *
 * @return true if the pipeline started successfully.
 */
bool
AttributeStreamNode::Start()
{
	if (fWriteTo == NULL) {
		// we are at the head of the stream, start drivin'
		return Drive();
	}

	return fWriteTo->Start();
}


/**
 * @brief Sever all upstream and downstream links and propagate detachment.
 *
 * After this call, the node is isolated; neither fReadFrom nor fWriteTo
 * is valid.
 */
void
AttributeStreamNode::Detach()
{
	AttributeStreamNode* tmpFrom = fReadFrom;
	AttributeStreamNode* tmpTo = fWriteTo;
	fReadFrom = NULL;
	fWriteTo = NULL;

	if (tmpFrom != NULL)
		tmpFrom->Detach();

	if (tmpTo != NULL)
		tmpTo->Detach();
}


//	#pragma mark - AttributeStreamFileNode


/**
 * @brief Default constructor; creates a file node not yet bound to a BNode.
 */
AttributeStreamFileNode::AttributeStreamFileNode()
	:
	fNode(NULL)
{
}


/**
 * @brief Construct and immediately bind to @a node.
 *
 * @param node  The BNode whose extended attributes will be read or written.
 */
AttributeStreamFileNode::AttributeStreamFileNode(BNode* node)
	:
	fNode(node)
{
	ASSERT(fNode != NULL);
}


/**
 * @brief Rewind the attribute iterator on the underlying BNode.
 */
void
AttributeStreamFileNode::Rewind()
{
	_inherited::Rewind();
	fNode->RewindAttrs();
}


/**
 * @brief Bind this node to a different BNode.
 *
 * @param node  The new BNode to use for subsequent attribute operations.
 */
void
AttributeStreamFileNode::SetTo(BNode* node)
{
	fNode = node;
}


/**
 * @brief Check whether the bound BNode has an attribute matching name and type.
 *
 * @param name  Attribute name to look up.
 * @param type  Expected attribute type code.
 * @return Size of the attribute in bytes, or 0 if absent or type mismatch.
 */
off_t
AttributeStreamFileNode::Contains(const char* name, uint32 type)
{
	ThrowOnAssert(fNode != NULL);

	attr_info info;
	if (fNode->GetAttrInfo(name, &info) != B_OK)
		return 0;

	if (info.type != type)
		return 0;

	return info.size;
}


/**
 * @brief Read an attribute from the bound BNode into @a buffer.
 *
 * Tries the native @a name first; falls back to @a foreignName and byte-swaps
 * the data using @a swapFunc if that succeeds.
 *
 * @param name         Native attribute name.
 * @param foreignName  Alternate (foreign byte-order) attribute name, or NULL.
 * @param type         Expected attribute type code.
 * @param size         Size of @a buffer in bytes.
 * @param buffer       Destination for the attribute data.
 * @param swapFunc     Optional byte-swap function applied for foreign reads.
 * @return @a size on success, 0 if neither name yielded a full read.
 */
off_t
AttributeStreamFileNode::Read(const char* name, const char* foreignName,
	uint32 type, off_t size, void* buffer, void (*swapFunc)(void*))
{
	if (name != NULL
		&& fNode->ReadAttr(name, type, 0, buffer, (size_t)size) == size) {
		return size;
	}

	// didn't find the attribute under the native name, try the foreign name
	if (foreignName != NULL && fNode->ReadAttr(foreignName, type, 0, buffer,
			(size_t)size) == size) {
		// foreign attribute, swap the data
		if (swapFunc != NULL)
			(swapFunc)(buffer);

		return size;
	}

	return 0;
}


/**
 * @brief Write an attribute to the bound BNode and remove any stale foreign copy.
 *
 * @param name         Native attribute name to write.
 * @param foreignName  Foreign attribute name to remove after a successful write.
 * @param type         Attribute type code.
 * @param size         Size of @a buffer in bytes.
 * @param buffer       Source data to write.
 * @return Number of bytes written, or a negative error code on failure.
 */
off_t
AttributeStreamFileNode::Write(const char* name, const char* foreignName,
	uint32 type, off_t size, const void* buffer)
{
	ThrowOnAssert(fNode != NULL);

	off_t result = fNode->WriteAttr(name, type, 0, buffer, (size_t)size);
	if (result == size && foreignName != NULL) {
		// the write operation worked fine, remove the foreign attribute
		// to not let stale data hang around
		fNode->RemoveAttr(foreignName);
	}

	return result;
}


/**
 * @brief Pull all attributes from the upstream node and write them to the bound BNode.
 *
 * @return true when the drive loop completes (partial-write errors are silently tolerated).
 */
bool
AttributeStreamFileNode::Drive()
{
	if (!_inherited::Drive())
		return false;

	ThrowOnAssert(fNode != NULL);

	const AttributeInfo* attr;
	while ((attr = fReadFrom->Next()) != 0) {
		const char* data = fReadFrom->Get();
		off_t result = fNode->WriteAttr(attr->Name(), attr->Type(), 0,
			data, (size_t)attr->Size());
		if (result < attr->Size())
			return true;
	}

	return true;
}


/**
 * @brief Not valid for file source nodes — asserts and returns NULL.
 *
 * @return NULL (always).
 */
const char*
AttributeStreamFileNode::Get()
{
	ASSERT(fNode != NULL);
	TRESPASS();

	return NULL;
}


/**
 * @brief Copy the current attribute's raw data from the BNode into @a buffer.
 *
 * @param buffer  Caller-allocated buffer of at least fCurrentAttr.Size() bytes.
 * @return true if the full attribute was read successfully.
 */
bool
AttributeStreamFileNode::Fill(char* buffer) const
{
	ThrowOnAssert(fNode != NULL);

	return fNode->ReadAttr(fCurrentAttr.Name(), fCurrentAttr.Type(), 0,
		buffer, (size_t)fCurrentAttr.Size()) == (ssize_t)fCurrentAttr.Size();
}


/**
 * @brief Advance the BNode attribute iterator and return the next AttributeInfo.
 *
 * @return Pointer to the next attribute descriptor, or NULL at end of iteration.
 */
const AttributeInfo*
AttributeStreamFileNode::Next()
{
	ASSERT(fReadFrom == NULL);
	ThrowOnAssert(fNode != NULL);

	char attrName[256];
	if (fNode->GetNextAttrName(attrName) != B_OK)
		return NULL;

	attr_info info;
	if (fNode->GetAttrInfo(attrName, &info) != B_OK)
		return NULL;

	fCurrentAttr.SetTo(attrName, info);

	return &fCurrentAttr;
}


//	#pragma mark - AttributeStreamMemoryNode


/**
 * @brief Default constructor; creates an empty in-memory attribute store.
 */
AttributeStreamMemoryNode::AttributeStreamMemoryNode()
	:
	fAttributes(5),
	fCurrentIndex(-1)
{
}


/**
 * @brief Remove all cached attribute nodes from the in-memory store.
 */
void
AttributeStreamMemoryNode::MakeEmpty()
{
	fAttributes.MakeEmpty();
}


/**
 * @brief Reset the iteration cursor to before the first cached attribute.
 */
void
AttributeStreamMemoryNode::Rewind()
{
	_inherited::Rewind();
	fCurrentIndex = -1;
}


/**
 * @brief Locate a cached attribute by name and type.
 *
 * @param name  Attribute name to search for.
 * @param type  Attribute type code to match.
 * @return Index into fAttributes, or -1 if not found.
 */
int32
AttributeStreamMemoryNode::Find(const char* name, uint32 type) const
{
	int32 count = fAttributes.CountItems();
	for (int32 index = 0; index < count; index++) {
		if (strcmp(fAttributes.ItemAt(index)->fAttr.Name(), name) == 0
			&& fAttributes.ItemAt(index)->fAttr.Type() == type) {
			return index;
		}
	}

	return -1;
}


/**
 * @brief Return the size of a cached attribute, or 0 if not present.
 *
 * @param name  Attribute name.
 * @param type  Attribute type code.
 * @return Size in bytes, or 0 if not found.
 */
off_t
AttributeStreamMemoryNode::Contains(const char* name, uint32 type)
{
	int32 index = Find(name, type);

	return index < 0 ? 0 : fAttributes.ItemAt(index)->fAttr.Size();
}


/**
 * @brief Read an attribute from the in-memory cache, fetching from upstream if necessary.
 *
 * @param name        Native attribute name.
 * @param foreignName Must be NULL; memory nodes do not support foreign names.
 * @param type        Attribute type code.
 * @param bufferSize  Size of @a buffer in bytes.
 * @param buffer      Destination for the attribute data.
 * @param swapFunc    Must be NULL; memory nodes do not byte-swap.
 * @return Size of the attribute on success, 0 on failure.
 */
off_t
AttributeStreamMemoryNode::Read(const char* name,
	const char* DEBUG_ONLY(foreignName), uint32 type, off_t bufferSize,
	void* buffer, void (*DEBUG_ONLY(swapFunc))(void*))
{
	ASSERT(foreignName == NULL);
	ASSERT(swapFunc == NULL);

	AttrNode* attrNode = NULL;

	int32 index = Find(name, type);
	if (index < 0) {
		if (fReadFrom == NULL)
			return 0;

		off_t size = fReadFrom->Contains(name, type);
		if (size == 0)
			return 0;

		attrNode = BufferingGet(name, type, size);
		if (attrNode == NULL)
			return 0;
	} else
		attrNode = fAttributes.ItemAt(index);

	if (attrNode->fAttr.Size() > bufferSize)
		return 0;

	memcpy(buffer, attrNode->fData, (size_t)attrNode->fAttr.Size());

	return attrNode->fAttr.Size();
}


/**
 * @brief Copy @a buffer into a newly allocated AttrNode and store it in the cache.
 *
 * @param name    Attribute name.
 * @param type    Attribute type code.
 * @param size    Size of @a buffer in bytes.
 * @param buffer  Source data to copy.
 * @return @a size on success.
 */
off_t
AttributeStreamMemoryNode::Write(const char* name, const char*, uint32 type,
	off_t size, const void* buffer)
{
	char* newBuffer = new char[size];
	memcpy(newBuffer, buffer, (size_t)size);

	AttrNode* attrNode = new AttrNode(name, type, size, newBuffer);
	fAttributes.AddItem(attrNode);

	return size;
}


/**
 * @brief Eagerly pull all upstream attributes into the in-memory cache.
 *
 * @return true when all attributes have been buffered.
 */
bool
AttributeStreamMemoryNode::Drive()
{
	if (!_inherited::Drive())
		return false;

	while (BufferingGet())
		;

	return true;
}


/**
 * @brief Fetch one attribute of the given name/type/size from the upstream node.
 *
 * @param name  Attribute name.
 * @param type  Attribute type code.
 * @param size  Expected size in bytes.
 * @return Pointer to the newly created AttrNode, or NULL on failure.
 */
AttributeStreamMemoryNode::AttrNode*
AttributeStreamMemoryNode::BufferingGet(const char* name, uint32 type,
	off_t size)
{
	char* newBuffer = new char[size];
	if (!fReadFrom->Fill(newBuffer)) {
		delete[] newBuffer;
		return NULL;
	}

	AttrNode* attrNode = new AttrNode(name, type, size, newBuffer);
	fAttributes.AddItem(attrNode);

	return fAttributes.LastItem();
}


/**
 * @brief Fetch the next attribute from the upstream node using its own Next()/Fill().
 *
 * @return Pointer to the buffered AttrNode, or NULL if upstream is exhausted.
 */
AttributeStreamMemoryNode::AttrNode*
AttributeStreamMemoryNode::BufferingGet()
{
	if (fReadFrom == NULL)
		return NULL;

	const AttributeInfo* attr = fReadFrom->Next();
	if (attr == NULL)
		return NULL;

	return BufferingGet(attr->Name(), attr->Type(), attr->Size());
}


/**
 * @brief Advance the iteration cursor and return the next cached AttributeInfo.
 *
 * Buffers one more attribute from upstream when mid-stream.
 *
 * @return Pointer to the next AttributeInfo, or NULL when the cache is exhausted.
 */
const AttributeInfo*
AttributeStreamMemoryNode::Next()
{
	if (fReadFrom != NULL) {
		// the buffer is in the middle of the stream, get
		// one buffer at a time
		BufferingGet();
	}

	if (fCurrentIndex + 1 >= fAttributes.CountItems())
		return NULL;

	return &fAttributes.ItemAt(++fCurrentIndex)->fAttr;
}


/**
 * @brief Return a pointer to the raw data of the current attribute in the cache.
 *
 * @return Pointer to the attribute data owned by the current AttrNode.
 */
const char*
AttributeStreamMemoryNode::Get()
{
	ASSERT(fCurrentIndex < fAttributes.CountItems());

	return fAttributes.ItemAt(fCurrentIndex)->fData;
}


/**
 * @brief Copy the current cached attribute's data into @a buffer.
 *
 * @param buffer  Caller-allocated buffer of at least the current attribute's size.
 * @return true always (the data is already in memory).
 */
bool
AttributeStreamMemoryNode::Fill(char* buffer) const
{
	ASSERT(fCurrentIndex < fAttributes.CountItems());
	memcpy(buffer, fAttributes.ItemAt(fCurrentIndex)->fData,
		(size_t)fAttributes.ItemAt(fCurrentIndex)->fAttr.Size());

	return true;
}


//	#pragma mark - AttributeStreamTemplateNode


/**
 * @brief Construct a read-only node backed by a compile-time attribute template array.
 *
 * @param attrTemplates  Pointer to a static array of AttributeTemplate entries.
 * @param count          Number of entries in @a attrTemplates.
 */
AttributeStreamTemplateNode::AttributeStreamTemplateNode(
	const AttributeTemplate* attrTemplates, int32 count)
	:
	fAttributes(attrTemplates),
	fCurrentIndex(-1),
	fCount(count)
{
}


/**
 * @brief Return the size of a template attribute matching the given name and type.
 *
 * @param name  Attribute name to search for.
 * @param type  Attribute type code to match.
 * @return Attribute size in bytes, or 0 if not found.
 */
off_t
AttributeStreamTemplateNode::Contains(const char* name, uint32 type)
{
	int32 index = Find(name, type);
	if (index < 0)
		return 0;

	return fAttributes[index].fSize;
}


/**
 * @brief Reset the iteration cursor to before the first template entry.
 */
void
AttributeStreamTemplateNode::Rewind()
{
	fCurrentIndex = -1;
}


/**
 * @brief Advance to the next template entry and return its AttributeInfo.
 *
 * @return Pointer to the updated fCurrentAttr, or NULL at end of template array.
 */
const AttributeInfo*
AttributeStreamTemplateNode::Next()
{
	if (fCurrentIndex + 1 >= fCount)
		return NULL;

	++fCurrentIndex;

	fCurrentAttr.SetTo(fAttributes[fCurrentIndex].fAttributeName,
		fAttributes[fCurrentIndex].fAttributeType,
		fAttributes[fCurrentIndex].fSize);

	return &fCurrentAttr;
}


/**
 * @brief Return a pointer to the static data bits for the current template entry.
 *
 * @return Pointer to the compile-time attribute data.
 */
const char*
AttributeStreamTemplateNode::Get()
{
	ASSERT(fCurrentIndex < fCount);

	return fAttributes[fCurrentIndex].fBits;
}


/**
 * @brief Copy the current template entry's data into @a buffer.
 *
 * @param buffer  Caller-allocated buffer of at least the current entry's size.
 * @return true always (data is compile-time constant).
 */
bool
AttributeStreamTemplateNode::Fill(char* buffer) const
{
	ASSERT(fCurrentIndex < fCount);
	memcpy(buffer, fAttributes[fCurrentIndex].fBits,
		(size_t)fAttributes[fCurrentIndex].fSize);

	return true;
}


/**
 * @brief Locate a template entry by name and type.
 *
 * @param name  Attribute name to search for.
 * @param type  Attribute type code to match.
 * @return Index into fAttributes, or -1 if not found.
 */
int32
AttributeStreamTemplateNode::Find(const char* name, uint32 type) const
{
	for (int32 index = 0; index < fCount; index++) {
		if (fAttributes[index].fAttributeType == type &&
			strcmp(name, fAttributes[index].fAttributeName) == 0) {
			return index;
		}
	}

	return -1;
}


//	#pragma mark - AttributeStreamFilterNode


/**
 * @brief Default filter policy; passes every attribute through.
 *
 * Subclasses override this to selectively reject attributes.
 *
 * @return false always (accept all attributes).
 */
bool
AttributeStreamFilterNode::Reject(const char*, uint32, off_t)
{
	// simple pass everything filter
	return false;
}


/**
 * @brief Return the next upstream attribute that is not rejected by this filter.
 *
 * @return Pointer to the next accepted AttributeInfo, or NULL at end of stream.
 */
const AttributeInfo*
AttributeStreamFilterNode::Next()
{
	if (fReadFrom == NULL)
		return NULL;

	for (;;) {
		const AttributeInfo* attr = fReadFrom->Next();
		if (attr == NULL)
			break;

		if (!Reject(attr->Name(), attr->Type(), attr->Size()))
			return attr;
	}

	return NULL;
}


/**
 * @brief Check whether a non-rejected attribute exists upstream.
 *
 * @param name  Attribute name.
 * @param type  Attribute type code.
 * @return Size in bytes if the attribute exists and is accepted, 0 otherwise.
 */
off_t
AttributeStreamFilterNode::Contains(const char* name, uint32 type)
{
	if (fReadFrom == NULL)
		return 0;

	off_t size = fReadFrom->Contains(name, type);

	if (!Reject(name, type, size))
		return size;

	return 0;
}


/**
 * @brief Read an attribute from upstream, subject to this filter's policy.
 *
 * @param name         Native attribute name.
 * @param foreignName  Foreign attribute name fallback.
 * @param type         Attribute type code.
 * @param size         Size of @a buffer in bytes.
 * @param buffer       Destination for the attribute data.
 * @param swapFunc     Optional byte-swap function.
 * @return Number of bytes read, or 0 if the attribute was rejected or read failed.
 */
off_t
AttributeStreamFilterNode::Read(const char* name, const char* foreignName,
	uint32 type, off_t size, void* buffer, void (*swapFunc)(void*))
{
	if (fReadFrom == NULL)
		return 0;

	if (!Reject(name, type, size)) {
		return fReadFrom->Read(name, foreignName, type, size, buffer,
			swapFunc);
	}

	return 0;
}


/**
 * @brief Write an attribute downstream, subject to this filter's policy.
 *
 * Rejected attributes are silently swallowed (return @a size without writing).
 *
 * @param name         Native attribute name.
 * @param foreignName  Foreign attribute name to remove after successful write.
 * @param type         Attribute type code.
 * @param size         Size of @a buffer in bytes.
 * @param buffer       Source data to write.
 * @return @a size on success or rejection, 0 if no downstream node is connected.
 */
off_t
AttributeStreamFilterNode::Write(const char* name, const char* foreignName,
	uint32 type, off_t size, const void* buffer)
{
	if (fWriteTo == NULL)
		return 0;

	if (!Reject(name, type, size))
		return fWriteTo->Write(name, foreignName, type, size, buffer);

	return size;
}


//	#pragma mark - NamesToAcceptAttrFilter


/**
 * @brief Construct a filter that accepts only attributes in @a nameList.
 *
 * @param nameList  NULL-terminated array of attribute name strings to pass through.
 */
NamesToAcceptAttrFilter::NamesToAcceptAttrFilter(const char** nameList)
	:
	fNameList(nameList)
{
}


/**
 * @brief Reject an attribute unless its name appears in the allow-list.
 *
 * @param name  Attribute name to test.
 * @return false if @a name is in the allow-list (accepted), true otherwise.
 */
bool
NamesToAcceptAttrFilter::Reject(const char* name, uint32, off_t)
{
	for (int32 index = 0; ; index++) {
		if (fNameList[index] == NULL)
			break;

		if (strcmp(name, fNameList[index]) == 0) {
			//PRINT(("filter passing through %s\n", name));
			return false;
		}
	}

	//PRINT(("filter rejecting %s\n", name));
	return true;
}


//	#pragma mark - SelectiveAttributeTransformer


/**
 * @brief Construct a transformer that applies @a transformFunc to a single named attribute.
 *
 * @param attributeName  Name of the attribute to intercept and transform.
 * @param transformFunc  Callback invoked on matching attribute data; returns true on success.
 * @param params         Opaque user parameter forwarded to @a transformFunc.
 */
SelectiveAttributeTransformer::SelectiveAttributeTransformer(
	const char* attributeName,
	bool (*transformFunc)(const char* , uint32 , off_t, void*, void*),
	void* params)
	:
	fAttributeNameToTransform(attributeName),
	fTransformFunc(transformFunc),
	fTransformParams(params),
	fTransformedBuffers(10)
{
}


/**
 * @brief Destructor; frees all heap buffers allocated for transformed attribute data.
 */
SelectiveAttributeTransformer::~SelectiveAttributeTransformer()
{
	for (int32 index = fTransformedBuffers.CountItems() - 1; index >= 0;
			index--) {
		delete[] fTransformedBuffers.ItemAt(index);
	}
}


/**
 * @brief Discard all transformed-data buffers and reset the pipeline for re-use.
 */
void
SelectiveAttributeTransformer::Rewind()
{
	for (int32 index = fTransformedBuffers.CountItems() - 1; index >= 0;
			index--) {
		delete[] fTransformedBuffers.ItemAt(index);
	}

	fTransformedBuffers.MakeEmpty();
}


/**
 * @brief Read an attribute from upstream and apply the transform if the name matches.
 *
 * @param name         Native attribute name.
 * @param foreignName  Foreign attribute name fallback.
 * @param type         Attribute type code.
 * @param size         Size of @a buffer in bytes.
 * @param buffer       Destination for the (possibly transformed) attribute data.
 * @param swapFunc     Optional byte-swap function applied before the transform.
 * @return Number of bytes read from upstream.
 */
off_t
SelectiveAttributeTransformer::Read(const char* name, const char* foreignName,
	uint32 type, off_t size, void* buffer, void (*swapFunc)(void*))
{
	if (fReadFrom == NULL)
		return 0;

	off_t result = fReadFrom->Read(name, foreignName, type, size, buffer,
		swapFunc);

	if (WillTransform(name, type, size, (const char*)buffer))
		ApplyTransformer(name, type, size, (char*)buffer);

	return result;
}


/**
 * @brief Return whether this attribute should be transformed.
 *
 * @param name  Attribute name to test.
 * @return true if @a name matches fAttributeNameToTransform.
 */
bool
SelectiveAttributeTransformer::WillTransform(const char* name, uint32, off_t,
	const char*) const
{
	return strcmp(name, fAttributeNameToTransform) == 0;
}


/**
 * @brief Apply the transform function to @a data in place.
 *
 * @param name  Attribute name.
 * @param type  Attribute type code.
 * @param size  Size of @a data in bytes.
 * @param data  Mutable attribute data buffer.
 * @return Return value of fTransformFunc.
 */
bool
SelectiveAttributeTransformer::ApplyTransformer(const char* name, uint32 type,
	off_t size, char* data)
{
	return (fTransformFunc)(name, type, size, data, fTransformParams);
}


/**
 * @brief Copy @a data and apply the transform to the copy, returning the new buffer.
 *
 * The caller must not free the returned pointer; it is owned by fTransformedBuffers.
 *
 * @param name  Attribute name.
 * @param type  Attribute type code.
 * @param size  Size of @a data in bytes.
 * @param data  Original attribute data (may be NULL).
 * @return Heap-allocated transformed buffer, or NULL if the transform fails.
 */
char*
SelectiveAttributeTransformer::CopyAndApplyTransformer(const char* name,
	uint32 type, off_t size, const char* data)
{
	char* result = NULL;

	if (data != NULL) {
		result = new char[size];
		memcpy(result, data, (size_t)size);
	}

	if (!(fTransformFunc)(name, type, size, result, fTransformParams)) {
		delete[] result;
		return NULL;
	}

	return result;
}


/**
 * @brief Advance to the next upstream attribute and snapshot its info.
 *
 * @return Pointer to the next AttributeInfo, or NULL at end of stream.
 */
const AttributeInfo*
SelectiveAttributeTransformer::Next()
{
	const AttributeInfo* result = fReadFrom->Next();

	if (result == NULL)
		return NULL;

	fCurrentAttr.SetTo(*result);
	return result;
}


/**
 * @brief Return (possibly transformed) data for the current attribute.
 *
 * If the current attribute name matches, the data is copied and transformed before
 * being returned; the transformed buffer is tracked for later cleanup.
 *
 * @return Pointer to the attribute data (may be a newly allocated transformed copy).
 */
const char*
SelectiveAttributeTransformer::Get()
{
	if (fReadFrom == NULL)
		return NULL;

	const char* result = fReadFrom->Get();

	if (!WillTransform(fCurrentAttr.Name(), fCurrentAttr.Type(),
			fCurrentAttr.Size(), result)) {
		return result;
	}

	char* transformedData = CopyAndApplyTransformer(fCurrentAttr.Name(),
		fCurrentAttr.Type(), fCurrentAttr.Size(), result);

	// enlist for proper disposal when our job is done
	if (transformedData != NULL) {
		fTransformedBuffers.AddItem(transformedData);
		return transformedData;
	}

	return result;
}
