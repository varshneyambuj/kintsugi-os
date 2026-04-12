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
 * @file SolverResult.cpp
 * @brief Implementation of BSolverResult and BSolverResultElement.
 *
 * BSolverResult holds the ordered sequence of install/uninstall operations
 * that the solver determined are necessary to satisfy the requested job set.
 * Each operation is represented by a BSolverResultElement carrying a type
 * (B_TYPE_INSTALL or B_TYPE_UNINSTALL) and a pointer to the affected package.
 *
 * @see BSolver, BSolverPackage, BPackageManager
 */


#include <package/solver/SolverResult.h>


namespace BPackageKit {


// #pragma mark - BSolverResultElement


/**
 * @brief Construct a BSolverResultElement.
 *
 * @param type     Whether this element installs or uninstalls a package.
 * @param package  The package affected by this result element.
 */
BSolverResultElement::BSolverResultElement(BType type, BSolverPackage* package)
	:
	fType(type),
	fPackage(package)
{
}


/**
 * @brief Copy-construct a BSolverResultElement.
 *
 * @param other  The element to copy.
 */
BSolverResultElement::BSolverResultElement(const BSolverResultElement& other)
	:
	fType(other.fType),
	fPackage(other.fPackage)
{
}


/**
 * @brief Destroy the BSolverResultElement.
 */
BSolverResultElement::~BSolverResultElement()
{
}


/**
 * @brief Return the operation type for this element.
 *
 * @return B_TYPE_INSTALL or B_TYPE_UNINSTALL.
 */
BSolverResultElement::BType
BSolverResultElement::Type() const
{
	return fType;
}


/**
 * @brief Return the package associated with this element.
 *
 * @return Pointer to the BSolverPackage; not owned by this element.
 */
BSolverPackage*
BSolverResultElement::Package() const
{
	return fPackage;
}


/**
 * @brief Assignment operator.
 *
 * @param other  The element to copy.
 * @return Reference to this element.
 */
BSolverResultElement&
BSolverResultElement::operator=(const BSolverResultElement& other)
{
	fType = other.fType;
	fPackage = other.fPackage;
	return *this;
}


// #pragma mark - BSolverResult


/**
 * @brief Default-construct an empty BSolverResult.
 */
BSolverResult::BSolverResult()
	:
	fElements(20)
{
}


/**
 * @brief Destroy the BSolverResult and all its elements.
 */
BSolverResult::~BSolverResult()
{
}


/**
 * @brief Check whether the result contains no elements.
 *
 * @return True if the element list is empty.
 */
bool
BSolverResult::IsEmpty() const
{
	return fElements.IsEmpty();
}


/**
 * @brief Return the number of elements in the result.
 *
 * @return Count of BSolverResultElement objects.
 */
int32
BSolverResult::CountElements() const
{
	return fElements.CountItems();
}


/**
 * @brief Return the element at the given index.
 *
 * @param index  Zero-based index of the element.
 * @return Pointer to the BSolverResultElement, or NULL if out of range.
 */
const BSolverResultElement*
BSolverResult::ElementAt(int32 index) const
{
	return fElements.ItemAt(index);
}


/**
 * @brief Remove all elements from the result.
 */
void
BSolverResult::MakeEmpty()
{
	fElements.MakeEmpty();
}


/**
 * @brief Append a copy of the given element to the result.
 *
 * @param element  The element to copy and append.
 * @return True on success, false on allocation failure.
 */
bool
BSolverResult::AppendElement(const BSolverResultElement& element)
{
	BSolverResultElement* newElement
		= new(std::nothrow) BSolverResultElement(element);
	if (newElement == NULL || !fElements.AddItem(newElement)) {
		delete newElement;
		return false;
	}

	return true;
}


}	// namespace BPackageKit
