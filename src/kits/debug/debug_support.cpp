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
 *   Copyright 2005-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2010-2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file debug_support.cpp
 * @brief C-level debug support API for communicating with the kernel nub port.
 *
 * Implements the public debug_support API declared in <debug_support.h>.
 * Functions cover context lifecycle (init/destroy), memory read/write, CPU
 * state retrieval, stack unwinding, and the full symbol-lookup pipeline
 * including iterator-based symbol enumeration over both live team images and
 * standalone ELF files on disk.
 *
 * @see BDebugContext, SymbolLookup
 */


#include <new>

#include <string.h>

#include <AutoDeleter.h>
#include <debug_support.h>

#include "arch_debug_support.h"
#include "Image.h"
#include "SymbolLookup.h"


using std::nothrow;


/** @brief Opaque handle returned by debug_create_symbol_lookup_context(). */
struct debug_symbol_lookup_context {
	SymbolLookup*	lookup;
};

/** @brief Iterator state returned by debug_create_image_symbol_iterator(). */
struct debug_symbol_iterator : BPrivate::Debug::SymbolIterator {
	bool	ownsImage;

	/**
	 * @brief Default constructor — marks the iterator as not owning its image.
	 */
	debug_symbol_iterator()
		:
		ownsImage(false)
	{
	}

	/**
	 * @brief Destructor — deletes the image if this iterator owns it.
	 */
	~debug_symbol_iterator()
	{
		if (ownsImage)
			delete image;
	}
};


/**
 * @brief Initialise a debug_context for communicating with a team's nub port.
 *
 * Stores the team ID and nub port in @a context and creates a private reply
 * port used to receive responses from the debug nub.
 *
 * @param context   Caller-allocated context structure to initialise.
 * @param team      ID of the team to debug.
 * @param nubPort   The nub port obtained from install_team_debugger().
 * @return B_OK on success, B_BAD_VALUE if arguments are invalid, or the
 *         negative error code from create_port() on failure.
 */
// init_debug_context
status_t
init_debug_context(debug_context *context, team_id team, port_id nubPort)
{
	if (!context || team < 0 || nubPort < 0)
		return B_BAD_VALUE;

	context->team = team;
	context->nub_port = nubPort;

	// create the reply port
	context->reply_port = create_port(1, "debug reply port");
	if (context->reply_port < 0)
		return context->reply_port;

	return B_OK;
}


/**
 * @brief Release all resources held by a debug_context.
 *
 * Deletes the reply port and resets all fields to sentinel values so the
 * context cannot be accidentally reused after destruction.
 *
 * @param context  The context to destroy; may be NULL (no-op).
 */
// destroy_debug_context
void
destroy_debug_context(debug_context *context)
{
	if (context) {
		if (context->reply_port >= 0)
			delete_port(context->reply_port);

		context->team = -1;
		context->nub_port = -1;
		context->reply_port = -1;
	}
}


/**
 * @brief Send a typed message to the debug nub and optionally wait for a reply.
 *
 * Writes the message to the nub port (retrying on B_INTERRUPTED). If @a reply
 * is non-NULL, blocks on the reply port until a response arrives.
 *
 * @param context      Initialised debug context.
 * @param messageCode  The B_DEBUG_MESSAGE_* code identifying the request.
 * @param message      Pointer to the request structure.
 * @param messageSize  Byte size of @a message.
 * @param reply        Buffer to receive the reply, or NULL for fire-and-forget.
 * @param replySize    Byte capacity of @a reply.
 * @return B_OK on success, B_BAD_VALUE if context is NULL, or a port error.
 */
