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
 *   Copyright 2007-2013, Haiku, Inc. All Rights Reserved.
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Stephan Aßmus, superstippi@gmx.de
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file PathMonitor.cpp
 * @brief Implementation of the BPathMonitor filesystem path monitoring service.
 *
 * This file implements BPathMonitor, which allows applications to watch an
 * arbitrary filesystem path (file or directory) for changes, including creation,
 * removal, and movement of entries.  It handles paths that do not yet exist by
 * watching ancestor directories in the hierarchy and transitioning to full
 * recursive monitoring once the target path appears.
 *
 * The implementation maintains a tree of Ancestor objects for each path
 * component, a graph of Node/Directory objects for the monitored subtree (in
 * recursive mode), and a per-target Watcher that aggregates multiple
 * PathHandler instances.  All monitoring is driven by a dedicated BLooper
 * ("PathMonitor looper") that receives raw B_NODE_MONITOR messages and
 * translates them into B_PATH_MONITOR messages delivered to the caller's
 * BMessenger target.
 *
 * @see BPathMonitor
 */


#include <PathMonitor.h>

#include <pthread.h>
#include <stdio.h>

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <Handler.h>
#include <Locker.h>
#include <Looper.h>
#include <Path.h>
#include <String.h>

#include <AutoDeleter.h>
#include <NotOwningEntryRef.h>
#include <ObjectList.h>
#include <util/OpenHashTable.h>
#include <util/SinglyLinkedList.h>


#undef TRACE
//#define TRACE_PATH_MONITOR
#ifdef TRACE_PATH_MONITOR
#	define TRACE(...) debug_printf("BPathMonitor: " __VA_ARGS__)
#else
#	define TRACE(...) ;
#endif


// TODO: Support symlink components in the path.
// TODO: Support mounting/unmounting of volumes in path components and within
// the watched path tree.


#define WATCH_NODE_FLAG_MASK	0x00ff


namespace {


class Directory;
class Node;
struct WatcherHashDefinition;
typedef BOpenHashTable<WatcherHashDefinition> WatcherMap;


static pthread_once_t sInitOnce = PTHREAD_ONCE_INIT;
static WatcherMap* sWatchers = NULL;
static BLooper* sLooper = NULL;
static BPathMonitor::BWatchingInterface* sDefaultWatchingInterface = NULL;
static BPathMonitor::BWatchingInterface* sWatchingInterface = NULL;


//	#pragma mark -


/**
 * @brief Concatenate a parent path and a sub-path component with a '/' separator.
 *
 * Returns an empty BString when either \a parent or \a subPath is empty, or
 * when a memory allocation fails during string construction.
 *
 * @param parent   The leading portion of the path (must not be empty).
 * @param subPath  The trailing portion to append (must not be empty).
 * @return The combined path, or an empty BString on failure.
 */
static BString
make_path(const BString& parent, const char* subPath)
{
	BString path = parent;
	int32 length = path.Length();
	if (length == 0 || subPath[0] == '\0')
		return BString();

	if (parent.ByteAt(length - 1) != '/') {
		path << '/';
		if (path.Length() < ++length)
			return BString();
	}

	path << subPath;
	if (path.Length() <= length)
		return BString();
	return path;
}


//	#pragma mark - Ancestor


/**
 * @brief Represents one component of the monitored path hierarchy.
 *
 * Each Ancestor corresponds to a single path segment (e.g. one directory
 * level) between the filesystem root and the target path.  Ancestors are
 * linked in a parent/child chain and are watched so that the PathHandler can
 * react when an intermediate directory is created, renamed, or removed.
 */
class Ancestor {
public:
	/**
	 * @brief Construct an Ancestor for the path component at \a pathComponentOffset.
	 *
	 * @param parent              Pointer to the parent Ancestor, or NULL for root.
	 * @param path                The absolute path up to and including this component.
	 * @param pathComponentOffset Byte offset within \a path where this component's
	 *                            name begins.
	 */
	Ancestor(Ancestor* parent, const BString& path, size_t pathComponentOffset)
		:
		fParent(parent),
		fChild(NULL),
		fPath(path),
		fEntryRef(-1, -1, fPath.String() + pathComponentOffset),
		fNodeRef(),
		fWatchingFlags(0),
		fIsDirectory(false)
	{
		if (pathComponentOffset == 0) {
			// must be "/"
			fEntryRef.SetTo(-1, -1, ".");
		}

		if (fParent != NULL)
			fParent->fChild = this;
	}

	/**
	 * @brief Return the parent Ancestor in the chain, or NULL for the root.
	 * @return Pointer to the parent Ancestor.
	 */
	Ancestor* Parent() const
	{
		return fParent;
	}

	/**
	 * @brief Return the child Ancestor in the chain, or NULL for the leaf.
	 * @return Pointer to the child Ancestor.
	 */
	Ancestor* Child() const
	{
		return fChild;
	}

	/**
	 * @brief Return the absolute path represented by this Ancestor.
	 * @return Reference to the stored path string.
	 */
	const BString& Path() const
	{
		return fPath;
	}

	/**
	 * @brief Return the name (last path component) of this Ancestor.
	 * @return C-string pointer to the entry name within the stored entry ref.
	 */
	const char* Name() const
	{
		return fEntryRef.name;
	}

	/**
	 * @brief Return whether this Ancestor's filesystem node is currently known.
	 * @return true if the ancestor exists on-disk (device >= 0), false otherwise.
	 */
	bool Exists() const
	{
		return fNodeRef.device >= 0;
	}

	/**
	 * @brief Return the entry ref for this Ancestor.
	 * @return Reference to the stored NotOwningEntryRef.
	 */
	const NotOwningEntryRef& EntryRef() const
	{
		return fEntryRef;
	}

	/**
	 * @brief Return the node ref for this Ancestor.
	 * @return Reference to the stored node_ref.
	 */
	const node_ref& NodeRef() const
	{
		return fNodeRef;
	}

	/**
	 * @brief Return whether this Ancestor represents a directory node.
	 * @return true if the node is a directory.
	 */
	bool IsDirectory() const
	{
		return fIsDirectory;
	}

	/**
	 * @brief Begin watching this Ancestor's filesystem node.
	 *
	 * Resolves the entry and node ref from the stored path, then registers
	 * a node monitor with the watching interface using \a pathFlags (for the
	 * leaf) or B_WATCH_DIRECTORY (for intermediate ancestors).
	 *
	 * @param pathFlags  Watch flags to apply when this is the leaf ancestor.
	 * @param target     The BHandler that will receive node monitor messages.
	 * @return B_OK on success, or an error code if the entry cannot be
	 *         resolved or monitoring cannot be started.
	 */
	status_t StartWatching(uint32 pathFlags, BHandler* target)
	{
		// init entry ref
		BEntry entry;
		status_t error = entry.SetTo(fPath);
		if (error != B_OK)
			return error;

		entry_ref entryRef;
		error = entry.GetRef(&entryRef);
		if (error != B_OK)
			return error;

		fEntryRef.device = entryRef.device;
		fEntryRef.directory = entryRef.directory;

		// init node ref
		struct stat st;
		error = entry.GetStat(&st);
		if (error != B_OK)
			return error == B_ENTRY_NOT_FOUND ? B_OK : error;

		fNodeRef = node_ref(st.st_dev, st.st_ino);
		fIsDirectory = S_ISDIR(st.st_mode);

		// start watching
		uint32 flags = fChild == NULL ? pathFlags : B_WATCH_DIRECTORY;
			// In theory B_WATCH_NAME would suffice for all existing ancestors,
			// plus B_WATCH_DIRECTORY for the parent of the first not existing
			// ancestor. In practice this complicates the transitions when an
			// ancestor is created/removed/moved.
		if (flags != 0) {
			error = sWatchingInterface->WatchNode(&fNodeRef, flags, target);
			TRACE("  started to watch ancestor %p (\"%s\", %#" B_PRIx32
				") -> %s\n", this, Name(), flags, strerror(error));
			if (error != B_OK)
				return error;
		}

		fWatchingFlags = flags;
		return B_OK;
	}

	/**
	 * @brief Stop watching this Ancestor's filesystem node and reset its refs.
	 *
	 * Cancels any active node monitor, clears the directory flag, and
	 * resets both the node ref and the directory portion of the entry ref.
	 *
	 * @param target  The BHandler originally passed to StartWatching().
	 */
	void StopWatching(BHandler* target)
	{
		// stop watching
		if (fWatchingFlags != 0) {
			sWatchingInterface->WatchNode(&fNodeRef, B_STOP_WATCHING, target);
			fWatchingFlags = 0;
		}

		// uninitialize node and entry ref
		fIsDirectory = false;
		fNodeRef = node_ref();
		fEntryRef.SetDirectoryNodeRef(node_ref());
	}

	/**
	 * @brief Intrusive hash-table link accessor used by AncestorMap.
	 * @return Reference to the internal hash-next pointer.
	 */
	Ancestor*& HashNext()
	{
		return fHashNext;
	}

private:
	Ancestor*			fParent;
	Ancestor*			fChild;
	Ancestor*			fHashNext;
	BString				fPath;
	NotOwningEntryRef	fEntryRef;
	node_ref			fNodeRef;
	uint32				fWatchingFlags;
	bool				fIsDirectory;
};


//	#pragma mark - AncestorMap


/**
 * @brief Hash table definition for mapping node_ref keys to Ancestor values.
 */
struct AncestorHashDefinition {
	typedef	node_ref	KeyType;
	typedef	Ancestor	ValueType;

	/**
	 * @brief Hash a node_ref key by XOR-ing device and inode numbers.
	 * @param key  The node_ref to hash.
	 * @return     Hash value.
	 */
	size_t HashKey(const node_ref& key) const
	{
		return size_t(key.device ^ key.node);
	}

	/**
	 * @brief Hash an Ancestor value via its stored NodeRef.
	 * @param value  Pointer to the Ancestor.
	 * @return       Hash value.
	 */
	size_t Hash(Ancestor* value) const
	{
		return HashKey(value->NodeRef());
	}

	/**
	 * @brief Compare a node_ref key against an Ancestor value.
	 * @param key    The lookup key.
	 * @param value  The candidate Ancestor.
	 * @return       true if the key equals the Ancestor's NodeRef.
	 */
	bool Compare(const node_ref& key, Ancestor* value) const
	{
		return key == value->NodeRef();
	}

	/**
	 * @brief Return the intrusive hash-next link for an Ancestor.
	 * @param value  Pointer to the Ancestor.
	 * @return       Reference to the hash-next pointer.
	 */
	Ancestor*& GetLink(Ancestor* value) const
	{
		return value->HashNext();
	}
};


typedef BOpenHashTable<AncestorHashDefinition> AncestorMap;


//	#pragma mark - Entry


/**
 * @brief Represents a named directory entry within the monitored tree.
 *
 * An Entry links a parent Directory to a child Node using the entry's name.
 * Multiple Entry objects may refer to the same Node when hard links exist.
 */
class Entry : public SinglyLinkedListLinkImpl<Entry> {
public:
	/**
	 * @brief Construct an Entry with the given parent, name, and node.
	 *
	 * @param parent  The Directory that contains this entry.
	 * @param name    The filesystem name of this entry.
	 * @param node    The Node this entry points to (may be NULL initially).
	 */
	Entry(Directory* parent, const BString& name, ::Node* node)
		:
		fParent(parent),
		fName(name),
		fNode(node)
	{
	}

	/**
	 * @brief Return the parent Directory of this entry.
	 * @return Pointer to the parent Directory.
	 */
	Directory* Parent() const
	{
		return fParent;
	}

	/**
	 * @brief Return the name of this entry.
	 * @return Reference to the stored name string.
	 */
	const BString& Name() const
	{
		return fName;
	}

