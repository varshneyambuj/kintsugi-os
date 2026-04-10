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
 *   Copyright 2008-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file device_manager.cpp
 * @brief Kernel device manager — driver binding, node tree, and attribute management.
 *
 * Implements the new-style device manager. Manages the device node tree where
 * each node corresponds to a hardware device or bus. Drivers are bound to
 * nodes by the device manager when their probe() callback matches. Provides
 * attribute storage per node and the driver_module_info interface.
 *
 * @see devfs.cpp, legacy_drivers.cpp, module.cpp
 */


#include <kdevice_manager.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>
#include <module.h>
#include <PCI.h>

#include <boot_device.h>
#include <device_manager_defs.h>
#include <fs/devfs.h>
#include <fs/KPath.h>
#include <generic_syscall.h>
#include <kernel.h>
#include <kmodule.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <util/Stack.h>

#include "AbstractModuleDevice.h"
#include "devfs_private.h"
#include "id_generator.h"
#include "io_resources.h"
#include "IOSchedulerRoster.h"


//#define TRACE_DEVICE_MANAGER
#ifdef TRACE_DEVICE_MANAGER
#	define TRACE(a) dprintf a
#else
#	define TRACE(a) ;
#endif


#define DEVICE_MANAGER_ROOT_NAME "system/devices_root/driver_v1"
#define DEVICE_MANAGER_GENERIC_NAME "system/devices_generic/driver_v1"


struct device_attr_private : device_attr,
		DoublyLinkedListLinkImpl<device_attr_private> {
						device_attr_private();
						device_attr_private(const device_attr& attr);
						~device_attr_private();

			status_t	InitCheck();
			status_t	CopyFrom(const device_attr& attr);

	static	int			Compare(const device_attr* attrA,
							const device_attr *attrB);

private:
			void		_Unset();
};

typedef DoublyLinkedList<device_attr_private> AttributeList;

// I/O resource
typedef struct io_resource_info {
	struct io_resource_info *prev, *next;
	device_node*		owner;			// associated node; NULL for temporary allocation
	io_resource			resource;		// info about actual resource
} io_resource_info;


namespace {


class Device : public AbstractModuleDevice,
	public DoublyLinkedListLinkImpl<Device> {
public:
							Device(device_node* node, const char* moduleName);
	virtual					~Device();

			status_t		InitCheck() const;

			const char*		ModuleName() const { return fModuleName; }

	virtual	status_t		InitDevice();
	virtual	void			UninitDevice();

	virtual void			Removed();

	virtual	status_t		Control(void* cookie, int32 op, void* buffer, size_t length);

			void			SetRemovedFromParent(bool removed)
								{ fRemovedFromParent = removed; }

private:
	const char*				fModuleName;
	bool					fRemovedFromParent;
};


} // unnamed namespace


typedef DoublyLinkedList<Device> DeviceList;
typedef DoublyLinkedList<device_node> NodeList;

struct device_node : DoublyLinkedListLinkImpl<device_node> {
							device_node(const char* moduleName,
								const device_attr* attrs);
							~device_node();

			status_t		InitCheck() const;

			status_t		AcquireResources(const io_resource* resources);

			const char*		ModuleName() const { return fModuleName; }
			device_node*	Parent() const { return fParent; }
			AttributeList&	Attributes() { return fAttributes; }
			const AttributeList& Attributes() const { return fAttributes; }

			status_t		InitDriver();
			bool			UninitDriver();
			void			UninitUnusedDriver();

			// The following two are only valid, if the node's driver is
			// initialized
			driver_module_info* DriverModule() const { return fDriver; }
			void*			DriverData() const { return fDriverData; }

			void			AddChild(device_node *node);
			void			RemoveChild(device_node *node);
			const NodeList&	Children() const { return fChildren; }
			void			DeviceRemoved();

			status_t		Register(device_node* parent);
			status_t		Probe(const char* devicePath, uint32 updateCycle);
			status_t		Reprobe();
			status_t		Rescan();

			bool			IsRegistered() const { return fRegistered; }
			bool			IsInitialized() const { return fInitialized > 0; }
			bool			IsProbed() const { return fLastUpdateCycle != 0; }
			uint32			Flags() const { return fFlags; }

			void			Acquire();
			bool			Release();

			const DeviceList& Devices() const { return fDevices; }
			void			AddDevice(Device* device);
			void			RemoveDevice(Device* device);

			int				CompareTo(const device_attr* attributes) const;
			device_node*	FindChild(const device_attr* attributes) const;
			device_node*	FindChild(const char* moduleName) const;

			int32			Priority();

			void			Dump(int32 level = 0);

private:
			status_t		_RegisterFixed(uint32& registered);
			bool			_AlwaysRegisterDynamic();
			status_t		_AddPath(Stack<KPath*>& stack, const char* path,
								const char* subPath = NULL);
			status_t		_GetNextDriverPath(void*& cookie, KPath& _path);
			status_t		_GetNextDriver(void* list,
								driver_module_info*& driver);
			status_t		_FindBestDriver(const char* path,
								driver_module_info*& bestDriver,
								float& bestSupport,
								device_node* previous = NULL);
			status_t		_RegisterPath(const char* path);
			status_t		_RegisterDynamic(device_node* previous = NULL);
			status_t		_RemoveChildren();
			device_node*	_FindCurrentChild();
			status_t		_Probe();
			void			_ReleaseWaiting();

	device_node*			fParent;
	NodeList				fChildren;
	int32					fRefCount;
	int32					fInitialized;
	bool					fRegistered;
	uint32					fFlags;
	float					fSupportsParent;
	uint32					fLastUpdateCycle;

	const char*				fModuleName;

	driver_module_info*		fDriver;
	void*					fDriverData;

	DeviceList				fDevices;
	AttributeList			fAttributes;
	ResourceList			fResources;
};

// flags in addition to those specified by B_DEVICE_FLAGS
enum node_flags {
	NODE_FLAG_REGISTER_INITIALIZED	= 0x00010000,
	NODE_FLAG_DEVICE_REMOVED		= 0x00020000,
	NODE_FLAG_OBSOLETE_DRIVER		= 0x00040000,
	NODE_FLAG_WAITING_FOR_DRIVER	= 0x00080000,

	NODE_FLAG_PUBLIC_MASK			= 0x0000ffff
};


static device_node *sRootNode;
static recursive_lock sLock;
static const char* sGenericContextPath;


//	#pragma mark -


/**
 * @brief Search for a named attribute on a device node, optionally walking up the tree.
 *
 * @param node      Node to start the search from.
 * @param name      Attribute name to look for.
 * @param recursive If true, continue searching parent nodes until found or root reached.
 * @param type      Required type code; pass B_ANY_TYPE to match any type.
 * @return Pointer to the matching device_attr_private, or NULL if not found.
 */
static device_attr_private*
find_attr(const device_node* node, const char* name, bool recursive,
	type_code type)
{
	do {
		AttributeList::ConstIterator iterator
			= node->Attributes().GetIterator();

		while (iterator.HasNext()) {
			device_attr_private* attr = iterator.Next();

			if (type != B_ANY_TYPE && attr->type != type)
				continue;

			if (!strcmp(attr->name, name))
				return attr;
		}

		node = node->Parent();
	} while (node != NULL && recursive);

	return NULL;
}


/**
 * @brief Print indentation spaces for tree-dump formatting.
 *
 * @param level Nesting level; each level adds three spaces of indentation.
 */
static void
put_level(int32 level)
{
	while (level-- > 0)
		kprintf("   ");
}


/**
 * @brief Dump a single device attribute to the kernel debugger console.
 *
 * @param attr  Attribute to display; does nothing if NULL.
 * @param level Indentation level passed to put_level().
 */
static void
dump_attribute(device_attr* attr, int32 level)
{
	if (attr == NULL)
		return;

	put_level(level + 2);
	kprintf("\"%s\" : ", attr->name);
	switch (attr->type) {
		case B_STRING_TYPE:
			kprintf("string : \"%s\"", attr->value.string);
			break;
		case B_INT8_TYPE:
		case B_UINT8_TYPE:
			kprintf("uint8 : %" B_PRIu8 " (%#" B_PRIx8 ")", attr->value.ui8,
				attr->value.ui8);
			break;
		case B_INT16_TYPE:
		case B_UINT16_TYPE:
			kprintf("uint16 : %" B_PRIu16 " (%#" B_PRIx16 ")", attr->value.ui16,
				attr->value.ui16);
			break;
		case B_INT32_TYPE:
		case B_UINT32_TYPE:
			kprintf("uint32 : %" B_PRIu32 " (%#" B_PRIx32 ")", attr->value.ui32,
				attr->value.ui32);
			break;
		case B_INT64_TYPE:
		case B_UINT64_TYPE:
			kprintf("uint64 : %" B_PRIu64 " (%#" B_PRIx64 ")", attr->value.ui64,
				attr->value.ui64);
			break;
		default:
			kprintf("raw data");
	}
	kprintf("\n");
}


/**
 * @brief Kernel debugger command that prints the entire device node tree.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return Always returns 0.
 */
static int
dump_device_nodes(int argc, char** argv)
{
	sRootNode->Dump();
	return 0;
}


/**
 * @brief Publish devfs directories for drivers found under @a subPath.
 *
 * When the boot device is not yet available, iterates over the module list to
 * discover driver directories and publishes each one into devfs.
 *
 * @param subPath Sub-path beneath "drivers/" to enumerate; empty string for
 *                the top-level directory.
 */
static void
publish_directories(const char* subPath)
{
	if (gBootDevice < 0) {
		if (subPath[0]) {
			// we only support the top-level directory for modules
			return;
		}

		// we can only iterate over the known modules to find all directories
		KPath path("drivers");
		if (path.Append(subPath) != B_OK)
			return;

		size_t length = strlen(path.Path()) + 1;
			// account for the separating '/'

		void* list = open_module_list_etc(path.Path(), "driver_v1");
		char name[B_FILE_NAME_LENGTH];
		size_t nameLength = sizeof(name);
		while (read_next_module_name(list, name, &nameLength) == B_OK) {
			if (nameLength == length)
				continue;

			char* leaf = name + length;
			char* end = strchr(leaf, '/');
			if (end != NULL)
				end[0] = '\0';

			path.SetTo(subPath);
			path.Append(leaf);

			devfs_publish_directory(path.Path());
		}
		close_module_list(list);
	} else {
		// TODO: implement module directory traversal!
	}
}


/**
 * @brief Generic syscall handler for the device manager subsystem.
 *
 * Handles userland requests to enumerate the device node tree and its
 * attributes via the DM_GET_ROOT / DM_GET_CHILD / DM_GET_NEXT_CHILD /
 * DM_GET_NEXT_ATTRIBUTE sub-functions.
 *
 * @param subsystem  Subsystem name string (unused beyond dispatch).
 * @param function   One of the DM_* function codes.
 * @param buffer     User-space buffer holding the request/response cookie.
 * @param bufferSize Expected size of @a buffer; validated before use.
 * @retval B_OK            Operation succeeded; result written to @a buffer.
 * @retval B_BAD_ADDRESS   @a buffer is not a valid user address.
 * @retval B_BAD_VALUE     @a bufferSize does not match the expected size.
 * @retval B_ENTRY_NOT_FOUND No further child or attribute to iterate.
 * @retval B_BAD_HANDLER   Unknown @a function code.
 * @note This function currently passes raw kernel pointers to userland and
 *       accepts them back without validation — it is unsafe and should be
 *       redesigned before any security-sensitive use.
 */