// send_debug_message
status_t
send_debug_message(debug_context *context, int32 messageCode,
	const void *message, int32 messageSize, void *reply, int32 replySize)
{
	if (!context)
		return B_BAD_VALUE;

	// send message
	while (true) {
		status_t result = write_port(context->nub_port, messageCode, message,
			messageSize);
		if (result == B_OK)
			break;
		if (result != B_INTERRUPTED)
			return result;
	}

	if (!reply)
		return B_OK;

	// read reply
	while (true) {
		int32 code;
		ssize_t bytesRead = read_port(context->reply_port, &code, reply,
			replySize);
		if (bytesRead > 0)
			return B_OK;
		if (bytesRead != B_INTERRUPTED)
			return bytesRead;
	}
}


/**
 * @brief Read up to B_MAX_READ_WRITE_MEMORY_SIZE bytes from the target team.
 *
 * Sends a B_DEBUG_MESSAGE_READ_MEMORY request and copies the returned data
 * into @a buffer. If @a size exceeds the maximum, it is silently clamped.
 *
 * @param context  Initialised debug context.
 * @param address  Address in the target team's address space to read from.
 * @param buffer   Destination buffer in the caller's address space.
 * @param size     Number of bytes requested.
 * @return The number of bytes actually read on success, or a negative error
 *         code on failure.
 */
// debug_read_memory_partial
ssize_t
debug_read_memory_partial(debug_context *context, const void *address,
	void *buffer, size_t size)
{
	if (!context)
		return B_BAD_VALUE;

	if (size == 0)
		return 0;
	if (size > B_MAX_READ_WRITE_MEMORY_SIZE)
		size = B_MAX_READ_WRITE_MEMORY_SIZE;

	// prepare the message
	debug_nub_read_memory message;
	message.reply_port = context->reply_port;
	message.address = (void*)address;
	message.size = size;

	// send the message
	debug_nub_read_memory_reply reply;
	status_t error = send_debug_message(context, B_DEBUG_MESSAGE_READ_MEMORY,
		&message, sizeof(message), &reply, sizeof(reply));

	if (error != B_OK)
		return error;
	if (reply.error != B_OK)
		return reply.error;

	// copy the read data
	memcpy(buffer, reply.data, reply.size);
	return reply.size;
}


/**
 * @brief Read an arbitrary number of bytes from the target team's address space.
 *
 * Issues successive debug_read_memory_partial() calls until all @a size bytes
 * have been read or an error occurs. Partial reads (e.g. at a segment boundary)
 * are returned rather than treated as errors when some data was already read.
 *
 * @param context  Initialised debug context.
 * @param _address Starting address in the target team.
 * @param _buffer  Destination buffer in the caller's address space.
 * @param size     Total number of bytes to read.
 * @return Total bytes read on success, or a negative error code on failure.
 */
// debug_read_memory
ssize_t
debug_read_memory(debug_context *context, const void *_address, void *_buffer,
	size_t size)
{
	const char *address = (const char *)_address;
	char *buffer = (char*)_buffer;

	// check parameters
	if (!context || !address || !buffer)
		return B_BAD_VALUE;
	if (size == 0)
		return 0;

	// read as long as we can read data
	ssize_t sumRead = 0;
	while (size > 0) {
		ssize_t bytesRead = debug_read_memory_partial(context, address, buffer,
			size);
		if (bytesRead < 0) {
			if (sumRead > 0)
				return sumRead;
			return bytesRead;
		}

		address += bytesRead;
		buffer += bytesRead;
		sumRead += bytesRead;
		size -= bytesRead;
	}

	return sumRead;
}


/**
 * @brief Read a NUL-terminated string from the target team's address space.
 *
 * Reads data in chunks until a NUL terminator is found or the buffer is full.
 * The output buffer is always NUL-terminated on return. If the buffer is too
 * small to hold the full string, the last character is replaced with '\0' and
 * the total bytes consumed (including the truncated portion) are returned.
 *
 * @param context  Initialised debug context.
 * @param _address Address of the string in the target team.
 * @param buffer   Caller-supplied destination buffer.
 * @param size     Byte capacity of @a buffer (must be > 0).
 * @return Number of characters stored (excluding NUL) on success, or a
 *         negative error code if the first read fails.
 * @note B_BAD_VALUE is returned when any pointer argument is NULL or size == 0.
 */