	/**
	 * @brief Return the Node this entry refers to.
	 * @return Pointer to the associated Node.
	 */
	::Node* Node() const
	{
		return fNode;
	}

	/**
	 * @brief Set or replace the Node associated with this entry.
	 * @param node  The new Node pointer.
	 */
	void SetNode(::Node* node)
	{
		fNode = node;
	}

	/**
	 * @brief Build and return a NotOwningEntryRef for this entry.
	 * @return A NotOwningEntryRef composed from the parent's NodeRef and this
	 *         entry's name.
	 */
	inline NotOwningEntryRef EntryRef() const;

	/**
	 * @brief Intrusive hash-table link accessor used by EntryMap.
	 * @return Reference to the internal hash-next pointer.
	 */
	Entry*& HashNext()
	{
		return fHashNext;
	}

private:
	Directory*	fParent;
	BString		fName;
	::Node*		fNode;
	Entry*		fHashNext;
};

typedef SinglyLinkedList<Entry> EntryList;


// EntryMap


/**
 * @brief Hash table definition for mapping entry name (C-string) keys to Entry values.
 */
struct EntryHashDefinition {
	typedef	const char*	KeyType;
	typedef	Entry		ValueType;

	/**
	 * @brief Hash a C-string entry name using BString's hash function.
	 * @param key  The entry name.
	 * @return     Hash value.
	 */
	size_t HashKey(const char* key) const
	{
		return BString::HashValue(key);
	}

	/**
	 * @brief Hash an Entry value via its stored name.
	 * @param value  Pointer to the Entry.
	 * @return       Hash value.
	 */
	size_t Hash(Entry* value) const
	{
		return value->Name().HashValue();
	}

	/**
	 * @brief Compare a C-string key against an Entry's name.
	 * @param key    The lookup key.
	 * @param value  The candidate Entry.
	 * @return       true if the names are equal.
	 */
	bool Compare(const char* key, Entry* value) const
	{
		return value->Name() == key;
	}

	/**
	 * @brief Return the intrusive hash-next link for an Entry.
	 * @param value  Pointer to the Entry.
	 * @return       Reference to the hash-next pointer.
	 */
	Entry*& GetLink(Entry* value) const
	{
		return value->HashNext();
	}
};


typedef BOpenHashTable<EntryHashDefinition> EntryMap;


//	#pragma mark - Node


/**
 * @brief Represents a filesystem node (file or directory) within the monitored tree.
 *
 * Node is the base class for both plain files and directories.  It stores a
 * node_ref for identification and maintains the list of Entry objects that
 * refer to it (supporting hard links).
 */
class Node {
public:
	/**
	 * @brief Construct a Node identified by \a nodeRef.
	 * @param nodeRef  The filesystem node reference (device + inode).
	 */
	Node(const node_ref& nodeRef)
		:
		fNodeRef(nodeRef)
	{
	}

	/**
	 * @brief Virtual destructor.
	 */
	virtual ~Node()
	{
	}

	/**
	 * @brief Return whether this Node is a directory.
	 * @return false (overridden by Directory to return true).
	 */
	virtual bool IsDirectory() const
	{
		return false;
	}

	/**
	 * @brief Return this Node cast to a Directory, or NULL if it is not one.
	 * @return NULL (overridden by Directory).
	 */
	virtual Directory* ToDirectory()
	{
		return NULL;
	}

	/**
	 * @brief Return the node_ref identifying this Node.
	 * @return Reference to the stored node_ref.
	 */
	const node_ref& NodeRef() const
	{
		return fNodeRef;
	}

	/**
	 * @brief Return the list of Entry objects that refer to this Node.
	 * @return Reference to the intrusive entry list.
	 */
	const EntryList& Entries() const
	{
		return fEntries;
	}

	/**
	 * @brief Return whether any Entry objects currently refer to this Node.
	 * @return true if the entry list is non-empty.
	 */
	bool HasEntries() const
	{
		return !fEntries.IsEmpty();
	}

	/**
	 * @brief Return the first Entry in the node's entry list.
	 * @return Pointer to the first Entry, or NULL if the list is empty.
	 */
	Entry* FirstNodeEntry() const
	{
		return fEntries.Head();
	}

	/**
	 * @brief Return whether \a entry is the sole Entry referring to this Node.
	 * @param entry  The Entry to test.
	 * @return true if \a entry is both the head and the only element.
	 */
	bool IsOnlyNodeEntry(Entry* entry) const
	{
		return entry == fEntries.Head() && fEntries.GetNext(entry) == NULL;
	}

	/**
	 * @brief Add \a entry to the list of entries referring to this Node.
	 * @param entry  The Entry to add.
	 */
	void AddNodeEntry(Entry* entry)
	{
		fEntries.Add(entry);
	}

	/**
	 * @brief Remove \a entry from the list of entries referring to this Node.
	 * @param entry  The Entry to remove.
	 */
	void RemoveNodeEntry(Entry* entry)
	{
		fEntries.Remove(entry);
	}

	/**
	 * @brief Intrusive hash-table link accessor used by NodeMap.
	 * @return Reference to the internal hash-next pointer.
	 */
	Node*& HashNext()
	{
		return fHashNext;
	}

private:
	node_ref			fNodeRef;
	EntryList			fEntries;
	Node*				fHashNext;
};


/**
 * @brief Hash table definition for mapping node_ref keys to Node values.
 */
struct NodeHashDefinition {
	typedef	node_ref	KeyType;
	typedef	Node		ValueType;

	/**
	 * @brief Hash a node_ref key by XOR-ing device and inode.
	 * @param key  The node_ref to hash.
	 * @return     Hash value.
	 */
	size_t HashKey(const node_ref& key) const
	{
		return size_t(key.device ^ key.node);
	}

	/**
	 * @brief Hash a Node value via its stored NodeRef.
	 * @param value  Pointer to the Node.
	 * @return       Hash value.
	 */
	size_t Hash(Node* value) const
	{
		return HashKey(value->NodeRef());
	}

	/**
	 * @brief Compare a node_ref key against a Node value.
	 * @param key    The lookup key.
	 * @param value  The candidate Node.
	 * @return       true if the key equals the Node's NodeRef.
	 */
	bool Compare(const node_ref& key, Node* value) const
	{
		return key == value->NodeRef();
	}

	/**
	 * @brief Return the intrusive hash-next link for a Node.
	 * @param value  Pointer to the Node.
	 * @return       Reference to the hash-next pointer.
	 */
	Node*& GetLink(Node* value) const
	{
		return value->HashNext();
	}
};


typedef BOpenHashTable<NodeHashDefinition> NodeMap;


//	#pragma mark - Directory


/**
 * @brief Specialisation of Node that holds a hash map of child Entry objects.
 *
 * Directory extends Node with the ability to look up, create, add, remove, and
 * iterate over its direct children.  It is created via the static factory
 * method Create() to allow proper two-phase initialisation of the internal
 * hash table.
 */
class Directory : public Node {
public:
	/**
	 * @brief Factory method: allocate and initialise a Directory for \a nodeRef.
	 *
	 * @param nodeRef  The filesystem node reference for this directory.
	 * @return Pointer to the new Directory, or NULL on allocation/init failure.
	 */
	static Directory* Create(const node_ref& nodeRef)
	{
		Directory* directory = new(std::nothrow) Directory(nodeRef);
		if (directory == NULL || directory->fEntries.Init() != B_OK) {
			delete directory;
			return NULL;
		}

		return directory;
	}

	/**
	 * @brief Return true, indicating this Node is a Directory.
	 * @return true.
	 */
	virtual bool IsDirectory() const
	{
		return true;
	}

	/**
	 * @brief Return this object cast to a Directory pointer.
	 * @return this.
	 */
	virtual Directory* ToDirectory()
	{
		return this;
	}

	/**
	 * @brief Look up a child Entry by name.
	 *
	 * @param name  The entry name to search for.
	 * @return Pointer to the matching Entry, or NULL if not found.
	 */
	Entry* FindEntry(const char* name) const
	{
		return fEntries.Lookup(name);
	}

	/**
	 * @brief Allocate a new Entry with \a name and \a node and add it to this Directory.
	 *
	 * @param name  The name for the new entry.
	 * @param node  The Node the entry should point to (may be NULL).
	 * @return Pointer to the new Entry, or NULL on allocation failure.
	 */
	Entry* CreateEntry(const BString& name, Node* node)
	{
		Entry* entry = new(std::nothrow) Entry(this, name, node);
		if (entry == NULL || entry->Name().IsEmpty()) {
			delete entry;
			return NULL;
		}

		AddEntry(entry);
		return entry;
	}

	/**
	 * @brief Insert a pre-allocated Entry into this Directory's hash table.
	 * @param entry  The Entry to insert.
	 */
	void AddEntry(Entry* entry)
	{
		fEntries.Insert(entry);
	}

	/**
	 * @brief Remove an Entry from this Directory's hash table.
	 * @param entry  The Entry to remove.
	 */
	void RemoveEntry(Entry* entry)
	{
		fEntries.Remove(entry);
	}

	/**
	 * @brief Return an iterator over all Entries in this Directory.
	 * @return An EntryMap::Iterator positioned at the first entry.
	 */
	EntryMap::Iterator GetEntryIterator() const
	{
		return fEntries.GetIterator();
	}

	/**
	 * @brief Remove and return all Entries from this Directory in one operation.
	 *
	 * The returned pointer is the head of a singly-linked list chained via
	 * HashNext().  The caller is responsible for freeing each Entry.
	 *
	 * @return Pointer to the first removed Entry, or NULL if the directory was empty.
	 */
	Entry* RemoveAllEntries()
	{
		return fEntries.Clear(true);
	}

private:
	/**
	 * @brief Private constructor — use Create() instead.
	 * @param nodeRef  The filesystem node reference for this directory.
	 */
	Directory(const node_ref& nodeRef)
		:
		Node(nodeRef)
	{
	}

private:
	EntryMap	fEntries;
};


//	#pragma mark - Entry


/**
 * @brief Build and return a NotOwningEntryRef composed from the parent's NodeRef and this entry's name.
 *
 * @return A NotOwningEntryRef that does not own the name string.
 */
inline NotOwningEntryRef
Entry::EntryRef() const
{
	return NotOwningEntryRef(fParent->NodeRef(), fName);
}


//	#pragma mark - PathHandler


/**
 * @brief BHandler subclass that monitors a single path and dispatches B_PATH_MONITOR messages.
 *
 * PathHandler is added to the shared PathMonitor looper.  It owns an Ancestor
 * chain for every path component, a NodeMap/EntryMap graph for the subtree
 * (recursive mode), and translates raw B_NODE_MONITOR messages into the
 * higher-level B_PATH_MONITOR protocol understood by BPathMonitor clients.
 */
class PathHandler : public BHandler {
public:
								PathHandler(const char* path, uint32 flags,
									const BMessenger& target, BLooper* looper);
	virtual						~PathHandler();

			status_t			InitCheck() const;
			void				Quit();

			const BString&		OriginalPath() const
									{ return fOriginalPath; }
			uint32				Flags() const	{ return fFlags; }

	virtual	void				MessageReceived(BMessage* message);

			PathHandler*&		HashNext()	{ return fHashNext; }

private:
			status_t			_CreateAncestors();
			status_t			_StartWatchingAncestors(Ancestor* ancestor,
									bool notify);
			void				_StopWatchingAncestors(Ancestor* ancestor,
									bool notify);

			void				_EntryCreated(BMessage* message);
			void				_EntryRemoved(BMessage* message);
			void				_EntryMoved(BMessage* message);
			void				_NodeChanged(BMessage* message);