static status_t
control_device_manager(const char* subsystem, uint32 function, void* buffer,
	size_t bufferSize)
{
	// TODO: this function passes pointers to userland, and uses pointers
	// to device nodes that came from userland - this is completely unsafe
	// and should be changed.
	switch (function) {
		case DM_GET_ROOT:
		{
			device_node_cookie cookie;
			if (!IS_USER_ADDRESS(buffer))
				return B_BAD_ADDRESS;
			if (bufferSize != sizeof(device_node_cookie))
				return B_BAD_VALUE;
			cookie = (device_node_cookie)sRootNode;

			// copy back to user space
			return user_memcpy(buffer, &cookie, sizeof(device_node_cookie));
		}

		case DM_GET_CHILD:
		{
			if (!IS_USER_ADDRESS(buffer))
				return B_BAD_ADDRESS;
			if (bufferSize != sizeof(device_node_cookie))
				return B_BAD_VALUE;

			device_node_cookie cookie;
			if (user_memcpy(&cookie, buffer, sizeof(device_node_cookie)) < B_OK)
				return B_BAD_ADDRESS;

			device_node* node = (device_node*)cookie;
			NodeList::ConstIterator iterator = node->Children().GetIterator();

			if (!iterator.HasNext()) {
				return B_ENTRY_NOT_FOUND;
			}
			node = iterator.Next();
			cookie = (device_node_cookie)node;

			// copy back to user space
			return user_memcpy(buffer, &cookie, sizeof(device_node_cookie));
		}

		case DM_GET_NEXT_CHILD:
		{
			if (!IS_USER_ADDRESS(buffer))
				return B_BAD_ADDRESS;
			if (bufferSize != sizeof(device_node_cookie))
				return B_BAD_VALUE;

			device_node_cookie cookie;
			if (user_memcpy(&cookie, buffer, sizeof(device_node_cookie)) < B_OK)
				return B_BAD_ADDRESS;

			device_node* last = (device_node*)cookie;
			if (!last->Parent())
				return B_ENTRY_NOT_FOUND;

			NodeList::ConstIterator iterator
				= last->Parent()->Children().GetIterator();

			// skip those we already traversed
			while (iterator.HasNext()) {
				device_node* node = iterator.Next();

				if (node == last)
					break;
			}

			if (!iterator.HasNext())
				return B_ENTRY_NOT_FOUND;
			device_node* node = iterator.Next();
			cookie = (device_node_cookie)node;

			// copy back to user space
			return user_memcpy(buffer, &cookie, sizeof(device_node_cookie));
		}

		case DM_GET_NEXT_ATTRIBUTE:
		{
			struct device_attr_info attrInfo;
			if (!IS_USER_ADDRESS(buffer))
				return B_BAD_ADDRESS;
			if (bufferSize != sizeof(device_attr_info))
				return B_BAD_VALUE;
			if (user_memcpy(&attrInfo, buffer, sizeof(device_attr_info)) < B_OK)
				return B_BAD_ADDRESS;

			device_node* node = (device_node*)attrInfo.node_cookie;
			device_attr* last = (device_attr*)attrInfo.cookie;
			AttributeList::Iterator iterator = node->Attributes().GetIterator();
			// skip those we already traversed
			while (iterator.HasNext() && last != NULL) {
				device_attr* attr = iterator.Next();

				if (attr == last)
					break;
			}

			if (!iterator.HasNext()) {
				attrInfo.cookie = 0;
				return B_ENTRY_NOT_FOUND;
			}

			device_attr* attr = iterator.Next();
			attrInfo.cookie = (device_node_cookie)attr;
			if (attr->name != NULL)
				strlcpy(attrInfo.name, attr->name, 254);
			else
				attrInfo.name[0] = '\0';
			attrInfo.type = attr->type;
			switch (attrInfo.type) {
				case B_UINT8_TYPE:
					attrInfo.value.ui8 = attr->value.ui8;
					break;
				case B_UINT16_TYPE:
					attrInfo.value.ui16 = attr->value.ui16;
					break;
				case B_UINT32_TYPE:
					attrInfo.value.ui32 = attr->value.ui32;
					break;
				case B_UINT64_TYPE:
					attrInfo.value.ui64 = attr->value.ui64;
					break;
				case B_STRING_TYPE:
					if (attr->value.string != NULL)
						strlcpy(attrInfo.value.string, attr->value.string, 254);
					else
						attrInfo.value.string[0] = '\0';
					break;
				/*case B_RAW_TYPE:
					if (attr.value.raw.length > attr_info->attr.value.raw.length)
						attr.value.raw.length = attr_info->attr.value.raw.length;
					user_memcpy(attr.value.raw.data, attr_info->attr.value.raw.data,
						attr.value.raw.length);
					break;*/
			}

			// copy back to user space
			return user_memcpy(buffer, &attrInfo, sizeof(device_attr_info));
		}
	}

	return B_BAD_HANDLER;
}


//	#pragma mark - Device Manager module API


/**
 * @brief Trigger a rescan of a device node's child devices.
 *
 * Acquires the device manager lock and delegates to device_node::Rescan().
 *
 * @param node Node whose children should be rescanned.
 * @retval B_OK On success.
 * @retval B_NO_INIT If the node's driver could not be initialized.
 */
static status_t
rescan_node(device_node* node)
{
	RecursiveLocker _(sLock);
	return node->Rescan();
}


/**
 * @brief Register a new device node as a child of @a parent.
 *
 * Allocates a new device_node, copies attributes and I/O resources, then
 * calls Register() to attach it to the tree and initialize its driver.
 *
 * @param parent      Parent node; pass NULL only for the root node.
 * @param moduleName  Fully-qualified driver module name (e.g. "drivers/disk/…/driver_v1").
 * @param attrs       NULL-terminated array of attributes to attach, or NULL.
 * @param ioResources NULL-terminated array of I/O resources to claim, or NULL.
 * @param _node       On success, receives a pointer to the newly created node; may be NULL.
 * @retval B_OK          Node created and registered successfully.
 * @retval B_BAD_VALUE   @a parent is NULL but the root node already exists, or
 *                       @a moduleName is NULL.
 * @retval B_NAME_IN_USE An equivalent child node already exists under @a parent.
 * @retval B_NO_MEMORY   Allocation of the node structure failed.
 */
static status_t
register_node(device_node* parent, const char* moduleName,
	const device_attr* attrs, const io_resource* ioResources,
	device_node** _node)
{
	if ((parent == NULL && sRootNode != NULL) || moduleName == NULL)
		return B_BAD_VALUE;

	if (parent != NULL && parent->FindChild(attrs) != NULL) {
		// A node like this one already exists for this parent
		return B_NAME_IN_USE;
	}

	RecursiveLocker _(sLock);

	device_node* newNode = new(std::nothrow) device_node(moduleName, attrs);
	if (newNode == NULL)
		return B_NO_MEMORY;

	TRACE(("%p: register node \"%s\", parent %p\n", newNode, moduleName,
		parent));

	status_t status = newNode->InitCheck();
	if (status == B_OK)
		status = newNode->AcquireResources(ioResources);
	if (status == B_OK)
		status = newNode->Register(parent);

	if (status != B_OK) {
		newNode->Release();
		return status;
	}

	if (_node)
		*_node = newNode;

	return B_OK;
}


/*!	Unregisters the device \a node.

	If the node is currently in use, this function will return B_BUSY to
	indicate that the node hasn't been removed yet - it will still remove
	the node as soon as possible.
*/
/**
 * @brief Unregister a device node, notifying its driver and children.
 *
 * Marks the node as removed via DeviceRemoved(). If the node's driver is
 * currently initialized the node cannot be freed immediately and B_BUSY is
 * returned; the node will be released once all references are dropped.
 *
 * @param node Node to unregister.
 * @retval B_OK   Node was idle and has been removed.
 * @retval B_BUSY Node is still in use; removal is deferred.
 */
static status_t
unregister_node(device_node* node)
{
	TRACE(("unregister_node(node %p)\n", node));
	RecursiveLocker _(sLock);

	bool initialized = node->IsInitialized();

	node->DeviceRemoved();

	return initialized ? B_BUSY : B_OK;
}


/**
 * @brief Retrieve the driver module and private data for an initialized node.
 *
 * @param node    Node whose driver info is requested.
 * @param _module On success, receives the driver_module_info pointer; may be NULL.
 * @param _data   On success, receives the driver's private data pointer; may be NULL.
 * @retval B_OK      Driver information populated successfully.
 * @retval B_NO_INIT Node has no driver currently initialized.
 */
static status_t
get_driver(device_node* node, driver_module_info** _module, void** _data)
{
	if (node->DriverModule() == NULL)
		return B_NO_INIT;

	if (_module != NULL)
		*_module = node->DriverModule();
	if (_data != NULL)
		*_data = node->DriverData();

	return B_OK;
}


/**
 * @brief Acquire a reference to and return the global root device node.
 *
 * @return Pointer to the root node with an incremented reference count, or
 *         NULL if the root has not been initialized yet.
 */
static device_node*
get_root_node(void)
{
	if (sRootNode != NULL)
		sRootNode->Acquire();

	return sRootNode;
}


/**
 * @brief Iterate over the registered children of @a parent that match @a attributes.
 *
 * On each successful call the previously returned node (stored in *_node) is
 * released and the next matching child is acquired and written to *_node.
 *
 * @param parent     Parent node whose children are traversed.
 * @param attributes NULL-terminated attribute array used to filter children;
 *                   pass NULL to match any child.
 * @param _node      In/out: on entry, the last node returned (or NULL to start);
 *                   on success, set to the next matching child.
 * @retval B_OK             Next matching child found and referenced.
 * @retval B_ENTRY_NOT_FOUND No further matching child exists.
 */
