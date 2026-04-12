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
 * @file SolverProblem.cpp
 * @brief Implementation of BSolverProblem, a dependency-resolution conflict descriptor.
 *
 * BSolverProblem encapsulates a single unresolvable conflict detected by the
 * solver: its type, the source and target packages involved, an optional
 * dependency expression, and a list of BSolverProblemSolution objects that the
 * user may select to resolve it. The ToString() method produces a localised,
 * human-readable description.
 *
 * @see BSolverProblemSolution, BSolver
 */


#include <Catalog.h>

#include <package/solver/SolverProblem.h>

#include <package/solver/SolverPackage.h>
#include <package/solver/SolverProblemSolution.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SolverProblem"


/** @brief Localisation templates indexed by BSolverProblem::BType. */
static const char* const kToStringTexts[] = {
	B_TRANSLATE_MARK("unspecified problem"),
	B_TRANSLATE_MARK("%source% does not belong to a distupgrade repository"),
	B_TRANSLATE_MARK("%source% has inferior architecture"),
	B_TRANSLATE_MARK("problem with installed package %source%"),
	B_TRANSLATE_MARK("conflicting requests"),
	B_TRANSLATE_MARK("nothing provides requested %dependency%"),
	B_TRANSLATE_MARK("%dependency% is provided by the system"),
	B_TRANSLATE_MARK("dependency problem"),
	B_TRANSLATE_MARK("package %source% is not installable"),
	B_TRANSLATE_MARK("nothing provides %dependency% needed by %source%"),
	B_TRANSLATE_MARK("cannot install both %source% and %target%"),
	B_TRANSLATE_MARK("package %source% conflicts with %dependency% provided "
		"by %target%"),
	B_TRANSLATE_MARK("package %source% obsoletes %dependency% provided by "
		"%target%"),
	B_TRANSLATE_MARK("installed package %source% obsoletes %dependency% "
		"provided by %target%"),
	B_TRANSLATE_MARK("package %source% implicitly obsoletes %dependency% "
		"provided by %target%"),
	B_TRANSLATE_MARK("package %source% requires %dependency%, but none of the "
		"providers can be installed"),
	B_TRANSLATE_MARK("package %source% conflicts with %dependency% provided by "
		"itself")
};


namespace BPackageKit {


/**
 * @brief Construct a BSolverProblem without a dependency expression.
 *
 * @param type           The type of conflict detected.
 * @param sourcePackage  The package that triggers the conflict; may be NULL.
 * @param targetPackage  The conflicting target package; may be NULL.
 */
BSolverProblem::BSolverProblem(BType type, BSolverPackage* sourcePackage,
	BSolverPackage* targetPackage)
	:
	fType(type),
	fSourcePackage(sourcePackage),
	fTargetPackage(targetPackage),
	fDependency(),
	fSolutions(10)
{
}


/**
 * @brief Construct a BSolverProblem with a dependency expression.
 *
 * @param type           The type of conflict detected.
 * @param sourcePackage  The package that triggers the conflict; may be NULL.
 * @param targetPackage  The conflicting target package; may be NULL.
 * @param dependency     The resolvable expression involved in the conflict.
 */
BSolverProblem::BSolverProblem(BType type, BSolverPackage* sourcePackage,
	BSolverPackage* targetPackage,
	const BPackageResolvableExpression& dependency)
	:
	fType(type),
	fSourcePackage(sourcePackage),
	fTargetPackage(targetPackage),
	fDependency(dependency),
	fSolutions(10)
{
}


/**
 * @brief Destroy the BSolverProblem and its associated solutions.
 */
BSolverProblem::~BSolverProblem()
{
}


/**
 * @brief Return the conflict type of this problem.
 *
 * @return The BType enum value describing the nature of the conflict.
 */
BSolverProblem::BType
BSolverProblem::Type() const
{
	return fType;
}


/**
 * @brief Return the source package involved in this conflict.
 *
 * @return Pointer to the source BSolverPackage, or NULL if not applicable.
 */
BSolverPackage*
BSolverProblem::SourcePackage() const
{
	return fSourcePackage;
}


/**
 * @brief Return the target package involved in this conflict.
 *
 * @return Pointer to the target BSolverPackage, or NULL if not applicable.
 */
BSolverPackage*
BSolverProblem::TargetPackage() const
{
	return fTargetPackage;
}


/**
 * @brief Return the dependency expression involved in this conflict.
 *
 * @return Reference to the BPackageResolvableExpression; check InitCheck()
 *         before use as it may be uninitialised.
 */
const BPackageResolvableExpression&
BSolverProblem::Dependency() const
{
	return fDependency;
}


/**
 * @brief Return the number of available solutions for this problem.
 *
 * @return Count of BSolverProblemSolution objects.
 */
int32
BSolverProblem::CountSolutions() const
{
	return fSolutions.CountItems();
}


/**
 * @brief Return the solution at the given index.
 *
 * @param index  Zero-based index of the solution.
 * @return Pointer to the BSolverProblemSolution, or NULL if out of range.
 */
const BSolverProblemSolution*
BSolverProblem::SolutionAt(int32 index) const
{
	return fSolutions.ItemAt(index);
}


/**
 * @brief Append a solution to the list of available solutions.
 *
 * @param solution  The solution to append; ownership is transferred.
 * @return True on success, false if the list could not grow.
 */
bool
BSolverProblem::AppendSolution(BSolverProblemSolution* solution)
{
	return fSolutions.AddItem(solution);
}


/**
 * @brief Produce a localised, human-readable description of this problem.
 *
 * Substitutes %source%, %target%, and %dependency% placeholders with the
 * versioned names of the involved packages and dependency expression.
 *
 * @return A BString containing the formatted problem description.
 */
BString
BSolverProblem::ToString() const
{
	size_t index = fType;
	if (index >= sizeof(kToStringTexts) / sizeof(kToStringTexts[0]))
		index = 0;

	return BString(B_TRANSLATE_NOCOLLECT(kToStringTexts[index]))
		.ReplaceAll("%source%",
			fSourcePackage != NULL
				? fSourcePackage->VersionedName().String() : "?")
		.ReplaceAll("%target%",
			fTargetPackage != NULL
				? fTargetPackage->VersionedName().String() : "?")
		.ReplaceAll("%dependency%",
			fDependency.InitCheck() == B_OK
				? fDependency.ToString().String() : "?");
}


}	// namespace BPackageKit