			bool				_EntryCreated(const NotOwningEntryRef& entryRef,
									const node_ref& nodeRef, bool isDirectory,
									bool dryRun, bool notify, Entry** _entry);
			bool				_EntryRemoved(const NotOwningEntryRef& entryRef,
									const node_ref& nodeRef, bool dryRun,
									bool notify, Entry** _keepEntry);

			bool				_CheckDuplicateEntryNotification(int32 opcode,
									const entry_ref& toEntryRef,
									const node_ref& nodeRef,
									const entry_ref* fromEntryRef = NULL);
			void				_UnsetDuplicateEntryNotification();

			Ancestor*			_GetAncestor(const node_ref& nodeRef) const;

			status_t			_AddNode(const node_ref& nodeRef,
									bool isDirectory, bool notify,
									Entry* entry = NULL, Node** _node = NULL);
			void				_DeleteNode(Node* node, bool notify);
			Node*				_GetNode(const node_ref& nodeRef) const;

			status_t			_AddEntryIfNeeded(Directory* directory,
									const char* name, const node_ref& nodeRef,
									bool isDirectory, bool notify,
									Entry** _entry = NULL);
			void				_DeleteEntry(Entry* entry, bool notify);
			void				_DeleteEntryAlreadyRemovedFromParent(
									Entry* entry, bool notify);

			void				_NotifyFilesCreatedOrRemoved(Entry* entry,
									int32 opcode) const;
			void				_NotifyEntryCreatedOrRemoved(Entry* entry,
									int32 opcode) const;
			void				_NotifyEntryCreatedOrRemoved(
									const entry_ref& entryRef,
									const node_ref& nodeRef, const char* path,
									bool isDirectory, int32 opcode) const;
			void				_NotifyEntryMoved(const entry_ref& fromEntryRef,
									const entry_ref& toEntryRef,
									const node_ref& nodeRef,
									const char* fromPath, const char* path,
									bool isDirectory, bool wasAdded,
									bool wasRemoved) const;
			void				_NotifyTarget(BMessage& message,
									const char* path) const;

			BString				_NodePath(const Node* node) const;
			BString				_EntryPath(const Entry* entry) const;


			bool				_WatchRecursively() const;
			bool				_WatchFilesOnly() const;
			bool				_WatchDirectoriesOnly() const;

private:
			BMessenger			fTarget;
			uint32				fFlags;
			status_t			fStatus;
			BString				fOriginalPath;
			BString				fPath;
			Ancestor*			fRoot;
			Ancestor*			fBaseAncestor;
			Node*				fBaseNode;
			AncestorMap			fAncestors;
			NodeMap				fNodes;
			PathHandler*		fHashNext;
			int32				fDuplicateEntryNotificationOpcode;
			node_ref			fDuplicateEntryNotificationNodeRef;
			entry_ref			fDuplicateEntryNotificationToEntryRef;
			entry_ref			fDuplicateEntryNotificationFromEntryRef;
};


/**
 * @brief Hash table definition for mapping path strings to PathHandler values.
 */
struct PathHandlerHashDefinition {
	typedef	const char*	KeyType;
	typedef	PathHandler	ValueType;

	/**
	 * @brief Hash a C-string path key.
	 * @param key  The path string.
	 * @return     Hash value.
	 */
	size_t HashKey(const char* key) const
	{
		return BString::HashValue(key);
	}

	/**
	 * @brief Hash a PathHandler value via its original path.
	 * @param value  Pointer to the PathHandler.
	 * @return       Hash value.
	 */
	size_t Hash(PathHandler* value) const
	{
		return value->OriginalPath().HashValue();
	}

	/**
	 * @brief Compare a path key against a PathHandler's original path.
	 * @param key    The lookup key.
	 * @param value  The candidate PathHandler.
	 * @return       true if the strings are equal.
	 */
	bool Compare(const char* key, PathHandler* value) const
	{
		return key == value->OriginalPath();
	}

	/**
	 * @brief Return the intrusive hash-next link for a PathHandler.
	 * @param value  Pointer to the PathHandler.
	 * @return       Reference to the hash-next pointer.
	 */
	PathHandler*& GetLink(PathHandler* value) const
	{
		return value->HashNext();
	}
};


typedef BOpenHashTable<PathHandlerHashDefinition> PathHandlerMap;


//	#pragma mark - Watcher


/**
 * @brief Aggregates all PathHandler instances associated with a single BMessenger target.
 *
 * Watcher extends PathHandlerMap (a hash map of path -> PathHandler) and
 * records the BMessenger it belongs to.  Created via the static factory
 * Watcher::Create().
 */
struct Watcher : public PathHandlerMap {
	/**
	 * @brief Factory method: allocate and initialise a Watcher for \a target.
	 *
	 * @param target  The BMessenger that will receive B_PATH_MONITOR messages.
	 * @return Pointer to the new Watcher, or NULL on failure.
	 */
	static Watcher* Create(const BMessenger& target)
	{
		Watcher* watcher = new(std::nothrow) Watcher(target);
		if (watcher == NULL || watcher->Init() != B_OK) {
			delete watcher;
			return NULL;
		}
		return watcher;
	}

	/**
	 * @brief Return the BMessenger target associated with this Watcher.
	 * @return Reference to the stored BMessenger.
	 */
	const BMessenger& Target() const
	{
		return fTarget;
	}

	/**
	 * @brief Intrusive hash-table link accessor used by WatcherMap.
	 * @return Reference to the internal hash-next pointer.
	 */
	Watcher*& HashNext()
	{
		return fHashNext;
	}

private:
	/**
	 * @brief Private constructor — use Create() instead.
	 * @param target  The BMessenger target.
	 */
	Watcher(const BMessenger& target)
		:
		fTarget(target)
	{
	}

private:
	BMessenger		fTarget;
	Watcher*		fHashNext;
};


/**
 * @brief Hash table definition for mapping BMessenger keys to Watcher values.
 */
struct WatcherHashDefinition {
	typedef	BMessenger	KeyType;
	typedef	Watcher		ValueType;

	/**
	 * @brief Hash a BMessenger key using its own HashValue() method.
	 * @param key  The BMessenger to hash.
	 * @return     Hash value.
	 */
	size_t HashKey(const BMessenger& key) const
	{
		return key.HashValue();
	}

	/**
	 * @brief Hash a Watcher value via its stored Target.
	 * @param value  Pointer to the Watcher.
	 * @return       Hash value.
	 */
	size_t Hash(Watcher* value) const
	{
		return HashKey(value->Target());
	}

	/**
	 * @brief Compare a BMessenger key against a Watcher's target.
	 * @param key    The lookup key.
	 * @param value  The candidate Watcher.
	 * @return       true if the messengers are equal.
	 */
	bool Compare(const BMessenger& key, Watcher* value) const
	{
		return key == value->Target();
	}

	/**
	 * @brief Return the intrusive hash-next link for a Watcher.
	 * @param value  Pointer to the Watcher.
	 * @return       Reference to the hash-next pointer.
	 */
	Watcher*& GetLink(Watcher* value) const
	{
		return value->HashNext();
	}
};


//	#pragma mark - PathHandler


/**
 * @brief Construct a PathHandler that monitors \a path with the given \a flags.
 *
 * Normalises the path (absolute, no duplicate slashes, no ".." components),
 * creates the Ancestor chain, adds this handler to \a looper, and begins
 * watching the ancestors and (if applicable) the full subtree.
 *
 * @param path    The filesystem path to monitor.
 * @param flags   Combination of B_WATCH_* flags controlling monitoring behaviour.
 * @param target  The BMessenger to which B_PATH_MONITOR notifications are sent.
 * @param looper  The BLooper to which this handler is added.
 */
PathHandler::PathHandler(const char* path, uint32 flags,
	const BMessenger& target, BLooper* looper)
	:
	BHandler(path),
	fTarget(target),
	fFlags(flags),
	fStatus(B_OK),
	fOriginalPath(path),
	fPath(),
	fRoot(NULL),
	fBaseAncestor(NULL),
	fBaseNode(NULL),
	fAncestors(),
	fNodes()
{
	TRACE("%p->PathHandler::PathHandler(\"%s\", %#" B_PRIx32 ")\n", this, path,
		flags);

	_UnsetDuplicateEntryNotification();

	fStatus = fAncestors.Init();
	if (fStatus != B_OK)
		return;

	fStatus = fNodes.Init();
	if (fStatus != B_OK)
		return;

	// normalize the flags
	if ((fFlags & B_WATCH_RECURSIVELY) != 0) {
		// We add B_WATCH_NAME and B_WATCH_DIRECTORY as needed, so clear them
		// here.
		fFlags &= ~uint32(B_WATCH_NAME | B_WATCH_DIRECTORY);
	} else {
		// The B_WATCH_*_ONLY flags are only valid for the recursive mode.
		// B_WATCH_NAME is implied (we watch the parent directory).
		fFlags &= ~uint32(B_WATCH_FILES_ONLY | B_WATCH_DIRECTORIES_ONLY
			| B_WATCH_NAME);
	}

	// Normalize the path a bit. We can't use BPath, as it may really normalize
	// the path, i.e. resolve symlinks and such, which may cause us to monitor
	// the wrong path. We want some normalization, though:
	// * relative -> absolute path
	// * fold duplicate '/'s
	// * omit "." components
	// * fail when encountering ".." components

	// make absolute
	BString normalizedPath;
	if (path[0] == '/') {
		normalizedPath = "/";
		path++;
	} else
		normalizedPath = BPath(".").Path();
	if (normalizedPath.IsEmpty()) {
		fStatus = B_NO_MEMORY;
		return;
	}

	// parse path components
	const char* pathEnd = path + strlen(path);
	for (;;) {
		// skip '/'s
		while (path[0] == '/')
			path++;
		if (path == pathEnd)
			break;

		const char* componentEnd = strchr(path, '/');
		if (componentEnd == NULL)
			componentEnd = pathEnd;
		size_t componentLength = componentEnd - path;

		// handle ".' and ".."
		if (path[0] == '.') {
			if (componentLength == 1) {
				path = componentEnd;
				continue;
			}
			if (componentLength == 2 && path[1] == '.') {
				fStatus = B_BAD_VALUE;
				return;
			}
		}

		int32 normalizedPathLength = normalizedPath.Length();
		if (normalizedPath.ByteAt(normalizedPathLength - 1) != '/') {
			normalizedPath << '/';
			normalizedPathLength++;
		}
		normalizedPath.Append(path, componentEnd - path);
		normalizedPathLength += int32(componentEnd - path);

		if (normalizedPath.Length() != normalizedPathLength) {
			fStatus = B_NO_MEMORY;
			return;
		}

		path = componentEnd;
	}

	fPath = normalizedPath;

	// Create the Ancestor objects -- they correspond to the path components and
	// are used for watching changes that affect the entries on the path.
	fStatus = _CreateAncestors();
	if (fStatus != B_OK)
		return;

	// add ourselves to the looper
	looper->AddHandler(this);

	// start watching
	fStatus = _StartWatchingAncestors(fRoot, false);
	if (fStatus != B_OK)
		return;
}


/**
 * @brief Destroy the PathHandler, stopping all active monitoring and freeing resources.
 *
 * Deletes the base node tree and the entire Ancestor chain.
 */
PathHandler::~PathHandler()
{
	TRACE("%p->PathHandler::~PathHandler(\"%s\", %#" B_PRIx32 ")\n", this,
		fPath.String(), fFlags);

	if (fBaseNode != NULL)
		_DeleteNode(fBaseNode, false);

	while (fRoot != NULL) {
		Ancestor* nextAncestor = fRoot->Child();
		delete fRoot;
		fRoot = nextAncestor;
	}
}


/**
 * @brief Return the initialisation status set during construction.
 *
 * @return B_OK if the handler was constructed successfully, otherwise an
 *         error code indicating what went wrong.
 */
status_t
PathHandler::InitCheck() const
{
	return fStatus;
}


/**
 * @brief Stop all watching activity, remove this handler from the looper, and delete it.
 *
 * After this call the PathHandler object no longer exists; the caller must not
 * dereference the pointer.
 */
void
PathHandler::Quit()
{
	TRACE("%p->PathHandler::Quit()\n", this);
	sWatchingInterface->StopWatching(this);
	sLooper->RemoveHandler(this);
	delete this;
}


/**
 * @brief Dispatch incoming BMessages, routing B_NODE_MONITOR opcodes to the appropriate handlers.
 *
 * Handles B_ENTRY_CREATED, B_ENTRY_REMOVED, and B_ENTRY_MOVED opcodes by
 * delegating to the private _Entry*() methods.  All other node-monitor
 * opcodes are forwarded to _NodeChanged().  Unrecognised messages are
 * passed to the base-class implementation.
 *
 * @param message  The incoming message to process.
 */
void
PathHandler::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
		{
			int32 opcode;
			if (message->FindInt32("opcode", &opcode) != B_OK)
				return;

			switch (opcode) {
				case B_ENTRY_CREATED:
					_EntryCreated(message);
					break;

				case B_ENTRY_REMOVED:
					_EntryRemoved(message);
					break;

				case B_ENTRY_MOVED:
					_EntryMoved(message);
					break;

				default:
					_UnsetDuplicateEntryNotification();
					_NodeChanged(message);
					break;
			}

			break;
		}