// debug_read_string
ssize_t
debug_read_string(debug_context *context, const void *_address, char *buffer,
	size_t size)
{
	const char *address = (const char *)_address;

	// check parameters
	if (!context || !address || !buffer || size == 0)
		return B_BAD_VALUE;

	// read as long as we can read data
	ssize_t sumRead = 0;
	while (size > 0) {
		ssize_t bytesRead = debug_read_memory_partial(context, address, buffer,
			size);
		if (bytesRead < 0) {
			// always null-terminate what we have (even, if it is an empty
			// string) and be done
			*buffer = '\0';
			return (sumRead > 0 ? sumRead : bytesRead);
		}

		int chunkSize = strnlen(buffer, bytesRead);
		if (chunkSize < bytesRead) {
			// we found a terminating null
			sumRead += chunkSize;
			return sumRead;
		}

		address += bytesRead;
		buffer += bytesRead;
		sumRead += bytesRead;
		size -= bytesRead;
	}

	// We filled the complete buffer without encountering a terminating null
	// replace the last char. But nevertheless return the full size to indicate
	// that the buffer was too small.
	buffer[-1] = '\0';

	return sumRead;
}


/**
 * @brief Write up to B_MAX_READ_WRITE_MEMORY_SIZE bytes into the target team.
 *
 * Sends a B_DEBUG_MESSAGE_WRITE_MEMORY request carrying the payload copied
 * from @a buffer. If @a size exceeds the maximum, it is silently clamped.
 *
 * @param context  Initialised debug context.
 * @param address  Destination address in the target team's address space.
 * @param buffer   Source buffer in the caller's address space.
 * @param size     Number of bytes to write.
 * @return The number of bytes actually written on success, or a negative error
 *         code on failure.
 */
// debug_write_memory_partial
ssize_t
debug_write_memory_partial(debug_context *context, const void *address,
	void *buffer, size_t size)
{
	if (!context)
		return B_BAD_VALUE;

	if (size == 0)
		return 0;
	if (size > B_MAX_READ_WRITE_MEMORY_SIZE)
		size = B_MAX_READ_WRITE_MEMORY_SIZE;

	// prepare the message
	debug_nub_write_memory message;
	message.reply_port = context->reply_port;
	message.address = (void*)address;
	message.size = size;
	memcpy(message.data, buffer, size);

	// send the message
	debug_nub_write_memory_reply reply;
	status_t error = send_debug_message(context, B_DEBUG_MESSAGE_WRITE_MEMORY,
		&message, sizeof(message), &reply, sizeof(reply));

	if (error != B_OK)
		return error;
	if (reply.error != B_OK)
		return reply.error;

	return reply.size;
}


/**
 * @brief Write an arbitrary number of bytes into the target team's address space.
 *
 * Issues successive debug_write_memory_partial() calls until all @a size bytes
 * have been written or an error occurs.
 *
 * @param context  Initialised debug context.
 * @param _address Starting destination address in the target team.
 * @param _buffer  Source buffer in the caller's address space.
 * @param size     Total number of bytes to write.
 * @return Total bytes written on success, or a negative error code on failure.
 */
// debug_write_memory
ssize_t
debug_write_memory(debug_context *context, const void *_address, void *_buffer,
	size_t size)
{
	const char *address = (const char *)_address;
	char *buffer = (char*)_buffer;

	// check parameters
	if (!context || !address || !buffer)
		return B_BAD_VALUE;
	if (size == 0)
		return 0;

	ssize_t sumWritten = 0;
	while (size > 0) {
		ssize_t bytesWritten = debug_write_memory_partial(context, address, buffer,
			size);
		if (bytesWritten < 0) {
			if (sumWritten > 0)
				return sumWritten;
			return bytesWritten;
		}

		address += bytesWritten;
		buffer += bytesWritten;
		sumWritten += bytesWritten;
		size -= bytesWritten;
	}

	return sumWritten;
}


