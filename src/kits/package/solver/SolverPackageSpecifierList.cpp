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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file SolverPackageSpecifierList.cpp
 * @brief Implementation of BSolverPackageSpecifierList, a growable list of package specifiers.
 *
 * BSolverPackageSpecifierList stores a sequence of BSolverPackageSpecifier objects
 * used to describe the packages that a solver operation should act on. The
 * underlying storage is a std::vector hidden behind a pointer-to-implementation to
 * avoid exposing STL types in the public API.
 *
 * @see BSolverPackageSpecifier, BSolver
 */


#include <package/solver/SolverPackageSpecifierList.h>

#include <new>
#include <vector>

#include <package/solver/SolverPackageSpecifier.h>


namespace BPackageKit {

/**
 * @brief Internal std::vector wrapper for BSolverPackageSpecifier storage.
 */
class BSolverPackageSpecifierList::Vector
	: public std::vector<BSolverPackageSpecifier> {
public:
	/**
	 * @brief Default-construct an empty Vector.
	 */
	Vector()
		:
		std::vector<BSolverPackageSpecifier>()
	{
	}

	/**
	 * @brief Copy-construct from another std::vector.
	 *
	 * @param other  The vector to copy.
	 */
	Vector(const std::vector<BSolverPackageSpecifier>& other)
		:
		std::vector<BSolverPackageSpecifier>(other)
	{
	}
};


/**
 * @brief Default-construct an empty BSolverPackageSpecifierList.
 */
BSolverPackageSpecifierList::BSolverPackageSpecifierList()
	:
	fSpecifiers(NULL)
{
}


/**
 * @brief Copy-construct a BSolverPackageSpecifierList.
 *
 * @param other  The list to copy.
 */
BSolverPackageSpecifierList::BSolverPackageSpecifierList(
	const BSolverPackageSpecifierList& other)
	:
	fSpecifiers(NULL)
{
	*this = other;
}


/**
 * @brief Destroy the list and free the internal vector.
 */
BSolverPackageSpecifierList::~BSolverPackageSpecifierList()
{
	delete fSpecifiers;
}


/**
 * @brief Check whether the list contains no specifiers.
 *
 * @return True if the list is empty or the vector has not been allocated yet.
 */
bool
BSolverPackageSpecifierList::IsEmpty() const
{
	return fSpecifiers == NULL || fSpecifiers->empty();
}


/**
 * @brief Return the number of specifiers in the list.
 *
 * @return Count of stored specifiers, or 0 if none have been added.
 */
int32
BSolverPackageSpecifierList::CountSpecifiers() const
{
	return fSpecifiers != NULL ? fSpecifiers->size() : 0;
}


/**
 * @brief Return the specifier at the given index.
 *
 * @param index  Zero-based index of the specifier to retrieve.
 * @return Pointer to the specifier, or NULL if @a index is out of range.
 */
const BSolverPackageSpecifier*
BSolverPackageSpecifierList::SpecifierAt(int32 index) const
{
	if (fSpecifiers == NULL || index < 0
		|| (size_t)index >= fSpecifiers->size()) {
		return NULL;
	}

	return &(*fSpecifiers)[index];
}


/**
 * @brief Append a single BSolverPackageSpecifier to the list.
 *
 * Allocates the internal vector on first use.
 *
 * @param specifier  The specifier to append.
 * @return True on success, false on allocation failure.
 */
bool
BSolverPackageSpecifierList::AppendSpecifier(
	const BSolverPackageSpecifier& specifier)
{
	try {
		if (fSpecifiers == NULL) {
			fSpecifiers = new(std::nothrow) Vector;
			if (fSpecifiers == NULL)
				return false;
		}

		fSpecifiers->push_back(specifier);
		return true;
	} catch (std::bad_alloc&) {
		return false;
	}
}


/**
 * @brief Append a specifier that identifies a direct BSolverPackage pointer.
 *
 * @param package  The package to wrap in a B_PACKAGE specifier.
 * @return True on success, false on allocation failure.
 */
bool
BSolverPackageSpecifierList::AppendSpecifier(BSolverPackage* package)
{
	return AppendSpecifier(BSolverPackageSpecifier(package));
}


/**
 * @brief Append a specifier from a select-string expression.
 *
 * @param selectString  The expression string to wrap in a B_SELECT_STRING specifier.
 * @return True on success, false on allocation failure.
 */
bool
BSolverPackageSpecifierList::AppendSpecifier(const BString& selectString)
{
	return AppendSpecifier(BSolverPackageSpecifier(selectString));
}


/**
 * @brief Append multiple select-string specifiers from a C-string array.
 *
 * If any append fails the already-added items are rolled back and false is returned.
 *
 * @param selectStrings  Array of C-strings to wrap as specifiers.
 * @param count          Number of entries in @a selectStrings.
 * @return True if all specifiers were appended successfully, false otherwise.
 */
bool
BSolverPackageSpecifierList::AppendSpecifiers(const char* const* selectStrings,
	int32 count)
{
	for (int32 i = 0; i < count; i++) {
		if (!AppendSpecifier(selectStrings[i])) {
			for (int32 k = i - 1; k >= 0; k--)
				fSpecifiers->pop_back();
			return false;
		}
	}

	return true;
}


/**
 * @brief Remove all specifiers from the list without freeing the vector.
 */
void
BSolverPackageSpecifierList::MakeEmpty()
{
	fSpecifiers->clear();
}


/**
 * @brief Assignment operator; replaces the current list with a copy of @a other.
 *
 * @param other  The list to copy.
 * @return Reference to this list.
 */
BSolverPackageSpecifierList&
BSolverPackageSpecifierList::operator=(const BSolverPackageSpecifierList& other)
{
	if (this == &other)
		return *this;

	delete fSpecifiers;
	fSpecifiers = NULL;

	if (other.fSpecifiers == NULL)
		return *this;

	try {
		fSpecifiers = new(std::nothrow) Vector(*other.fSpecifiers);
	} catch (std::bad_alloc&) {
	}

	return *this;
}


}	// namespace BPackageKit