		default:
			BHandler::MessageReceived(message);
			break;
	}
}


/**
 * @brief Create Ancestor objects for every component of the normalised path.
 *
 * Walks fPath character by character, creating one Ancestor per path segment
 * and linking them in a parent -> child chain.  Sets fRoot (topmost) and
 * fBaseAncestor (leaf) on success.
 *
 * @return B_OK on success, B_NO_MEMORY if an allocation fails.
 */
status_t
PathHandler::_CreateAncestors()
{
	TRACE("%p->PathHandler::_CreateAncestors()\n", this);

	// create the Ancestor objects
	const char* path = fPath.String();
	const char* pathEnd = path + fPath.Length();
	const char* component = path;

	Ancestor* ancestor = NULL;

	while (component < pathEnd) {
		const char* componentEnd = component == path
			? component + 1 : strchr(component, '/');
		if (componentEnd == NULL)
			componentEnd = pathEnd;

		BString ancestorPath(path, componentEnd - path);
		if (ancestorPath.IsEmpty())
			return B_NO_MEMORY;

		ancestor = new(std::nothrow) Ancestor(ancestor, ancestorPath,
			component - path);
		TRACE("  created ancestor %p (\"%s\" / \"%s\")\n", ancestor,
			ancestor->Path().String(), ancestor->Name());
		if (ancestor == NULL)
			return B_NO_MEMORY;

		if (fRoot == NULL)
			fRoot = ancestor;

		component = componentEnd[0] == '/' ? componentEnd + 1 : componentEnd;
	}

	fBaseAncestor = ancestor;

	return B_OK;
}


/**
 * @brief Start watching all Ancestors from \a startAncestor downward, optionally notifying.
 *
 * For each existing ancestor, registers a node monitor and inserts the ancestor
 * into fAncestors.  If the base ancestor exists and watching is recursive, also
 * populates the node/entry graph via _AddNode().
 *
 * @param startAncestor  The first ancestor to start watching (typically fRoot).
 * @param notify         If true, send B_ENTRY_CREATED notification for the base
 *                       path when it exists.
 * @return B_OK on success, or a node-monitor error code.
 */
