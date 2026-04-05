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
 *   Copyright 2002-2022, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001, Thomas Kurschel. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file module.cpp
 * @brief Kernel module loader — dynamic loading and unloading of kernel modules.
 *
 * Implements the kernel module system: get_module(), put_module(), and the
 * module iterator. Modules are shared libraries loaded on demand from
 * /boot/system/add-ons/kernel/ and its subdirectories. Reference counting
 * ensures a module remains loaded while any consumer holds a reference.
 *
 * @see device_manager.cpp, legacy_drivers.cpp
 */


#include <kmodule.h>

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <FindDirectory.h>
#include <NodeMonitor.h>

#include <boot_device.h>
#include <boot/elf.h>
#include <boot/kernel_args.h>
#include <elf.h>
#include <find_directory_private.h>
#include <fs/KPath.h>
#include <fs/node_monitor.h>
#include <lock.h>
#include <Notifications.h>
#include <safemode.h>
#include <syscalls.h>
#include <util/AutoLock.h>
#include <util/Stack.h>
#include <vfs.h>


//#define TRACE_MODULE
#ifdef TRACE_MODULE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif
#define FATAL(x) dprintf x


#define MODULE_HASH_SIZE 16

/*! The modules referenced by this structure are built-in
	modules that can't be loaded from disk.
*/
extern module_info gDeviceManagerModule;
extern module_info gDeviceRootModule;
extern module_info gDeviceGenericModule;
extern module_info gFrameBufferConsoleModule;

// file systems
extern module_info gRootFileSystem;
extern module_info gDeviceFileSystem;

static module_info* sBuiltInModules[] = {
	&gDeviceManagerModule,
	&gDeviceRootModule,
	&gDeviceGenericModule,
	&gFrameBufferConsoleModule,

	&gRootFileSystem,
	&gDeviceFileSystem,
	NULL
};

enum module_state {
	MODULE_QUERIED = 0,
	MODULE_LOADED,
	MODULE_INIT,
	MODULE_READY,
	MODULE_UNINIT,
	MODULE_ERROR
};


/* Each loaded module image (which can export several modules) is put
 * in a hash (gModuleImagesHash) to be easily found when you search
 * for a specific file name.
 * TODO: Could use only the inode number for hashing. Would probably be
 * a little bit slower, but would lower the memory foot print quite a lot.
 */

struct module_image {
	struct module_image* next;
	module_info**		info;		// the module_info we use
	module_dependency*	dependencies;
	char*				path;		// the full path for the module
	image_id			image;
	int32				ref_count;	// how many ref's to this file
};

/* Each known module will have this structure which is put in the
 * gModulesHash, and looked up by name.
 */

struct module {
	struct module*		next;
	::module_image*		module_image;
	char*				name;
	int32				ref_count;
	module_info*		info;		// will only be valid if ref_count > 0
	int32				offset;		// this is the offset in the headers
	module_state		state;
	uint32				flags;
};

#define B_BUILT_IN_MODULE	2

typedef struct module_path {
	const char*			name;
	uint32				base_length;
} module_path;

typedef struct module_iterator {
	module_path*		stack;
	int32				stack_size;
	int32				stack_current;

	char*				prefix;
	size_t				prefix_length;
	const char*			suffix;
	size_t				suffix_length;
	DIR*				current_dir;
	status_t			status;
	int32				module_offset;
		// This is used to keep track of which module_info
		// within a module we're addressing.
	::module_image*		module_image;
	module_info**		current_header;
	const char*			current_path;
	uint32				path_base_length;
	const char*			current_module_path;
	bool				builtin_modules;
	bool				loaded_modules;
} module_iterator;

namespace Module {

struct entry {
	dev_t				device;
	ino_t				node;
};

struct hash_entry : entry {
	~hash_entry()
	{
		free((char*)path);
	}

	hash_entry*			hash_link;
	const char*			path;
};

struct NodeHashDefinition {
	typedef entry* KeyType;
	typedef hash_entry ValueType;

	size_t Hash(ValueType* entry) const
		{ return HashKey(entry); }
	ValueType*& GetLink(ValueType* entry) const
		{ return entry->hash_link; }

	size_t HashKey(KeyType key) const
	{
		return ((uint32)(key->node >> 32) + (uint32)key->node) ^ key->device;
	}

	bool Compare(KeyType key, ValueType* entry) const
	{
		return key->device == entry->device
			&& key->node == entry->node;
	}
};

typedef BOpenHashTable<NodeHashDefinition> NodeHash;

struct module_listener : DoublyLinkedListLinkImpl<module_listener> {
	~module_listener()
	{
		free((char*)prefix);
	}

	NotificationListener* listener;
	const char*			prefix;
};

typedef DoublyLinkedList<module_listener> ModuleListenerList;

struct module_notification : DoublyLinkedListLinkImpl<module_notification> {
	~module_notification()
	{
		free((char*)name);
	}

	int32		opcode;
	dev_t		device;
	ino_t		directory;
	ino_t		node;
	const char*	name;
};

typedef DoublyLinkedList<module_notification> NotificationList;

class DirectoryWatcher : public NotificationListener {
public:
						DirectoryWatcher();
	virtual				~DirectoryWatcher();

	virtual void		EventOccurred(NotificationService& service,
							const KMessage* event);
};

class ModuleWatcher : public NotificationListener {
public:
						ModuleWatcher();
	virtual				~ModuleWatcher();

	virtual void		EventOccurred(NotificationService& service,
							const KMessage* event);
};

class ModuleNotificationService : public NotificationService {
public:
						ModuleNotificationService();
	virtual				~ModuleNotificationService();

			status_t	InitCheck();

			status_t	AddListener(const KMessage* eventSpecifier,
							NotificationListener& listener);
			status_t	UpdateListener(const KMessage* eventSpecifier,
							NotificationListener& listener);
			status_t	RemoveListener(const KMessage* eventSpecifier,
							NotificationListener& listener);

			bool		HasNode(dev_t device, ino_t node);

			void		Notify(int32 opcode, dev_t device, ino_t directory,
							ino_t node, const char* name);

	virtual const char*	Name() { return "modules"; }

	static	void		HandleNotifications(void *data, int iteration);

private:
			status_t	_RemoveNode(dev_t device, ino_t node);
			status_t	_AddNode(dev_t device, ino_t node, const char* path,
							uint32 flags, NotificationListener& listener);
			status_t	_AddDirectoryNode(dev_t device, ino_t node);
			status_t	_AddModuleNode(dev_t device, ino_t node, int fd,
							const char* name);

			status_t	_AddDirectory(const char* prefix);
			status_t	_ScanDirectory(char* directoryPath, const char* prefix,
							size_t& prefixPosition);
			status_t	_ScanDirectory(Stack<DIR*>& stack, DIR* dir,
							const char* prefix, size_t prefixPosition);

			void		_Notify(int32 opcode, dev_t device, ino_t directory,
							ino_t node, const char* name);
			void		_HandleNotifications();

	recursive_lock		fLock;
	ModuleListenerList	fListeners;
	NodeHash			fNodes;
	DirectoryWatcher	fDirectoryWatcher;
	ModuleWatcher		fModuleWatcher;
	NotificationList	fNotifications;
};


struct ModuleHash {
	typedef const char* KeyType;
	typedef module ValueType;

	size_t Hash(ValueType* module) const
		{ return HashKey(module->name); }
	ValueType*& GetLink(ValueType* entry) const
		{ return entry->next; }

	size_t HashKey(KeyType key) const
	{
		return hash_hash_string(key);
	}

	bool Compare(KeyType key, ValueType* module) const
	{
		if (key == NULL)
			return false;
		return strcmp(module->name, key) == 0;
	}
};

typedef BOpenHashTable<ModuleHash> ModuleTable;


struct ImageHash {
	typedef const char* KeyType;
	typedef module_image ValueType;

	size_t Hash(ValueType* image) const
		{ return HashKey(image->path); }
	ValueType*& GetLink(ValueType* entry) const
		{ return entry->next; }

	size_t HashKey(KeyType key) const
	{
		return hash_hash_string(key);
	}

	bool Compare(KeyType key, ValueType* image) const
	{
		if (key == NULL)
			return false;
		return strcmp(image->path, key) == 0;
	}
};

typedef BOpenHashTable<ImageHash> ImageTable;

}	// namespace Module

using namespace Module;

/* These are the standard base paths where we start to look for modules
 * to load. Order is important, the last entry here will be searched
 * first.
 */
static const directory_which kModulePaths[] = {
	B_SYSTEM_ADDONS_DIRECTORY,
	B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
	B_USER_ADDONS_DIRECTORY,
	B_USER_NONPACKAGED_ADDONS_DIRECTORY,
};

static const uint32 kNumModulePaths = sizeof(kModulePaths)
	/ sizeof(kModulePaths[0]);
static const uint32 kFirstNonSystemModulePath = 1;


static ModuleNotificationService sModuleNotificationService;
static bool sDisableUserAddOns = false;

/*	Locking scheme: There is a global lock only; having several locks
	makes trouble if dependent modules get loaded concurrently ->
	they have to wait for each other, i.e. we need one lock per module;
	also we must detect circular references during init and not dead-lock.

	Reference counting: get_module() increments the ref count of a module,
	put_module() decrements it. When a B_KEEP_LOADED module is initialized
	the ref count is incremented once more, so it never gets
	uninitialized/unloaded. A referenced module, unless it's built-in, has a
	non-null module_image and owns a reference to the image. When the last
	module reference is put, the image's reference is released and module_image
	zeroed (as long as the boot volume has not been mounted, it is not zeroed).
	An unreferenced module image is unloaded (when the boot volume is mounted).
*/
static recursive_lock sModulesLock;


/* We store the loaded modules by directory path, and all known modules
 * by module name in a hash table for quick access
 */
static ImageTable* sModuleImagesHash;
static ModuleTable* sModulesHash;




/**
 * @brief Load a kernel add-on image from \a path and register it in the image hash.
 *
 * Calls load_kernel_add_on(), resolves the mandatory "modules" symbol and the
 * optional "module_dependencies" symbol, then inserts the resulting
 * module_image into sModuleImagesHash.
 *
 * @param path          Absolute filesystem path to the add-on to load.
 * @param _moduleImage  Out-parameter filled with a pointer to the newly
 *                      allocated and registered module_image on success.
 * @retval B_OK          Add-on loaded and registered successfully.
 * @retval B_NO_MEMORY   Memory allocation for module_image or path copy failed.
 * @retval B_BAD_TYPE    Add-on does not export the required "modules" symbol.
 * @retval <0            Error code returned directly from load_kernel_add_on().
 * @note Must be called with sModulesLock held (recursive).
 */
