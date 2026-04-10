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
   /*
    * Copyright 2013-2014, Haiku, Inc. All Rights Reserved.
    * Distributed under the terms of the MIT License.
    *
    * Authors:
    *		Ingo Weinhold <ingo_weinhold@gmx.de>
    */
 */

/** @file PackageFile.h
 *  @brief Represents the on-disk package file with metadata and entry-ref tracking */

#ifndef PACKAGE_FILE_H
#define PACKAGE_FILE_H


#include <Node.h>
#include <package/PackageInfo.h>

#include <NotOwningEntryRef.h>
#include <Referenceable.h>
#include <util/OpenHashTable.h>


using namespace BPackageKit;


class PackageFileManager;


/** @brief Reference-counted representation of an on-disk package file and its parsed metadata */
class PackageFile : public BReferenceable {
public:
	/** @brief Construct an uninitialized PackageFile */
								PackageFile();
	/** @brief Destructor */
								~PackageFile();

	/** @brief Initialize from an entry ref, reading package info from disk */
			status_t			Init(const entry_ref& entryRef,
									PackageFileManager* owner);

	/** @brief Return the node reference for this file */
			const node_ref&		NodeRef() const
									{ return fNodeRef; }
	/** @brief Return the filename on disk */
			const BString&		FileName() const
									{ return fFileName; }

	/** @brief Return the node reference of the parent directory */
			const node_ref&		DirectoryRef() const
									{ return fDirectoryRef; }
	/** @brief Update the parent directory reference after a move */
			void				SetDirectoryRef(const node_ref& directoryRef)
									{ fDirectoryRef = directoryRef; }

	/** @brief Construct and return an entry reference from directory ref and filename */
			NotOwningEntryRef	EntryRef() const;

	/** @brief Return the parsed BPackageInfo metadata */
			const BPackageInfo & Info() const
									{ return fInfo; }

	/** @brief Return the name including version string */
			BString				RevisionedName() const;
	/** @brief Return the versioned name, throwing on allocation failure */
			BString				RevisionedNameThrows() const;

	/** @brief Return the current entry-created ignore nesting level */
			int32				EntryCreatedIgnoreLevel() const
									{ return fIgnoreEntryCreated; }
	/** @brief Increment the entry-created ignore nesting counter */
			void				IncrementEntryCreatedIgnoreLevel()
									{ fIgnoreEntryCreated++; }
	/** @brief Decrement the entry-created ignore nesting counter */
			void				DecrementEntryCreatedIgnoreLevel()
									{ fIgnoreEntryCreated--; }

	/** @brief Return the current entry-removed ignore nesting level */
			int32				EntryRemovedIgnoreLevel() const
									{ return fIgnoreEntryRemoved; }
	/** @brief Increment the entry-removed ignore nesting counter */
			void				IncrementEntryRemovedIgnoreLevel()
									{ fIgnoreEntryRemoved++; }
	/** @brief Decrement the entry-removed ignore nesting counter */
			void				DecrementEntryRemovedIgnoreLevel()
									{ fIgnoreEntryRemoved--; }

	/** @brief Hash-table link for entry-ref-based lookup */
			PackageFile*&		EntryRefHashTableNext()
									{ return fEntryRefHashTableNext; }

protected:
	/** @brief Notify the owning manager and self-delete when the last reference is released */
	virtual	void				LastReferenceReleased();

private:
			node_ref			fNodeRef;              /**< Inode reference for this file */
			node_ref			fDirectoryRef;         /**< Parent directory node reference */
			BString				fFileName;             /**< On-disk filename */
			BPackageInfo		fInfo;                 /**< Parsed package metadata */
			PackageFile*		fEntryRefHashTableNext;/**< Intrusive hash link (by entry ref) */
			PackageFileManager*	fOwner;                /**< Manager that owns this file in its cache */
			int32				fIgnoreEntryCreated;   /**< Nesting level for ignoring entry-created events */
			int32				fIgnoreEntryRemoved;   /**< Nesting level for ignoring entry-removed events */
};


inline NotOwningEntryRef
PackageFile::EntryRef() const
{
	return NotOwningEntryRef(fDirectoryRef, fFileName);
}


/** @brief Hash-table definition for looking up PackageFile objects by entry reference */
struct PackageFileEntryRefHashDefinition {
	typedef entry_ref		KeyType;
	typedef	PackageFile		ValueType;

	size_t HashKey(const entry_ref& key) const
	{
		size_t hash = BString::HashValue(key.name);
		hash ^= (size_t)key.device;
		hash ^= (size_t)key.directory;
		return hash;
	}

	size_t Hash(const PackageFile* value) const
	{
		return HashKey(value->EntryRef());
	}

	bool Compare(const entry_ref& key, const PackageFile* value) const
	{
		return value->EntryRef() == key;
	}

	PackageFile*& GetLink(PackageFile* value) const
	{
		return value->EntryRefHashTableNext();
	}
};


typedef BOpenHashTable<PackageFileEntryRefHashDefinition>
	PackageFileEntryRefHashTable;


#endif	// PACKAGE_FILE_H