status_t
PathHandler::_StartWatchingAncestors(Ancestor* startAncestor, bool notify)
{
	TRACE("%p->PathHandler::_StartWatchingAncestors(%p, %d)\n", this,
		startAncestor, notify);

	// The watch flags for the path (if it exists). Recursively implies
	// directory, since we need to watch the entries.
	uint32 watchFlags = (fFlags & WATCH_NODE_FLAG_MASK)
		| (_WatchRecursively() ? B_WATCH_DIRECTORY : 0);

	for (Ancestor* ancestor = startAncestor; ancestor != NULL;
		ancestor = ancestor->Child()) {
		status_t error = ancestor->StartWatching(watchFlags, this);
		if (error != B_OK)
			return error;

		if (!ancestor->Exists()) {
			TRACE("  -> ancestor doesn't exist\n");
			break;
		}

		fAncestors.Insert(ancestor);
	}

	if (!fBaseAncestor->Exists())
		return B_OK;

	if (notify) {
		_NotifyEntryCreatedOrRemoved(fBaseAncestor->EntryRef(),
			fBaseAncestor->NodeRef(), fPath, fBaseAncestor->IsDirectory(),
			B_ENTRY_CREATED);
	}

	if (!_WatchRecursively())
		return B_OK;

	status_t error = _AddNode(fBaseAncestor->NodeRef(),
		fBaseAncestor->IsDirectory(), notify && _WatchFilesOnly(), NULL,
		&fBaseNode);
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Stop watching all Ancestors from \a ancestor downward, optionally notifying.
 *
 * Tears down the recursive node/entry graph (if any), optionally sends a
 * B_ENTRY_REMOVED notification for the base path, and calls StopWatching()
 * on each ancestor while removing it from fAncestors.
 *
 * @param ancestor  The first ancestor in the chain to stop watching.
 * @param notify    If true, send B_ENTRY_REMOVED notification for the base
 *                  path when it currently exists.
 */
void
PathHandler::_StopWatchingAncestors(Ancestor* ancestor, bool notify)
{
	// stop watching the tree below path
	if (fBaseNode != NULL) {
		_DeleteNode(fBaseNode, notify && _WatchFilesOnly());
		fBaseNode = NULL;
	}

	if (notify && fBaseAncestor->Exists()
		&& (fBaseAncestor->IsDirectory()
			? !_WatchFilesOnly() : !_WatchDirectoriesOnly())) {
		_NotifyEntryCreatedOrRemoved(fBaseAncestor->EntryRef(),
			fBaseAncestor->NodeRef(), fPath, fBaseAncestor->IsDirectory(),
			B_ENTRY_REMOVED);
	}

	// stop watching the ancestors and uninitialize their entries
	for (; ancestor != NULL; ancestor = ancestor->Child()) {
		if (ancestor->Exists())
			fAncestors.Remove(ancestor);
		ancestor->StopWatching(this);
	}
}


/**
 * @brief Handle a B_ENTRY_CREATED node-monitor message.
 *
 * Extracts entry and node references from the message, rejects duplicate
 * notifications, verifies the on-disk state, and delegates to the core
 * _EntryCreated() logic.
 *
 * @param message  The B_NODE_MONITOR / B_ENTRY_CREATED message.
 */
void
PathHandler::_EntryCreated(BMessage* message)
{
	// TODO: Unless we're watching files only, we might want to forward (some
	// of) the messages that don't agree with our model, since our client
	// maintains its model at a different time and the notification might be
	// necessary to keep it up-to-date. E.g. consider the following case:
	// 1. a directory is created
	// 2. a file is created in the directory
	// 3. the file is removed from the directory
	// If we get the notification after 1. and before 2., we pass it on to the
	// client, which may get it after 2. and before 3., thus seeing the file.
	// If we then get the entry-created notification after 3., we don't see the
	// file anymore and ignore the notification as well as the following
	// entry-removed notification. That is the client will never know that the
	// file has been removed. This can only happen in recursive mode. Otherwise
	// (and with B_WATCH_DIRECTORY) we just pass on all notifications.
	// A possible solution could be to just create a zombie entry and pass on
	// the entry-created notification. We wouldn't be able to adhere to the
	// B_WATCH_FILES_ONLY/B_WATCH_DIRECTORIES_ONLY flags, but that should be
	// acceptable. Either the client hasn't seen the entry either -- then it
	// doesn't matter -- or it likely has ignored a not matching entry anyway.

	NotOwningEntryRef entryRef;
	node_ref nodeRef;

	if (message->FindInt32("device", &nodeRef.device) != B_OK
		|| message->FindInt64("node", &nodeRef.node) != B_OK
		|| message->FindInt64("directory", &entryRef.directory) != B_OK
		|| message->FindString("name", (const char**)&entryRef.name) != B_OK) {
		return;
	}
	entryRef.device = nodeRef.device;

	if (_CheckDuplicateEntryNotification(B_ENTRY_CREATED, entryRef, nodeRef))
		return;

	TRACE("%p->PathHandler::_EntryCreated(): entry: %" B_PRIdDEV ":%" B_PRIdINO
		":\"%s\", node: %" B_PRIdDEV ":%" B_PRIdINO "\n", this, entryRef.device,
		entryRef.directory, entryRef.name, nodeRef.device, nodeRef.node);

	BEntry entry;
	struct stat st;
	if (entry.SetTo(&entryRef) != B_OK || entry.GetStat(&st) != B_OK
		|| nodeRef != node_ref(st.st_dev, st.st_ino)) {
		return;
	}

	_EntryCreated(entryRef, nodeRef, S_ISDIR(st.st_mode), false, true, NULL);
}


/**
 * @brief Handle a B_ENTRY_REMOVED node-monitor message.
 *
 * Extracts the entry and node references from the message, rejects
 * duplicates, and delegates to the core _EntryRemoved() logic.
 *
 * @param message  The B_NODE_MONITOR / B_ENTRY_REMOVED message.
 */
void
PathHandler::_EntryRemoved(BMessage* message)
{
	NotOwningEntryRef entryRef;
	node_ref nodeRef;

	if (message->FindInt32("device", &nodeRef.device) != B_OK
		|| message->FindInt64("node", &nodeRef.node) != B_OK
		|| message->FindInt64("directory", &entryRef.directory) != B_OK
		|| message->FindString("name", (const char**)&entryRef.name) != B_OK) {
		return;
	}
	entryRef.device = nodeRef.device;

	if (_CheckDuplicateEntryNotification(B_ENTRY_REMOVED, entryRef, nodeRef))
		return;

	TRACE("%p->PathHandler::_EntryRemoved(): entry: %" B_PRIdDEV ":%" B_PRIdINO
		":\"%s\", node: %" B_PRIdDEV ":%" B_PRIdINO "\n", this, entryRef.device,
		entryRef.directory, entryRef.name, nodeRef.device, nodeRef.node);

	_EntryRemoved(entryRef, nodeRef, false, true, NULL);
}


/**
 * @brief Handle a B_ENTRY_MOVED node-monitor message.
 *
 * Extracts source and destination entry refs and the node ref from the
 * message, rejects duplicates, verifies on-disk state, and applies the
 * appropriate combination of ancestor resync, recursive entry remove/create,
 * and move notification.
 *
 * @param message  The B_NODE_MONITOR / B_ENTRY_MOVED message.
 */
void
PathHandler::_EntryMoved(BMessage* message)
{
	NotOwningEntryRef fromEntryRef;
	NotOwningEntryRef toEntryRef;
	node_ref nodeRef;

	if (message->FindInt32("node device", &nodeRef.device) != B_OK
		|| message->FindInt64("node", &nodeRef.node) != B_OK
		|| message->FindInt32("device", &fromEntryRef.device) != B_OK
		|| message->FindInt64("from directory", &fromEntryRef.directory) != B_OK
		|| message->FindInt64("to directory", &toEntryRef.directory) != B_OK
		|| message->FindString("from name", (const char**)&fromEntryRef.name)
			!= B_OK
		|| message->FindString("name", (const char**)&toEntryRef.name)
			!= B_OK) {
		return;
	}
	toEntryRef.device = fromEntryRef.device;

	if (_CheckDuplicateEntryNotification(B_ENTRY_MOVED, toEntryRef, nodeRef,
			&fromEntryRef)) {
		return;
	}

	TRACE("%p->PathHandler::_EntryMoved(): entry: %" B_PRIdDEV ":%" B_PRIdINO
		":\"%s\" -> %" B_PRIdDEV ":%" B_PRIdINO ":\"%s\", node: %" B_PRIdDEV
		":%" B_PRIdINO "\n", this, fromEntryRef.device, fromEntryRef.directory,
		fromEntryRef.name, toEntryRef.device, toEntryRef.directory,
		toEntryRef.name, nodeRef.device, nodeRef.node);

	BEntry entry;
	struct stat st;
	if (entry.SetTo(&toEntryRef) != B_OK || entry.GetStat(&st) != B_OK
		|| nodeRef != node_ref(st.st_dev, st.st_ino)) {
		_EntryRemoved(fromEntryRef, nodeRef, false, true, NULL);
		return;
	}
	bool isDirectory = S_ISDIR(st.st_mode);

	Ancestor* fromAncestor = _GetAncestor(fromEntryRef.DirectoryNodeRef());
	Ancestor* toAncestor = _GetAncestor(toEntryRef.DirectoryNodeRef());

	if (_WatchRecursively()) {
		Node* fromDirectoryNode = _GetNode(fromEntryRef.DirectoryNodeRef());
		Node* toDirectoryNode = _GetNode(toEntryRef.DirectoryNodeRef());
		if (fromDirectoryNode != NULL || toDirectoryNode != NULL) {
			// Check whether _EntryRemoved()/_EntryCreated() can handle the
			// respective entry regularly (i.e. don't encounter an out-of-sync
			// issue) or don't need to be called at all (entry outside the
			// monitored tree).
			if ((fromDirectoryNode == NULL
					|| _EntryRemoved(fromEntryRef, nodeRef, true, false, NULL))
				&& (toDirectoryNode == NULL
					|| _EntryCreated(toEntryRef, nodeRef, isDirectory, true,
						false, NULL))) {
				// The entries can be handled regularly. We delegate the work to
				// _EntryRemoved() and _EntryCreated() and only handle the
				// notification ourselves.

				// handle removed
				Entry* removedEntry = NULL;
				if (fromDirectoryNode != NULL) {
					_EntryRemoved(fromEntryRef, nodeRef, false, false,
						&removedEntry);
				}

				// handle created
				Entry* createdEntry = NULL;
				if (toDirectoryNode != NULL) {
					_EntryCreated(toEntryRef, nodeRef, isDirectory, false,
						false, &createdEntry);
				}

				// notify
				if (_WatchFilesOnly() && isDirectory) {
					// recursively iterate through the removed and created
					// hierarchy and send notifications for the files
					if (removedEntry != NULL) {
						_NotifyFilesCreatedOrRemoved(removedEntry,
							B_ENTRY_REMOVED);
					}

					if (createdEntry != NULL) {
						_NotifyFilesCreatedOrRemoved(createdEntry,
							B_ENTRY_CREATED);
					}
				} else {
					BString fromPath;
					if (fromDirectoryNode != NULL) {
						fromPath = make_path(_NodePath(fromDirectoryNode),
							fromEntryRef.name);
					}

					BString path;
					if (toDirectoryNode != NULL) {
						path = make_path(_NodePath(toDirectoryNode),
							toEntryRef.name);
					}

					_NotifyEntryMoved(fromEntryRef, toEntryRef, nodeRef,
						fromPath, path, isDirectory, fromDirectoryNode == NULL,
						toDirectoryNode == NULL);
				}

				if (removedEntry != NULL)
					_DeleteEntry(removedEntry, false);
			} else {
				// The entries can't be handled regularly. We delegate all the
				// work to _EntryRemoved() and _EntryCreated(). This will
				// generate separate entry-removed and entry-created
				// notifications.

				// handle removed
				if (fromDirectoryNode != NULL)
					_EntryRemoved(fromEntryRef, nodeRef, false, true, NULL);

				// handle created
				if (toDirectoryNode != NULL) {
					_EntryCreated(toEntryRef, nodeRef, isDirectory, false, true,
						NULL);
				}
			}

			return;
		}

		if (fromAncestor == fBaseAncestor || toAncestor == fBaseAncestor) {
			// That should never happen, as we should have found a matching
			// directory node in this case.
#ifdef DEBUG
			debugger("path ancestor exists, but doesn't have a directory");
			// Could actually be an out-of-memory situation, if we simply failed
			// to create the directory earlier.
#endif
			_StopWatchingAncestors(fRoot, false);
			_StartWatchingAncestors(fRoot, false);
			return;
		}
	} else {
		// Non-recursive mode: This notification is only of interest to us, if
		// it is either a move into/within/out of the path and B_WATCH_DIRECTORY
		// is set, or an ancestor might be affected.
		if (fromAncestor == NULL && toAncestor == NULL)
			return;

		if (fromAncestor == fBaseAncestor || toAncestor == fBaseAncestor) {
			if ((fFlags & B_WATCH_DIRECTORY) != 0) {
				BString fromPath;
				if (fromAncestor == fBaseAncestor)
					fromPath = make_path(fPath, fromEntryRef.name);

				BString path;
				if (toAncestor == fBaseAncestor)
					path = make_path(fPath, toEntryRef.name);

				_NotifyEntryMoved(fromEntryRef, toEntryRef, nodeRef,
					fromPath, path, isDirectory, fromAncestor == NULL,
					toAncestor == NULL);
			}
			return;
		}
	}

	if (fromAncestor == NULL && toAncestor == NULL)
		return;

	if (fromAncestor == NULL) {
		_EntryCreated(toEntryRef, nodeRef, isDirectory, false, true, NULL);
		return;
	}

	if (toAncestor == NULL) {
		_EntryRemoved(fromEntryRef, nodeRef, false, true, NULL);
		return;
	}

	// An entry was moved in a true ancestor directory or between true ancestor
	// directories. Unless the moved entry was or becomes our base ancestor, we
	// let _EntryRemoved() and _EntryCreated() handle it.
	bool fromIsBase = fromAncestor == fBaseAncestor->Parent()
		&& strcmp(fromEntryRef.name, fBaseAncestor->Name()) == 0;
	bool toIsBase = toAncestor == fBaseAncestor->Parent()
		&& strcmp(toEntryRef.name, fBaseAncestor->Name()) == 0;
	if (fromIsBase || toIsBase) {
		// This might be a duplicate notification. Check whether our model
		// already reflects the change. Otherwise stop/start watching the base
		// ancestor as required.
		bool notifyFilesRecursively = _WatchFilesOnly() && isDirectory;
		if (fromIsBase) {
			if (!fBaseAncestor->Exists())
				return;
			_StopWatchingAncestors(fBaseAncestor, notifyFilesRecursively);
		} else {
			if (fBaseAncestor->Exists()) {
				if (fBaseAncestor->NodeRef() == nodeRef
					&& isDirectory == fBaseAncestor->IsDirectory()) {
					return;
				}

				// We're out of sync with reality.
				_StopWatchingAncestors(fBaseAncestor, true);
				_StartWatchingAncestors(fBaseAncestor, true);
				return;
			}

			_StartWatchingAncestors(fBaseAncestor, notifyFilesRecursively);
		}

		if (!notifyFilesRecursively) {
			_NotifyEntryMoved(fromEntryRef, toEntryRef, nodeRef,
				fromIsBase ? fPath.String() : NULL,
				toIsBase ? fPath.String() : NULL,
				isDirectory, toIsBase, fromIsBase);
		}
		return;
	}

	_EntryRemoved(fromEntryRef, nodeRef, false, true, NULL);
	_EntryCreated(toEntryRef, nodeRef, isDirectory, false, true, NULL);
}


/**
 * @brief Handle a stat-changed or attribute-changed node-monitor message.
 *
 * Looks up the notified node in the ancestor map and the node map.  If found,
 * and if the node type matches the active watch filters, forwards the original
 * message to the target messenger via _NotifyTarget().
 *
 * @param message  The B_NODE_MONITOR message with a stat or attribute change.
 */
void
PathHandler::_NodeChanged(BMessage* message)
{
	node_ref nodeRef;

	if (message->FindInt32("device", &nodeRef.device) != B_OK
		|| message->FindInt64("node", &nodeRef.node) != B_OK) {
		return;
	}

	TRACE("%p->PathHandler::_NodeChanged(): node: %" B_PRIdDEV ":%" B_PRIdINO
		", %s%s\n", this, nodeRef.device, nodeRef.node,
			message->GetInt32("opcode", B_STAT_CHANGED) == B_ATTR_CHANGED
				? "attribute: " : "stat",
			message->GetInt32("opcode", B_STAT_CHANGED) == B_ATTR_CHANGED
				? message->GetString("attr", "") : "");

	bool isDirectory = false;
	BString path;
	if (Ancestor* ancestor = _GetAncestor(nodeRef)) {
		if (ancestor != fBaseAncestor)
			return;
		isDirectory = ancestor->IsDirectory();
		path = fPath;
	} else if (Node* node = _GetNode(nodeRef)) {
		isDirectory = node->IsDirectory();
		path = _NodePath(node);
	} else
		return;

	if (isDirectory ? _WatchFilesOnly() : _WatchDirectoriesOnly())
		return;

	_NotifyTarget(*message, path);
}


/**
 * @brief Core logic for handling the creation of a filesystem entry.
 *
 * Determines whether the new entry affects an ancestor, the monitored path
 * itself, or the recursive subtree, and updates internal data structures
 * accordingly.  In dry-run mode only reports whether the operation would
 * succeed without modifying state.
 *
 * @param entryRef    The entry ref of the newly created entry.
 * @param nodeRef     The node ref of the newly created entry.
 * @param isDirectory true if the new entry is a directory.
 * @param dryRun      If true, check feasibility only without changing state.
 * @param notify      If true, send B_ENTRY_CREATED notification to the target.
 * @param _entry      If non-NULL, receives a pointer to the newly created Entry
 *                    object on success.
 * @return true if the operation was handled cleanly; false if the internal
 *         model was out of sync and a resync was (or would be) required.
 */
bool
PathHandler::_EntryCreated(const NotOwningEntryRef& entryRef,
	const node_ref& nodeRef, bool isDirectory, bool dryRun, bool notify,
	Entry** _entry)
{
	if (_entry != NULL)
		*_entry = NULL;

	Ancestor* ancestor = _GetAncestor(nodeRef);
	if (ancestor != NULL) {
		if (isDirectory == ancestor->IsDirectory()
			&& entryRef == ancestor->EntryRef()) {
			// just a duplicate notification
			TRACE("  -> we already know the ancestor\n");
			return true;
		}

		struct stat ancestorStat;
		if (BEntry(&ancestor->EntryRef()).GetStat(&ancestorStat) == B_OK
			&& node_ref(ancestorStat.st_dev, ancestorStat.st_ino)
				== ancestor->NodeRef()
			&& S_ISDIR(ancestorStat.st_mode) == ancestor->IsDirectory()) {
			// Our information for the ancestor is up-to-date, so ignore the
			// notification.
			TRACE("  -> we know a different ancestor, but our info is "
				"up-to-date\n");
			return true;
		}

		// We're out of sync with reality.
		TRACE("  -> ancestor mismatch -> resyncing\n");
		if (!dryRun) {
			_StopWatchingAncestors(ancestor, true);
			_StartWatchingAncestors(ancestor, true);
		}
		return false;
	}

	ancestor = _GetAncestor(entryRef.DirectoryNodeRef());
	if (ancestor != NULL) {
		if (ancestor != fBaseAncestor) {
			// The directory is a true ancestor -- the notification is only of
			// interest, if the entry matches the child ancestor.
			Ancestor* childAncestor = ancestor->Child();
			if (strcmp(entryRef.name, childAncestor->Name()) != 0) {
				TRACE("  -> not an ancestor entry we're interested in "
					"(\"%s\")\n", childAncestor->Name());
				return true;
			}

			if (!dryRun) {
				if (childAncestor->Exists()) {
					TRACE("  ancestor entry mismatch -> resyncing\n");
					// We're out of sync with reality -- the new entry refers to
					// a different node.
					_StopWatchingAncestors(childAncestor, true);
				}

				TRACE("  -> starting to watch newly appeared ancestor\n");
				_StartWatchingAncestors(childAncestor, true);
			}
			return false;
		}

		// The directory is our path. If watching recursively, just fall
		// through. Otherwise, we want to pass on the notification, if directory
		// watching is enabled.
		if (!_WatchRecursively()) {
			if ((fFlags & B_WATCH_DIRECTORY) != 0) {
				_NotifyEntryCreatedOrRemoved(entryRef, nodeRef,
					make_path(fPath, entryRef.name), isDirectory,
					B_ENTRY_CREATED);
			}
			return true;
		}
	}

	if (!_WatchRecursively()) {
		// That shouldn't happen, since we only watch the ancestors in this
		// case.
		return true;
	}

	Node* directoryNode = _GetNode(entryRef.DirectoryNodeRef());
	if (directoryNode == NULL)
		return true;

	Directory* directory = directoryNode->ToDirectory();
	if (directory == NULL) {
		// We're out of sync with reality.
		if (!dryRun) {
			if (Entry* nodeEntry = directoryNode->FirstNodeEntry()) {
				// remove the entry that is in the way and re-add the proper
				// entry
				NotOwningEntryRef directoryEntryRef = nodeEntry->EntryRef();
				BString directoryName = nodeEntry->Name();
				_DeleteEntry(nodeEntry, true);
				_EntryCreated(directoryEntryRef, entryRef.DirectoryNodeRef(),
					true, false, notify, NULL);
			} else {
				// It's either the base node or something's severely fishy.
				// Resync the whole path.
				_StopWatchingAncestors(fBaseAncestor, true);
				_StartWatchingAncestors(fBaseAncestor, true);
			}
		}

		return false;
	}

	// Check, if there's a colliding entry.
	if (Entry* nodeEntry = directory->FindEntry(entryRef.name)) {
		Node* entryNode = nodeEntry->Node();
		if (entryNode != NULL && entryNode->NodeRef() == nodeRef)
			return true;

		// We're out of sync with reality -- the new entry refers to a different
		// node.
		_DeleteEntry(nodeEntry, true);
	}

	if (dryRun)
		return true;

	_AddEntryIfNeeded(directory, entryRef.name, nodeRef, isDirectory, notify,
		_entry);
	return true;
}


/**
 * @brief Core logic for handling the removal of a filesystem entry.
 *
 * Determines whether the removed entry affects an ancestor, the monitored
 * path, or the recursive subtree, and updates internal state accordingly.
 * In dry-run mode only checks feasibility without modifying state.
 *
 * @param entryRef    The entry ref of the removed entry.
 * @param nodeRef     The node ref of the removed entry.
 * @param dryRun      If true, check feasibility only without changing state.
 * @param notify      If true, send B_ENTRY_REMOVED notification to the target.
 * @param _keepEntry  If non-NULL, receives the Entry pointer rather than
 *                    deleting it, allowing the caller to manage its lifetime.
 * @return true if the operation was handled cleanly; false if a resync was
 *         (or would be) required.
 */
bool
PathHandler::_EntryRemoved(const NotOwningEntryRef& entryRef,
	const node_ref& nodeRef, bool dryRun, bool notify, Entry** _keepEntry)
{
	if (_keepEntry != NULL)
		*_keepEntry = NULL;

	Ancestor* ancestor = _GetAncestor(nodeRef);
	if (ancestor != NULL) {
		// The node is an ancestor. If this is a true match, stop watching the
		// ancestor.
		if (!ancestor->Exists())
			return true;

		if (entryRef != ancestor->EntryRef()) {
			// We might be out of sync with reality -- the new entry refers to a
			// different node.
			struct stat ancestorStat;
			if (BEntry(&ancestor->EntryRef()).GetStat(&ancestorStat) != B_OK) {
				if (!dryRun)
					_StopWatchingAncestors(ancestor, true);
				return false;
			}

			if (node_ref(ancestorStat.st_dev, ancestorStat.st_ino)
					!= ancestor->NodeRef()
				|| S_ISDIR(ancestorStat.st_mode) != ancestor->IsDirectory()) {
				if (!dryRun) {
					_StopWatchingAncestors(ancestor, true);
					_StartWatchingAncestors(ancestor, true);
				}
				return false;
			}
			return true;
		}

		if (!dryRun)
			_StopWatchingAncestors(ancestor, true);
		return false;
	}

	ancestor = _GetAncestor(entryRef.DirectoryNodeRef());
	if (ancestor != NULL) {
		if (ancestor != fBaseAncestor) {
			// The directory is a true ancestor -- the notification cannot be
			// of interest, since the node didn't match a known ancestor.
			return true;
		}

		// The directory is our path. If watching recursively, just fall
		// through. Otherwise, we want to pass on the notification, if directory
		// watching is enabled.
		if (!_WatchRecursively()) {
			if (notify && (fFlags & B_WATCH_DIRECTORY) != 0) {
				_NotifyEntryCreatedOrRemoved(entryRef, nodeRef,
					make_path(fPath, entryRef.name), false, B_ENTRY_REMOVED);
					// We don't know whether this was a directory, but it
					// doesn't matter in this case.
			}
			return true;
		}
	}

	if (!_WatchRecursively()) {
		// That shouldn't happen, since we only watch the ancestors in this
		// case.
		return true;
	}

	Node* directoryNode = _GetNode(entryRef.DirectoryNodeRef());
	if (directoryNode == NULL) {
		// We shouldn't get a notification, if we don't known the directory.
		return true;
	}

	Directory* directory = directoryNode->ToDirectory();
	if (directory == NULL) {
		// We might be out of sync with reality or the notification is just
		// late. The former case is extremely unlikely (we are watching the node
		// and its parent directory after all) and rather hard to verify.
		return true;
	}

	Entry* nodeEntry = directory->FindEntry(entryRef.name);
	if (nodeEntry == NULL) {
		// might be a non-directory node while we're in directories-only mode
		return true;
	}

	if (!dryRun) {
		if (_keepEntry != NULL)
			*_keepEntry = nodeEntry;
		else
			_DeleteEntry(nodeEntry, notify);
	}
	return true;
}


/**
 * @brief Detect and suppress duplicate entry notifications.
 *
 * Compares the incoming notification against the last recorded notification.
 * If they match (same opcode, node ref, destination entry ref, and optional
 * source entry ref), returns true to indicate a duplicate.  Otherwise,
 * records the new notification and returns false.
 *
 * @param opcode        The notification opcode (B_ENTRY_CREATED etc.).
 * @param toEntryRef    The destination entry ref of the notification.
 * @param nodeRef       The node ref of the affected node.
 * @param fromEntryRef  Optional source entry ref (for B_ENTRY_MOVED).
 * @return true if this notification is a duplicate of the previous one.
 */
bool
PathHandler::_CheckDuplicateEntryNotification(int32 opcode,
	const entry_ref& toEntryRef, const node_ref& nodeRef,
	const entry_ref* fromEntryRef)
{
	if (opcode == fDuplicateEntryNotificationOpcode
		&& nodeRef == fDuplicateEntryNotificationNodeRef
		&& toEntryRef == fDuplicateEntryNotificationToEntryRef
		&& (fromEntryRef == NULL
			|| *fromEntryRef == fDuplicateEntryNotificationFromEntryRef)) {
		return true;
	}

	fDuplicateEntryNotificationOpcode = opcode;
	fDuplicateEntryNotificationNodeRef = nodeRef;
	fDuplicateEntryNotificationToEntryRef = toEntryRef;
	fDuplicateEntryNotificationFromEntryRef = fromEntryRef != NULL
		? *fromEntryRef : entry_ref();
	return false;
}


/**
 * @brief Reset the duplicate-notification state to a sentinel "no pending notification".
 *
 * Sets the opcode to B_STAT_CHANGED and clears all cached refs so that the
 * next real notification will never be mistaken for a duplicate.
 */
void
PathHandler::_UnsetDuplicateEntryNotification()
{
	fDuplicateEntryNotificationOpcode = B_STAT_CHANGED;
	fDuplicateEntryNotificationNodeRef = node_ref();
	fDuplicateEntryNotificationFromEntryRef = entry_ref();
	fDuplicateEntryNotificationToEntryRef = entry_ref();
}


/**
 * @brief Look up an Ancestor by its node ref.
 *
 * @param nodeRef  The node ref to search for.
 * @return Pointer to the matching Ancestor, or NULL if not found.
 */
Ancestor*
PathHandler::_GetAncestor(const node_ref& nodeRef) const
{
	return fAncestors.Lookup(nodeRef);
}


/**
 * @brief Add a filesystem node (and optionally its full subtree) to the monitored tree.
 *
 * If the node is already known (e.g. via a hard link), simply links \a entry
 * to the existing Node.  Otherwise creates a new Node or Directory, starts
 * watching it, inserts it into fNodes, and (for directories) recursively adds
 * all children.
 *
 * @param nodeRef      The node ref of the node to add.
 * @param isDirectory  true if the node is a directory.
 * @param notify       If true, send B_ENTRY_CREATED notifications for children.
 * @param entry        If non-NULL, the Entry to associate with the new node.
 * @param _node        If non-NULL, receives a pointer to the Node on success.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or a
 *         watch_node() error code.
 */
status_t
PathHandler::_AddNode(const node_ref& nodeRef, bool isDirectory, bool notify,
	Entry* entry, Node** _node)
{
	TRACE("%p->PathHandler::_AddNode(%" B_PRIdDEV ":%" B_PRIdINO
		", isDirectory: %d, notify: %d)\n", this, nodeRef.device, nodeRef.node,
		isDirectory, notify);

	// If hard links are supported, we may already know the node.
	Node* node = _GetNode(nodeRef);
	if (node != NULL) {
		if (entry != NULL) {
			entry->SetNode(node);
			node->AddNodeEntry(entry);
		}

		if (_node != NULL)
			*_node = node;
		return B_OK;
	}

	// create the node
	Directory* directoryNode = NULL;
	if (isDirectory)
		node = directoryNode = Directory::Create(nodeRef);
	else
		node = new(std::nothrow) Node(nodeRef);

	if (node == NULL)
		return B_NO_MEMORY;

	ObjectDeleter<Node> nodeDeleter(node);

	// start watching (don't do that for the base node, since we watch it
	// already via fBaseAncestor)
	if (nodeRef != fBaseAncestor->NodeRef()) {
		uint32 flags = (fFlags & WATCH_NODE_FLAG_MASK) | B_WATCH_DIRECTORY;
		status_t error = sWatchingInterface->WatchNode(&nodeRef, flags, this);
		if (error != B_OK)
			return error;
	}

	fNodes.Insert(nodeDeleter.Detach());

	if (entry != NULL) {
		entry->SetNode(node);
		node->AddNodeEntry(entry);
	}

	if (_node != NULL)
		*_node = node;

	if (!isDirectory)
		return B_OK;

	// recursively add the directory's descendents
	BDirectory directory;
	if (directory.SetTo(&nodeRef) != B_OK) {
		if (_node != NULL)
			*_node = node;
		return B_OK;
	}

	entry_ref entryRef;
	while (directory.GetNextRef(&entryRef) == B_OK) {
		struct stat st;
		if (BEntry(&entryRef).GetStat(&st) != B_OK)
			continue;

		bool isDirectory = S_ISDIR(st.st_mode);
		status_t error = _AddEntryIfNeeded(directoryNode, entryRef.name,
			node_ref(st.st_dev, st.st_ino), isDirectory, notify);
		if (error != B_OK) {
			TRACE("%p->PathHandler::_AddNode(%" B_PRIdDEV ":%" B_PRIdINO
				", isDirectory: %d, notify: %d): failed to add directory "
				"entry: \"%s\"\n", this, nodeRef.device, nodeRef.node,
				isDirectory, notify, entryRef.name);
			continue;
		}
	}

	return B_OK;
}


/**
 * @brief Remove and delete a node and (recursively) all of its children.
 *
 * For directories, removes all child entries first via
 * _DeleteEntryAlreadyRemovedFromParent().  Stops the node monitor, removes
 * the Node from fNodes, and frees it.
 *
 * @param node    The Node to delete.
 * @param notify  If true, send B_ENTRY_REMOVED notifications for the subtree.
 */
void
PathHandler::_DeleteNode(Node* node, bool notify)
{
	if (Directory* directory = node->ToDirectory()) {
		Entry* entry = directory->RemoveAllEntries();
		while (entry != NULL) {
			Entry* nextEntry = entry->HashNext();
			_DeleteEntryAlreadyRemovedFromParent(entry, notify);
			entry = nextEntry;
		}
	}

	if (node->NodeRef() != fBaseAncestor->NodeRef())
		sWatchingInterface->WatchNode(&node->NodeRef(), B_STOP_WATCHING, this);

	fNodes.Remove(node);
	delete node;
}


/**
 * @brief Look up a Node by its node ref.
 *
 * @param nodeRef  The node ref to search for.
 * @return Pointer to the matching Node, or NULL if not found.
 */
Node*
PathHandler::_GetNode(const node_ref& nodeRef) const
{
	return fNodes.Lookup(nodeRef);
}


/**
 * @brief Add an entry to \a directory if it passes the active watch filters.
 *
 * Creates an Entry object, calls _AddNode() to create (or reuse) the
 * corresponding Node, and optionally notifies the target.  Entries for plain
 * files are skipped when B_WATCH_DIRECTORIES_ONLY is active.
 *
 * @param directory    The Directory to add the entry to.
 * @param name         The name of the entry.
 * @param nodeRef      The node ref of the entry.
 * @param isDirectory  true if the entry is a directory.
 * @param notify       If true, send B_ENTRY_CREATED notification.
 * @param _entry       If non-NULL, receives the newly created Entry on success.
 * @return B_OK on success, B_NO_MEMORY if allocation fails, or an error from
 *         _AddNode().
 */
status_t
PathHandler::_AddEntryIfNeeded(Directory* directory, const char* name,
	const node_ref& nodeRef, bool isDirectory, bool notify,
	Entry** _entry)
{
	TRACE("%p->PathHandler::_AddEntryIfNeeded(%" B_PRIdDEV ":%" B_PRIdINO
		":\"%s\", %" B_PRIdDEV ":%" B_PRIdINO
		", isDirectory: %d, notify: %d)\n", this, directory->NodeRef().device,
		directory->NodeRef().node, name, nodeRef.device, nodeRef.node,
		isDirectory, notify);

	if (!isDirectory && _WatchDirectoriesOnly()) {
		if (_entry != NULL)
			*_entry = NULL;
		return B_OK;
	}

	Entry* entry = directory->CreateEntry(name, NULL);
	if (entry == NULL)
		return B_NO_MEMORY;

	status_t error = _AddNode(nodeRef, isDirectory, notify && _WatchFilesOnly(),
		entry);
	if (error != B_OK) {
		directory->RemoveEntry(entry);
		delete entry;
		return error;
	}

	if (notify)
		_NotifyEntryCreatedOrRemoved(entry, B_ENTRY_CREATED);

	if (_entry != NULL)
		*_entry = entry;
	return B_OK;
}


/**
 * @brief Remove \a entry from its parent directory then delete it.
 *
 * Removes the Entry from the parent Directory's hash table, then delegates
 * to _DeleteEntryAlreadyRemovedFromParent().
 *
 * @param entry   The Entry to remove and delete.
 * @param notify  If true, send B_ENTRY_REMOVED notification.
 */
void
PathHandler::_DeleteEntry(Entry* entry, bool notify)
{
	entry->Parent()->RemoveEntry(entry);
	_DeleteEntryAlreadyRemovedFromParent(entry, notify);
}


/**
 * @brief Finish deleting an Entry that has already been removed from its parent.
 *
 * Optionally notifies, then deletes the Node if this was its only Entry, and
 * finally frees the Entry object itself.
 *
 * @param entry   The Entry to delete.
 * @param notify  If true, send B_ENTRY_REMOVED notification before deleting.
 */
void
PathHandler::_DeleteEntryAlreadyRemovedFromParent(Entry* entry, bool notify)
{
	if (notify)
		_NotifyEntryCreatedOrRemoved(entry, B_ENTRY_REMOVED);

	Node* node = entry->Node();
	if (node->IsOnlyNodeEntry(entry))
		_DeleteNode(node, notify && _WatchFilesOnly());

	delete entry;
}


/**
 * @brief Recursively send B_ENTRY_CREATED or B_ENTRY_REMOVED for all file entries under \a entry.
 *
 * If \a entry's node is a directory, iterates its children recursively.
 * If it is a plain file, calls _NotifyEntryCreatedOrRemoved() directly.
 *
 * @param entry   The entry from which to start the recursive walk.
 * @param opcode  B_ENTRY_CREATED or B_ENTRY_REMOVED.
 */
void
PathHandler::_NotifyFilesCreatedOrRemoved(Entry* entry, int32 opcode) const
{
	Directory* directory = entry->Node()->ToDirectory();
	if (directory == NULL) {
		_NotifyEntryCreatedOrRemoved(entry, opcode);
		return;
	}

	for (EntryMap::Iterator it = directory->GetEntryIterator(); it.HasNext();)
		_NotifyFilesCreatedOrRemoved(it.Next(), opcode);
}


/**
 * @brief Send a B_ENTRY_CREATED or B_ENTRY_REMOVED notification for a tracked Entry.
 *
 * Builds the entry ref and path from the Entry's parent and name, then
 * delegates to the ref-based overload.
 *
 * @param entry   The Entry that was created or removed.
 * @param opcode  B_ENTRY_CREATED or B_ENTRY_REMOVED.
 */
void
PathHandler::_NotifyEntryCreatedOrRemoved(Entry* entry, int32 opcode) const
{
	Node* node = entry->Node();
	_NotifyEntryCreatedOrRemoved(
		NotOwningEntryRef(entry->Parent()->NodeRef(), entry->Name()),
		node->NodeRef(), _EntryPath(entry), node->IsDirectory(), opcode);
}


/**
 * @brief Send a B_ENTRY_CREATED or B_ENTRY_REMOVED notification using explicit refs.
 *
 * Skips the notification when the entry's type is filtered out by
 * B_WATCH_FILES_ONLY or B_WATCH_DIRECTORIES_ONLY.  Builds a B_PATH_MONITOR
 * message and forwards it via _NotifyTarget().
 *
 * @param entryRef    The entry ref of the created/removed entry.
 * @param nodeRef     The node ref of the created/removed entry.
 * @param path        The absolute path of the entry (may be NULL or empty).
 * @param isDirectory true if the entry is a directory.
 * @param opcode      B_ENTRY_CREATED or B_ENTRY_REMOVED.
 */
void
PathHandler::_NotifyEntryCreatedOrRemoved(const entry_ref& entryRef,
	const node_ref& nodeRef, const char* path, bool isDirectory, int32 opcode)
	const
{
	if (isDirectory ? _WatchFilesOnly() : _WatchDirectoriesOnly())
		return;

	TRACE("%p->PathHandler::_NotifyEntryCreatedOrRemoved(): entry %s: %"
		B_PRIdDEV ":%" B_PRIdINO ":\"%s\", node: %" B_PRIdDEV ":%" B_PRIdINO
		"\n", this, opcode == B_ENTRY_CREATED ? "created" : "removed",
		entryRef.device, entryRef.directory, entryRef.name, nodeRef.device,
		nodeRef.node);

	BMessage message(B_PATH_MONITOR);
	message.AddInt32("opcode", opcode);
	message.AddInt32("device", entryRef.device);
	message.AddInt64("directory", entryRef.directory);
	message.AddInt32("node device", nodeRef.device);
		// This field is not in a usual node monitoring message, since the node
		// the created/removed entry refers to always belongs to the same FS as
		// the directory, as another FS cannot yet/no longer be mounted there.
		// In our case, however, this can very well be the case, e.g. when the
		// the notification is triggered in response to a directory tree having
		// been moved into/out of our path.
	message.AddInt64("node", nodeRef.node);
	message.AddString("name", entryRef.name);

	_NotifyTarget(message, path);
}


/**
 * @brief Send a B_ENTRY_MOVED notification to the target messenger.
 *
 * Filters out moves that don't match the active B_WATCH_FILES_ONLY /
 * B_WATCH_DIRECTORIES_ONLY flags.  Adds "added" and/or "removed" boolean
 * fields when the move crosses the boundary of the monitored path, and
 * includes "from path" when the source is inside the monitored tree.
 *
 * @param fromEntryRef  Entry ref of the source location.
 * @param toEntryRef    Entry ref of the destination location.
 * @param nodeRef       Node ref of the moved entry.
 * @param fromPath      Absolute path of the source (may be NULL or empty).
 * @param path          Absolute path of the destination (may be NULL or empty).
 * @param isDirectory   true if the moved entry is a directory.
 * @param wasAdded      true if the entry moved into the monitored tree.
 * @param wasRemoved    true if the entry moved out of the monitored tree.
 */
void
PathHandler::_NotifyEntryMoved(const entry_ref& fromEntryRef,
	const entry_ref& toEntryRef, const node_ref& nodeRef, const char* fromPath,
	const char* path, bool isDirectory, bool wasAdded, bool wasRemoved) const
{
	if ((isDirectory && _WatchFilesOnly())
		|| (!isDirectory && _WatchDirectoriesOnly())) {
		return;
	}

	TRACE("%p->PathHandler::_NotifyEntryMoved(): entry: %" B_PRIdDEV ":%"
		B_PRIdINO ":\"%s\" -> %" B_PRIdDEV ":%" B_PRIdINO ":\"%s\", node: %"
		B_PRIdDEV ":%" B_PRIdINO "\n", this, fromEntryRef.device,
		fromEntryRef.directory, fromEntryRef.name, toEntryRef.device,
		toEntryRef.directory, toEntryRef.name, nodeRef.device, nodeRef.node);

	BMessage message(B_PATH_MONITOR);
	message.AddInt32("opcode", B_ENTRY_MOVED);
	message.AddInt32("device", fromEntryRef.device);
	message.AddInt64("from directory", fromEntryRef.directory);
	message.AddInt64("to directory", toEntryRef.directory);
	message.AddInt32("node device", nodeRef.device);
	message.AddInt64("node", nodeRef.node);
	message.AddString("from name", fromEntryRef.name);
	message.AddString("name", toEntryRef.name);

	if (wasAdded)
		message.AddBool("added", true);
	if (wasRemoved)
		message.AddBool("removed", true);

	if (fromPath != NULL && fromPath[0] != '\0')
		message.AddString("from path", fromPath);

	_NotifyTarget(message, path);
}


/**
 * @brief Attach path metadata to \a message and deliver it to the target messenger.
 *
 * Sets the message's what field to B_PATH_MONITOR, optionally appends "path"
 * and always appends "watched_path", then sends the message via fTarget.
 *
 * @param message  The message to annotate and send (modified in place).
 * @param path     The path to include as the "path" field (may be NULL/empty).
 */
void
PathHandler::_NotifyTarget(BMessage& message, const char* path) const
{
	message.what = B_PATH_MONITOR;
	if (path != NULL && path[0] != '\0')
		message.AddString("path", path);
	message.AddString("watched_path", fPath.String());
	fTarget.SendMessage(&message);
}


/**
 * @brief Return the absolute path of \a node within the monitored tree.
 *
 * Uses the first known Entry for the node to construct the path recursively.
 * Falls back to fPath for the base node if it has no entries.
 *
 * @param node  The Node whose path is requested.
 * @return The absolute path as a BString, or an empty BString if unknown.
 */
BString
PathHandler::_NodePath(const Node* node) const
{
	if (Entry* entry = node->FirstNodeEntry())
		return _EntryPath(entry);
	return node == fBaseNode ? fPath : BString();
}


/**
 * @brief Return the absolute path of \a entry by combining the parent's path and the entry name.
 *
 * @param entry  The Entry whose path is requested.
 * @return The absolute path as a BString.
 */
BString
PathHandler::_EntryPath(const Entry* entry) const
{
	return make_path(_NodePath(entry->Parent()), entry->Name());
}


/**
 * @brief Return whether B_WATCH_RECURSIVELY is set in fFlags.
 * @return true if the handler watches the entire subtree recursively.
 */
bool
PathHandler::_WatchRecursively() const
{
	return (fFlags & B_WATCH_RECURSIVELY) != 0;
}


/**
 * @brief Return whether B_WATCH_FILES_ONLY is set in fFlags.
 * @return true if only plain-file entries are reported.
 */
bool
PathHandler::_WatchFilesOnly() const
{
	return (fFlags & B_WATCH_FILES_ONLY) != 0;
}


/**
 * @brief Return whether B_WATCH_DIRECTORIES_ONLY is set in fFlags.
 * @return true if only directory entries are reported.
 */
bool
PathHandler::_WatchDirectoriesOnly() const
{
	return (fFlags & B_WATCH_DIRECTORIES_ONLY) != 0;
}


} // namespace