static status_t
load_module_image(const char* path, module_image** _moduleImage)
{
	module_image* moduleImage;
	status_t status;
	image_id image;

	TRACE(("load_module_image(path = \"%s\", _image = %p)\n", path,
		_moduleImage));
	ASSERT_LOCKED_RECURSIVE(&sModulesLock);
	ASSERT(_moduleImage != NULL);

	image = load_kernel_add_on(path);
	if (image < 0) {
		dprintf("load_module_image(%s) failed: %s\n", path, strerror(image));
		return image;
	}

	moduleImage = (module_image*)malloc(sizeof(module_image));
	if (moduleImage == NULL) {
		status = B_NO_MEMORY;
		goto err;
	}

	if (get_image_symbol(image, "modules", B_SYMBOL_TYPE_DATA,
			(void**)&moduleImage->info) != B_OK) {
		TRACE(("load_module_image: Failed to load \"%s\" due to lack of "
			"'modules' symbol\n", path));
		status = B_BAD_TYPE;
		goto err1;
	}

	moduleImage->dependencies = NULL;
	get_image_symbol(image, "module_dependencies", B_SYMBOL_TYPE_DATA,
		(void**)&moduleImage->dependencies);
		// this is allowed to be NULL

	moduleImage->path = strdup(path);
	if (!moduleImage->path) {
		status = B_NO_MEMORY;
		goto err1;
	}

	moduleImage->image = image;
	moduleImage->ref_count = 0;

	sModuleImagesHash->Insert(moduleImage);

	TRACE(("load_module_image(\"%s\"): image loaded: %p\n", path, moduleImage));

	*_moduleImage = moduleImage;
	return B_OK;

err1:
	free(moduleImage);
err:
	unload_kernel_add_on(image);

	return status;
}


/**
 * @brief Unload a kernel add-on image and free all associated resources.
 *
 * Calls unload_kernel_add_on() and frees the module_image struct. Optionally
 * removes the image from sModuleImagesHash first.
 *
 * @param moduleImage  Pointer to the module_image to unload. Must not be NULL.
 * @param remove       If \c true, remove the entry from sModuleImagesHash
 *                     before unloading.
 * @retval B_OK     Image unloaded successfully.
 * @retval B_ERROR  The image's ref_count is not zero; unload refused.
 * @note Must be called with sModulesLock held (recursive).
 */
static status_t
unload_module_image(module_image* moduleImage, bool remove)
{
	TRACE(("unload_module_image(image %p, remove %d)\n", moduleImage, remove));
	ASSERT_LOCKED_RECURSIVE(&sModulesLock);

	if (moduleImage->ref_count != 0) {
		FATAL(("Can't unload %s due to ref_cnt = %" B_PRId32 "\n",
			moduleImage->path, moduleImage->ref_count));
		return B_ERROR;
	}

	if (remove)
		sModuleImagesHash->Remove(moduleImage);

	unload_kernel_add_on(moduleImage->image);
	free(moduleImage->path);
	free(moduleImage);

	return B_OK;
}


/**
 * @brief Decrement the reference count of a module_image, unloading it when
 *        the count reaches zero.
 *
 * The image is only physically unloaded once the boot device is available
 * (gBootDevice > 0); before that point the image stays resident so it can
 * still be referenced.
 *
 * @param image  Pointer to the module_image whose reference count to release.
 * @note Acquires sModulesLock internally; must not be called with the lock
 *       already held by the same thread in a non-reentrant context.
 */
static void
put_module_image(module_image* image)
{
	RecursiveLocker locker(sModulesLock);

	int32 refCount = atomic_add(&image->ref_count, -1);
	ASSERT(refCount > 0);

	// Don't unload anything when there is no boot device yet
	// (because chances are that we will never be able to access it again)

	if (refCount == 1 && gBootDevice > 0)
		unload_module_image(image, true);
}


/**
 * @brief Retrieve (or load) a module_image by filesystem path and increment
 *        its reference count.
 *
 * Looks up \a path in sModuleImagesHash. If not found, delegates to
 * load_module_image(). On success the caller owns one reference and must
 * eventually call put_module_image().
 *
 * @param path    Absolute filesystem path of the add-on to retrieve.
 * @param _image  Out-parameter filled with the module_image pointer on success.
 * @retval B_OK  Image found or loaded; reference count incremented.
 * @retval <0    Error propagated from load_module_image().
 * @note Acquires sModulesLock internally.
 */
static status_t
get_module_image(const char* path, module_image** _image)
{
	struct module_image* image;

	TRACE(("get_module_image(path = \"%s\")\n", path));

	RecursiveLocker _(sModulesLock);

	image = sModuleImagesHash->Lookup(path);
	if (image == NULL) {
		status_t status = load_module_image(path, &image);
		if (status < B_OK)
			return status;
	}

	atomic_add(&image->ref_count, 1);
	*_image = image;

	return B_OK;
}


/**
 * @brief Allocate and register a module struct in sModulesHash from a
 *        module_info descriptor.
 *
 * Creates a new module entry, copies the name, records the offset into the
 * image's module_info array, and inserts it into sModulesHash. The newly
 * created module starts in MODULE_QUERIED state with ref_count zero.
 *
 * @param info     Pointer to the module_info exported by the add-on.
 * @param offset   Index of \a info within the add-on's module_info array
 *                 (pass -1 for built-in modules).
 * @param _module  Optional out-parameter; if non-NULL, receives the new
 *                 module pointer on success.
 * @retval B_OK          Module created and inserted successfully.
 * @retval B_BAD_VALUE   \a info->name is NULL.
 * @retval B_FILE_EXISTS A module with the same name already exists in the hash.
 * @retval B_NO_MEMORY   Memory allocation failed.
 * @note Acquires sModulesLock around the hash insertion.
 */
static status_t
create_module(module_info* info, int offset, module** _module)
{
	module* module;

	TRACE(("create_module(info = %p, offset = %d, _module = %p)\n",
		info, offset, _module));

	if (!info->name)
		return B_BAD_VALUE;

	module = sModulesHash->Lookup(info->name);
	if (module) {
		FATAL(("Duplicate module name (%s) detected... ignoring new one\n",
			info->name));
		return B_FILE_EXISTS;
	}

	if ((module = (struct module*)malloc(sizeof(struct module))) == NULL)
		return B_NO_MEMORY;

	TRACE(("create_module: name = \"%s\"\n", info->name));

	module->module_image = NULL;
	module->name = strdup(info->name);
	if (module->name == NULL) {
		free(module);
		return B_NO_MEMORY;
	}

	module->state = MODULE_QUERIED;
	module->info = info;
	module->offset = offset;
		// record where the module_info can be found in the module_info array
	module->ref_count = 0;
	module->flags = info->flags;

	recursive_lock_lock(&sModulesLock);
	sModulesHash->Insert(module);
	recursive_lock_unlock(&sModulesLock);

	if (_module)
		*_module = module;

	return B_OK;
}


/**
 * @brief Load the add-on at \a path, scan all module_info entries it exports,
 *        and check whether \a searchedName is among them.
 *
 * Calls get_module_image() to load (or retrieve) the image, then iterates its
 * module_info array. For each entry that is not yet in sModulesHash a new
 * module is created via create_module(). If \a searchedName is found the
 * function returns \c B_OK and sets *_moduleImage to the image; otherwise it
 * releases the image reference and returns \c B_ENTRY_NOT_FOUND.
 *
 * Must only be called for files that have not been scanned yet.
 *
 * @param path          Absolute path of the add-on to inspect.
 * @param searchedName  Module name to look for, or NULL to register all modules
 *                      without requiring a specific match.
 * @param _moduleImage  Out-parameter filled with the loaded image on success
 *                      (caller inherits the reference).
 * @retval B_OK              \a searchedName was found (or was NULL and at least
 *                           one module was registered).
 * @retval B_ENTRY_NOT_FOUND \a searchedName not present in the image.
 * @retval B_ENTRY_NOT_FOUND get_module_image() failed for \a path.
 */
static status_t
check_module_image(const char* path, const char* searchedName,
	module_image** _moduleImage)
{
	status_t status = B_ENTRY_NOT_FOUND;
	module_image* image;
	module_info** info;
	int index = 0;

	TRACE(("check_module_image(path = \"%s\", searchedName = \"%s\")\n", path,
		searchedName));

	if (get_module_image(path, &image) < B_OK)
		return B_ENTRY_NOT_FOUND;

	for (info = image->info; *info; info++) {
		// try to create a module for every module_info, check if the
		// name matches if it was a new entry
		bool freshModule = false;
		struct module* module = sModulesHash->Lookup((*info)->name);
		if (module != NULL) {
			// Module does already exist
			if (module->module_image == NULL && module->ref_count == 0) {
				module->info = *info;
				module->offset = index;
				module->flags = (*info)->flags;
				module->state = MODULE_QUERIED;
				freshModule = true;
			}
		} else if (create_module(*info, index, NULL) == B_OK)
			freshModule = true;

		if (freshModule && searchedName != NULL
			&& strcmp((*info)->name, searchedName) == 0) {
			status = B_OK;
		}

		index++;
	}

	if (status != B_OK) {
		// decrement the ref we got in get_module_image
		put_module_image(image);
		return status;
	}

	*_moduleImage = image;
	return B_OK;
}


/**
 * @brief Search all module base directories for an add-on that exports the
 *        module named \a name.
 *
 * Iterates kModulePaths in reverse priority order (highest priority last),
 * asks the VFS for the physical path of the module via vfs_get_module_path(),
 * then calls check_module_image() on each candidate. Stops at the first
 * successful match. User add-on paths are skipped when sDisableUserAddOns is
 * set.
 *
 * @param name          Fully qualified module name to locate.
 * @param _moduleImage  Out-parameter receiving the image reference on success
 *                      (caller must eventually call put_module_image()).
 * @return Pointer to the module struct from sModulesHash on success, or NULL
 *         if the module could not be found in any search path.
 * @note Must be called with sModulesLock held; panics during kernel startup
 *       (gKernelStartup == true).
 */
static module*
search_module(const char* name, module_image** _moduleImage)
{
	status_t status = B_ENTRY_NOT_FOUND;
	uint32 i;

	TRACE(("search_module(%s)\n", name));

	if (gKernelStartup) {
		panic("search_module called during kernel startup! name: \"%s\"", name);
		return NULL;
	}

	for (i = kNumModulePaths; i-- > 0;) {
		if (sDisableUserAddOns && i >= kFirstNonSystemModulePath)
			continue;

		// let the VFS find that module for us

		KPath basePath;
		if (__find_directory(kModulePaths[i], gBootDevice, true,
				basePath.LockBuffer(), basePath.BufferSize()) != B_OK)
			continue;

		basePath.UnlockBuffer();
		basePath.Append("kernel");

		KPath path;
		status = vfs_get_module_path(basePath.Path(), name, path.LockBuffer(),
			path.BufferSize());
		if (status == B_OK) {
			path.UnlockBuffer();
			status = check_module_image(path.Path(), name, _moduleImage);
			if (status == B_OK)
				break;
		}
	}

	if (status != B_OK)
		return NULL;

	return sModulesHash->Lookup(name);
}


/**
 * @brief Release all module references held on behalf of a module's declared
 *        dependencies.
 *
 * Iterates the module_dependency array of \a module's image and calls
 * put_module() for each named dependency. Built-in modules (which have no
 * module_image) are silently skipped.
 *
 * @param module  The module whose dependency references should be released.
 * @retval B_OK  All dependency references released (or no dependencies).
 * @retval <0    First error returned by put_module() for a dependency.
 * @note Caller must hold sModulesLock.
 */