/**
 * @brief Retrieve the complete CPU register state of a suspended thread.
 *
 * Sends a B_DEBUG_MESSAGE_GET_CPU_STATE request and returns the full register
 * snapshot. Optionally also returns the debug message code that caused the
 * thread to stop.
 *
 * @param context      Initialised debug context.
 * @param thread       The thread whose CPU state is requested.
 * @param messageCode  Output — receives the message code, or unchanged if NULL.
 * @param cpuState     Output — receives the full CPU register state.
 * @return B_OK on success, B_BAD_VALUE if required pointers are NULL, or a
 *         nub communication error.
 */
// debug_get_cpu_state
status_t
debug_get_cpu_state(debug_context *context, thread_id thread,
	debug_debugger_message *messageCode, debug_cpu_state *cpuState)
{
	if (!context || !cpuState)
		return B_BAD_VALUE;

	// prepare message
	debug_nub_get_cpu_state message;
	message.reply_port = context->reply_port;
	message.thread = thread;

	// send message
	debug_nub_get_cpu_state_reply reply;
	status_t error = send_debug_message(context, B_DEBUG_MESSAGE_GET_CPU_STATE,
		&message, sizeof(message), &reply, sizeof(reply));
	if (error == B_OK)
		error = reply.error;

	// get state
	if (error == B_OK) {
		*cpuState = reply.cpu_state;
		if (messageCode)
			*messageCode = reply.message;
	}

	return error;
}


// #pragma mark -

/**
 * @brief Retrieve the instruction pointer and stack frame for a thread.
 *
 * Delegates to the architecture-specific arch_debug_get_instruction_pointer()
 * after validating all pointer arguments.
 *
 * @param context           Initialised debug context.
 * @param thread            Target thread ID.
 * @param ip                Output — receives the current instruction pointer.
 * @param stackFrameAddress Output — receives the current stack frame address.
 * @return B_OK on success, B_BAD_VALUE if any pointer is NULL, or an arch
 *         error.
 */
// debug_get_instruction_pointer
status_t
debug_get_instruction_pointer(debug_context *context, thread_id thread,
	void **ip, void **stackFrameAddress)
{
	if (!context || !ip || !stackFrameAddress)
		return B_BAD_VALUE;

	return arch_debug_get_instruction_pointer(context, thread, ip,
		stackFrameAddress);
}


/**
 * @brief Walk one level up the call stack from a given frame address.
 *
 * Delegates to the architecture-specific arch_debug_get_stack_frame() after
 * validating all pointer arguments.
 *
 * @param context            Initialised debug context.
 * @param stackFrameAddress  Address of the current frame in the target team.
 * @param stackFrameInfo     Output — receives parent frame and return address.
 * @return B_OK on success, B_BAD_VALUE if any pointer is NULL, or an arch
 *         error.
 */
// debug_get_stack_frame
status_t
debug_get_stack_frame(debug_context *context, void *stackFrameAddress,
	debug_stack_frame_info *stackFrameInfo)
{
	if (!context || !stackFrameAddress || !stackFrameInfo)
		return B_BAD_VALUE;

	return arch_debug_get_stack_frame(context, stackFrameAddress,
		stackFrameInfo);
}


// #pragma mark -

/**
 * @brief Create a symbol lookup context for the given team and optional image.
 *
 * Allocates a debug_symbol_lookup_context, constructs a SymbolLookup object,
 * and calls SymbolLookup::Init(). On success the caller receives a handle
 * suitable for debug_lookup_symbol_address() and related functions.
 *
 * @param context        Initialised debug context for the target team.
 * @param image          image_id to restrict lookup to one image, or -1 for
 *                       all images in the team.
 * @param _lookupContext Output — receives the newly allocated lookup context.
 * @return B_OK on success, B_BAD_VALUE if required pointers are NULL,
 *         B_NO_MEMORY on allocation failure, or an initialisation error from
 *         SymbolLookup::Init().
 */
