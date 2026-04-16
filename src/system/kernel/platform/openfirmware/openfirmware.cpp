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
 *   Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Copyright 2019, Adrien Destugues, pulkomandy@pulkomandy.tk
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file openfirmware.cpp
 * @brief Thin C wrappers around IEEE 1275 Open Firmware client services.
 *
 * Each entry here packs its arguments into the struct layout expected by
 * the Open Firmware client-interface trampoline (gCallOpenFirmware) and
 * returns the result to the caller. Covers device-tree traversal
 * (finddevice/child/peer/parent), property access (getprop/setprop/
 * getproplen/nextprop), package/instance conversion, I/O (open/close/
 * read/write/seek and block queries), memory claim/release, and
 * miscellaneous services (test, milliseconds, exit).
 */

#include <platform/openfirmware/openfirmware.h>

#include <stdarg.h>


// OpenFirmware entry function
static intptr_t (*gCallOpenFirmware)(void *) = 0;
intptr_t gChosen;


/**
 * @brief Initializes the Open Firmware client wrapper by storing the
 *        entry-point trampoline and caching the /chosen node handle.
 * @param openFirmwareEntry Pointer to the firmware client-interface callback.
 * @return B_OK on success; B_ERROR if /chosen cannot be located.
 */
status_t
of_init(intptr_t (*openFirmwareEntry)(void *))
{
	gCallOpenFirmware = openFirmwareEntry;

	gChosen = of_finddevice("/chosen");
	if (gChosen == OF_FAILED)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Generic client-function trampoline: packs a variadic argument
 *        list into the firmware call structure and copies back return
 *        values.
 * @param method     Name of the Open Firmware client service to invoke.
 * @param numArgs    Number of input arguments that follow.
 * @param numReturns Number of output arguments expected (pointers to store into).
 * @return 0 on success; OF_FAILED if the firmware call fails.
 */
intptr_t
of_call_client_function(const char *method, intptr_t numArgs,
	intptr_t numReturns, ...)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		void		*args[10];
	} args = {method, numArgs, numReturns};
	va_list list;
	int i;

	// iterate over all arguments and copy them into the
	// structure passed over to the OpenFirmware

	va_start(list, numReturns);
	for (i = 0; i < numArgs; i++) {
		// copy args
		args.args[i] = (void *)va_arg(list, void *);
	}
	for (i = numArgs; i < numArgs + numReturns; i++) {
		// clear return values
		args.args[i] = NULL;
	}

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	if (numReturns > 0) {
		// copy return values over to the provided location

		for (i = numArgs; i < numArgs + numReturns; i++) {
			void **store = va_arg(list, void **);
			if (store)
				*store = args.args[i];
		}
	}
	va_end(list);

	return 0;
}


/**
 * @brief Evaluates a Forth command string via the "interpret" client
 *        service, pushing arguments on the stack and collecting the
 *        requested number of results.
 * @param command    Forth source text to evaluate.
 * @param numArgs    Number of stack arguments to push.
 * @param numReturns Number of stack results to pop back.
 * @return 0 on success; OF_FAILED if the firmware reports a catch.
 */
intptr_t
of_interpret(const char *command, intptr_t numArgs, intptr_t numReturns, ...)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
			// "IN:	[string] cmd, stack_arg1, ..., stack_argP
			// OUT:	catch-result, stack_result1, ..., stack_resultQ
			// [...]
			// An implementation shall allow at least six stack_arg and six
			// stack_result items."
		const char	*command;
		void		*args[13];
	} args = {"interpret", numArgs + 1, numReturns + 1, command};
	va_list list;
	int i;

	// iterate over all arguments and copy them into the
	// structure passed over to the OpenFirmware

	va_start(list, numReturns);
	for (i = 0; i < numArgs; i++) {
		// copy args
		args.args[i] = (void *)va_arg(list, void *);
	}
	for (i = numArgs; i < numArgs + numReturns + 1; i++) {
		// clear return values
		args.args[i] = NULL;
	}

	// args.args[numArgs] is the "catch-result" return value
	if (gCallOpenFirmware(&args) == OF_FAILED || args.args[numArgs])
		return OF_FAILED;

	if (numReturns > 0) {
		// copy return values over to the provided location

		for (i = numArgs + 1; i < numArgs + 1 + numReturns; i++) {
			void **store = va_arg(list, void **);
			if (store)
				*store = args.args[i];
		}
	}
	va_end(list);

	return 0;
}


