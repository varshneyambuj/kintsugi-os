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

/** @file Package.h
 *  @brief Represents an installed package with activation state and hash-table support */

#ifndef PACKAGE_H
#define PACKAGE_H


#include <set>

#include "PackageFile.h"


using namespace BPackageKit;


/** @brief Wraps a PackageFile with activation state for use in volume package tracking */
class Package {
public:
	/** @brief Construct a package backed by the given file (acquires a reference) */
								Package(PackageFile* file);
	/** @brief Destructor (releases the PackageFile reference) */
								~Package();

	/** @brief Return the underlying PackageFile */
			PackageFile*		File() const
									{ return fFile; }

	/** @brief Return the inode/device reference for this package */
			const node_ref&		NodeRef() const
									{ return fFile->NodeRef(); }
	/** @brief Return the on-disk filename */
			const BString&		FileName() const
									{ return fFile->FileName(); }
	/** @brief Return the entry reference for this package */
			NotOwningEntryRef	EntryRef() const
									{ return fFile->EntryRef(); }

	/** @brief Return the parsed package metadata */
			const BPackageInfo & Info() const
									{ return fFile->Info(); }

	/** @brief Return the versioned name (name-version) */
			BString				RevisionedName() const
									{ return fFile->RevisionedName(); }
	/** @brief Return the versioned name, throwing on allocation failure */
			BString				RevisionedNameThrows() const
									{ return fFile->RevisionedNameThrows(); }

	/** @brief Check whether this package is currently activated */
			bool				IsActive() const
									{ return fActive; }
	/** @brief Set the activation state of this package */
			void				SetActive(bool active)
									{ fActive = active; }

	/** @brief Create a deep copy of this package sharing the same PackageFile */
			Package*			Clone() const;

	/** @brief Hash-table link for filename-based lookup */
			Package*&			FileNameHashTableNext()
									{ return fFileNameHashTableNext; }
	/** @brief Hash-table link for node-ref-based lookup */
			Package*&			NodeRefHashTableNext()
									{ return fNodeRefHashTableNext; }

private:
			PackageFile*		fFile;                  /**< Underlying package file with metadata */
			bool				fActive;                /**< Whether this package is activated */
			Package*			fFileNameHashTableNext;  /**< Intrusive hash link (by filename) */
			Package*			fNodeRefHashTableNext;   /**< Intrusive hash link (by node ref) */
};


/** @brief Hash-table definition for looking up packages by filename */
struct PackageFileNameHashDefinition {
	typedef const char*		KeyType;
	typedef	Package			ValueType;

	size_t HashKey(const char* key) const
	{
		return BString::HashValue(key);
	}

	size_t Hash(const Package* value) const
	{
		return HashKey(value->FileName());
	}

	bool Compare(const char* key, const Package* value) const
	{
		return value->FileName() == key;
	}

	Package*& GetLink(Package* value) const
	{
		return value->FileNameHashTableNext();
	}
};


/** @brief Hash-table definition for looking up packages by node reference */
struct PackageNodeRefHashDefinition {
	typedef node_ref		KeyType;
	typedef	Package			ValueType;

	size_t HashKey(const node_ref& key) const
	{
		return (size_t)key.device + 17 * (size_t)key.node;
	}

	size_t Hash(const Package* value) const
	{
		return HashKey(value->NodeRef());
	}

	bool Compare(const node_ref& key, const Package* value) const
	{
		return key == value->NodeRef();
	}

	Package*& GetLink(Package* value) const
	{
		return value->NodeRefHashTableNext();
	}
};


typedef BOpenHashTable<PackageFileNameHashDefinition> PackageFileNameHashTable;
typedef BOpenHashTable<PackageNodeRefHashDefinition> PackageNodeRefHashTable;
typedef std::set<Package*> PackageSet;


#endif	// PACKAGE_H