static status_t
put_dependent_modules(struct module* module)
{
	module_image* image = module->module_image;
	module_dependency* dependencies;

	// built-in modules don't have a module_image structure
	if (image == NULL
		|| (dependencies = image->dependencies) == NULL)
		return B_OK;

	for (int32 i = 0; dependencies[i].name != NULL; i++) {
		status_t status = put_module(dependencies[i].name);
		if (status < B_OK)
			return status;
	}

	return B_OK;
}


/**
 * @brief Acquire module references for all modules listed as dependencies of
 *        \a module.
 *
 * Iterates the module_dependency array and calls get_module() for each entry,
 * populating the info pointer recorded in the dependency descriptor. Built-in
 * modules (no module_image) are silently skipped.
 *
 * @param module  The module whose dependencies should be resolved and loaded.
 * @retval B_OK  All dependencies resolved successfully (or no dependencies).
 * @retval <0    Error returned by get_module() for a dependency; the caller
 *               is responsible for rolling back already-acquired references.
 * @note Caller must hold sModulesLock.
 */
static status_t
get_dependent_modules(struct module* module)
{
	module_image* image = module->module_image;
	module_dependency* dependencies;

	// built-in modules don't have a module_image structure
	if (image == NULL
		|| (dependencies = image->dependencies) == NULL)
		return B_OK;

	TRACE(("resolving module dependencies...\n"));

	for (int32 i = 0; dependencies[i].name != NULL; i++) {
		status_t status = get_module(dependencies[i].name,
			dependencies[i].info);
		if (status < B_OK) {
			dprintf("loading dependent module %s of %s failed!\n",
				dependencies[i].name, module->name);
			return status;
		}
	}

	return B_OK;
}


/**
 * @brief Initialize a module by invoking its std_ops(B_MODULE_INIT) callback,
 *        transitioning it through the MODULE_INIT -> MODULE_READY state
 *        sequence.
 *
 * If the module is in MODULE_QUERIED or MODULE_LOADED state its dependencies
 * are first resolved via get_dependent_modules(). Circular or re-entrant
 * initialization attempts are detected and reported as errors.
 *
 * @param module  Pointer to the module to initialize.
 * @retval B_OK     Module initialized (or was already MODULE_READY).
 * @retval B_ERROR  Circular dependency or module in MODULE_UNINIT/MODULE_ERROR
 *                  state.
 * @retval <0       Error returned by std_ops(B_MODULE_INIT) or
 *                  get_dependent_modules().
 * @note Caller must hold sModulesLock.
 */
static inline status_t
init_module(module* module)
{
	switch (module->state) {
		case MODULE_QUERIED:
		case MODULE_LOADED:
		{
			status_t status;
			module->state = MODULE_INIT;

			// resolve dependencies

			status = get_dependent_modules(module);
			if (status < B_OK) {
				module->state = MODULE_LOADED;
				return status;
			}

			// init module

			TRACE(("initializing module %s (at %p)... \n", module->name,
				module->info->std_ops));

			if (module->info->std_ops != NULL)
				status = module->info->std_ops(B_MODULE_INIT);

			TRACE(("...done (%s)\n", strerror(status)));

			if (status >= B_OK)
				module->state = MODULE_READY;
			else {
				put_dependent_modules(module);
				module->state = MODULE_LOADED;
			}

			return status;
		}

		case MODULE_READY:
			return B_OK;

		case MODULE_INIT:
			FATAL(("circular reference to %s\n", module->name));
			return B_ERROR;

		case MODULE_UNINIT:
			FATAL(("tried to load module %s which is currently unloading\n",
				module->name));
			return B_ERROR;

		case MODULE_ERROR:
			FATAL(("cannot load module %s because its earlier unloading "
				"failed\n", module->name));
			return B_ERROR;

		default:
			return B_ERROR;
	}
	// never trespasses here
}


/**
 * @brief Uninitialize a module by invoking its std_ops(B_MODULE_UNINIT)
 *        callback, transitioning it from MODULE_READY back to MODULE_LOADED.
 *
 * If uninit succeeds, dependent module references are released via
 * put_dependent_modules(). On failure the module is placed in MODULE_ERROR
 * state, flagged B_KEEP_LOADED, and its ref count is incremented to prevent
 * future unload attempts.
 *
 * @param module  Pointer to the module to uninitialize.
 * @retval B_NO_ERROR  Module was already in MODULE_QUERIED or MODULE_LOADED
 *                     state (no-op).
 * @retval B_OK        std_ops(B_MODULE_UNINIT) succeeded.
 * @retval B_ERROR     Module is in MODULE_INIT or MODULE_UNINIT state (panic).
 * @retval <0          Error returned by std_ops(B_MODULE_UNINIT); module moved
 *                     to MODULE_ERROR.
 * @note Caller must hold sModulesLock.
 */
static inline int
uninit_module(module* module)
{
	TRACE(("uninit_module(%s)\n", module->name));

	switch (module->state) {
		case MODULE_QUERIED:
		case MODULE_LOADED:
			return B_NO_ERROR;

		case MODULE_INIT:
			panic("Trying to unload module %s which is initializing\n",
				module->name);
			return B_ERROR;

		case MODULE_UNINIT:
			panic("Trying to unload module %s which is un-initializing\n",
				module->name);
			return B_ERROR;

		case MODULE_READY:
		{
			status_t status = B_OK;
			module->state = MODULE_UNINIT;

			TRACE(("uninitializing module %s...\n", module->name));

			if (module->info->std_ops != NULL)
				status = module->info->std_ops(B_MODULE_UNINIT);

			TRACE(("...done (%s)\n", strerror(status)));

			if (status == B_OK) {
				module->state = MODULE_LOADED;
				put_dependent_modules(module);
				return B_OK;
			}

			FATAL(("Error unloading module %s (%s)\n", module->name,
				strerror(status)));

			module->state = MODULE_ERROR;
			module->flags |= B_KEEP_LOADED;
			module->ref_count++;

			return status;
		}
		default:
			return B_ERROR;
	}
	// never trespasses here
}


/**
 * @brief Pop and return the topmost path from an iterator's directory stack.
 *
 * Decrements stack_current and optionally returns the base_length of the
 * popped entry through \a _baseLength.
 *
 * @param iterator    The module_iterator whose stack to pop.
 * @param _baseLength Optional out-parameter receiving the base_length of the
 *                    popped path entry; may be NULL.
 * @return The path string that was on top of the stack, or NULL if the stack
 *         is empty.
 */
static const char*
iterator_pop_path_from_stack(module_iterator* iterator, uint32* _baseLength)
{
	if (iterator->stack_current <= 0)
		return NULL;

	if (_baseLength)
		*_baseLength = iterator->stack[iterator->stack_current - 1].base_length;

	return iterator->stack[--iterator->stack_current].name;
}


/**
 * @brief Push a directory path onto an iterator's traversal stack, growing
 *        the stack buffer as needed.
 *
 * @param iterator    The module_iterator whose stack to extend.
 * @param path        The path string to push (ownership transferred to the
 *                    stack; must remain valid until popped).
 * @param baseLength  The base_length value to associate with this path entry.
 * @retval B_OK        Path pushed successfully.
 * @retval B_NO_MEMORY realloc() of the stack buffer failed.
 */
static status_t
iterator_push_path_on_stack(module_iterator* iterator, const char* path,
	uint32 baseLength)
{
	if (iterator->stack_current + 1 > iterator->stack_size) {
		// allocate new space on the stack
		module_path* stack = (module_path*)realloc(iterator->stack,
			(iterator->stack_size + 8) * sizeof(module_path));
		if (stack == NULL)
			return B_NO_MEMORY;

		iterator->stack = stack;
		iterator->stack_size += 8;
	}

	iterator->stack[iterator->stack_current].name = path;
	iterator->stack[iterator->stack_current++].base_length = baseLength;
	return B_OK;
}


/**
 * @brief Test whether a module name matches the suffix filter of an iterator.
 *
 * Returns true if the iterator has no suffix requirement, or if \a name ends
 * with a '/' followed by the iterator's suffix string.
 *
 * @param iterator  The iterator whose suffix filter to apply.
 * @param name      Fully qualified module name to test.
 * @return true if the name satisfies the suffix constraint, false otherwise.
 */
static bool
match_iterator_suffix(module_iterator* iterator, const char* name)
{
	if (iterator->suffix == NULL || iterator->suffix_length == 0)
		return true;

	size_t length = strlen(name);
	if (length <= iterator->suffix_length)
		return false;

	return name[length - iterator->suffix_length - 1] == '/'
		&& !strcmp(name + length - iterator->suffix_length, iterator->suffix);
}


/**
 * @brief Advance an iterator to the next module name that matches the prefix
 *        and suffix filters.
 *
 * Proceeds through three phases in order:
 *  1. Built-in modules (sBuiltInModules array).
 *  2. Already-loaded modules cached in sModulesHash.
 *  3. On-disk add-ons discovered by walking the directory stack.
 *
 * @param iterator    The active module_iterator driving the traversal.
 * @param buffer      Caller-supplied buffer to receive the module name.
 * @param _bufferSize In/out: on entry the capacity of \a buffer; on success
 *                   updated to the length of the written name.
 * @retval B_OK              A matching module name was written to \a buffer.
 * @retval B_ENTRY_NOT_FOUND No more matching modules exist.
 * @retval B_NO_MEMORY       Memory allocation failed during traversal.
 * @retval B_BUFFER_OVERFLOW Path construction exceeded the KPath buffer.
 * @note Must be called with sModulesLock held.
 */