/**
 * @brief Invokes a method on an Open Firmware instance via the
 *        "call-method" client service.
 * @param handle     Instance handle the method is invoked on.
 * @param method     Name of the method to call.
 * @param numArgs    Number of stack arguments to push.
 * @param numReturns Number of stack results to pop.
 * @return 0 on success; OF_FAILED if the firmware reports a catch.
 */
intptr_t
of_call_method(uint32_t handle, const char *method, intptr_t numArgs,
	intptr_t numReturns, ...)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
			// "IN:	[string] method, ihandle, stack_arg1, ..., stack_argP
			// OUT:	catch-result, stack_result1, ..., stack_resultQ
			// [...]
			// An implementation shall allow at least six stack_arg and six
			// stack_result items."
		const char	*method;
		intptr_t	handle;
		void		*args[13];
	} args = {"call-method", numArgs + 2, numReturns + 1, method, handle};
	va_list list;
	int i;

	// iterate over all arguments and copy them into the
	// structure passed over to the OpenFirmware

	va_start(list, numReturns);
	for (i = 0; i < numArgs; i++) {
		// copy args
		args.args[i] = (void *)va_arg(list, void *);
	}
	for (i = numArgs; i < numArgs + numReturns + 1; i++) {
		// clear return values
		args.args[i] = NULL;
	}

	// args.args[numArgs] is the "catch-result" return value
	if (gCallOpenFirmware(&args) == OF_FAILED || args.args[numArgs])
		return OF_FAILED;

	if (numReturns > 0) {
		// copy return values over to the provided location

		for (i = numArgs + 1; i < numArgs + 1 + numReturns; i++) {
			void **store = va_arg(list, void **);
			if (store)
				*store = args.args[i];
		}
	}
	va_end(list);

	return 0;
}


/**
 * @brief Looks up a device-tree node by its path string.
 * @param device Device path (e.g. "/chosen").
 * @return Phandle of the node, or OF_FAILED if it does not exist.
 */
intptr_t
of_finddevice(const char *device)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		const char	*device;
		intptr_t	handle;
	} args = {"finddevice", 1, 1, device, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.handle;
}


/**
 * @brief Returns the first child of the given device-tree node.
 * @param node Parent phandle.
 * @return Phandle of the first child, 0 if none, OF_FAILED on error.
 */
intptr_t
of_child(intptr_t node)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	node;
		intptr_t	child;
	} args = {"child", 1, 1, node, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.child;
}


/**
 * @brief Returns the next sibling of the given device-tree node.
 * @param node Current phandle (0 asks for the root's first sibling).
 * @return Phandle of the sibling, 0 if none, OF_FAILED on error.
 */
intptr_t
of_peer(intptr_t node)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	node;
		intptr_t	next_sibling;
	} args = {"peer", 1, 1, node, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.next_sibling;
}


/**
 * @brief Returns the parent of the given device-tree node.
 * @param node Child phandle.
 * @return Phandle of the parent, or OF_FAILED on error.
 */
intptr_t
of_parent(intptr_t node)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	node;
		intptr_t	parent;
	} args = {"parent", 1, 1, node, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.parent;
}


/**
 * @brief Converts an instance handle (ihandle) to a device path string.
 * @param instance   Open instance handle.
 * @param pathBuffer Caller-supplied buffer that receives the path.
 * @param bufferSize Size of pathBuffer in bytes.
 * @return Number of bytes written, or OF_FAILED on error.
 */
intptr_t
of_instance_to_path(uint32_t instance, char *pathBuffer, intptr_t bufferSize)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	instance;
		char		*path_buffer;
		intptr_t	buffer_size;
		intptr_t	size;
	} args = {"instance-to-path", 3, 1, instance, pathBuffer, bufferSize, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.size;
}


/**
 * @brief Converts an instance handle (ihandle) to its package phandle.
 * @param instance Open instance handle.
 * @return The corresponding package phandle, or OF_FAILED on error.
 */
intptr_t
of_instance_to_package(uint32_t instance)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	instance;
		intptr_t	package;
	} args = {"instance-to-package", 1, 1, instance, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.package;
}


