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
 * @file SolverPackage.cpp
 * @brief Implementation of BSolverPackage, a package node inside a solver repository.
 *
 * BSolverPackage pairs a BSolverRepository pointer with a BPackageInfo object,
 * giving the solver a lightweight handle to each candidate package during
 * dependency resolution. Versioned name access and copy semantics are also
 * provided.
 *
 * @see BSolverRepository, BPackageInfo, BSolver
 */


#include <package/solver/SolverPackage.h>


namespace BPackageKit {


/**
 * @brief Construct a BSolverPackage owned by the given repository.
 *
 * @param repository   The repository that contains this package.
 * @param packageInfo  The metadata describing this package.
 */
BSolverPackage::BSolverPackage(BSolverRepository* repository,
	const BPackageInfo& packageInfo)
	:
	fRepository(repository),
	fInfo(packageInfo)
{
}


/**
 * @brief Copy-construct a BSolverPackage.
 *
 * @param other  The package to copy.
 */
BSolverPackage::BSolverPackage(const BSolverPackage& other)
	:
	fRepository(other.fRepository),
	fInfo(other.fInfo)
{
}


/**
 * @brief Destroy the BSolverPackage.
 */
BSolverPackage::~BSolverPackage()
{
}


/**
 * @brief Return the repository that owns this package.
 *
 * @return Pointer to the owning BSolverRepository.
 */
BSolverRepository*
BSolverPackage::Repository() const
{
	return fRepository;
}


/**
 * @brief Return the package metadata.
 *
 * @return Reference to the BPackageInfo for this package.
 */
const BPackageInfo&
BSolverPackage::Info() const
{
	return fInfo;
}


/**
 * @brief Return just the package name without the version suffix.
 *
 * @return A BString containing the package name.
 */
BString
BSolverPackage::Name() const
{
	return fInfo.Name();
}


/**
 * @brief Return the package name with its version appended as "name-version".
 *
 * If the version is not initialised the plain name is returned.
 *
 * @return A BString of the form "name-version".
 */
BString
BSolverPackage::VersionedName() const
{
	if (fInfo.Version().InitCheck() != B_OK)
		return Name();
	BString result = Name();
	return result << '-' << fInfo.Version().ToString();
}


/**
 * @brief Return the package version.
 *
 * @return Reference to the BPackageVersion for this package.
 */
const BPackageVersion&
BSolverPackage::Version() const
{
	return fInfo.Version();
}


/**
 * @brief Assignment operator.
 *
 * @param other  The package to copy.
 * @return Reference to this package.
 */
BSolverPackage&
BSolverPackage::operator=(const BSolverPackage& other)
{
	fRepository = other.fRepository;
	fInfo = other.fInfo;
	return *this;
}


}	// namespace BPackageKit