static status_t
iterator_get_next_module(module_iterator* iterator, char* buffer,
	size_t* _bufferSize)
{
	status_t status;

	TRACE(("iterator_get_next_module() -- start\n"));

	if (iterator->builtin_modules) {
		for (int32 i = iterator->module_offset; sBuiltInModules[i] != NULL;
				i++) {
			// the module name must fit the prefix
			if (strncmp(sBuiltInModules[i]->name, iterator->prefix,
					iterator->prefix_length)
				|| !match_iterator_suffix(iterator, sBuiltInModules[i]->name))
				continue;

			*_bufferSize = strlcpy(buffer, sBuiltInModules[i]->name,
				*_bufferSize);
			iterator->module_offset = i + 1;
			return B_OK;
		}
		iterator->builtin_modules = false;
	}

	if (iterator->loaded_modules) {
		RecursiveLocker _(sModulesLock);
		ModuleTable::Iterator hashIterator(sModulesHash);

		for (int32 i = 0; hashIterator.HasNext(); i++) {
			struct module* module = hashIterator.Next();

			if (i >= iterator->module_offset) {
				if (!strncmp(module->name, iterator->prefix,
						iterator->prefix_length)
					&& match_iterator_suffix(iterator, module->name)) {
					*_bufferSize = strlcpy(buffer, module->name, *_bufferSize);
					iterator->module_offset = i + 1;

					return B_OK;
				}
			}
		}

		// prevent from falling into modules hash iteration again
		iterator->loaded_modules = false;
	}

nextPath:
	if (iterator->current_dir == NULL) {
		// get next directory path from the stack
		const char* path = iterator_pop_path_from_stack(iterator,
			&iterator->path_base_length);
		if (path == NULL) {
			// we are finished, there are no more entries on the stack
			return B_ENTRY_NOT_FOUND;
		}

		free((char*)iterator->current_path);
		iterator->current_path = path;
		iterator->current_dir = opendir(path);
		TRACE(("open directory at %s -> %p\n", path, iterator->current_dir));

		if (iterator->current_dir == NULL) {
			// we don't throw an error here, but silently go to
			// the next directory on the stack
			goto nextPath;
		}
	}

nextModuleImage:
	// TODO: remember which directories were already scanned, and don't search
	// through them again, unless they change (use DirectoryWatcher)

	if (iterator->current_header == NULL) {
		// get next entry from the current directory

		errno = 0;

		struct dirent* dirent;
		if ((dirent = readdir(iterator->current_dir)) == NULL) {
			closedir(iterator->current_dir);
			iterator->current_dir = NULL;

			if (errno < B_OK)
				return errno;

			goto nextPath;
		}

		// check if the prefix matches
		int32 passedOffset, commonLength;
		passedOffset = strlen(iterator->current_path) + 1;
		commonLength = iterator->path_base_length + iterator->prefix_length
			- passedOffset;

		if (commonLength > 0) {
			// the prefix still reaches into the new path part
			int32 length = strlen(dirent->d_name);
			if (commonLength > length)
				commonLength = length;

			if (strncmp(dirent->d_name, iterator->prefix + passedOffset
					- iterator->path_base_length, commonLength))
				goto nextModuleImage;
		}

		// we're not interested in traversing these (again)
		if (!strcmp(dirent->d_name, ".")
			|| !strcmp(dirent->d_name, "..")
			// TODO: this is a bit unclean, as we actually only want to prevent
			// drivers/bin and drivers/dev to be scanned
			|| !strcmp(dirent->d_name, "bin")
			|| !strcmp(dirent->d_name, "dev"))
			goto nextModuleImage;

		// build absolute path to current file
		KPath path(iterator->current_path);
		if (path.InitCheck() != B_OK)
			return B_NO_MEMORY;

		if (path.Append(dirent->d_name) != B_OK)
			return B_BUFFER_OVERFLOW;

		// find out if it's a directory or a file
		struct stat stat;
		if (::stat(path.Path(), &stat) < 0)
			return errno;

		iterator->current_module_path = strdup(path.Path());
		if (iterator->current_module_path == NULL)
			return B_NO_MEMORY;

		if (S_ISDIR(stat.st_mode)) {
			status = iterator_push_path_on_stack(iterator,
				iterator->current_module_path, iterator->path_base_length);
			if (status != B_OK)
				return status;

			iterator->current_module_path = NULL;
			goto nextModuleImage;
		}

		if (!S_ISREG(stat.st_mode))
			return B_BAD_TYPE;

		TRACE(("open module at %s\n", path.Path()));

		status = get_module_image(path.Path(), &iterator->module_image);
		if (status < B_OK) {
			free((char*)iterator->current_module_path);
			iterator->current_module_path = NULL;
			goto nextModuleImage;
		}

		iterator->current_header = iterator->module_image->info;
		iterator->module_offset = 0;
	}

	// search the current module image until we've got a match
	while (*iterator->current_header != NULL) {
		module_info* info = *iterator->current_header;

		// TODO: we might want to create a module here and cache it in the
		// hash table

		iterator->current_header++;
		iterator->module_offset++;

		if (strncmp(info->name, iterator->prefix, iterator->prefix_length)
			|| !match_iterator_suffix(iterator, info->name))
			continue;

		*_bufferSize = strlcpy(buffer, info->name, *_bufferSize);
		return B_OK;
	}

	// leave this module and get the next one

	iterator->current_header = NULL;
	free((char*)iterator->current_module_path);
	iterator->current_module_path = NULL;

	put_module_image(iterator->module_image);
	iterator->module_image = NULL;

	goto nextModuleImage;
}


/**
 * @brief Register every entry in the NULL-terminated sBuiltInModules array as
 *        a built-in module.
 *
 * Sets B_BUILT_IN_MODULE on each module_info's flags and creates a
 * corresponding module entry via create_module(). Intended to be called once
 * during module_init() before any preloaded images are processed.
 *
 * @param info  NULL-terminated array of module_info pointers to register.
 */
static void
register_builtin_modules(struct module_info** info)
{
	for (; *info; info++) {
		(*info)->flags |= B_BUILT_IN_MODULE;
			// this is an internal flag, it doesn't have to be set by modules
			// itself

		if (create_module(*info, -1, NULL) != B_OK) {
			dprintf("creation of built-in module \"%s\" failed!\n",
				(*info)->name);
		}
	}
}


/**
 * @brief Register a preloaded boot-time image as a module_image and create
 *        module entries for all module_info structs it exports.
 *
 * Called during module_init() for every image in kernel_args::preloaded_images.
 * Sets image->is_module to indicate whether the image was recognised as a
 * module add-on. If the "modules" symbol is missing the function returns an
 * error and unloads the image (provided is_module was set true before the
 * failure).
 *
 * @param image  Pointer to the preloaded_image descriptor from the boot loader.
 * @retval B_OK        Image registered and module entries created.
 * @retval B_BAD_VALUE image->id is negative (image not loaded).
 * @retval B_BAD_TYPE  "modules" symbol not found in the image.
 * @retval B_BAD_DATA  First entry in the modules array is NULL.
 * @retval B_NO_MEMORY Memory allocation for the module_image struct failed.
 */
static status_t
register_preloaded_module_image(struct preloaded_image* image)
{
	module_image* moduleImage;
	struct module_info** info;
	status_t status;
	int32 index = 0;

	TRACE(("register_preloaded_module_image(image = %p, name = \"%s\")\n",
		image, image->name.Pointer()));

	image->is_module = false;

	if (image->id < 0)
		return B_BAD_VALUE;

	moduleImage = (module_image*)malloc(sizeof(module_image));
	if (moduleImage == NULL)
		return B_NO_MEMORY;

	if (get_image_symbol(image->id, "modules", B_SYMBOL_TYPE_DATA,
			(void**)&moduleImage->info) != B_OK) {
		status = B_BAD_TYPE;
		goto error;
	}

	image->is_module = true;

	if (moduleImage->info[0] == NULL) {
		status = B_BAD_DATA;
		goto error;
	}

	moduleImage->dependencies = NULL;
	get_image_symbol(image->id, "module_dependencies", B_SYMBOL_TYPE_DATA,
		(void**)&moduleImage->dependencies);
		// this is allowed to be NULL

	moduleImage->path = strdup(image->name);
	if (moduleImage->path == NULL) {
		status = B_NO_MEMORY;
		goto error;
	}

	moduleImage->image = image->id;
	moduleImage->ref_count = 0;

	sModuleImagesHash->Insert(moduleImage);

	for (info = moduleImage->info; *info; info++) {
		struct module* module = NULL;
		if (create_module(*info, index++, &module) == B_OK)
			module->module_image = moduleImage;
	}

	return B_OK;

error:
	free(moduleImage);

	// We don't need this image anymore. We keep it, if it doesn't look like
	// a module at all. It might be an old-style driver.
	if (image->is_module)
		unload_kernel_add_on(image->id);

	return status;
}


/**
 * @brief Kernel debugger command that dumps all known modules and loaded
 *        module images to the kernel console.
 *
 * Iterates sModulesHash and prints each module's address, name, image path,
 * offset, ref_count, state, and module_image pointer. Then iterates
 * sModuleImagesHash and prints each image's address, path, image_id, info
 * pointer, and ref_count.
 *
 * @param argc  Argument count (unused).
 * @param argv  Argument vector (unused).
 * @return Always returns 0.
 * @note Callable from the kernel debugger only; not for general use.
 */
static int
dump_modules(int argc, char** argv)
{
	struct module_image* image;

	ModuleTable::Iterator iterator(sModulesHash);
	kprintf("-- known modules:\n");

	while (iterator.HasNext()) {
		struct module* module = iterator.Next();
		kprintf("%p: \"%s\", \"%s\" (%" B_PRId32 "), refcount = %" B_PRId32 ", "
			"state = %d, mimage = %p\n", module, module->name,
			module->module_image ? module->module_image->path : "",
			module->offset, module->ref_count, module->state,
			module->module_image);
	}

	ImageTable::Iterator imageIterator(sModuleImagesHash);
	kprintf("\n-- loaded module images:\n");

	while (imageIterator.HasNext()) {
		image = imageIterator.Next();
		kprintf("%p: \"%s\" (image_id = %" B_PRId32 "), info = %p, refcount = "
			"%" B_PRId32 "\n", image, image->path, image->image, image->info,
			image->ref_count);
	}
	return 0;
}


//	#pragma mark - DirectoryWatcher


DirectoryWatcher::DirectoryWatcher()
{
}


DirectoryWatcher::~DirectoryWatcher()
{
}


/**
 * @brief Handle a VFS node-monitor event for a watched module directory.
 *
 * Translates a B_ENTRY_MOVED event into either B_ENTRY_CREATED (destination
 * is a watched directory) or B_ENTRY_REMOVED (source was a watched directory),
 * then forwards the resolved opcode and entry information to
 * sModuleNotificationService.Notify().
 *
 * @param service  The NotificationService that delivered the event (unused).
 * @param event    KMessage containing "opcode", "device", "directory", "node",
 *                 and "name" fields, plus "to directory"/"from directory" for
 *                 move events.
 */
void
DirectoryWatcher::EventOccurred(NotificationService& service,
	const KMessage* event)
{
	int32 opcode = event->GetInt32("opcode", -1);
	dev_t device = event->GetInt32("device", -1);
	ino_t directory = event->GetInt64("directory", -1);
	ino_t node = event->GetInt64("node", -1);
	const char *name = event->GetString("name", NULL);

	if (opcode == B_ENTRY_MOVED) {
		// Determine whether it's a move within, out of, or into one
		// of our watched directories.
		directory = event->GetInt64("to directory", -1);
		if (!sModuleNotificationService.HasNode(device, directory)) {
			directory = event->GetInt64("from directory", -1);
			opcode = B_ENTRY_REMOVED;
		} else {
			// Move within, doesn't sound like a good idea for modules
			opcode = B_ENTRY_CREATED;
		}
	}

	sModuleNotificationService.Notify(opcode, device, directory, node, name);
}


//	#pragma mark - ModuleWatcher


ModuleWatcher::ModuleWatcher()
{
}


ModuleWatcher::~ModuleWatcher()
{
}


/**
 * @brief Handle a VFS stat-change event for a watched module file.
 *
 * Filters for B_STAT_CHANGED events that include a modification-time change
 * and notifies sModuleNotificationService with B_STAT_CHANGED so that
 * registered listeners can react to module file updates.
 *
 * @param service  The NotificationService that delivered the event (unused).
 * @param event    KMessage containing "opcode", "fields", "device", and "node".
 */