/**
 * @brief Retrieves the value of a named property from a device-tree node.
 * @param package    Phandle of the node to query.
 * @param property   Property name.
 * @param buffer     Output buffer that receives the value.
 * @param bufferSize Size of buffer in bytes.
 * @return Number of bytes written, or OF_FAILED on error.
 */
intptr_t
of_getprop(intptr_t package, const char *property, void *buffer, intptr_t bufferSize)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	package;
		const char	*property;
		void		*buffer;
		intptr_t	buffer_size;
		intptr_t	size;
	} args = {"getprop", 4, 1, package, property, buffer, bufferSize, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.size;
}


/**
 * @brief Sets the value of a named property on a device-tree node.
 * @param package    Phandle of the node to modify.
 * @param property   Property name.
 * @param buffer     New value to write.
 * @param bufferSize Size of buffer in bytes.
 * @return Number of bytes actually written, or OF_FAILED on error.
 */
intptr_t
of_setprop(intptr_t package, const char *property, const void *buffer,
	intptr_t bufferSize)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	package;
		const char	*property;
		const void	*buffer;
		intptr_t	buffer_size;
		intptr_t	size;
	} args = {"setprop", 4, 1, package, property, buffer, bufferSize, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.size;
}


/**
 * @brief Returns the length in bytes of a named property.
 * @param package  Phandle of the node to query.
 * @param property Property name.
 * @return Length in bytes, or OF_FAILED on error.
 */
intptr_t
of_getproplen(intptr_t package, const char *property)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	package;
		const char	*property;
		intptr_t	size;
	} args = {"getproplen", 2, 1, package, property, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.size;
}


/**
 * @brief Iterates over the property names on a node.
 * @param package          Phandle of the node being walked.
 * @param previousProperty Name of the previous property, or NULL to start.
 * @param nextProperty     Buffer (at least 32 bytes) that receives the next name.
 * @return 1 if a next property was returned, 0 if the walk is complete,
 *         OF_FAILED on error.
 */
intptr_t
of_nextprop(intptr_t package, const char *previousProperty, char *nextProperty)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	package;
		const char	*previous_property;
		char		*next_property;
		intptr_t	flag;
	} args = {"nextprop", 3, 1, package, previousProperty, nextProperty, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.flag;
}


/**
 * @brief Converts a package phandle to its device-tree path string.
 * @param package    Phandle to resolve.
 * @param pathBuffer Caller-supplied buffer that receives the path.
 * @param bufferSize Size of pathBuffer in bytes.
 * @return Number of bytes written, or OF_FAILED on error.
 */
intptr_t
of_package_to_path(intptr_t package, char *pathBuffer, intptr_t bufferSize)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	package;
		char		*path_buffer;
		intptr_t	buffer_size;
		intptr_t	size;
	} args = {"package-to-path", 3, 1, package, pathBuffer, bufferSize, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.size;
}


//	I/O functions


/**
 * @brief Opens a device by path, returning an instance handle suitable
 *        for I/O calls.
 * @param nodeName Device path to open.
 * @return Non-zero instance handle on success; OF_FAILED on error.
 */
intptr_t
of_open(const char *nodeName)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		const char	*node_name;
		intptr_t	handle;
	} args = {"open", 1, 1, nodeName, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED || args.handle == 0)
		return OF_FAILED;

	return args.handle;
}


/**
 * @brief Closes a previously opened instance handle.
 * @param handle Instance handle to close.
 */
void
of_close(intptr_t handle)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	handle;
	} args = {"close", 1, 0, handle};

	gCallOpenFirmware(&args);
}


/**
 * @brief Reads from an opened instance.
 * @param handle     Open instance handle.
 * @param buffer     Buffer that receives the data.
 * @param bufferSize Maximum number of bytes to read.
 * @return Number of bytes actually read, or OF_FAILED on error.
 */
intptr_t
of_read(intptr_t handle, void *buffer, intptr_t bufferSize)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	handle;
		void		*buffer;
		intptr_t	buffer_size;
		intptr_t	size;
	} args = {"read", 3, 1, handle, buffer, bufferSize, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.size;
}


/**
 * @brief Writes to an opened instance.
 * @param handle     Open instance handle.
 * @param buffer     Data to write.
 * @param bufferSize Number of bytes to write.
 * @return Number of bytes actually written, or OF_FAILED on error.
 */