//	#pragma mark - BPathMonitor


namespace BPrivate {


/**
 * @brief Construct a BPathMonitor instance.
 *
 * The constructor performs no observable work; all monitoring is set up
 * through the static StartWatching() method.
 */
BPathMonitor::BPathMonitor()
{
}


/**
 * @brief Destroy the BPathMonitor instance.
 */
BPathMonitor::~BPathMonitor()
{
}


/**
 * @brief Begin monitoring \a path for filesystem changes and deliver notifications to \a target.
 *
 * Initialises the global monitoring infrastructure (looper, watcher map) if
 * necessary.  Creates or replaces a PathHandler for the given path/target pair.
 * When a handler already exists for the same path, the new \a flags are merged
 * with the existing ones (resolving mutually exclusive flags in favour of the
 * new value).
 *
 * @param path    The filesystem path to monitor (must be non-empty).
 * @param flags   Combination of B_WATCH_* flags; B_WATCH_FILES_ONLY and
 *                B_WATCH_DIRECTORIES_ONLY are mutually exclusive.
 * @param target  The BMessenger to receive B_PATH_MONITOR messages.
 * @return B_OK on success, B_BAD_VALUE for invalid arguments, B_NO_MEMORY if
 *         allocation fails, or another error from the node monitor layer.
 */
/*static*/ status_t
BPathMonitor::StartWatching(const char* path, uint32 flags,
	const BMessenger& target)
{
	TRACE("BPathMonitor::StartWatching(%s, %" B_PRIx32 ")\n", path, flags);

	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;

	// B_WATCH_FILES_ONLY and B_WATCH_DIRECTORIES_ONLY are mutual exclusive
	if ((flags & B_WATCH_FILES_ONLY) != 0
		&& (flags & B_WATCH_DIRECTORIES_ONLY) != 0) {
		return B_BAD_VALUE;
	}

	status_t status = _InitIfNeeded();
	if (status != B_OK)
		return status;

	BAutolock _(sLooper);

	Watcher* watcher = sWatchers->Lookup(target);
	bool newWatcher = false;
	if (watcher != NULL) {
		// If there's already a handler for the path, we'll replace it, but
		// add its flags.
		if (PathHandler* handler = watcher->Lookup(path)) {
			// keep old flags save for conflicting mutually exclusive ones
			uint32 oldFlags = handler->Flags();
			const uint32 kMutuallyExclusiveFlags
				= B_WATCH_FILES_ONLY | B_WATCH_DIRECTORIES_ONLY;
			if ((flags & kMutuallyExclusiveFlags) != 0)
				oldFlags &= ~(uint32)kMutuallyExclusiveFlags;
			flags |= oldFlags;

			watcher->Remove(handler);
			handler->Quit();
		}
	} else {
		watcher = Watcher::Create(target);
		if (watcher == NULL)
			return B_NO_MEMORY;
		sWatchers->Insert(watcher);
		newWatcher = true;
	}

	PathHandler* handler = new (std::nothrow) PathHandler(path, flags, target,
		sLooper);
	status = handler != NULL ? handler->InitCheck() : B_NO_MEMORY;

	if (status != B_OK) {
		if (handler != NULL)
			handler->Quit();

		if (newWatcher) {
			sWatchers->Remove(watcher);
			delete watcher;
		}
		return status;
	}

	watcher->Insert(handler);
	return B_OK;
}


/**
 * @brief Stop monitoring a specific \a path for the given \a target.
 *
 * Looks up the Watcher for \a target and the PathHandler for \a path within
 * it, removes the handler, and cleans up the Watcher if it becomes empty.
 *
 * @param path    The path that was previously passed to StartWatching().
 * @param target  The BMessenger previously passed to StartWatching().
 * @return B_OK on success, B_BAD_VALUE if the path or target is not currently
 *         being watched.
 */
/*static*/ status_t
BPathMonitor::StopWatching(const char* path, const BMessenger& target)
{
	if (sLooper == NULL)
		return B_BAD_VALUE;

	TRACE("BPathMonitor::StopWatching(%s)\n", path);

	BAutolock _(sLooper);

	Watcher* watcher = sWatchers->Lookup(target);
	if (watcher == NULL)
		return B_BAD_VALUE;

	PathHandler* handler = watcher->Lookup(path);
	if (handler == NULL)
		return B_BAD_VALUE;

	watcher->Remove(handler);
	handler->Quit();

	if (watcher->IsEmpty()) {
		sWatchers->Remove(watcher);
		delete watcher;
	}

	return B_OK;
}


/**
 * @brief Stop monitoring all paths for the given \a target.
 *
 * Removes and destroys every PathHandler registered under the Watcher for
 * \a target, then removes and destroys the Watcher itself.
 *
 * @param target  The BMessenger whose entire watch registration is to be cancelled.
 * @return B_OK on success, B_BAD_VALUE if \a target is not currently watching
 *         any path.
 */
/*static*/ status_t
BPathMonitor::StopWatching(const BMessenger& target)
{
	if (sLooper == NULL)
		return B_BAD_VALUE;

	BAutolock _(sLooper);

	Watcher* watcher = sWatchers->Lookup(target);
	if (watcher == NULL)
		return B_BAD_VALUE;

	// delete handlers
	PathHandler* handler = watcher->Clear(true);
	while (handler != NULL) {
		PathHandler* nextHandler = handler->HashNext();
		handler->Quit();
		handler = nextHandler;
	}

	sWatchers->Remove(watcher);
	delete watcher;

	return B_OK;
}


/**
 * @brief Replace the active watching interface with a custom implementation.
 *
 * Allows the caller to substitute a custom BWatchingInterface (e.g. for
 * testing or alternative kernel APIs).  Passing NULL restores the default
 * implementation.
 *
 * @param watchingInterface  The new interface to use, or NULL to restore the default.
 */
/*static*/ void
BPathMonitor::SetWatchingInterface(BWatchingInterface* watchingInterface)
{
	sWatchingInterface = watchingInterface != NULL
		? watchingInterface : sDefaultWatchingInterface;
}


/**
 * @brief Initialise the global monitoring infrastructure if it has not been set up yet.
 *
 * Uses pthread_once() to ensure _Init() is called exactly once.
 *
 * @return B_OK if the looper is running, B_NO_MEMORY if initialisation failed.
 */
/*static*/ status_t
BPathMonitor::_InitIfNeeded()
{
	pthread_once(&sInitOnce, &BPathMonitor::_Init);
	return sLooper != NULL ? B_OK : B_NO_MEMORY;
}


/**
 * @brief One-time initialisation of the PathMonitor subsystem.
 *
 * Creates the default BWatchingInterface, the global WatcherMap, and the
 * dedicated BLooper ("PathMonitor looper") that receives all node-monitor
 * messages.  Called exactly once via pthread_once().
 */
/*static*/ void
BPathMonitor::_Init()
{
	sDefaultWatchingInterface = new(std::nothrow) BWatchingInterface;
	if (sDefaultWatchingInterface == NULL)
		return;

	sWatchers = new(std::nothrow) WatcherMap;
	if (sWatchers == NULL || sWatchers->Init() != B_OK)
		return;

	if (sWatchingInterface == NULL)
		SetWatchingInterface(sDefaultWatchingInterface);

	BLooper* looper = new (std::nothrow) BLooper("PathMonitor looper");
	TRACE("Start PathMonitor looper\n");
	if (looper == NULL)
		return;
	thread_id thread = looper->Run();
	if (thread < 0) {
		delete looper;
		return;
	}

	sLooper = looper;
}


// #pragma mark - BWatchingInterface


/**
 * @brief Construct a BWatchingInterface.
 */
BPathMonitor::BWatchingInterface::BWatchingInterface()
{
}


/**
 * @brief Destroy the BWatchingInterface.
 */
BPathMonitor::BWatchingInterface::~BWatchingInterface()
{
}


/**
 * @brief Start watching \a node using a BMessenger target.
 *
 * Delegates directly to the kernel watch_node() function.
 *
 * @param node    The node ref to watch.
 * @param flags   Combination of B_WATCH_* flags.
 * @param target  The BMessenger to receive node-monitor messages.
 * @return B_OK on success, or a kernel error code.
 */
status_t
BPathMonitor::BWatchingInterface::WatchNode(const node_ref* node, uint32 flags,
	const BMessenger& target)
{
	return watch_node(node, flags, target);
}


/**
 * @brief Start watching \a node using a BHandler/BLooper pair as the target.
 *
 * Delegates directly to the kernel watch_node() function.
 *
 * @param node     The node ref to watch.
 * @param flags    Combination of B_WATCH_* flags.
 * @param handler  The BHandler to receive node-monitor messages.
 * @param looper   The BLooper that owns \a handler.
 * @return B_OK on success, or a kernel error code.
 */
status_t
BPathMonitor::BWatchingInterface::WatchNode(const node_ref* node, uint32 flags,
	const BHandler* handler, const BLooper* looper)
{
	return watch_node(node, flags, handler, looper);
}


/**
 * @brief Stop all watching activity for the given BMessenger target.
 *
 * Delegates directly to the kernel stop_watching() function.
 *
 * @param target  The BMessenger whose node monitors should be cancelled.
 * @return B_OK on success, or a kernel error code.
 */
status_t
BPathMonitor::BWatchingInterface::StopWatching(const BMessenger& target)
{
	return stop_watching(target);
}


/**
 * @brief Stop all watching activity for the given BHandler/BLooper pair.
 *
 * Delegates directly to the kernel stop_watching() function.
 *
 * @param handler  The BHandler whose node monitors should be cancelled.
 * @param looper   The BLooper that owns \a handler.
 * @return B_OK on success, or a kernel error code.
 */
status_t
BPathMonitor::BWatchingInterface::StopWatching(const BHandler* handler,
	const BLooper* looper)
{
	return stop_watching(handler, looper);
}


}	// namespace BPrivate