void
ModuleWatcher::EventOccurred(NotificationService& service, const KMessage* event)
{
	if (event->GetInt32("opcode", -1) != B_STAT_CHANGED
		|| (event->GetInt32("fields", 0) & B_STAT_MODIFICATION_TIME) == 0)
		return;

	dev_t device = event->GetInt32("device", -1);
	ino_t node = event->GetInt64("node", -1);

	sModuleNotificationService.Notify(B_STAT_CHANGED, device, -1, node, NULL);
}


//	#pragma mark - ModuleNotificationService


ModuleNotificationService::ModuleNotificationService()
{
	recursive_lock_init(&fLock, "module notifications");
}


ModuleNotificationService::~ModuleNotificationService()
{
	recursive_lock_destroy(&fLock);
}


/**
 * @brief Register a NotificationListener to receive module change events whose
 *        path begins with a given prefix.
 *
 * Extracts "prefix" from \a eventSpecifier, allocates a module_listener, and
 * calls _AddDirectory() to begin watching the corresponding filesystem
 * subtree. The listener is added to fListeners on success.
 *
 * @param eventSpecifier  KMessage containing a "prefix" string key.
 * @param listener        The NotificationListener to invoke on matching events.
 * @retval B_OK         Listener registered successfully.
 * @retval B_BAD_VALUE  "prefix" key missing from \a eventSpecifier.
 * @retval B_NO_MEMORY  Allocation of module_listener or prefix copy failed.
 * @retval <0           Error returned by _AddDirectory().
 */
status_t
ModuleNotificationService::AddListener(const KMessage* eventSpecifier,
	NotificationListener& listener)
{
	const char* prefix = eventSpecifier->GetString("prefix", NULL);
	if (prefix == NULL)
		return B_BAD_VALUE;

	module_listener* moduleListener = new(std::nothrow) module_listener;
	if (moduleListener == NULL)
		return B_NO_MEMORY;

	moduleListener->prefix = strdup(prefix);
	if (moduleListener->prefix == NULL) {
		delete moduleListener;
		return B_NO_MEMORY;
	}

	status_t status = _AddDirectory(prefix);
	if (status != B_OK) {
		delete moduleListener;
		return status;
	}

	moduleListener->listener = &listener;
	fListeners.Add(moduleListener);

	return B_OK;
}


/**
 * @brief Update an existing module notification listener (not implemented).
 *
 * @param eventSpecifier  Unused.
 * @param listener        Unused.
 * @retval B_ERROR  Always; not implemented.
 */
status_t
ModuleNotificationService::UpdateListener(const KMessage* eventSpecifier,
	NotificationListener& listener)
{
	return B_ERROR;
}


/**
 * @brief Remove a module notification listener (not implemented).
 *
 * @param eventSpecifier  Unused.
 * @param listener        Unused.
 * @retval B_ERROR  Always; not implemented.
 */
status_t
ModuleNotificationService::RemoveListener(const KMessage* eventSpecifier,
	NotificationListener& listener)
{
	return B_ERROR;
}


/**
 * @brief Check whether a (device, node) pair is currently being watched by
 *        this service.
 *
 * @param device  Device ID of the filesystem node.
 * @param node    Inode number of the filesystem node.
 * @return true if the node is in fNodes, false otherwise.
 * @note Acquires fLock internally.
 */
bool
ModuleNotificationService::HasNode(dev_t device, ino_t node)
{
	RecursiveLocker _(fLock);

	struct entry entry = {device, node};
	return fNodes.Lookup(&entry) != NULL;
}


/**
 * @brief Remove a (device, node) pair from the watch set and unregister the
 *        corresponding VFS node listener.
 *
 * @param device  Device ID of the node to stop watching.
 * @param node    Inode number of the node to stop watching.
 * @retval B_OK              Node removed and listener unregistered.
 * @retval B_ENTRY_NOT_FOUND Node was not in fNodes.
 * @note Acquires fLock internally.
 */
status_t
ModuleNotificationService::_RemoveNode(dev_t device, ino_t node)
{
	RecursiveLocker _(fLock);

	struct entry key = {device, node};
	hash_entry* entry = fNodes.Lookup(&key);
	if (entry == NULL)
		return B_ENTRY_NOT_FOUND;

	remove_node_listener(device, node, entry->path != NULL
		? (NotificationListener&)fModuleWatcher
		: (NotificationListener&)fDirectoryWatcher);

	fNodes.Remove(entry);
	delete entry;

	return B_OK;
}


/**
 * @brief Add a (device, node) pair to the watch set if not already present.
 *
 * Allocates a hash_entry, copies the optional path, registers the VFS node
 * listener with the requested \a flags, and inserts the entry into fNodes.
 *
 * @param device    Device ID of the node to watch.
 * @param node      Inode number to watch.
 * @param path      Optional filesystem path associated with the node (may be
 *                  NULL for directory nodes).
 * @param flags     VFS watch flags (e.g. B_WATCH_DIRECTORY or B_WATCH_STAT).
 * @param listener  The NotificationListener to attach.
 * @retval B_OK        Node added and listener registered.
 * @retval B_OK        Node was already present (early return, no-op).
 * @retval B_NO_MEMORY Allocation of hash_entry or path copy failed.
 * @retval <0          Error from add_node_listener().
 * @note Acquires fLock internally.
 */
status_t
ModuleNotificationService::_AddNode(dev_t device, ino_t node, const char* path,
	uint32 flags, NotificationListener& listener)
{
	RecursiveLocker locker(fLock);

	if (HasNode(device, node))
		return B_OK;

	struct hash_entry* entry = new(std::nothrow) hash_entry;
	if (entry == NULL)
		return B_NO_MEMORY;

	if (path != NULL) {
		entry->path = strdup(path);
		if (entry->path == NULL) {
			delete entry;
			return B_NO_MEMORY;
		}
	} else
		entry->path = NULL;

	status_t status = add_node_listener(device, node, flags, listener);
	if (status != B_OK) {
		delete entry;
		return status;
	}

	//dprintf("  add %s %ld:%lld (%s)\n", flags == B_WATCH_DIRECTORY
	//	? "dir" : "file", device, node, path);

	entry->device = device;
	entry->node = node;
	fNodes.Insert(entry);

	return B_OK;
}


/**
 * @brief Register a directory node for B_WATCH_DIRECTORY monitoring.
 *
 * @param device  Device ID of the directory inode.
 * @param node    Inode number of the directory.
 * @retval B_OK  Successfully added (or already present).
 * @retval <0    Error from _AddNode().
 */
status_t
ModuleNotificationService::_AddDirectoryNode(dev_t device, ino_t node)
{
	return _AddNode(device, node, NULL, B_WATCH_DIRECTORY, fDirectoryWatcher);
}


/**
 * @brief Register a module file node for B_WATCH_STAT monitoring.
 *
 * Resolves the vnode for \a fd to obtain its parent directory, constructs the
 * full path, and calls _AddNode() with B_WATCH_STAT and fModuleWatcher.
 *
 * @param device  Device ID of the module file.
 * @param node    Inode number of the module file.
 * @param fd      File descriptor open on the parent directory.
 * @param name    Name of the module file within the parent directory.
 * @retval B_OK  Node registered successfully.
 * @retval <0    Error from vfs_get_vnode_from_fd(), vfs_entry_ref_to_path(),
 *               or _AddNode().
 */
status_t
ModuleNotificationService::_AddModuleNode(dev_t device, ino_t node, int fd,
	const char* name)
{
	struct vnode* vnode;
	status_t status = vfs_get_vnode_from_fd(fd, true, &vnode);
	if (status != B_OK)
		return status;

	ino_t directory;
	vfs_vnode_to_node_ref(vnode, &device, &directory);

	KPath path;
	status = path.InitCheck();
	if (status == B_OK) {
		status = vfs_entry_ref_to_path(device, directory, name, true,
			path.LockBuffer(), path.BufferSize());
	}
	if (status != B_OK)
		return status;

	path.UnlockBuffer();

	return _AddNode(device, node, path.Path(), B_WATCH_STAT, fModuleWatcher);
}


/**
 * @brief Begin watching all filesystem locations that correspond to the
 *        module namespace prefix \a prefix.
 *
 * Iterates kModulePaths, constructs the full directory path for each base,
 * and calls _ScanDirectory() to register watchers for every matching node.
 * Succeeds if at least one base path could be scanned.
 *
 * @param prefix  Module namespace prefix (e.g. "bus_managers/").
 * @retval B_OK    At least one directory was scanned successfully.
 * @retval B_ERROR No directory could be opened or scanned.
 */
status_t
ModuleNotificationService::_AddDirectory(const char* prefix)
{
	status_t status = B_ERROR;

	for (uint32 i = 0; i < kNumModulePaths; i++) {
		if (sDisableUserAddOns && i >= kFirstNonSystemModulePath)
			break;

		KPath pathBuffer;
		if (__find_directory(kModulePaths[i], gBootDevice, true,
				pathBuffer.LockBuffer(), pathBuffer.BufferSize()) != B_OK)
			continue;

		pathBuffer.UnlockBuffer();
		pathBuffer.Append("kernel");
		pathBuffer.Append(prefix);

		size_t prefixPosition = strlen(prefix);
		status_t scanStatus = _ScanDirectory(pathBuffer.LockBuffer(), prefix,
			prefixPosition);

		pathBuffer.UnlockBuffer();

		// It's enough if we succeed for one directory
		if (status != B_OK)
			status = scanStatus;
	}

	return status;
}


/**
 * @brief Open the deepest accessible ancestor of \a directoryPath and start
 *        a recursive directory scan from that point.
 *
 * If the full path cannot be opened, path components are progressively
 * stripped until a parent directory is accessible or the root is reached.
 * The adjusted \a prefixPosition is passed on to the recursive overload.
 *
 * @param directoryPath  Absolute path to open; may be shortened in-place.
 * @param prefix         Module namespace prefix being watched.
 * @param prefixPosition In/out: offset within \a prefix at which the scan
 *                       begins; adjusted downward if the full path is not
 *                       accessible.
 * @retval B_OK    Directory opened and scan completed successfully.
 * @retval B_ERROR No accessible directory found, or scan returned an error.
 */