static status_t
get_next_child_node(device_node* parent, const device_attr* attributes,
	device_node** _node)
{
	RecursiveLocker _(sLock);

	NodeList::ConstIterator iterator = parent->Children().GetIterator();
	device_node* last = *_node;

	// skip those we already traversed
	while (iterator.HasNext() && last != NULL) {
		device_node* node = iterator.Next();

		if (node != last)
			continue;
	}

	// find the next one that fits
	while (iterator.HasNext()) {
		device_node* node = iterator.Next();

		if (!node->IsRegistered())
			continue;

		if (!node->CompareTo(attributes)) {
			if (last != NULL)
				last->Release();

			node->Acquire();
			*_node = node;
			return B_OK;
		}
	}

	if (last != NULL)
		last->Release();

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Acquire a reference to and return the parent of @a node.
 *
 * @param node Child node whose parent is requested.
 * @return Pointer to the parent node with an incremented reference count, or
 *         NULL if @a node is NULL.
 */
static device_node*
get_parent_node(device_node* node)
{
	if (node == NULL)
		return NULL;

	RecursiveLocker _(sLock);

	device_node* parent = node->Parent();
	parent->Acquire();

	return parent;
}


/**
 * @brief Release a reference to @a node obtained from get_root_node(),
 *        get_parent_node(), or get_next_child_node().
 *
 * @param node Node whose reference count should be decremented.
 */
static void
put_node(device_node* node)
{
	RecursiveLocker _(sLock);
	node->Release();
}


/**
 * @brief Publish a device to devfs, making it accessible to userland.
 *
 * Creates a Device object, registers it with devfs at the given path, and
 * stores path/driver attributes on the owning node for later inspection.
 *
 * @param node       Owner node the device belongs to.
 * @param path       devfs path under which the device should appear (e.g. "disk/usb/0/raw").
 * @param moduleName Fully-qualified device module name.
 * @retval B_OK        Device published successfully.
 * @retval B_BAD_VALUE @a path or @a moduleName is NULL or empty.
 * @retval B_NO_MEMORY Allocation of the Device object failed.
 */
static status_t
publish_device(device_node *node, const char *path, const char *moduleName)
{
	if (path == NULL || !path[0] || moduleName == NULL || !moduleName[0])
		return B_BAD_VALUE;

	RecursiveLocker _(sLock);
	dprintf("publish device: node %p, path %s, module %s\n", node, path,
		moduleName);

	Device* device = new(std::nothrow) Device(node, moduleName);
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = device->InitCheck();
	if (status == B_OK)
		status = devfs_publish_device(path, device);
	if (status != B_OK) {
		delete device;
		return status;
	}

	node->AddDevice(device);

	device_attr_private* attr;

	attr = new(std::nothrow) device_attr_private();
	if (attr != NULL) {
		char buf[256];
		sprintf(buf, "dev/%" B_PRIdINO "/path", device->ID());
		attr->name = strdup(buf);
		attr->type = B_STRING_TYPE;
		attr->value.string = strdup(path);
		node->Attributes().Add(attr);
	}

	attr = new(std::nothrow) device_attr_private();
	if (attr != NULL) {
		char buf[256];
		sprintf(buf, "dev/%" B_PRIdINO "/driver", device->ID());
		attr->name = strdup(buf);
		attr->type = B_STRING_TYPE;
		attr->value.string = strdup(moduleName);
		node->Attributes().Add(attr);
	}

	return B_OK;
}


/**
 * @brief Remove a previously published device from devfs.
 *
 * Looks up the device by path in devfs, verifies it belongs to @a node, then
 * unpublishes it.
 *
 * @param node Owner node that published the device.
 * @param path devfs path that was passed to publish_device().
 * @retval B_OK        Device removed from devfs successfully.
 * @retval B_BAD_VALUE @a path is NULL, the device at @a path is not a managed
 *                     Device, or it does not belong to @a node.
 */
static status_t
unpublish_device(device_node *node, const char *path)
{
	if (path == NULL)
		return B_BAD_VALUE;

	BaseDevice* baseDevice;
	status_t error = devfs_get_device(path, baseDevice);
	if (error != B_OK)
		return error;
	CObjectDeleter<BaseDevice, void, devfs_put_device>
		baseDevicePutter(baseDevice);

	Device* device = dynamic_cast<Device*>(baseDevice);
	if (device == NULL || device->Node() != node)
		return B_BAD_VALUE;

	return devfs_unpublish_device(device, true);
}


/**
 * @brief Read a uint8 attribute from a device node.
 *
 * @param node      Node to query.
 * @param name      Attribute name.
 * @param _value    Receives the attribute value on success.
 * @param recursive If true, search ancestor nodes when the attribute is not
 *                  found on @a node itself.
 * @retval B_OK             Value written to *_value.
 * @retval B_BAD_VALUE      One of the pointer arguments is NULL.
 * @retval B_NAME_NOT_FOUND Attribute does not exist on the node (or ancestors).
 */
static status_t
get_attr_uint8(const device_node* node, const char* name, uint8* _value,
	bool recursive)
{
	if (node == NULL || name == NULL || _value == NULL)
		return B_BAD_VALUE;

	device_attr_private* attr = find_attr(node, name, recursive, B_UINT8_TYPE);
	if (attr == NULL)
		return B_NAME_NOT_FOUND;

	*_value = attr->value.ui8;
	return B_OK;
}


/**
 * @brief Read a uint16 attribute from a device node.
 *
 * @param node      Node to query.
 * @param name      Attribute name.
 * @param _value    Receives the attribute value on success.
 * @param recursive If true, search ancestor nodes when the attribute is not
 *                  found on @a node itself.
 * @retval B_OK             Value written to *_value.
 * @retval B_BAD_VALUE      One of the pointer arguments is NULL.
 * @retval B_NAME_NOT_FOUND Attribute does not exist on the node (or ancestors).
 */
static status_t
get_attr_uint16(const device_node* node, const char* name, uint16* _value,
	bool recursive)
{
	if (node == NULL || name == NULL || _value == NULL)
		return B_BAD_VALUE;

	device_attr_private* attr = find_attr(node, name, recursive, B_UINT16_TYPE);
	if (attr == NULL)
		return B_NAME_NOT_FOUND;

	*_value = attr->value.ui16;
	return B_OK;
}


/**
 * @brief Read a uint32 attribute from a device node.
 *
 * @param node      Node to query.
 * @param name      Attribute name.
 * @param _value    Receives the attribute value on success.
 * @param recursive If true, search ancestor nodes when the attribute is not
 *                  found on @a node itself.
 * @retval B_OK             Value written to *_value.
 * @retval B_BAD_VALUE      One of the pointer arguments is NULL.
 * @retval B_NAME_NOT_FOUND Attribute does not exist on the node (or ancestors).
 */
static status_t
get_attr_uint32(const device_node* node, const char* name, uint32* _value,
	bool recursive)
{
	if (node == NULL || name == NULL || _value == NULL)
		return B_BAD_VALUE;

	device_attr_private* attr = find_attr(node, name, recursive, B_UINT32_TYPE);
	if (attr == NULL)
		return B_NAME_NOT_FOUND;

	*_value = attr->value.ui32;
	return B_OK;
}


/**
 * @brief Read a uint64 attribute from a device node.
 *
 * @param node      Node to query.
 * @param name      Attribute name.
 * @param _value    Receives the attribute value on success.
 * @param recursive If true, search ancestor nodes when the attribute is not
 *                  found on @a node itself.
 * @retval B_OK             Value written to *_value.
 * @retval B_BAD_VALUE      One of the pointer arguments is NULL.
 * @retval B_NAME_NOT_FOUND Attribute does not exist on the node (or ancestors).
 */
static status_t
get_attr_uint64(const device_node* node, const char* name,
	uint64* _value, bool recursive)
{
	if (node == NULL || name == NULL || _value == NULL)
		return B_BAD_VALUE;

	device_attr_private* attr = find_attr(node, name, recursive, B_UINT64_TYPE);
	if (attr == NULL)
		return B_NAME_NOT_FOUND;

	*_value = attr->value.ui64;
	return B_OK;
}


/**
 * @brief Read a string attribute from a device node.
 *
 * The returned pointer points into the node's internal attribute storage and
 * must not be freed or modified by the caller.
 *
 * @param node      Node to query.
 * @param name      Attribute name.
 * @param _value    Receives a pointer to the attribute's string value on success.
 * @param recursive If true, search ancestor nodes when the attribute is not
 *                  found on @a node itself.
 * @retval B_OK             *_value set to the string value.
 * @retval B_BAD_VALUE      One of the pointer arguments is NULL.
 * @retval B_NAME_NOT_FOUND Attribute does not exist on the node (or ancestors).
 */
static status_t
get_attr_string(const device_node* node, const char* name,
	const char** _value, bool recursive)
{
	if (node == NULL || name == NULL || _value == NULL)
		return B_BAD_VALUE;

	device_attr_private* attr = find_attr(node, name, recursive, B_STRING_TYPE);
	if (attr == NULL)
		return B_NAME_NOT_FOUND;

	*_value = attr->value.string;
	return B_OK;
}


/**
 * @brief Read a raw-blob attribute from a device node.
 *
 * Either @a _data or @a _length (or both) must be non-NULL.
 *
 * @param node      Node to query.
 * @param name      Attribute name.
 * @param _data     If non-NULL, receives a pointer to the raw data buffer.
 * @param _length   If non-NULL, receives the length of the raw data in bytes.
 * @param recursive If true, search ancestor nodes when the attribute is not
 *                  found on @a node itself.
 * @retval B_OK             Attribute found; requested fields populated.
 * @retval B_BAD_VALUE      @a node or @a name is NULL, or both @a _data and
 *                          @a _length are NULL.
 * @retval B_NAME_NOT_FOUND Attribute does not exist on the node (or ancestors).
 */
static status_t
get_attr_raw(const device_node* node, const char* name, const void** _data,
	size_t* _length, bool recursive)
{
	if (node == NULL || name == NULL || (_data == NULL && _length == NULL))
		return B_BAD_VALUE;

	device_attr_private* attr = find_attr(node, name, recursive, B_RAW_TYPE);
	if (attr == NULL)
		return B_NAME_NOT_FOUND;

	if (_data != NULL)
		*_data = attr->value.raw.data;
	if (_length != NULL)
		*_length = attr->value.raw.length;
	return B_OK;
}


/**
 * @brief Advance an attribute iterator for the given device node.
 *
 * Pass *_attr == NULL to obtain the first attribute. On each subsequent call
 * pass the previously returned pointer to advance to the next attribute.
 *
 * @param node  Node whose attribute list is traversed.
 * @param _attr In/out pointer to the current attribute; updated on success.
 * @retval B_OK             *_attr set to the next attribute.
 * @retval B_BAD_VALUE      @a node is NULL.
 * @retval B_ENTRY_NOT_FOUND No more attributes in the list.
 */
static status_t
get_next_attr(device_node* node, device_attr** _attr)
{
	if (node == NULL)
		return B_BAD_VALUE;

	device_attr_private* next;
	device_attr_private* attr = *(device_attr_private**)_attr;

	if (attr != NULL) {
		// next attribute
		next = attr->GetDoublyLinkedListLink()->next;
	} else {
		// first attribute
		next = node->Attributes().First();
	}

	*_attr = next;

	return next ? B_OK : B_ENTRY_NOT_FOUND;
}


/**
 * @brief Recursively search the subtree rooted at @a parent for a node whose
 *        attributes match @a attributes, starting after *_node.
 *
 * Internal helper used by the public find_child_node() overload. Traverses
 * the child list depth-first and sets *_lastFound once the node referenced by
 * *_node has been passed, then returns the first subsequent match.
 *
 * @param parent     Root of the subtree to search.
 * @param attributes Attribute filter; a node matches when CompareTo() returns 0.
 * @param _node      In/out: resume point on entry, matching node on exit.
 * @param _lastFound In/out flag tracking whether the previous node was seen.
 * @retval B_OK             Matching node found; *_node updated and referenced.
 * @retval B_ENTRY_NOT_FOUND No matching node found in this subtree.
 */
static status_t
find_child_node(device_node* parent, const device_attr* attributes,
	device_node** _node, bool *_lastFound)
{
	RecursiveLocker _(sLock);

	NodeList::ConstIterator iterator = parent->Children().GetIterator();
	device_node* last = *_node;

	// find the next one that fits
	while (iterator.HasNext()) {
		device_node* node = iterator.Next();

		if (!node->IsRegistered())
			continue;

		if (node == last)
			*_lastFound = true;
		else if (!node->CompareTo(attributes) && *_lastFound) {
			if (last != NULL)
				last->Release();

			node->Acquire();
			*_node = node;
			return B_OK;
		}
		if (find_child_node(node, attributes, _node, _lastFound) == B_OK)
			return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Find the next descendant node (anywhere in the tree) that matches
 *        @a attributes, starting after *_node.
 *
 * Public wrapper around the recursive overload above. Pass *_node == NULL to
 * obtain the first match. Each successful call acquires a reference on the
 * returned node; the previous reference (if any) is released.
 *
 * @param parent     Root node of the subtree to search.
 * @param attributes Attribute filter array (NULL-terminated).
 * @param _node      In/out: previous match on entry (or NULL); next match on exit.
 * @retval B_OK             Matching node found; *_node updated and referenced.
 * @retval B_ENTRY_NOT_FOUND No further matching node exists.
 */
static status_t
find_child_node(device_node* parent, const device_attr* attributes,
	device_node** _node)
{
	device_node* last = *_node;
	bool lastFound = last == NULL;
	status_t status = find_child_node(parent, attributes, _node, &lastFound);
	if (status == B_ENTRY_NOT_FOUND && last != NULL && lastFound)
		last->Release();
	return status;
}


struct device_manager_info gDeviceManagerModule = {
	{
		B_DEVICE_MANAGER_MODULE_NAME,
		0,
		NULL
	},

	// device nodes
	rescan_node,
	register_node,
	unregister_node,
	get_driver,
	get_root_node,
	get_next_child_node,
	get_parent_node,
	put_node,

	// devices
	publish_device,
	unpublish_device,

	// I/O resources

	// ID generator
	dm_create_id,
	dm_free_id,

	// attributes
	get_attr_uint8,
	get_attr_uint16,
	get_attr_uint32,
	get_attr_uint64,
	get_attr_string,
	get_attr_raw,
	get_next_attr,
	find_child_node
};


//	#pragma mark - device_attr


/**
 * @brief Default-construct an empty device_attr_private.
 *
 * @note All fields are zeroed; the attribute is invalid until CopyFrom() or
 *       direct field assignment is performed.
 */
device_attr_private::device_attr_private()
{
	name = NULL;
	type = 0;
	value.raw.data = NULL;
	value.raw.length = 0;
}


/**
 * @brief Construct a device_attr_private by copying from a plain device_attr.
 *
 * @param attr Source attribute; strings and raw data are deep-copied.
 */
device_attr_private::device_attr_private(const device_attr& attr)
{
	CopyFrom(attr);
}


/**
 * @brief Destroy the attribute, freeing any heap-allocated string or raw data.
 */
device_attr_private::~device_attr_private()
{
	_Unset();
}


/**
 * @brief Check whether this attribute has been properly initialized.
 *
 * @retval B_OK     The name field is non-NULL and the attribute is usable.
 * @retval B_NO_INIT The name field is NULL; the attribute was never initialized.
 */
status_t
device_attr_private::InitCheck()
{
	return name != NULL ? B_OK : B_NO_INIT;
}


/**
 * @brief Deep-copy the fields of @a attr into this object.
 *
 * Duplicates the name string and, for string/raw types, the value payload.
 * On allocation failure the object is reset via _Unset().
 *
 * @param attr Source attribute to copy from.
 * @retval B_OK        Copy succeeded.
 * @retval B_NO_MEMORY A memory allocation failed; the object has been reset.
 * @retval B_BAD_VALUE The attribute type is not one of the supported types.
 */
status_t
device_attr_private::CopyFrom(const device_attr& attr)
{
	name = strdup(attr.name);
	if (name == NULL)
		return B_NO_MEMORY;

	type = attr.type;

	switch (type) {
		case B_UINT8_TYPE:
		case B_UINT16_TYPE:
		case B_UINT32_TYPE:
		case B_UINT64_TYPE:
			value.ui64 = attr.value.ui64;
			break;

		case B_STRING_TYPE:
			if (attr.value.string != NULL) {
				value.string = strdup(attr.value.string);
				if (value.string == NULL) {
					_Unset();
					return B_NO_MEMORY;
				}
			} else
				value.string = NULL;
			break;

		case B_RAW_TYPE:
			value.raw.data = malloc(attr.value.raw.length);
			if (value.raw.data == NULL) {
				_Unset();
				return B_NO_MEMORY;
			}

			value.raw.length = attr.value.raw.length;
			memcpy((void*)value.raw.data, attr.value.raw.data,
				attr.value.raw.length);
			break;

		default:
			return B_BAD_VALUE;
	}

	return B_OK;
}


/**
 * @brief Free all heap memory owned by this attribute and reset fields to NULL/0.
 */
void
device_attr_private::_Unset()
{
	if (type == B_STRING_TYPE)
		free((char*)value.string);
	else if (type == B_RAW_TYPE)
		free((void*)value.raw.data);

	free((char*)name);

	name = NULL;
	value.raw.data = NULL;
	value.raw.length = 0;
}


/**
 * @brief Three-way comparison of two device attributes.
 *
 * Both attributes must have the same type; mismatched types always return -1.
 * For numeric types a signed difference is returned; for strings strcmp() is
 * used; for raw blobs a length check precedes memcmp().
 *
 * @param attrA First attribute.
 * @param attrB Second attribute.
 * @return 0 if equal, negative if A < B, positive if A > B, -1 on type mismatch.
 */
/*static*/ int
device_attr_private::Compare(const device_attr* attrA, const device_attr *attrB)
{
	if (attrA->type != attrB->type)
		return -1;

	switch (attrA->type) {
		case B_UINT8_TYPE:
			return (int)attrA->value.ui8 - (int)attrB->value.ui8;

		case B_UINT16_TYPE:
			return (int)attrA->value.ui16 - (int)attrB->value.ui16;

		case B_UINT32_TYPE:
			if (attrA->value.ui32 > attrB->value.ui32)
				return 1;
			if (attrA->value.ui32 < attrB->value.ui32)
				return -1;
			return 0;

		case B_UINT64_TYPE:
			if (attrA->value.ui64 > attrB->value.ui64)
				return 1;
			if (attrA->value.ui64 < attrB->value.ui64)
				return -1;
			return 0;

		case B_STRING_TYPE:
			return strcmp(attrA->value.string, attrB->value.string);

		case B_RAW_TYPE:
			if (attrA->value.raw.length != attrB->value.raw.length)
				return -1;

			return memcmp(attrA->value.raw.data, attrB->value.raw.data,
				attrA->value.raw.length);
	}

	return -1;
}


//	#pragma mark - Device


/**
 * @brief Construct a Device associated with @a node using the given driver module.
 *
 * @param node       Owning device_node; stored as fNode via the AbstractModuleDevice base.
 * @param moduleName Device module name; duplicated onto the heap.
 */
Device::Device(device_node* node, const char* moduleName)
	:
	fModuleName(strdup(moduleName)),
	fRemovedFromParent(false)
{
	fNode = node;
}


/**
 * @brief Destroy the Device and free its module name string.
 */
Device::~Device()
{
	free((char*)fModuleName);
}


/**
 * @brief Check whether the Device was constructed successfully.
 *
 * @retval B_OK      fModuleName was duplicated successfully.
 * @retval B_NO_MEMORY strdup() failed during construction.
 */
status_t
Device::InitCheck() const
{
	return fModuleName != NULL ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Initialize (or re-initialize) this device for an open operation.
 *
 * Loads the device module, ensures the parent node's driver is initialized,
 * and calls the module's init_device() callback. Reference-counted: safe to
 * call multiple times concurrently.
 *
 * @retval B_OK    Device initialized; parent driver reference acquired.
 * @retval ENODEV  The owning node has been marked as removed.
 * @retval B_BUSY  The owning node is waiting for its driver to be replaced.
 */
status_t
Device::InitDevice()
{
	RecursiveLocker _(sLock);

	if ((fNode->Flags() & NODE_FLAG_DEVICE_REMOVED) != 0) {
		// TODO: maybe the device should be unlinked in devfs, too
		return ENODEV;
	}
	if ((fNode->Flags() & NODE_FLAG_WAITING_FOR_DRIVER) != 0)
		return B_BUSY;

	if (fInitialized++ > 0) {
		fNode->InitDriver();
			// acquire another reference to our parent as well
		return B_OK;
	}

	status_t status = get_module(ModuleName(), (module_info**)&fDeviceModule);
	if (status == B_OK) {
		// our parent always has to be initialized
		status = fNode->InitDriver();
	}
	if (status < B_OK) {
		fInitialized--;
		return status;
	}

	if (Module()->init_device != NULL)
		status = Module()->init_device(fNode->DriverData(), &fDeviceData);

	if (status < B_OK) {
		fNode->UninitDriver();
		fInitialized--;

		put_module(ModuleName());
		fDeviceModule = NULL;
		fDeviceData = NULL;
	}

	return status;
}


/**
 * @brief Release one initialization reference to this device.
 *
 * When the last reference is released, calls uninit_device() and puts the
 * module reference. Also releases one reference on the parent node's driver.
 */
void
Device::UninitDevice()
{
	RecursiveLocker _(sLock);

	if (fInitialized-- > 1) {
		fNode->UninitDriver();
		return;
	}

	TRACE(("uninit driver for node %p\n", this));

	if (Module()->uninit_device != NULL)
		Module()->uninit_device(fDeviceData);

	fDeviceModule = NULL;
	fDeviceData = NULL;

	put_module(ModuleName());

	fNode->UninitDriver();
}


/**
 * @brief Notify the device that its underlying hardware has been removed.
 *
 * Removes the device from its parent node's device list (unless already
 * detached) and deletes the object.
 */
void
Device::Removed()
{
	RecursiveLocker _(sLock);

	if (!fRemovedFromParent)
		fNode->RemoveDevice(this);

	delete this;
}


/**
 * @brief Handle an ioctl-style control operation on this device.
 *
 * Currently handles B_GET_DRIVER_FOR_DEVICE to retrieve the on-disk path of
 * the driver module; all other operations are forwarded to
 * AbstractModuleDevice::Control().
 *
 * @param _cookie Caller's open cookie (unused for B_GET_DRIVER_FOR_DEVICE).
 * @param op      Operation code.
 * @param buffer  User buffer to receive the result.
 * @param length  Size of @a buffer in bytes.
 * @retval B_OK    Operation completed successfully.
 * @retval ERANGE  @a length is too small to hold the driver path.
 */
status_t
Device::Control(void* _cookie, int32 op, void* buffer, size_t length)
{
	switch (op) {
		case B_GET_DRIVER_FOR_DEVICE:
		{
			char* path = NULL;
			status_t status = module_get_path(ModuleName(), &path);
			if (status != B_OK)
				return status;
			if (length != 0 && length <= strlen(path))
				return ERANGE;
			status = user_strlcpy(static_cast<char*>(buffer), path, length);
			free(path);
			return status;
		}
		default:
			return AbstractModuleDevice::Control(_cookie, op, buffer, length);;
	}
}


//	#pragma mark - device_node


/**
 * @brief Construct a device_node with the given driver module name and initial attributes.
 *
 * Duplicates @a moduleName, copies all supplied attributes into fAttributes,
 * appends a "device/driver" string attribute, and reads the B_DEVICE_FLAGS
 * attribute into fFlags.
 *
 * @param moduleName Fully-qualified driver module name for this node.
 * @param attrs      NULL-terminated array of initial attributes, or NULL.
 * @note If fModuleName allocation fails, InitCheck() will return B_NO_MEMORY.
 */
device_node::device_node(const char* moduleName, const device_attr* attrs)
{
	fModuleName = strdup(moduleName);
	if (fModuleName == NULL)
		return;

	fParent = NULL;
	fRefCount = 1;
	fInitialized = 0;
	fRegistered = false;
	fFlags = 0;
	fSupportsParent = 0.0;
	fLastUpdateCycle = 0;
	fDriver = NULL;
	fDriverData = NULL;

	// copy attributes

	while (attrs != NULL && attrs->name != NULL) {
		device_attr_private* attr
			= new(std::nothrow) device_attr_private(*attrs);
		if (attr == NULL)
			break;

		fAttributes.Add(attr);
		attrs++;
	}

	device_attr_private* attr = new(std::nothrow) device_attr_private();
	if (attr != NULL) {
		attr->name = strdup("device/driver");
		attr->type = B_STRING_TYPE;
		attr->value.string = strdup(fModuleName);
		fAttributes.Add(attr);
	}

	get_attr_uint32(this, B_DEVICE_FLAGS, &fFlags, false);
	fFlags &= NODE_FLAG_PUBLIC_MASK;
}


/**
 * @brief Destroy the device_node, releasing children, devices, attributes, and resources.
 *
 * If this node was using an obsolete driver, notifies the parent to release
 * any waiting replacement driver. Removes itself from the parent's child list.
 *
 * @note The driver must already have been uninitialized (DriverModule() == NULL)
 *       before destruction, as asserted internally.
 */
device_node::~device_node()
{
	TRACE(("delete node %p\n", this));
	ASSERT(DriverModule() == NULL);

	if (Parent() != NULL) {
		if ((fFlags & NODE_FLAG_OBSOLETE_DRIVER) != 0) {
			// This driver has been obsoleted; another driver has been waiting
			// for us - make it available
			Parent()->_ReleaseWaiting();
		}
		Parent()->RemoveChild(this);
	}

	// Delete children
	while (device_node* child = fChildren.RemoveHead()) {
		delete child;
	}

	// Delete devices
	while (Device* device = fDevices.RemoveHead()) {
		device->SetRemovedFromParent(true);
		devfs_unpublish_device(device, true);
	}

	// Delete attributes
	while (device_attr_private* attr = fAttributes.RemoveHead()) {
		delete attr;
	}

	// Delete resources
	while (io_resource_private* resource = fResources.RemoveHead()) {
		delete resource;
	}

	free((char*)fModuleName);
}


/**
 * @brief Check whether the node was constructed successfully.
 *
 * @retval B_OK      fModuleName is valid; the node is usable.
 * @retval B_NO_MEMORY fModuleName allocation failed during construction.
 */
status_t
device_node::InitCheck() const
{
	return fModuleName != NULL ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Claim and store the I/O resources listed in @a resources.
 *
 * Iterates the NULL-terminated resource array, allocating and acquiring each
 * entry via io_resource_private::Acquire().
 *
 * @param resources NULL-terminated array of I/O resources to acquire, or NULL
 *                  to do nothing.
 * @retval B_OK        All resources acquired successfully.
 * @retval B_NO_MEMORY Allocation of an io_resource_private failed.
 * @retval other       A resource could not be acquired; partial resources may
 *                     have been stored in fResources.
 */
status_t
device_node::AcquireResources(const io_resource* resources)
{
	if (resources == NULL)
		return B_OK;

	for (uint32 i = 0; resources[i].type != 0; i++) {
		io_resource_private* resource = new(std::nothrow) io_resource_private;
		if (resource == NULL)
			return B_NO_MEMORY;

		status_t status = resource->Acquire(resources[i]);
		if (status != B_OK) {
			delete resource;
			return status;
		}

		fResources.Add(resource);
	}

	return B_OK;
}


/**
 * @brief Initialize (or reference-count) the driver bound to this node.
 *
 * Loads the driver module and, if this node has a parent, recursively calls
 * InitDriver() on it. Calls driver->init_driver() on the first initialization.
 * Reference-counted: subsequent calls increment fInitialized and acquire an
 * additional node reference without reloading the module.
 *
 * @retval B_OK    Driver is initialized; node reference acquired.
 * @retval other   Module load or init_driver() failed; fInitialized was rolled back.
 */
status_t
device_node::InitDriver()
{
	if (fInitialized++ > 0) {
		if (Parent() != NULL) {
			Parent()->InitDriver();
				// acquire another reference to our parent as well
		}
		Acquire();
		return B_OK;
	}

	status_t status = get_module(ModuleName(), (module_info**)&fDriver);
	if (status == B_OK && Parent() != NULL) {
		// our parent always has to be initialized
		status = Parent()->InitDriver();
	}
	if (status < B_OK) {
		fInitialized--;
		return status;
	}

	if (fDriver->init_driver != NULL) {
		status = fDriver->init_driver(this, &fDriverData);
		if (status != B_OK) {
			dprintf("driver %s init failed: %s\n", ModuleName(),
				strerror(status));
		}
	}

	if (status < B_OK) {
		if (Parent() != NULL)
			Parent()->UninitDriver();
		fInitialized--;

		put_module(ModuleName());
		fDriver = NULL;
		fDriverData = NULL;
		return status;
	}

	Acquire();
	return B_OK;
}


/**
 * @brief Release one initialization reference from the driver bound to this node.
 *
 * When the last reference is released, calls driver->uninit_driver() and puts
 * the module reference. Also releases one reference on the parent node's driver.
 *
 * @return true if this was the last reference and the driver was fully unloaded;
 *         false if further references remain.
 */
bool
device_node::UninitDriver()
{
	if (fInitialized-- > 1) {
		if (Parent() != NULL)
			Parent()->UninitDriver();
		Release();
		return false;
	}

	TRACE(("uninit driver for node %p\n", this));

	if (fDriver->uninit_driver != NULL)
		fDriver->uninit_driver(fDriverData);

	fDriver = NULL;
	fDriverData = NULL;

	put_module(ModuleName());

	if (Parent() != NULL)
		Parent()->UninitDriver();
	Release();

	return true;
}


/**
 * @brief Insert @a node as a child of this node, maintaining priority order.
 *
 * Children are ordered from highest to lowest priority so that the most
 * specific driver is probed first. Acquiring this node ensures it is not
 * deleted while it still has children.
 *
 * @param node Child node to insert; its fParent pointer is updated.
 */
void
device_node::AddChild(device_node* node)
{
	// we must not be destroyed	as long as we have children
	Acquire();
	node->fParent = this;

	int32 priority = node->Priority();

	// Enforce an order in which the children are traversed - from most
	// specific to least specific child.
	NodeList::Iterator iterator = fChildren.GetIterator();
	device_node* before = NULL;
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();
		if (child->Priority() < priority) {
			before = child;
			break;
		}
	}

	fChildren.InsertBefore(before, node);
}


/**
 * @brief Remove @a node from this node's child list and release the parent reference.
 *
 * @param node Child node to remove; its fParent pointer is cleared.
 */
void
device_node::RemoveChild(device_node* node)
{
	node->fParent = NULL;
	fChildren.Remove(node);
	Release();
}


/*!	Registers this node, and all of its children that have to be registered.
	Also initializes the driver and keeps it that way on return in case
	it returns successfully.
*/
/**
 * @brief Register this node in the device tree, initializing its driver and probing children.
 *
 * Adds this node to @a parent (or sets it as the root), initializes the driver,
 * optionally registers fixed children, calls register_child_devices(), and
 * finally performs dynamic child registration.
 *
 * @param parent Parent node to attach to, or NULL if this is the root node.
 * @retval B_OK    Node registered and driver initialized (driver reference kept).
 * @retval other   Registration or driver initialization failed; unused driver
 *                 references are cleaned up.
 * @note On success the driver remains initialized (fInitialized > 0); the
 *       caller must eventually call UninitUnusedDriver() to drop the extra
 *       reference acquired during registration.
 */
status_t
device_node::Register(device_node* parent)
{
	// make it public
	if (parent != NULL)
		parent->AddChild(this);
	else
		sRootNode = this;

	status_t status = InitDriver();
	if (status != B_OK)
		return status;

	if ((fFlags & B_KEEP_DRIVER_LOADED) != 0) {
		// We keep this driver loaded by having it always initialized
		InitDriver();
	}

	fFlags |= NODE_FLAG_REGISTER_INITIALIZED;
		// We don't uninitialize the driver - this is done by the caller
		// in order to save reinitializing during driver loading.

	uint32 registeredFixedCount;
	status = _RegisterFixed(registeredFixedCount);
	if (status != B_OK) {
		UninitUnusedDriver();
		return status;
	}

	// Register the children the driver wants

	if (DriverModule()->register_child_devices != NULL) {
		status = DriverModule()->register_child_devices(DriverData());
		if (status != B_OK) {
			UninitUnusedDriver();
			return status;
		}

		if (!fChildren.IsEmpty()) {
			fRegistered = true;
			return B_OK;
		}
	}

	if (registeredFixedCount > 0) {
		// Nodes with fixed children cannot have any dynamic children, so bail
		// out here
		fRegistered = true;
		return B_OK;
	}

	// Register all possible child device nodes

	status = _RegisterDynamic();
	if (status == B_OK)
		fRegistered = true;
	else
		UninitUnusedDriver();

	return status;
}


/*!	Registers any children that are identified via the B_DEVICE_FIXED_CHILD
	attribute.
	If any of these children cannot be registered, this call will fail (we
	don't remove children we already registered up to this point in this case).
*/
/**
 * @brief Register all children listed via B_DEVICE_FIXED_CHILD attributes.
 *
 * For each B_DEVICE_FIXED_CHILD attribute on this node, loads the named
 * driver module and calls its register_device() callback.
 *
 * @param registered On return, contains the count of successfully registered
 *                   fixed children.
 * @retval B_OK    All fixed children registered (or none were listed).
 * @retval other   A required fixed child module could not be loaded or its
 *                 register_device() failed.
 */
status_t
device_node::_RegisterFixed(uint32& registered)
{
	AttributeList::Iterator iterator = fAttributes.GetIterator();
	registered = 0;

	while (iterator.HasNext()) {
		device_attr_private* attr = iterator.Next();
		if (strcmp(attr->name, B_DEVICE_FIXED_CHILD))
			continue;

		driver_module_info* driver;
		status_t status = get_module(attr->value.string,
			(module_info**)&driver);
		if (status != B_OK) {
			TRACE(("register fixed child %s failed: %s\n", attr->value.string,
				strerror(status)));
			return status;
		}

		if (driver->register_device != NULL) {
			status = driver->register_device(this);
			if (status == B_OK)
				registered++;
		}

		put_module(attr->value.string);

		if (status != B_OK)
			return status;
	}

	return B_OK;
}


/**
 * @brief Append a driver search path (basePath[/subPath]) to @a stack.
 *
 * Allocates a KPath, constructs the path, and pushes it onto @a stack for
 * later iteration by _GetNextDriverPath().
 *
 * @param stack    Target stack to push the new path onto.
 * @param basePath Base directory (e.g. "drivers" or "busses/usb").
 * @param subPath  Optional sub-directory to append; NULL or empty to omit.
 * @retval B_OK        Path constructed and pushed.
 * @retval B_NO_MEMORY KPath allocation failed.
 * @retval other       KPath::SetTo() or Append() failed.
 */
status_t
device_node::_AddPath(Stack<KPath*>& stack, const char* basePath,
	const char* subPath)
{
	KPath* path = new(std::nothrow) KPath;
	if (path == NULL)
		return B_NO_MEMORY;

	status_t status = path->SetTo(basePath);
	if (status == B_OK && subPath != NULL && subPath[0])
		status = path->Append(subPath);
	if (status == B_OK)
		status = stack.Push(path);

	TRACE(("  add path: \"%s\", %" B_PRId32 "\n", path->Path(), status));

	if (status != B_OK)
		delete path;

	return status;
}


/**
 * @brief Iterate over all relevant driver search directories for this node.
 *
 * On the first call (cookie == NULL) the function examines the node's PCI
 * type/subtype attributes and builds a prioritized stack of search paths.
 * Subsequent calls pop the next path off the stack.
 *
 * @param cookie State cookie; set to NULL on the first call, then pass back
 *               the value returned by the previous call. Freed internally
 *               when the stack is exhausted.
 * @param _path  Receives the next search path on success.
 * @retval B_OK             Path written to _path; further paths may remain.
 * @retval B_NO_MEMORY      Stack or path allocation failed.
 * @retval B_ENTRY_NOT_FOUND All paths have been returned; cookie is freed.
 */
status_t
device_node::_GetNextDriverPath(void*& cookie, KPath& _path)
{
	Stack<KPath*>* stack = NULL;

	if (cookie == NULL) {
		// find all paths and add them
		stack = new(std::nothrow) Stack<KPath*>();
		if (stack == NULL)
			return B_NO_MEMORY;

		StackDeleter<KPath*> stackDeleter(stack);

		bool generic = false;
		uint16 type = 0;
		uint16 subType = 0;
		if (get_attr_uint16(this, B_DEVICE_TYPE, &type, false) != B_OK
			|| get_attr_uint16(this, B_DEVICE_SUB_TYPE, &subType, false)
					!= B_OK)
			generic = true;

		// TODO: maybe make this extendible via settings file?
		switch (type) {
			case PCI_mass_storage:
				switch (subType) {
					case PCI_scsi:
						_AddPath(*stack, "busses", "scsi");
						_AddPath(*stack, "busses", "virtio");
						break;
					case PCI_ide:
						_AddPath(*stack, "busses", "ata");
						_AddPath(*stack, "busses", "ide");
						break;
					case PCI_sata:
						// TODO: check for ahci interface
						_AddPath(*stack, "busses", "scsi");
						_AddPath(*stack, "busses", "ata");
						_AddPath(*stack, "busses", "ide");
						break;
					case PCI_nvm:
						_AddPath(*stack, "drivers", "disk");
						break;
					default:
						_AddPath(*stack, "busses");
						break;
				}
				break;
			case PCI_serial_bus:
				switch (subType) {
					case PCI_firewire:
						_AddPath(*stack, "busses", "firewire");
						break;
					case PCI_usb:
						_AddPath(*stack, "busses", "usb");
						break;
					default:
						_AddPath(*stack, "busses");
						break;
				}
				break;
			case PCI_network:
				_AddPath(*stack, "drivers", "net");
				_AddPath(*stack, "busses", "virtio");
				break;
			case PCI_display:
				_AddPath(*stack, "drivers", "graphics");
				_AddPath(*stack, "busses", "virtio");
				break;
			case PCI_multimedia:
				switch (subType) {
					case PCI_audio:
					case PCI_hd_audio:
						_AddPath(*stack, "drivers", "audio");
						_AddPath(*stack, "busses", "virtio");
						break;
					case PCI_video:
						_AddPath(*stack, "drivers", "video");
						break;
					default:
						_AddPath(*stack, "drivers");
						break;
				}
				break;
			case PCI_base_peripheral:
				switch (subType) {
					case PCI_sd_host:
						_AddPath(*stack, "busses", "mmc");
						break;
					case PCI_system_peripheral_other:
						_AddPath(*stack, "busses", "mmc");
						_AddPath(*stack, "drivers");
						break;
					default:
						_AddPath(*stack, "drivers");
						break;
				}
				break;
			case PCI_encryption_decryption:
				switch (subType) {
					case PCI_encryption_decryption_other:
						_AddPath(*stack, "busses", "random");
						break;
					default:
						_AddPath(*stack, "drivers");
						break;
				}
				break;
			case PCI_data_acquisition:
				switch (subType) {
					case PCI_data_acquisition_other:
						_AddPath(*stack, "busses", "i2c");
						_AddPath(*stack, "drivers");
						break;
					default:
						_AddPath(*stack, "drivers");
						break;
				}
				break;
			default:
				if (sRootNode == this) {
					_AddPath(*stack, "busses/pci");
					_AddPath(*stack, "bus_managers");
				} else if (!generic) {
					_AddPath(*stack, "drivers");
					_AddPath(*stack, "busses/virtio");
				} else {
					// For generic drivers, we only allow busses when the
					// request is more specified
					if (sGenericContextPath != NULL
						&& (!strcmp(sGenericContextPath, "disk")
							|| !strcmp(sGenericContextPath, "ports")
							|| !strcmp(sGenericContextPath, "bus"))) {
						_AddPath(*stack, "busses");
					}
					const char* bus;
					if (get_attr_string(this, B_DEVICE_BUS, &bus, false) == B_OK) {
						if (strcmp(bus, "virtio") == 0 || strcmp(bus, "hyperv") == 0)
							_AddPath(*stack, "busses/scsi");
					}
					_AddPath(*stack, "drivers", sGenericContextPath);
					_AddPath(*stack, "busses/i2c");
					_AddPath(*stack, "busses/random");
					_AddPath(*stack, "busses/virtio");
					_AddPath(*stack, "bus_managers/pci");
					_AddPath(*stack, "busses/pci");
					_AddPath(*stack, "busses/mmc");
				}
				break;
		}

		stackDeleter.Detach();

		cookie = (void*)stack;
	} else
		stack = static_cast<Stack<KPath*>*>(cookie);

	KPath* path;
	if (stack->Pop(&path)) {
		_path.Adopt(*path);
		delete path;
		return B_OK;
	}

	delete stack;
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Read the next driver_v1 module from an open module list.
 *
 * Skips the node's own module and any module that lacks supports_device or
 * register_device callbacks.
 *
 * @param list   Open module list handle from open_module_list_etc().
 * @param driver Receives the loaded driver_module_info on success.
 * @retval B_OK    A suitable driver module was loaded into @a driver.
 * @retval other   read_next_module_name() or get_module() returned an error,
 *                 indicating the list is exhausted.
 */
status_t
device_node::_GetNextDriver(void* list, driver_module_info*& driver)
{
	while (true) {
		char name[B_FILE_NAME_LENGTH];
		size_t nameLength = sizeof(name);

		status_t status = read_next_module_name(list, name, &nameLength);
		if (status != B_OK)
			return status;

		if (!strcmp(fModuleName, name))
			continue;

		if (get_module(name, (module_info**)&driver) != B_OK)
			continue;

		if (driver->supports_device == NULL
			|| driver->register_device == NULL) {
			put_module(name);
			continue;
		}

		return B_OK;
	}
}


/**
 * @brief Scan @a path for the driver with the highest supports_device() score.
 *
 * Iterates all driver_v1 modules in @a path, calling supports_device() on
 * each. Keeps a reference to the best-scoring module. Skips the driver
 * currently bound to @a previous.
 *
 * @param path        Module directory path to scan.
 * @param bestDriver  In/out: best driver found so far; updated if a better
 *                    match is found. Caller must put_module() the reference on
 *                    success.
 * @param bestSupport In/out: support score of @a bestDriver.
 * @param previous    Currently bound driver node to skip, or NULL.
 * @retval B_OK             At least one driver scored higher than @a bestSupport.
 * @retval B_ENTRY_NOT_FOUND No improvement found in @a path.
 */
status_t
device_node::_FindBestDriver(const char* path, driver_module_info*& bestDriver,
	float& bestSupport, device_node* previous)
{
	if (bestDriver == NULL)
		bestSupport = previous != NULL ? previous->fSupportsParent : 0.0f;

	void* list = open_module_list_etc(path, "driver_v1");
	driver_module_info* driver;
	while (_GetNextDriver(list, driver) == B_OK) {
		if (previous != NULL && driver == previous->DriverModule()) {
			put_module(driver->info.name);
			continue;
		}

		float support = driver->supports_device(this);
		if (support > bestSupport) {
			if (bestDriver != NULL)
				put_module(bestDriver->info.name);

			bestDriver = driver;
			bestSupport = support;
			continue;
				// keep reference to best module around
		}

		put_module(driver->info.name);
	}
	close_module_list(list);

	return bestDriver != NULL ? B_OK : B_ENTRY_NOT_FOUND;
}


/**
 * @brief Register every driver in @a path whose supports_device() score is positive.
 *
 * Used when B_FIND_MULTIPLE_CHILDREN is set; all matching drivers create
 * child nodes.
 *
 * @param path Module directory path to scan.
 * @retval B_OK             At least one driver was registered.
 * @retval B_ENTRY_NOT_FOUND No driver in @a path supported this node.
 */
status_t
device_node::_RegisterPath(const char* path)
{
	void* list = open_module_list_etc(path, "driver_v1");
	driver_module_info* driver;
	uint32 count = 0;

	while (_GetNextDriver(list, driver) == B_OK) {
		float support = driver->supports_device(this);
		if (support > 0.0) {
			TRACE(("  register module \"%s\", support %f\n", driver->info.name,
				support));
			if (driver->register_device(this) == B_OK)
				count++;
		}

		put_module(driver->info.name);
	}
	close_module_list(list);

	return count > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


/**
 * @brief Determine whether this node should always register dynamic children.
 *
 * Returns true for node types (serial bus, bridge, encryption, and type 0)
 * that should have their children enumerated unconditionally, ignoring the
 * B_FIND_CHILD_ON_DEMAND flag.
 *
 * @return true if dynamic children must always be registered.
 */
bool
device_node::_AlwaysRegisterDynamic()
{
	uint16 type = 0;
	uint16 subType = 0;
	get_attr_uint16(this, B_DEVICE_TYPE, &type, false);
	get_attr_uint16(this, B_DEVICE_SUB_TYPE, &subType, false);

	switch (type) {
		case PCI_serial_bus:
		case PCI_bridge:
		case PCI_encryption_decryption:
		case 0:
			return true;
	}
	return false;
		// TODO: we may want to be a bit more specific in the future
}


/**
 * @brief Dynamically register child nodes by scanning driver directories.
 *
 * If B_FIND_MULTIPLE_CHILDREN is set, all positively-scoring drivers from
 * all relevant paths are registered. Otherwise, the single best-scoring driver
 * across all paths is registered, and the previous driver node (if any) is
 * marked obsolete.
 *
 * @param previous Currently active child node for single-child mode, or NULL.
 * @retval B_OK Always; individual registration failures are silently ignored.
 */
status_t
device_node::_RegisterDynamic(device_node* previous)
{
	// If this is not a bus, we don't have to scan it
	if (find_attr(this, B_DEVICE_BUS, false, B_STRING_TYPE) == NULL)
		return B_OK;

	// If we're not being probed, we honour the B_FIND_CHILD_ON_DEMAND
	// requirements
	if (!IsProbed() && (fFlags & B_FIND_CHILD_ON_DEMAND) != 0
		&& !_AlwaysRegisterDynamic())
		return B_OK;

	KPath path;

	if ((fFlags & B_FIND_MULTIPLE_CHILDREN) == 0) {
		// find the one driver
		driver_module_info* bestDriver = NULL;
		float bestSupport = 0.0;
		void* cookie = NULL;

		while (_GetNextDriverPath(cookie, path) == B_OK) {
			_FindBestDriver(path.Path(), bestDriver, bestSupport, previous);
		}

		if (bestDriver != NULL) {
			TRACE(("  register best module \"%s\", support %f\n",
				bestDriver->info.name, bestSupport));
			if (bestDriver->register_device(this) == B_OK) {
				// There can only be one node of this driver
				// (usually only one at all, but there might be a new driver
				// "waiting" for its turn)
				device_node* child = FindChild(bestDriver->info.name);
				if (child != NULL) {
					child->fSupportsParent = bestSupport;
					if (previous != NULL) {
						previous->fFlags |= NODE_FLAG_OBSOLETE_DRIVER;
						previous->Release();
						child->fFlags |= NODE_FLAG_WAITING_FOR_DRIVER;
					}
				}
				// TODO: if this fails, we could try the second best driver,
				// and so on...
			}
			put_module(bestDriver->info.name);
		}
	} else {
		// register all drivers that match
		void* cookie = NULL;
		while (_GetNextDriverPath(cookie, path) == B_OK) {
			_RegisterPath(path.Path());
		}
	}

	return B_OK;
}


/**
 * @brief Clear the NODE_FLAG_WAITING_FOR_DRIVER flag from all direct children.
 *
 * Called when an obsolete driver node is destroyed, allowing any waiting
 * replacement driver to proceed.
 */
void
device_node::_ReleaseWaiting()
{
	NodeList::Iterator iterator = fChildren.GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();

		child->fFlags &= ~NODE_FLAG_WAITING_FOR_DRIVER;
	}
}


/**
 * @brief Release all child nodes, returning whether any remained busy.
 *
 * Calls Release() on every child. If children still exist after the loop
 * (because they are referenced elsewhere) B_BUSY is returned.
 *
 * @retval B_OK   All children were released.
 * @retval B_BUSY At least one child still holds an external reference.
 */
status_t
device_node::_RemoveChildren()
{
	NodeList::Iterator iterator = fChildren.GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();
		child->Release();
	}

	return fChildren.IsEmpty() ? B_OK : B_BUSY;
}


/**
 * @brief Find the currently active (non-waiting) child node.
 *
 * @return The first child whose NODE_FLAG_WAITING_FOR_DRIVER is not set, or
 *         NULL if all children are waiting.
 */
device_node*
device_node::_FindCurrentChild()
{
	NodeList::Iterator iterator = fChildren.GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();

		if ((child->Flags() & NODE_FLAG_WAITING_FOR_DRIVER) == 0)
			return child;
	}

	return NULL;
}


/**
 * @brief Internal probe: remove stale child nodes and re-run dynamic registration.
 *
 * For B_FIND_CHILD_ON_DEMAND nodes that have already been probed and have a
 * single active child, the child is passed as @a previous to _RegisterDynamic()
 * so that the best available driver can replace it if a better one is now
 * available.
 *
 * @retval B_OK    Probe (and optional re-registration) succeeded.
 * @retval other   _RegisterDynamic() returned an error.
 */
status_t
device_node::_Probe()
{
	device_node* previous = NULL;

	if (IsProbed() && !fChildren.IsEmpty()
		&& (fFlags & (B_FIND_CHILD_ON_DEMAND | B_FIND_MULTIPLE_CHILDREN))
				== B_FIND_CHILD_ON_DEMAND) {
		// We already have a driver that claims this node; remove all
		// (unused) nodes, and evaluate it again
		_RemoveChildren();

		previous = _FindCurrentChild();
		if (previous != NULL) {
			// This driver is still active - give it back the reference
			// that was stolen by _RemoveChildren() - _RegisterDynamic()
			// will release it, if it really isn't needed anymore
			previous->Acquire();
		}
	}

	return _RegisterDynamic(previous);
}


/**
 * @brief Probe this node against a devfs device path and update cycle.
 *
 * If the node has B_FIND_CHILD_ON_DEMAND set, checks whether the PCI type
 * matches the given @a devicePath context before probing. Otherwise, probes
 * all child nodes recursively.
 *
 * @param devicePath  devfs context path (e.g. "disk", "net", "audio").
 * @param updateCycle Monotonically increasing counter; nodes already probed
 *                    in the current cycle are skipped.
 * @retval B_OK    Probe completed (node may or may not have matched).
 * @retval other   InitDriver() or _Probe() returned an error.
 */
status_t
device_node::Probe(const char* devicePath, uint32 updateCycle)
{
	if ((fFlags & NODE_FLAG_DEVICE_REMOVED) != 0
		|| updateCycle == fLastUpdateCycle)
		return B_OK;

	status_t status = InitDriver();
	if (status < B_OK)
		return status;

	MethodDeleter<device_node, bool, &device_node::UninitDriver> uninit(this);

	if ((fFlags & B_FIND_CHILD_ON_DEMAND) != 0) {
		bool matches = false;
		uint16 type = 0;
		uint16 subType = 0;
		if (get_attr_uint16(this, B_DEVICE_SUB_TYPE, &subType, false) == B_OK
			&& get_attr_uint16(this, B_DEVICE_TYPE, &type, false) == B_OK) {
			// Check if this node matches the device path
			// TODO: maybe make this extendible via settings file?
			if (!strcmp(devicePath, "disk")) {
				matches = type == PCI_mass_storage
					|| (type == PCI_base_peripheral
						&& (subType == PCI_sd_host
							|| subType == PCI_system_peripheral_other));
			} else if (!strcmp(devicePath, "audio")) {
				matches = type == PCI_multimedia
					&& (subType == PCI_audio || subType == PCI_hd_audio);
			} else if (!strcmp(devicePath, "net")) {
				matches = type == PCI_network;
			} else if (!strcmp(devicePath, "graphics")) {
				matches = type == PCI_display;
			} else if (!strcmp(devicePath, "video")) {
				matches = type == PCI_multimedia && subType == PCI_video;
			} else if (!strcmp(devicePath, "power")) {
				matches = type == PCI_data_acquisition;
			} else if (!strcmp(devicePath, "input")) {
				matches = type == PCI_data_acquisition
					&& subType == PCI_data_acquisition_other;
			}
		} else {
			// This driver does not support types, but still wants to its
			// children explored on demand only.
			matches = true;
			sGenericContextPath = devicePath;
		}

		if (matches) {
			fLastUpdateCycle = updateCycle;
				// This node will be probed in this update cycle

			status = _Probe();

			sGenericContextPath = NULL;
			return status;
		}

		return B_OK;
	}

	NodeList::Iterator iterator = fChildren.GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();

		status = child->Probe(devicePath, updateCycle);
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


/**
 * @brief Re-probe this node and all of its children unconditionally.
 *
 * Initializes the driver, calls _Probe() to re-evaluate the best driver, then
 * recursively reprobes every child node.
 *
 * @retval B_OK    Reprobe completed successfully for this node and all children.
 * @retval other   InitDriver() or _Probe() or child->Reprobe() returned an error.
 */
status_t
device_node::Reprobe()
{
	status_t status = InitDriver();
	if (status < B_OK)
		return status;

	MethodDeleter<device_node, bool, &device_node::UninitDriver> uninit(this);

	// If this child has been probed already, probe it again
	status = _Probe();
	if (status != B_OK)
		return status;

	NodeList::Iterator iterator = fChildren.GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();

		status = child->Reprobe();
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


/**
 * @brief Rescan this node's driver for new children, then rescan all children.
 *
 * Initializes the driver and calls its rescan_child_devices() callback if
 * present, then recursively rescans every child node.
 *
 * @retval B_OK    Rescan completed successfully.
 * @retval other   InitDriver() or rescan_child_devices() or a child rescan failed.
 */
status_t
device_node::Rescan()
{
	status_t status = InitDriver();
	if (status < B_OK)
		return status;

	MethodDeleter<device_node, bool, &device_node::UninitDriver> uninit(this);

	if (DriverModule()->rescan_child_devices != NULL) {
		status = DriverModule()->rescan_child_devices(DriverData());
		if (status != B_OK)
			return status;
	}

	NodeList::Iterator iterator = fChildren.GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();

		status = child->Rescan();
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


/*!	Uninitializes all temporary references to the driver. The registration
	process keeps the driver initialized to optimize the startup procedure;
	this function gives this reference away again.
*/
/**
 * @brief Release the extra driver reference held since Register() returned.
 *
 * Traverses to the leaves of the subtree and calls UninitDriver() on each
 * node that still carries the NODE_FLAG_REGISTER_INITIALIZED mark.
 *
 * @note Must be called after Register() to avoid leaking driver references
 *       that were kept alive to speed up initial probing.
 */
void
device_node::UninitUnusedDriver()
{
	// First, we need to go to the leaf, and go back from there

	NodeList::Iterator iterator = fChildren.GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();

		child->UninitUnusedDriver();
	}

	if (!IsInitialized()
		|| (fFlags & NODE_FLAG_REGISTER_INITIALIZED) == 0)
		return;

	fFlags &= ~NODE_FLAG_REGISTER_INITIALIZED;

	UninitDriver();
}


/*!	Calls device_removed() on this node and all of its children - starting
	with the deepest and last child.
	It will also remove the one reference that every node gets on its creation.
*/
/**
 * @brief Notify this node and all descendants that the underlying device has gone away.
 *
 * Recursively notifies children first, then notifies each published Device,
 * marks this node as removed, calls the driver's device_removed() callback,
 * drops the B_KEEP_DRIVER_LOADED reference if set, and finally releases the
 * creation reference.
 */
void
device_node::DeviceRemoved()
{
	// notify children
	NodeList::ConstIterator iterator = Children().GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();

		child->DeviceRemoved();
	}

	// notify devices
	DeviceList::ConstIterator deviceIterator = Devices().GetIterator();
	while (deviceIterator.HasNext()) {
		Device* device = deviceIterator.Next();

		if (device->Module() != NULL
			&& device->Module()->device_removed != NULL)
			device->Module()->device_removed(device->Data());
	}

	fFlags |= NODE_FLAG_DEVICE_REMOVED;

	if (IsInitialized() && DriverModule()->device_removed != NULL)
		DriverModule()->device_removed(this);

	if ((fFlags & B_KEEP_DRIVER_LOADED) != 0) {
		// There is no point in keeping this driver loaded when its device
		// is gone
		UninitDriver();
	}

	UninitUnusedDriver();
	Release();
}


/**
 * @brief Atomically increment the node's reference count.
 */
void
device_node::Acquire()
{
	atomic_add(&fRefCount, 1);
}


/**
 * @brief Decrement the node's reference count, deleting the node when it reaches zero.
 *
 * @return true if this was the last reference and the node was deleted;
 *         false if further references remain.
 */
bool
device_node::Release()
{
	if (atomic_add(&fRefCount, -1) > 1)
		return false;

	delete this;
	return true;
}


/**
 * @brief Append @a device to this node's device list.
 *
 * @param device Device to add; ownership is shared with devfs.
 */
void
device_node::AddDevice(Device* device)
{
	fDevices.Add(device);
}


/**
 * @brief Remove @a device from this node's device list and delete its path/driver attributes.
 *
 * Removes the "dev/<id>/path" and "dev/<id>/driver" attributes that were
 * added by publish_device() and unlinks @a device from fDevices.
 *
 * @param device Device to remove.
 */
void
device_node::RemoveDevice(Device* device)
{
	char attrName[256];
	device_attr_private* attr;

	sprintf(attrName, "dev/%" B_PRIdINO "/path", device->ID());
	attr = find_attr(this, attrName, false, B_STRING_TYPE);
	if (attr != NULL) {
		fAttributes.Remove(attr);
		delete attr;
	}

	sprintf(attrName, "dev/%" B_PRIdINO "/driver", device->ID());
	attr = find_attr(this, attrName, false, B_STRING_TYPE);
	if (attr != NULL) {
		fAttributes.Remove(attr);
		delete attr;
	}

	fDevices.Remove(device);
}


/**
 * @brief Compare this node's attributes against a filter array.
 *
 * For each attribute named in @a attributes, the matching attribute on this
 * node is located and compared via device_attr_private::Compare(). Returns 0
 * only when every attribute in the filter matches.
 *
 * @param attributes NULL-terminated attribute filter array; NULL input returns -1.
 * @return 0 if all filter attributes match; non-zero otherwise.
 */
int
device_node::CompareTo(const device_attr* attributes) const
{
	if (attributes == NULL)
		return -1;

	for (; attributes->name != NULL; attributes++) {
		// find corresponding attribute
		AttributeList::ConstIterator iterator = Attributes().GetIterator();
		device_attr_private* attr = NULL;
		bool found = false;

		while (iterator.HasNext()) {
			attr = iterator.Next();

			if (!strcmp(attr->name, attributes->name)) {
				found = true;
				break;
			}
		}
		if (!found)
			return -1;

		int compare = device_attr_private::Compare(attr, attributes);
		if (compare != 0)
			return compare;
	}

	return 0;
}


/**
 * @brief Find the first direct child that matches @a attributes.
 *
 * Only children not flagged NODE_FLAG_DEVICE_REMOVED are considered.
 *
 * @param attributes NULL-terminated attribute filter; NULL returns NULL immediately.
 * @return Matching child node, or NULL if none found.
 */
device_node*
device_node::FindChild(const device_attr* attributes) const
{
	if (attributes == NULL)
		return NULL;

	NodeList::ConstIterator iterator = Children().GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();

		// ignore nodes that are pending to be removed
		if ((child->Flags() & NODE_FLAG_DEVICE_REMOVED) == 0
			&& !child->CompareTo(attributes))
			return child;
	}

	return NULL;
}


/**
 * @brief Find the first direct child whose module name equals @a moduleName.
 *
 * @param moduleName Module name to search for; NULL returns NULL immediately.
 * @return Matching child node, or NULL if none found.
 */
device_node*
device_node::FindChild(const char* moduleName) const
{
	if (moduleName == NULL)
		return NULL;

	NodeList::ConstIterator iterator = Children().GetIterator();
	while (iterator.HasNext()) {
		device_node* child = iterator.Next();

		if (!strcmp(child->ModuleName(), moduleName))
			return child;
	}

	return NULL;
}


/*!	This returns the priority or importance of this node. Nodes with higher
	priority are registered/probed first.
	Currently, only the B_FIND_MULTIPLE_CHILDREN flag alters the priority;
	it might make sense to be able to directly set the priority via an
	attribute.
*/
/**
 * @brief Return the registration priority for this node.
 *
 * Nodes without B_FIND_MULTIPLE_CHILDREN have priority 100 (probed first);
 * those with the flag have priority 0 (probed last).
 *
 * @return Integer priority value used by AddChild() to order the sibling list.
 */
int32
device_node::Priority()
{
	return (fFlags & B_FIND_MULTIPLE_CHILDREN) != 0 ? 0 : 100;
}


/**
 * @brief Recursively dump this node and all descendants to the kernel debugger.
 *
 * Prints module name, reference count, initialization count, driver module
 * pointer, driver data pointer, all attributes, and all published devices at
 * the given indentation level, then recurses into children.
 *
 * @param level Indentation level; 0 for the root call.
 */
void
device_node::Dump(int32 level)
{
	put_level(level);
	kprintf("(%" B_PRId32 ") @%p \"%s\" (ref %" B_PRId32 ", init %" B_PRId32
		", module %p, data %p)\n", level, this, ModuleName(), fRefCount,
		fInitialized, DriverModule(), DriverData());

	AttributeList::Iterator attribute = Attributes().GetIterator();
	while (attribute.HasNext()) {
		dump_attribute(attribute.Next(), level);
	}

	DeviceList::Iterator deviceIterator = fDevices.GetIterator();
	while (deviceIterator.HasNext()) {
		Device* device = deviceIterator.Next();
		put_level(level);
		kprintf("device: %s, %p\n", device->ModuleName(), device->Data());
	}

	NodeList::ConstIterator iterator = Children().GetIterator();
	while (iterator.HasNext()) {
		iterator.Next()->Dump(level + 1);
	}
}


//	#pragma mark - root node


/**
 * @brief Create and register the root and generic device nodes at boot time.
 *
 * Registers the "Devices Root" node (B_FIND_MULTIPLE_CHILDREN |
 * B_KEEP_DRIVER_LOADED) and, as a child of it, the "Generic" node
 * (additionally B_FIND_CHILD_ON_DEMAND). Failure is reported via dprintf but
 * is not fatal; the kernel continues booting.
 */
static void
init_node_tree(void)
{
	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Devices Root"}},
		{B_DEVICE_BUS, B_STRING_TYPE, {.string = "root"}},
		{B_DEVICE_FLAGS, B_UINT32_TYPE,
			{.ui32 = B_FIND_MULTIPLE_CHILDREN | B_KEEP_DRIVER_LOADED }},
		{NULL}
	};

	device_node* node = NULL;
	if (register_node(NULL, DEVICE_MANAGER_ROOT_NAME, attrs, NULL, &node)
			!= B_OK) {
		dprintf("Cannot register Devices Root Node\n");
	}

	device_attr genericAttrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Generic"}},
		{B_DEVICE_BUS, B_STRING_TYPE, {.string = "generic"}},
		{B_DEVICE_FLAGS, B_UINT32_TYPE, {.ui32 = B_FIND_MULTIPLE_CHILDREN
			| B_KEEP_DRIVER_LOADED | B_FIND_CHILD_ON_DEMAND}},
		{NULL}
	};

	if (register_node(node, DEVICE_MANAGER_GENERIC_NAME, genericAttrs, NULL,
			NULL) != B_OK) {
		dprintf("Cannot register Generic Devices Node\n");
	}
}


driver_module_info gDeviceRootModule = {
	{
		DEVICE_MANAGER_ROOT_NAME,
		0,
		NULL,
	},
};


driver_module_info gDeviceGenericModule = {
	{
		DEVICE_MANAGER_GENERIC_NAME,
		0,
		NULL,
	},
	NULL
};


//	#pragma mark - private kernel API


/**
 * @brief Probe the device tree for drivers matching the given devfs path context.
 *
 * Publishes devfs directories for the path, then delegates to
 * sRootNode->Probe() to walk the tree and register any newly matching drivers.
 *
 * @param path        devfs sub-path context (e.g. "disk", "net") passed to
 *                    each node's Probe() method.
 * @param updateCycle Monotonically increasing value used to avoid probing the
 *                    same node twice within a single scan pass.
 * @retval B_OK    Probe completed (individual driver failures are non-fatal).
 * @retval other   sRootNode->Probe() returned an error.
 */
status_t
device_manager_probe(const char* path, uint32 updateCycle)
{
	TRACE(("device_manager_probe(\"%s\")\n", path));
	RecursiveLocker _(sLock);

	// first, publish directories in the driver directory
	publish_directories(path);

	return sRootNode->Probe(path, updateCycle);
}


/**
 * @brief Initialize the device manager subsystem during early kernel boot.
 *
 * Sets up the I/O scheduler roster, ID generator, I/O resource allocator,
 * the recursive lock, the generic syscall interface, the kernel debugger
 * command, and the initial root/generic device nodes.
 *
 * @param args Kernel boot arguments (currently unused by this function).
 * @retval B_OK Always; individual sub-initialization failures are non-fatal.
 */
status_t
device_manager_init(struct kernel_args* args)
{
	TRACE(("device manager init\n"));

	IOSchedulerRoster::Init();

	dm_init_id_generator();
	dm_init_io_resources();

	recursive_lock_init(&sLock, "device manager");

	register_generic_syscall(DEVICE_MANAGER_SYSCALLS, control_device_manager,
		1, 0);

	add_debugger_command("dm_tree", &dump_device_nodes,
		"dump device node tree");

	init_node_tree();

	return B_OK;
}


/**
 * @brief Perform post-module-load device tree re-probing.
 *
 * Called after all kernel modules are available. Re-probes the entire device
 * tree so that drivers loaded after the initial boot scan can be bound to
 * their nodes.
 *
 * @param args Kernel boot arguments (currently unused).
 * @retval B_OK    Reprobe completed successfully.
 * @retval other   sRootNode->Reprobe() returned an error.
 */
status_t
device_manager_init_post_modules(struct kernel_args* args)
{
	RecursiveLocker _(sLock);
	return sRootNode->Reprobe();
}


/**
 * @brief Return a pointer to the device manager's global recursive lock.
 *
 * @return Pointer to sLock; valid for the lifetime of the kernel.
 */
recursive_lock*
device_manager_get_lock()
{
	return &sLock;
}