// debug_create_symbol_lookup_context
status_t
debug_create_symbol_lookup_context(debug_context *context, image_id image,
	debug_symbol_lookup_context **_lookupContext)
{
	if (context == NULL || _lookupContext == NULL)
		return B_BAD_VALUE;

	// create the lookup context
	debug_symbol_lookup_context *lookupContext
		= new(std::nothrow) debug_symbol_lookup_context;
	if (lookupContext == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<debug_symbol_lookup_context> contextDeleter(lookupContext);

	// create and init symbol lookup
	SymbolLookup *lookup = new(std::nothrow) SymbolLookup(context, image);
	if (lookup == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<SymbolLookup> lookupDeleter(lookup);

	try {
		status_t error = lookup->Init();
		if (error != B_OK)
			return error;
	} catch (BPrivate::Debug::Exception& exception) {
		return exception.Error();
	}

	// everything went fine: return the result
	lookupContext->lookup = lookup;
	*_lookupContext = lookupContext;
	contextDeleter.Detach();
	lookupDeleter.Detach();

	return B_OK;
}


/**
 * @brief Destroy a symbol lookup context and release all associated resources.
 *
 * @param lookupContext  The context to destroy; may be NULL (no-op).
 */
// debug_delete_symbol_lookup_context
void
debug_delete_symbol_lookup_context(debug_symbol_lookup_context *lookupContext)
{
	if (lookupContext) {
		delete lookupContext->lookup;
		delete lookupContext;
	}
}


/**
 * @brief Look up a symbol by name and type within a specific image.
 *
 * @param lookupContext   A valid lookup context previously created with
 *                        debug_create_symbol_lookup_context().
 * @param image           The image_id to search within.
 * @param name            Null-terminated symbol name.
 * @param symbolType      Required type (B_SYMBOL_TYPE_TEXT, _DATA, or _ANY).
 * @param _symbolLocation Output — receives the runtime address of the symbol.
 * @param _symbolSize     Output — receives the symbol's size in bytes.
 * @param _symbolType     Output — receives the resolved symbol type.
 * @return B_OK on success, B_BAD_VALUE if lookupContext is invalid, or
 *         B_ENTRY_NOT_FOUND if the symbol is not present.
 */
// debug_get_symbol
status_t
debug_get_symbol(debug_symbol_lookup_context* lookupContext, image_id image,
	const char* name, int32 symbolType, void** _symbolLocation,
	size_t* _symbolSize, int32* _symbolType)
{
	if (!lookupContext || !lookupContext->lookup)
		return B_BAD_VALUE;
	SymbolLookup* lookup = lookupContext->lookup;

	return lookup->GetSymbol(image, name, symbolType, _symbolLocation,
		_symbolSize, _symbolType);
}


/**
 * @brief Resolve a runtime address to a symbol name and image name.
 *
 * Searches all loaded images for the symbol that best covers @a address and
 * fills in the caller-supplied string buffers. If the address falls outside
 * any known symbol's range, the image name is still returned with a NULL
 * symbol name.
 *
 * @param lookupContext  A valid lookup context.
 * @param address        Runtime address to resolve.
 * @param baseAddress    Output — receives the base address of the found symbol.
 * @param symbolName     Output buffer for the symbol name.
 * @param symbolNameSize Byte capacity of @a symbolName.
 * @param imageName      Output buffer for the image path.
 * @param imageNameSize  Byte capacity of @a imageName (clamped to B_PATH_NAME_LENGTH).
 * @param exactMatch     Output — set to true when the address is within the symbol.
 * @return B_OK on success, B_BAD_VALUE if lookupContext is invalid, or
 *         B_ENTRY_NOT_FOUND if the address belongs to no known image.
 */
// debug_lookup_symbol_address
status_t
debug_lookup_symbol_address(debug_symbol_lookup_context *lookupContext,
	const void *address, void **baseAddress, char *symbolName,
	int32 symbolNameSize, char *imageName, int32 imageNameSize,
	bool *exactMatch)
{
	if (!lookupContext || !lookupContext->lookup)
		return B_BAD_VALUE;
	SymbolLookup *lookup = lookupContext->lookup;

	// find the symbol
	addr_t _baseAddress;
	const char *_symbolName;
	size_t _symbolNameLen;
	const char *_imageName;
	try {
		status_t error = lookup->LookupSymbolAddress((addr_t)address,
			&_baseAddress, &_symbolName, &_symbolNameLen, &_imageName,
			exactMatch);
		if (error != B_OK)
			return error;
	} catch (BPrivate::Debug::Exception& exception) {
		return exception.Error();
	}

	// translate/copy the results
	if (baseAddress)
		*baseAddress = (void*)_baseAddress;

	if (symbolName && symbolNameSize > 0) {
		if (_symbolName && _symbolNameLen > 0) {
			strlcpy(symbolName, _symbolName,
				min_c((size_t)symbolNameSize, _symbolNameLen + 1));
		} else
			symbolName[0] = '\0';
	}

	if (imageName) {
		if (imageNameSize > B_PATH_NAME_LENGTH)
			imageNameSize = B_PATH_NAME_LENGTH;
		strlcpy(imageName, _imageName, imageNameSize);
	}

	return B_OK;
}


/**
 * @brief Create a symbol iterator anchored to a specific image_id.
 *
 * Allocates a debug_symbol_iterator and calls SymbolLookup::InitSymbolIterator().
 * If the initial lookup fails (e.g. after a fork where image IDs may not yet
 * match), falls back to an address-based search using the image's text base.
 *
 * @param lookupContext  A valid lookup context.
 * @param imageID        The image to iterate over.
 * @param _iterator      Output — receives the newly allocated iterator.
 * @return B_OK on success, B_BAD_VALUE if lookupContext is invalid,
 *         B_NO_MEMORY on allocation failure, or B_ENTRY_NOT_FOUND if the image
 *         cannot be located.
 */
status_t
debug_create_image_symbol_iterator(debug_symbol_lookup_context* lookupContext,
	image_id imageID, debug_symbol_iterator** _iterator)
{
	if (!lookupContext || !lookupContext->lookup)
		return B_BAD_VALUE;
	SymbolLookup *lookup = lookupContext->lookup;

	debug_symbol_iterator* iterator = new(std::nothrow) debug_symbol_iterator;
	if (iterator == NULL)
		return B_NO_MEMORY;

	status_t error;
	try {
		error = lookup->InitSymbolIterator(imageID, *iterator);
	} catch (BPrivate::Debug::Exception& exception) {
		error = exception.Error();
	}

	// Work-around for a runtime loader problem. A freshly fork()ed child does
	// still have image_t structures with the parent's image ID's, so we
	// wouldn't find the image in this case.
	if (error != B_OK) {
		// Get the image info and re-try looking with the text base address.
		// Note, that we can't easily check whether the image is part of the
		// target team at all (there's no image_info::team, we'd have to
		// iterate through all images).
		image_info imageInfo;
		error = get_image_info(imageID, &imageInfo);
		if (error == B_OK) {
			try {
				error = lookup->InitSymbolIteratorByAddress(
					(addr_t)imageInfo.text, *iterator);
			} catch (BPrivate::Debug::Exception& exception) {
				error = exception.Error();
			}
		}
	}

	if (error != B_OK) {
		delete iterator;
		return error;
	}

	*_iterator = iterator;
	return B_OK;
}


/**
 * @brief Create a symbol iterator for a standalone ELF file on disk.
 *
 * Opens the file at @a path, maps it, and parses its ELF symbol table without
 * requiring an active debug context. Useful for offline symbol resolution.
 *
 * @param path       Absolute path to the ELF file on disk.
 * @param _iterator  Output — receives the newly allocated iterator.
 * @return B_OK on success, B_BAD_VALUE if @a path is NULL, B_NO_MEMORY on
 *         allocation failure, or a file/ELF parse error.
 */
status_t
debug_create_file_symbol_iterator(const char* path,
	debug_symbol_iterator** _iterator)
{
	if (path == NULL)
		return B_BAD_VALUE;

	// create the iterator
	debug_symbol_iterator* iterator = new(std::nothrow) debug_symbol_iterator;
	if (iterator == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<debug_symbol_iterator> iteratorDeleter(iterator);

	// create the image file
	ImageFile* imageFile = new(std::nothrow) ImageFile;
	if (imageFile == NULL)
		return B_NO_MEMORY;

	// init the iterator
	iterator->image = imageFile;
	iterator->ownsImage = true;
	iterator->currentIndex = -1;

	// init the image file
	status_t error = imageFile->Init(path);
	if (error != B_OK)
		return error;

	iteratorDeleter.Detach();
	*_iterator = iterator;

	return B_OK;
}


/**
 * @brief Destroy a symbol iterator and free its resources.
 *
 * @param iterator  The iterator to destroy; may be NULL (no-op).
 */
void
debug_delete_symbol_iterator(debug_symbol_iterator* iterator)
{
	delete iterator;
}


/**
 * @brief Advance a symbol iterator and return the next symbol's attributes.
 *
 * Calls the underlying Image::NextSymbol() on the iterator's associated image
 * and copies the name (truncated to fit @a nameBufferLength) into the caller's
 * buffer.
 *
 * @param iterator          A valid iterator returned by debug_create_*_symbol_iterator().
 * @param nameBuffer        Caller-supplied buffer to receive the symbol name.
 * @param nameBufferLength  Byte capacity of @a nameBuffer.
 * @param _symbolType       Output — receives B_SYMBOL_TYPE_TEXT or _DATA.
 * @param _symbolLocation   Output — receives the runtime address of the symbol.
 * @param _symbolSize       Output — receives the symbol size in bytes.
 * @return B_OK when a symbol is returned, B_BAD_VALUE if iterator or its image
 *         is NULL, B_ENTRY_NOT_FOUND at end-of-table.
 */
// debug_next_image_symbol
status_t
debug_next_image_symbol(debug_symbol_iterator* iterator, char* nameBuffer,
	size_t nameBufferLength, int32* _symbolType, void** _symbolLocation,
	size_t* _symbolSize)
{
	if (iterator == NULL || iterator->image == NULL)
		return B_BAD_VALUE;

	const char* symbolName;
	size_t symbolNameLen;
	addr_t symbolLocation;

	try {
		status_t error = iterator->image->NextSymbol(iterator->currentIndex,
			&symbolName, &symbolNameLen, &symbolLocation, _symbolSize,
			_symbolType);
		if (error != B_OK)
			return error;
	} catch (BPrivate::Debug::Exception& exception) {
		return exception.Error();
	}

	*_symbolLocation = (void*)symbolLocation;

	if (symbolName != NULL && symbolNameLen > 0) {
		strlcpy(nameBuffer, symbolName,
			min_c(nameBufferLength, symbolNameLen + 1));
	} else
		nameBuffer[0] = '\0';

	return B_OK;
}


/**
 * @brief Retrieve the image_info for the image associated with an iterator.
 *
 * @param iterator  A valid symbol iterator.
 * @param info      Output — receives a copy of the image's image_info record.
 * @return B_OK on success, B_BAD_VALUE if any pointer is NULL.
 */
status_t
debug_get_symbol_iterator_image_info(debug_symbol_iterator* iterator,
	image_info* info)
{
	if (iterator == NULL || iterator->image == NULL || info == NULL)
		return B_BAD_VALUE;

	*info = iterator->image->Info();
	return B_OK;
}