status_t
ModuleNotificationService::_ScanDirectory(char* directoryPath,
	const char* prefix, size_t& prefixPosition)
{
	DIR* dir = NULL;
	while (true) {
		dir = opendir(directoryPath);
		if (dir != NULL || prefixPosition == 0)
			break;

		// the full prefix is not accessible, remove path components
		const char* parentPrefix = prefix + prefixPosition - 1;
		while (parentPrefix != prefix && parentPrefix[0] != '/')
			parentPrefix--;

		size_t cutPosition = parentPrefix - prefix;
		size_t length = strlen(directoryPath);
		directoryPath[length - prefixPosition + cutPosition] = '\0';
		prefixPosition = cutPosition;
	}

	if (dir == NULL)
		return B_ERROR;

	Stack<DIR*> stack;
	stack.Push(dir);

	while (stack.Pop(&dir)) {
		status_t status = _ScanDirectory(stack, dir, prefix, prefixPosition);
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


/**
 * @brief Recursively scan a directory, registering VFS watchers for every
 *        module file and subdirectory that matches the prefix.
 *
 * For each dirent: subdirectories matching the prefix component are pushed
 * onto \a stack and registered with _AddDirectoryNode(); regular files are
 * registered with _AddModuleNode(). If no direct prefix match is found the
 * parent directory is registered so that future additions can be detected.
 *
 * @param stack           Stack accumulating subdirectory DIR* handles for
 *                        subsequent iterations.
 * @param dir             The currently open directory to scan.
 * @param prefix          Full module namespace prefix being watched.
 * @param prefixPosition  Offset into \a prefix for the current directory depth.
 * @retval B_OK  Scan completed (individual node registration errors are
 *               silently ignored).
 */
status_t
ModuleNotificationService::_ScanDirectory(Stack<DIR*>& stack, DIR* dir,
	const char* prefix, size_t prefixPosition)
{
	bool directMatchAdded = false;
	struct dirent* dirent;

	while ((dirent = readdir(dir)) != NULL) {
		if (dirent->d_name[0] == '.')
			continue;

		bool directMatch = false;

		if (prefix[prefixPosition] != '\0') {
			// the start must match
			const char* startPrefix = prefix + prefixPosition;
			if (startPrefix[0] == '/')
				startPrefix++;

			const char* endPrefix = strchr(startPrefix, '/');
			size_t length;

			if (endPrefix != NULL)
				length = endPrefix - startPrefix;
			else
				length = strlen(startPrefix);

			if (strncmp(dirent->d_name, startPrefix, length))
				continue;

			if (dirent->d_name[length] == '\0')
				directMatch = true;
		}

		struct stat stat;
		status_t status = vfs_read_stat(dirfd(dir), dirent->d_name, true, &stat,
			true);
		if (status != B_OK)
			continue;

		if (S_ISDIR(stat.st_mode)) {
			int fd = _kern_open_dir(dirfd(dir), dirent->d_name);
			if (fd < 0)
				continue;

			DIR* subDir = fdopendir(fd);
			if (subDir == NULL) {
				close(fd);
				continue;
			}

			stack.Push(subDir);

			if (_AddDirectoryNode(stat.st_dev, stat.st_ino) == B_OK
				&& directMatch)
				directMatchAdded = true;
		} else if (S_ISREG(stat.st_mode)) {
			if (_AddModuleNode(stat.st_dev, stat.st_ino, dirfd(dir),
					dirent->d_name) == B_OK && directMatch)
				directMatchAdded = true;
		}
	}

	if (!directMatchAdded) {
		// We need to monitor this directory to see if a matching file
		// is added.
		struct stat stat;
		status_t status = vfs_read_stat(dirfd(dir), NULL, true, &stat, true);
		if (status == B_OK)
			_AddDirectoryNode(stat.st_dev, stat.st_ino);
	}

	closedir(dir);
	return B_OK;
}


/**
 * @brief Resolve a raw (device, directory, node, name) tuple into a module
 *        namespace path and dispatch it to all matching listeners.
 *
 * Constructs the full filesystem path from the entry ref or the fNodes cache,
 * strips the module base path prefix to obtain a module-namespace-relative
 * path, then iterates fListeners and calls EventOccurred() on each whose
 * prefix matches. Also triggers _AddDirectory() for B_ENTRY_CREATED events
 * and _RemoveNode() for B_ENTRY_REMOVED events.
 *
 * @param opcode     Node-monitor opcode (B_ENTRY_CREATED, B_ENTRY_REMOVED,
 *                   B_STAT_CHANGED, etc.).
 * @param device     Device ID of the affected node.
 * @param directory  Parent directory inode (used when \a name is non-NULL).
 * @param node       Inode of the affected node.
 * @param name       Entry name within \a directory, or NULL if only a node ref
 *                   is available.
 * @note Acquires fLock internally when resolving a node-only reference.
 */
void
ModuleNotificationService::_Notify(int32 opcode, dev_t device, ino_t directory,
	ino_t node, const char* name)
{
	// construct path

	KPath pathBuffer;
	const char* path;

	if (name != NULL) {
		// we have an entry ref
		if (pathBuffer.InitCheck() != B_OK
			|| vfs_entry_ref_to_path(device, directory, name, true,
				pathBuffer.LockBuffer(), pathBuffer.BufferSize()) != B_OK)
			return;

		pathBuffer.UnlockBuffer();
		path = pathBuffer.Path();
	} else {
		// we only have a node ref
		RecursiveLocker _(fLock);

		struct entry key = {device, node};
		hash_entry* entry = fNodes.Lookup(&key);
		if (entry == NULL || entry->path == NULL)
			return;

		path = entry->path;
	}

	// remove kModulePaths from path

	for (uint32 i = 0; i < kNumModulePaths; i++) {
		KPath modulePath;
		if (__find_directory(kModulePaths[i], gBootDevice, true,
				modulePath.LockBuffer(), modulePath.BufferSize()) != B_OK)
			continue;

		modulePath.UnlockBuffer();
		modulePath.Append("kernel");

		if (strncmp(path, modulePath.Path(), modulePath.Length()))
			continue;

		path += modulePath.Length();
		if (path[i] == '/')
			path++;

		break;
	}

	KMessage event;

	// find listeners by prefix/path

	ModuleListenerList::Iterator iterator = fListeners.GetIterator();
	while (iterator.HasNext()) {
		module_listener* listener = iterator.Next();

		if (strncmp(path, listener->prefix, strlen(listener->prefix)))
			continue;

		if (event.IsEmpty()) {
			// construct message only when needed
			event.AddInt32("opcode", opcode);
			event.AddString("path", path);
		}

		// notify them!
		listener->listener->EventOccurred(*this, &event);

		// we might need to watch new files now
		if (opcode == B_ENTRY_CREATED)
			_AddDirectory(listener->prefix);

	}

	// remove notification listeners, if needed

	if (opcode == B_ENTRY_REMOVED)
		_RemoveNode(device, node);
}


/**
 * @brief Drain the pending notification queue and dispatch each notification
 *        via _Notify().
 *
 * Called periodically from the kernel daemon registered in
 * module_init_post_threads(). Holds fLock for the duration to prevent
 * concurrent modifications to fNotifications.
 *
 * @note Acquires fLock internally.
 */
void
ModuleNotificationService::_HandleNotifications()
{
	RecursiveLocker _(fLock);

	NotificationList::Iterator iterator = fNotifications.GetIterator();
	while (iterator.HasNext()) {
		module_notification* notification = iterator.Next();

		_Notify(notification->opcode, notification->device,
			notification->directory, notification->node, notification->name);

		iterator.Remove();
		delete notification;
	}
}


/**
 * @brief Enqueue a filesystem event for deferred delivery to module listeners.
 *
 * Allocates a module_notification, copies the optional \a name string, and
 * appends the notification to fNotifications. Actual delivery happens when
 * HandleNotifications() is invoked by the kernel daemon.
 *
 * @param opcode     Node-monitor opcode identifying the type of change.
 * @param device     Device ID of the affected node.
 * @param directory  Parent directory inode number.
 * @param node       Inode number of the affected node.
 * @param name       Entry name, or NULL if only a node ref is available.
 * @note Acquires fLock internally. Safe to call from interrupt context or
 *       VFS callbacks that cannot block.
 */
void
ModuleNotificationService::Notify(int32 opcode, dev_t device, ino_t directory,
	ino_t node, const char* name)
{
	module_notification* notification = new(std::nothrow) module_notification;
	if (notification == NULL)
		return;

	if (name != NULL) {
		notification->name = strdup(name);
		if (notification->name == NULL) {
			delete notification;
			return;
		}
	} else
		notification->name = NULL;

	notification->opcode = opcode;
	notification->device = device;
	notification->directory = directory;
	notification->node = node;

	RecursiveLocker _(fLock);
	fNotifications.Add(notification);
}


/**
 * @brief Kernel-daemon callback that triggers periodic processing of queued
 *        module filesystem notifications.
 *
 * Registered with register_kernel_daemon() in module_init_post_threads() to
 * run approximately once per second. Delegates to
 * sModuleNotificationService._HandleNotifications().
 *
 * @param data       Unused daemon data pointer.
 * @param iteration  Unused daemon iteration counter.
 */
/*static*/ void
ModuleNotificationService::HandleNotifications(void * /*data*/,
	int /*iteration*/)
{
	sModuleNotificationService._HandleNotifications();
}


//	#pragma mark - Exported Kernel API (private part)


/**
 * @brief Unload a kernel add-on by filesystem path if it is no longer in use.
 *
 * Looks up the image in sModuleImagesHash and calls put_module_image() to
 * release one reference. The image is physically unloaded when the reference
 * count drops to zero (and the boot device is available).
 *
 * @param path  Absolute filesystem path of the add-on to unload.
 * @retval B_OK              Reference released successfully.
 * @retval B_ENTRY_NOT_FOUND No image with \a path is currently registered.
 * @note Acquires sModulesLock internally.
 */
status_t
unload_module(const char* path)
{
	struct module_image* moduleImage;

	recursive_lock_lock(&sModulesLock);
	moduleImage = sModuleImagesHash->Lookup(path);
	recursive_lock_unlock(&sModulesLock);

	if (moduleImage == NULL)
		return B_ENTRY_NOT_FOUND;

	put_module_image(moduleImage);
	return B_OK;
}


/**
 * @brief Load a kernel add-on by filesystem path and return its module_info
 *        array without initializing any individual modules.
 *
 * Unlike get_module(), this function uses an explicit filesystem path rather
 * than a module name. The caller receives a pointer to the NULL-terminated
 * module_info array but must still call get_module() on any module before
 * using it. When finished, the caller must call unload_module() exactly once,
 * regardless of whether get_module() was called on any of the exported modules.
 *
 * @param path     Absolute filesystem path to the add-on to load.
 * @param _modules Out-parameter filled with the module_info** array on success.
 * @retval B_OK  Image loaded and \a *_modules set.
 * @retval <0    Error from get_module_image().
 * @note Acquires sModulesLock internally (via get_module_image()).
 */
status_t
load_module(const char* path, module_info*** _modules)
{
	module_image* moduleImage;
	status_t status = get_module_image(path, &moduleImage);
	if (status != B_OK)
		return status;

	*_modules = moduleImage->info;
	return B_OK;
}


/**
 * @brief Retrieve the filesystem path of the add-on that provides a named
 *        module.
 *
 * Looks up \a moduleName in sModulesHash and returns a heap-allocated copy of
 * the associated module_image path. The caller is responsible for freeing
 * *filePath with free().
 *
 * @param moduleName  Fully qualified module name to look up.
 * @param filePath    Out-parameter receiving a newly allocated path string.
 * @retval B_OK           Path returned in *filePath.
 * @retval B_BAD_VALUE    \a moduleName or \a filePath is NULL.
 * @retval ENOTSUP        Module is built-in and has no associated image file.
 * @retval B_NO_MEMORY    strdup() of the path failed.
 * @retval B_NAME_NOT_FOUND Module not found in sModulesHash.
 * @note Acquires sModulesLock internally.
 */
status_t
module_get_path(const char* moduleName, char** filePath)
{
	if (moduleName == NULL || filePath == NULL)
		return B_BAD_VALUE;

	RecursiveLocker _(sModulesLock);

	// Check if the module and its image are already cached in the module system.
	module* foundModule = sModulesHash->Lookup(moduleName);
	if (foundModule != NULL) {
		if (foundModule->module_image == NULL)
			return ENOTSUP;
				// The module is built-in and has no associated image.
		*filePath = strdup(foundModule->module_image->path);
		return *filePath != NULL ? B_OK : B_NO_MEMORY;
	}

	return B_NAME_NOT_FOUND;
}


/**
 * @brief Begin watching the module namespace subtree rooted at \a prefix for
 *        filesystem changes.
 *
 * Constructs a KMessage specifier with the given prefix and forwards it to
 * sModuleNotificationService.AddListener().
 *
 * @param prefix    Module namespace prefix to watch (e.g. "bus_managers/").
 * @param listener  NotificationListener to invoke when matching events occur.
 * @retval B_OK  Watcher registered successfully.
 * @retval <0    Error from ModuleNotificationService::AddListener().
 */
status_t
start_watching_modules(const char* prefix, NotificationListener& listener)
{
	KMessage specifier;
	status_t status = specifier.AddString("prefix", prefix);
	if (status != B_OK)
		return status;

	return sModuleNotificationService.AddListener(&specifier, listener);
}


/**
 * @brief Stop watching the module namespace subtree rooted at \a prefix.
 *
 * Constructs a KMessage specifier with the given prefix and forwards it to
 * sModuleNotificationService.RemoveListener().
 *
 * @param prefix    Module namespace prefix to stop watching.
 * @param listener  The NotificationListener previously registered with
 *                  start_watching_modules().
 * @retval B_OK  Watcher removed successfully.
 * @retval <0    Error from ModuleNotificationService::RemoveListener().
 */
status_t
stop_watching_modules(const char* prefix, NotificationListener& listener)
{
	KMessage specifier;
	status_t status = specifier.AddString("prefix", prefix);
	if (status != B_OK)
		return status;

	return sModuleNotificationService.RemoveListener(&specifier, listener);
}


/**
 * @brief Initialize the kernel module subsystem.
 *
 * Must be called before any other module function. Initialises sModulesLock,
 * allocates and seeds the module and image hash tables, registers all
 * built-in modules, registers every preloaded boot image from \a args,
 * constructs the ModuleNotificationService, reads the safemode flag for user
 * add-ons, and installs the "modules" debugger command.
 *
 * @param args  Pointer to the kernel_args structure provided by the boot
 *              loader; its preloaded_images list is iterated here.
 * @retval B_OK        Initialization succeeded.
 * @retval B_NO_MEMORY Hash table allocation failed.
 * @note Must be called single-threaded, before multi-threading is enabled.
 */
status_t
module_init(kernel_args* args)
{
	struct preloaded_image* image;

	recursive_lock_init(&sModulesLock, "modules rlock");

	sModulesHash = new(std::nothrow) ModuleTable();
	if (sModulesHash == NULL
			|| sModulesHash->Init(MODULE_HASH_SIZE) != B_OK)
		return B_NO_MEMORY;

	sModuleImagesHash = new(std::nothrow) ImageTable();
	if (sModuleImagesHash == NULL
			|| sModuleImagesHash->Init(MODULE_HASH_SIZE) != B_OK)
		return B_NO_MEMORY;

	// register built-in modules

	register_builtin_modules(sBuiltInModules);

	// register preloaded images

	for (image = args->preloaded_images; image != NULL; image = image->next) {
		status_t status = register_preloaded_module_image(image);
		if (status != B_OK && image->is_module) {
			dprintf("Could not register image \"%s\": %s\n", (char *)image->name,
				strerror(status));
		}
	}

	new(&sModuleNotificationService) ModuleNotificationService();

	sDisableUserAddOns = get_safemode_boolean(B_SAFEMODE_DISABLE_USER_ADD_ONS,
		false);

	add_debugger_command("modules", &dump_modules,
		"list all known & loaded modules");

	return B_OK;
}


/**
 * @brief Post-threading initialization for the module subsystem.
 *
 * Registers the ModuleNotificationService::HandleNotifications() kernel daemon
 * to run approximately once per second (every 10 scheduler ticks), enabling
 * asynchronous delivery of filesystem-change notifications to module watchers.
 *
 * @retval B_OK  Daemon registered successfully.
 * @note Must be called after the scheduler and kernel threads are operational.
 */
status_t
module_init_post_threads(void)
{
	return register_kernel_daemon(
		&ModuleNotificationService::HandleNotifications, NULL, 10);
		// once every second

	return B_OK;
}


/**
 * @brief Finalize module initialization after the boot device becomes
 *        available.
 *
 * Performs two passes over the module and image hash tables:
 *  1. Clears module_image pointers for any unreferenced non-built-in modules
 *     so that get_module() will reload them from disk on next use.
 *  2. Drops unused preloaded images (ref_count == 0), and optionally normalizes
 *     the paths of in-use images when booting from the boot loader volume.
 *
 * @param bootingFromBootLoaderVolume  If true, attempt to normalize relative or
 *                                     unresolved absolute paths of in-use images
 *                                     against the known module base paths.
 * @retval B_OK  Always; individual normalization failures are logged but do not
 *               abort the process.
 * @note Must be called with the boot device already mounted and gBootDevice set.
 *       Acquires sModulesLock internally.
 */
status_t
module_init_post_boot_device(bool bootingFromBootLoaderVolume)
{
	// Remove all unused pre-loaded module images. Now that the boot device is
	// available, we can load an image when we need it.
	// When the boot volume is also where the boot loader pre-loaded the images
	// from, we get the actual paths for those images.
	TRACE(("module_init_post_boot_device(%d)\n", bootingFromBootLoaderVolume));

	RecursiveLocker _(sModulesLock);

	// First of all, clear all pre-loaded module's module_image, if the module
	// isn't in use.
	ModuleTable::Iterator iterator(sModulesHash);
	struct module* module;
	while (iterator.HasNext()) {
		module = iterator.Next();
		if (module->ref_count == 0
			&& (module->flags & B_BUILT_IN_MODULE) == 0) {
			TRACE(("  module %p, \"%s\" unused, clearing image\n", module,
				module->name));
			module->module_image = NULL;
		}
	}

	// Now iterate through the images and drop them respectively normalize their
	// paths.
	ImageTable::Iterator imageIterator(sModuleImagesHash);

	module_image* imagesToReinsert = NULL;
		// When renamed, an image is added to this list to be re-entered in the
		// hash at the end. We can't do that during the iteration.

	while (imageIterator.HasNext()) {
		struct module_image* image = imageIterator.Next();

		if (image->ref_count == 0) {
			// not in use -- unload it
			TRACE(("  module image %p, \"%s\" unused, removing\n", image,
				image->path));
			// Using RemoveUnchecked to avoid invalidating the iterator
			sModuleImagesHash->RemoveUnchecked(image);
			unload_module_image(image, false);
		} else if (bootingFromBootLoaderVolume) {
			bool pathNormalized = false;
			KPath pathBuffer;
			if (image->path[0] != '/') {
				// relative path
				for (uint32 i = kNumModulePaths; i-- > 0;) {
					if (sDisableUserAddOns && i >= kFirstNonSystemModulePath)
						continue;

					if (__find_directory(kModulePaths[i], gBootDevice, true,
							pathBuffer.LockBuffer(), pathBuffer.BufferSize())
								!= B_OK) {
						pathBuffer.UnlockBuffer();
						continue;
					}

					pathBuffer.UnlockBuffer();

					// Append the relative boot module directory and the
					// relative image path, normalize the path, and check
					// whether it exists.
					struct stat st;
					if (pathBuffer.Append("kernel/boot") != B_OK
						|| pathBuffer.Append(image->path) != B_OK
						|| pathBuffer.Normalize(true) != B_OK
						|| lstat(pathBuffer.Path(), &st) != 0) {
						continue;
					}

					pathNormalized = true;
					break;
				}
			} else {
				// absolute path -- try to normalize it anyway
				struct stat st;
				if (pathBuffer.SetPath(image->path) == B_OK
					&& pathBuffer.Normalize(true) == B_OK
					&& lstat(pathBuffer.Path(), &st) == 0) {
					pathNormalized = true;
				}
			}

			if (pathNormalized) {
				TRACE(("  normalized path of module image %p, \"%s\" -> "
					"\"%s\"\n", image, image->path, pathBuffer.Path()));

				// remove the image -- its hash value has probably changed,
				// so we need to re-insert it later
				sModuleImagesHash->RemoveUnchecked(image);

				// set the new path
				free(image->path);
				size_t pathLen = pathBuffer.Length();
				image->path = (char*)realloc(pathBuffer.DetachBuffer(),
					pathLen + 1);

				image->next = imagesToReinsert;
				imagesToReinsert = image;
			} else {
				dprintf("module_init_post_boot_device() failed to normalize "
					"path of module image %p, \"%s\"\n", image, image->path);
			}
		}
	}

	// re-insert the images that have got a new path
	while (module_image* image = imagesToReinsert) {
		imagesToReinsert = image->next;
		sModuleImagesHash->Insert(image);
	}

	TRACE(("module_init_post_boot_device() done\n"));

	return B_OK;
}


//	#pragma mark - Exported Kernel API (public part)


/**
 * @brief Open a filtered iterator over all available modules matching a prefix
 *        and optional suffix.
 *
 * Allocates a module_iterator and pushes the kernel add-on search paths onto
 * its traversal stack. If the boot device is not yet available, only modules
 * already in sModulesHash are enumerated. The returned handle must be released
 * with close_module_list().
 *
 * @param prefix  Module namespace prefix to filter by (e.g. "bus_managers/").
 *                Pass NULL or "" to enumerate all modules.
 * @param suffix  Optional module name suffix filter (e.g. "device_manager").
 *                Pass NULL to disable suffix filtering.
 * @return Opaque iterator handle on success, or NULL on memory allocation
 *         failure or if the module system is not yet initialized.
 */
void*
open_module_list_etc(const char* prefix, const char* suffix)
{
	TRACE(("open_module_list(prefix = %s)\n", prefix));

	if (sModulesHash == NULL) {
		dprintf("open_module_list() called too early!\n");
		return NULL;
	}

	module_iterator* iterator = (module_iterator*)malloc(
		sizeof(module_iterator));
	if (iterator == NULL)
		return NULL;

	memset(iterator, 0, sizeof(module_iterator));

	iterator->prefix = strdup(prefix != NULL ? prefix : "");
	if (iterator->prefix == NULL) {
		free(iterator);
		return NULL;
	}
	iterator->prefix_length = strlen(iterator->prefix);

	iterator->suffix = suffix;
	if (suffix != NULL)
		iterator->suffix_length = strlen(iterator->suffix);

	if (gBootDevice > 0) {
		// We do have a boot device to scan

		// first, we'll traverse over the built-in modules
		iterator->builtin_modules = true;
		iterator->loaded_modules = false;

		// put all search paths on the stack
		for (uint32 i = 0; i < kNumModulePaths; i++) {
			if (sDisableUserAddOns && i >= kFirstNonSystemModulePath)
				break;

			KPath pathBuffer;
			if (__find_directory(kModulePaths[i], gBootDevice, true,
					pathBuffer.LockBuffer(), pathBuffer.BufferSize()) != B_OK)
				continue;

			pathBuffer.UnlockBuffer();
			pathBuffer.Append("kernel");

			// Copy base path onto the iterator stack
			char* path = strdup(pathBuffer.Path());
			if (path == NULL)
				continue;

			size_t length = strlen(path);

			// TODO: it would currently be nicer to use the commented
			// version below, but the iterator won't work if the prefix
			// is inside a module then.
			// It works this way, but should be done better.
#if 0
			// Build path component: base path + '/' + prefix
			size_t length = strlen(sModulePaths[i]);
			char* path = (char*)malloc(length + iterator->prefix_length + 2);
			if (path == NULL) {
				// ToDo: should we abort the whole operation here?
				//	if we do, don't forget to empty the stack
				continue;
			}

			memcpy(path, sModulePaths[i], length);
			path[length] = '/';
			memcpy(path + length + 1, iterator->prefix,
				iterator->prefix_length + 1);
#endif

			iterator_push_path_on_stack(iterator, path, length + 1);
		}
	} else {
		// include loaded modules in case there is no boot device yet
		iterator->builtin_modules = false;
		iterator->loaded_modules = true;
	}

	return (void*)iterator;
}


/**
 * @brief Open an iterator over all available modules matching a prefix.
 *
 * Convenience wrapper around open_module_list_etc() with no suffix filter.
 *
 * @param prefix  Module namespace prefix to filter by; NULL for all modules.
 * @return Opaque iterator handle, or NULL on failure.
 * @see open_module_list_etc(), close_module_list()
 */
void*
open_module_list(const char* prefix)
{
	return open_module_list_etc(prefix, NULL);
}


/**
 * @brief Free all resources associated with a module iterator.
 *
 * Pops and frees all remaining paths from the iterator stack, releases any
 * open module_image reference, closes any open directory handle, and frees
 * all heap allocations including the iterator itself.
 *
 * @param cookie  Iterator handle previously returned by open_module_list() or
 *                open_module_list_etc().
 * @retval B_OK        Iterator freed successfully.
 * @retval B_BAD_VALUE \a cookie is NULL.
 */
status_t
close_module_list(void* cookie)
{
	module_iterator* iterator = (module_iterator*)cookie;
	const char* path;

	TRACE(("close_module_list()\n"));

	if (iterator == NULL)
		return B_BAD_VALUE;

	// free stack
	while ((path = iterator_pop_path_from_stack(iterator, NULL)) != NULL)
		free((char*)path);

	// close what have been left open
	if (iterator->module_image != NULL)
		put_module_image(iterator->module_image);

	if (iterator->current_dir != NULL)
		closedir(iterator->current_dir);

	free(iterator->stack);
	free((char*)iterator->current_path);
	free((char*)iterator->current_module_path);

	free(iterator->prefix);
	free(iterator);

	return B_OK;
}


/**
 * @brief Retrieve the next module name from an open module iterator.
 *
 * Copies the next matching module name into \a buffer and updates
 * \a *_bufferSize with the number of bytes written. Iteration continues until
 * B_ENTRY_NOT_FOUND is returned; at that point close_module_list() should be
 * called.
 *
 * @param cookie       Iterator handle returned by open_module_list() or
 *                     open_module_list_etc().
 * @param buffer       Caller-supplied buffer to receive the module name.
 * @param _bufferSize  In/out: capacity of \a buffer on entry; number of bytes
 *                     written (excluding NUL) on success.
 * @retval B_OK              A module name was written to \a buffer.
 * @retval B_ENTRY_NOT_FOUND No more modules available.
 * @retval B_BAD_VALUE       \a cookie, \a buffer, or \a _bufferSize is NULL.
 * @note Acquires sModulesLock internally for the duration of each call.
 */
status_t
read_next_module_name(void* cookie, char* buffer, size_t* _bufferSize)
{
	module_iterator* iterator = (module_iterator*)cookie;
	status_t status;

	TRACE(("read_next_module_name: looking for next module\n"));

	if (iterator == NULL || buffer == NULL || _bufferSize == NULL)
		return B_BAD_VALUE;

	if (iterator->status < B_OK)
		return iterator->status;

	status = iterator->status;
	recursive_lock_lock(&sModulesLock);

	status = iterator_get_next_module(iterator, buffer, _bufferSize);

	iterator->status = status;
	recursive_lock_unlock(&sModulesLock);

	TRACE(("read_next_module_name: finished with status %s\n",
		strerror(status)));
	return status;
}


/**
 * @brief Iterate over all modules currently known to the module subsystem.
 *
 * Walks sModulesHash using an integer cookie as an offset counter and copies
 * the name of the module at position *_cookie into \a buffer. On success
 * *_cookie is advanced to the next position.
 *
 * @param _cookie     In/out: opaque position cookie; initialize to 0 before
 *                    the first call and pass the returned value to each
 *                    subsequent call.
 * @param buffer      Caller-supplied buffer to receive the module name.
 * @param _bufferSize In/out: capacity of \a buffer; updated with the length
 *                    of the written name on success.
 * @retval B_OK              Module name written; *_cookie advanced.
 * @retval B_ENTRY_NOT_FOUND No module exists at the current offset.
 * @retval B_BAD_VALUE       Any pointer argument is NULL.
 * @retval B_ERROR           Called before the module subsystem is initialized.
 * @note Acquires sModulesLock internally.
 */
status_t
get_next_loaded_module_name(uint32* _cookie, char* buffer, size_t* _bufferSize)
{
	if (sModulesHash == NULL) {
		dprintf("get_next_loaded_module_name() called too early!\n");
		return B_ERROR;
	}

	//TRACE(("get_next_loaded_module_name(\"%s\")\n", buffer));

	if (_cookie == NULL || buffer == NULL || _bufferSize == NULL)
		return B_BAD_VALUE;

	status_t status = B_ENTRY_NOT_FOUND;
	uint32 offset = *_cookie;

	RecursiveLocker _(sModulesLock);

	ModuleTable::Iterator iterator(sModulesHash);

	for (uint32 i = 0; iterator.HasNext(); i++) {
		struct module* module = iterator.Next();
		if (i >= offset) {
			*_bufferSize = strlcpy(buffer, module->name, *_bufferSize);
			*_cookie = i + 1;
			status = B_OK;
			break;
		}
	}

	return status;
}


/**
 * @brief Acquire a reference to a kernel module and retrieve its module_info
 *        pointer.
 *
 * If the module is not yet cached, search_module() locates the add-on on
 * disk. If the module's reference count is zero, init_module() is invoked to
 * run B_MODULE_INIT. B_KEEP_LOADED modules receive an extra permanent
 * reference on first initialization. On success *_info points to the
 * module's module_info struct and the caller must eventually call put_module()
 * to release the reference.
 *
 * @param path   Fully qualified module name (e.g. "bus_managers/isa/v1").
 * @param _info  Out-parameter filled with a pointer to the module_info on
 *               success.
 * @retval B_OK              Module initialized and reference acquired.
 * @retval B_BAD_VALUE       \a path is NULL.
 * @retval B_ENTRY_NOT_FOUND Module not found in any search path.
 * @retval <0                Error returned by init_module() or
 *                           search_module().
 * @note Acquires sModulesLock for the duration of the call.
 */
status_t
get_module(const char* path, module_info** _info)
{
	module_image* moduleImage = NULL;
	module* module;
	status_t status;

	TRACE(("get_module(%s)\n", path));

	if (path == NULL)
		return B_BAD_VALUE;

	RecursiveLocker _(sModulesLock);

	module = sModulesHash->Lookup(path);

	// if we don't have it cached yet, search for it
	if (module == NULL || ((module->flags & B_BUILT_IN_MODULE) == 0
			&& module->module_image == NULL)) {
		module = search_module(path, &moduleImage);
		if (module == NULL) {
			FATAL(("module: Search for %s failed.\n", path));
			return B_ENTRY_NOT_FOUND;
		}

		module->info = moduleImage->info[module->offset];
		module->module_image = moduleImage;
	} else if ((module->flags & B_BUILT_IN_MODULE) == 0 && gBootDevice < 0
		&& module->ref_count == 0) {
		// The boot volume isn't available yet. I.e. instead of searching the
		// right module image, we already know it and just increment the ref
		// count.
		atomic_add(&module->module_image->ref_count, 1);
	}

	// The state will be adjusted by the call to init_module
	// if we have just loaded the file
	if (module->ref_count == 0) {
		status = init_module(module);
		// For "keep loaded" modules we increment the ref count here. That will
		// cause them never to get unloaded.
		if (status == B_OK && (module->flags & B_KEEP_LOADED) != 0)
			module->ref_count++;
	} else
		status = B_OK;

	if (status == B_OK) {
		ASSERT(module->ref_count >= 0);
		module->ref_count++;
		*_info = module->info;
	} else if ((module->flags & B_BUILT_IN_MODULE) == 0
		&& module->ref_count == 0) {
		// initialization failed -- release the image reference
		put_module_image(module->module_image);
		if (gBootDevice >= 0)
			module->module_image = NULL;
	}

	return status;
}


/**
 * @brief Release a reference to a kernel module previously acquired with
 *        get_module().
 *
 * Decrements the module's reference count. When the count reaches zero and
 * the module is not flagged B_KEEP_LOADED, uninit_module() is called to run
 * B_MODULE_UNINIT and the associated module_image reference is released. If
 * the boot device is mounted the module_image pointer is also cleared so that
 * a subsequent get_module() will reload the image from disk.
 *
 * @param path  Fully qualified module name passed to the corresponding
 *              get_module() call.
 * @retval B_OK        Reference released (and module uninitialized if needed).
 * @retval B_BAD_VALUE \a path not found in sModulesHash, or module already has
 *                     zero references.
 * @note Acquires sModulesLock for the duration of the call. Panics if a
 *       B_KEEP_LOADED module's reference count drops to zero.
 */
status_t
put_module(const char* path)
{
	module* module;

	TRACE(("put_module(path = %s)\n", path));

	RecursiveLocker _(sModulesLock);

	module = sModulesHash->Lookup(path);
	if (module == NULL) {
		FATAL(("module: We don't seem to have a reference to module %s\n",
			path));
		return B_BAD_VALUE;
	}

	if (module->ref_count == 0) {
		panic("module %s has no references.\n", path);
		return B_BAD_VALUE;
	}

	if (--module->ref_count == 0) {
		if ((module->flags & B_KEEP_LOADED) != 0) {
			panic("ref count of B_KEEP_LOADED module %s dropped to 0!",
				module->name);
			module->ref_count++;
			return B_BAD_VALUE;
		}

		uninit_module(module);

		if ((module->flags & B_BUILT_IN_MODULE) == 0
			&& module->ref_count == 0) {
				// uninit_module() increments the ref count on failure
			put_module_image(module->module_image);
			// Unless we don't have a boot device yet, we clear the module's
			// image pointer if the ref count dropped to 0. get_module() will
			// have to reload the image.
			if (gBootDevice >= 0)
				module->module_image = NULL;
		}
	}

	return B_OK;
}