intptr_t
of_write(intptr_t handle, const void *buffer, intptr_t bufferSize)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	handle;
		const void	*buffer;
		intptr_t	buffer_size;
		intptr_t	size;
	} args = {"write", 3, 1, handle, buffer, bufferSize, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.size;
}


/**
 * @brief Seeks on an opened instance, splitting the 64-bit position into
 *        the high/low word pair the firmware expects when off_t is wider
 *        than intptr_t.
 * @param handle Open instance handle.
 * @param pos    New file position in bytes.
 * @return Firmware-defined status value, or OF_FAILED on error.
 */
intptr_t
of_seek(intptr_t handle, off_t pos)
{
	intptr_t pos_hi = 0;
	if (sizeof(off_t) > sizeof(intptr_t))
		pos_hi = pos >> ((sizeof(off_t) - sizeof(intptr_t)) * CHAR_BIT);

	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	handle;
		intptr_t	pos_hi;
		intptr_t	pos;
		intptr_t	status;
	} args = {"seek", 3, 1, handle, pos_hi, pos, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.status;
}


/**
 * @brief Calls the "#blocks" method on a block-device instance.
 * @param handle Open instance handle of a block device.
 * @return Total number of blocks, or OF_FAILED on error.
 */
intptr_t
of_blocks(intptr_t handle)
{
	struct {
		const char      *name;
		intptr_t        num_args;
		intptr_t        num_returns;
		intptr_t        handle;
		intptr_t        result;
		intptr_t        blocks;
	} args = {"#blocks", 2, 1, handle, 0, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;
	return args.blocks;
}


/**
 * @brief Calls the "block-size" method on a block-device instance.
 * @param handle Open instance handle of a block device.
 * @return Block size in bytes, or OF_FAILED on error.
 */
intptr_t
of_block_size(intptr_t handle)
{
	struct {
		const char      *name;
		intptr_t        num_args;
		intptr_t        num_returns;
		intptr_t        handle;
		intptr_t        result;
		intptr_t        size;
	} args = {"block-size", 2, 1, handle, 0, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;
	return args.size;
}


// memory functions


/**
 * @brief Releases a region of virtual memory previously claimed from
 *        Open Firmware.
 * @param virtualAddress Start of the region to release.
 * @param size           Size of the region in bytes.
 * @return Firmware return code.
 */
intptr_t
of_release(void *virtualAddress, intptr_t size)
{
	struct {
		const char *name;
		intptr_t	num_args;
		intptr_t	num_returns;
		void		*virtualAddress;
		intptr_t	size;
	} args = {"release", 2, 0, virtualAddress, size};

	return gCallOpenFirmware(&args);
}


/**
 * @brief Claims a region of virtual memory from Open Firmware.
 * @param virtualAddress Requested virtual address, or NULL to let the
 *                       firmware choose one when align is non-zero.
 * @param size           Size in bytes to claim.
 * @param align          Required alignment (0 to use virtualAddress as-is).
 * @return Claimed address on success; NULL if the claim failed.
 */
void *
of_claim(void *virtualAddress, intptr_t size, intptr_t align)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		void		*virtualAddress;
		intptr_t	size;
		intptr_t	align;
		void		*address;
	} args = {"claim", 3, 1, virtualAddress, size, align};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return NULL;

	return args.address;
}


// misc functions


/**
 * @brief Tests whether a given Open Firmware service is available.
 * @param service Name of the client service to probe.
 * @return Zero if the service is available; non-zero (missing) otherwise;
 *         OF_FAILED on call failure.
 */
intptr_t
of_test(const char *service)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		const char	*service;
		intptr_t	missing;
	} args = {"test", 1, 1, service, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.missing;
}


/**
 * @brief Reads the Open Firmware millisecond counter.
 * @return Current value of the millisecond tick, or OF_FAILED on error.
 */
intptr_t
of_milliseconds(void)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
		intptr_t	milliseconds;
	} args = {"milliseconds", 0, 1, 0};

	if (gCallOpenFirmware(&args) == OF_FAILED)
		return OF_FAILED;

	return args.milliseconds;
}


/**
 * @brief Asks Open Firmware to terminate the client program (no return).
 */
void
of_exit(void)
{
	struct {
		const char	*name;
		intptr_t	num_args;
		intptr_t	num_returns;
	} args = {"exit", 0, 0};

	gCallOpenFirmware(&args);
}

