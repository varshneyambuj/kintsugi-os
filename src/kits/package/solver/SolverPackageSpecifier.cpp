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
 * @file SolverPackageSpecifier.cpp
 * @brief Implementation of BSolverPackageSpecifier, a discriminated union of package selectors.
 *
 * A BSolverPackageSpecifier can identify a package in one of three ways: as an
 * unspecified sentinel, as a direct pointer to a BSolverPackage, or as a string
 * expression passed to the solver's selection engine. It is used to build the
 * job lists for install, uninstall, update, and find operations.
 *
 * @see BSolverPackageSpecifierList, BSolver
 */


#include <package/solver/SolverPackageSpecifier.h>


namespace BPackageKit {


/**
 * @brief Default-construct an unspecified BSolverPackageSpecifier.
 */
BSolverPackageSpecifier::BSolverPackageSpecifier()
	:
	fType(B_UNSPECIFIED),
	fPackage(NULL),
	fSelectString()
{
}


/**
 * @brief Construct a specifier that identifies a specific BSolverPackage.
 *
 * @param package  Pointer to the package to select; must not be NULL.
 */
BSolverPackageSpecifier::BSolverPackageSpecifier(BSolverPackage* package)
	:
	fType(B_PACKAGE),
	fPackage(package),
	fSelectString()
{
}


/**
 * @brief Construct a specifier from a select-string expression.
 *
 * The string is forwarded to the solver's selection engine (e.g. "libfoo >= 1.2").
 *
 * @param selectString  The expression string used to match packages.
 */
BSolverPackageSpecifier::BSolverPackageSpecifier(const BString& selectString)
	:
	fType(B_SELECT_STRING),
	fPackage(NULL),
	fSelectString(selectString)
{
}


/**
 * @brief Copy-construct a BSolverPackageSpecifier.
 *
 * @param other  The specifier to copy.
 */
BSolverPackageSpecifier::BSolverPackageSpecifier(
	const BSolverPackageSpecifier& other)
	:
	fType(other.fType),
	fPackage(other.fPackage),
	fSelectString(other.fSelectString)
{
}


/**
 * @brief Destroy the BSolverPackageSpecifier.
 */
BSolverPackageSpecifier::~BSolverPackageSpecifier()
{
}


/**
 * @brief Return the discriminant type of this specifier.
 *
 * @return B_UNSPECIFIED, B_PACKAGE, or B_SELECT_STRING.
 */
BSolverPackageSpecifier::BType
BSolverPackageSpecifier::Type() const
{
	return fType;
}


/**
 * @brief Return the direct package pointer (valid when Type() == B_PACKAGE).
 *
 * @return Pointer to the BSolverPackage, or NULL for other types.
 */
BSolverPackage*
BSolverPackageSpecifier::Package() const
{
	return fPackage;
}


/**
 * @brief Return the selection string (valid when Type() == B_SELECT_STRING).
 *
 * @return Reference to the expression string, or an empty string for other types.
 */
const BString&
BSolverPackageSpecifier::SelectString() const
{
	return fSelectString;
}


/**
 * @brief Assignment operator.
 *
 * @param other  The specifier to copy.
 * @return Reference to this specifier.
 */
BSolverPackageSpecifier&
BSolverPackageSpecifier::operator=(const BSolverPackageSpecifier& other)
{
	fType = other.fType;
	fPackage = other.fPackage;
	fSelectString = other.fSelectString;
	return *this;
}


}	// namespace BPackageKit
