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
 * @file SolverProblemSolution.cpp
 * @brief Implementation of BSolverProblemSolution and its element class.
 *
 * BSolverProblemSolution represents one way to resolve a BSolverProblem. It
 * is composed of one or more BSolverProblemSolutionElement objects, each
 * describing a concrete change to the job queue (e.g. "do not install X",
 * "allow downgrade of Y to Z"). The ToString() methods on both classes produce
 * localised, human-readable descriptions.
 *
 * @see BSolverProblem, BSolver
 */


#include <Catalog.h>

#include <package/solver/SolverProblemSolution.h>

#include <package/solver/SolverPackage.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SolverProblemSolution"


/** @brief Localisation templates indexed by BSolverProblemSolutionElement::BType. */
static const char* const kToStringTexts[] = {
	B_TRANSLATE_MARK("do something"),
	B_TRANSLATE_MARK("do not keep %source% installed"),
	B_TRANSLATE_MARK("do not install \"%selection%\""),
	B_TRANSLATE_MARK("do not install the most recent version of "
		"\"%selection%\""),
	B_TRANSLATE_MARK("do not forbid installation of %source%"),
	B_TRANSLATE_MARK("do not deinstall \"%selection%\""),
	B_TRANSLATE_MARK("do not deinstall all resolvables \"%selection%\""),
	B_TRANSLATE_MARK("do not lock \"%selection%\""),
	B_TRANSLATE_MARK("keep %source% despite its inferior architecture"),
	B_TRANSLATE_MARK("keep %source% from excluded repository"),
	B_TRANSLATE_MARK("keep old %source%"),
	B_TRANSLATE_MARK("install %source% despite its inferior architecture"),
	B_TRANSLATE_MARK("install %source% from excluded repository"),
	B_TRANSLATE_MARK("install %selection% despite its old version"),
	B_TRANSLATE_MARK("allow downgrade of %source% to %target%"),
	B_TRANSLATE_MARK("allow name change of %source% to %target%"),
	B_TRANSLATE_MARK("allow architecture change of %source% to %target%"),
	B_TRANSLATE_MARK("allow vendor change from \"%sourceVendor%\" (%source%) "
		"to \"%targetVendor%\" (%target%)"),
	B_TRANSLATE_MARK("allow replacement of %source% with %target%"),
	B_TRANSLATE_MARK("allow deinstallation of %source%")
};


namespace BPackageKit {


// #pragma mark - BSolverProblemSolutionElement


/**
 * @brief Construct a BSolverProblemSolutionElement.
 *
 * @param type           The type of change this element proposes.
 * @param sourcePackage  The source package involved; may be NULL.
 * @param targetPackage  The target package involved; may be NULL.
 * @param selection      A string selector used for certain element types.
 */
BSolverProblemSolutionElement::BSolverProblemSolutionElement(BType type,
	BSolverPackage* sourcePackage, BSolverPackage* targetPackage,
	const BString& selection)
	:
	fType(type),
	fSourcePackage(sourcePackage),
	fTargetPackage(targetPackage),
	fSelection(selection)
{
}


/**
 * @brief Destroy the BSolverProblemSolutionElement.
 */
BSolverProblemSolutionElement::~BSolverProblemSolutionElement()
{
}


/**
 * @brief Return the type of change proposed by this element.
 *
 * @return The BType enum value.
 */
BSolverProblemSolutionElement::BType
BSolverProblemSolutionElement::Type() const
{
	return fType;
}


/**
 * @brief Return the source package for this element.
 *
 * @return Pointer to the source BSolverPackage, or NULL if not applicable.
 */
BSolverPackage*
BSolverProblemSolutionElement::SourcePackage() const
{
	return fSourcePackage;
}


/**
 * @brief Return the target package for this element.
 *
 * @return Pointer to the target BSolverPackage, or NULL if not applicable.
 */
BSolverPackage*
BSolverProblemSolutionElement::TargetPackage() const
{
	return fTargetPackage;
}


/**
 * @brief Return the selection string for this element.
 *
 * @return Reference to the selection string; may be empty.
 */
const BString&
BSolverProblemSolutionElement::Selection() const
{
	return fSelection;
}


/**
 * @brief Produce a localised, human-readable description of this solution element.
 *
 * Substitutes %source%, %target%, %selection%, %sourceVendor%, and
 * %targetVendor% with the appropriate package names or vendor strings.
 *
 * @return A BString containing the formatted element description.
 */
BString
BSolverProblemSolutionElement::ToString() const
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
		.ReplaceAll("%selection%", fSelection)
		.ReplaceAll("%sourceVendor%",
			fSourcePackage != NULL
				? fSourcePackage->Info().Vendor().String() : "?")
		.ReplaceAll("%targetVendor%",
			fTargetPackage != NULL
				? fTargetPackage->Info().Vendor().String() : "?");
}


// #pragma mark - BSolverProblemSolution


/**
 * @brief Default-construct an empty BSolverProblemSolution.
 */
BSolverProblemSolution::BSolverProblemSolution()
	:
	fElements(10)
{
}


/**
 * @brief Destroy the solution and its element list.
 */
BSolverProblemSolution::~BSolverProblemSolution()
{
}


/**
 * @brief Return the number of elements in this solution.
 *
 * @return Count of BSolverProblemSolutionElement objects.
 */
int32
BSolverProblemSolution::CountElements() const
{
	return fElements.CountItems();
}


/**
 * @brief Return the element at the given index.
 *
 * @param index  Zero-based index of the element.
 * @return Pointer to the Element, or NULL if out of range.
 */
const BSolverProblemSolution::Element*
BSolverProblemSolution::ElementAt(int32 index) const
{
	return fElements.ItemAt(index);
}


/**
 * @brief Append a copy of the given element to this solution.
 *
 * @param element  The element to copy and append.
 * @return True on success, false on allocation failure.
 */
bool
BSolverProblemSolution::AppendElement(const Element& element)
{
	Element* newElement = new(std::nothrow) Element(element);
	if (newElement == NULL || !fElements.AddItem(newElement)) {
		delete newElement;
		return false;
	}

	return true;
}


}	// namespace BPackageKit
