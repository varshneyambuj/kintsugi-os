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
 *   Copyright 2011, Oliver Tappe <zooey@hirschkaefer.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageResolvableExpression.cpp
 * @brief Implementation of BPackageResolvableExpression, a versioned dependency constraint.
 *
 * A BPackageResolvableExpression expresses a dependency requirement: a named
 * resolvable together with an optional comparison operator and version. For
 * example "libfoo >= 1.2" means the package requires a resolvable named
 * "libfoo" at version 1.2 or newer. The class also provides the Matches()
 * family of methods to test whether a given BPackageResolvable satisfies the
 * constraint.
 *
 * @see BPackageResolvable, BPackageInfo
 */


#include <package/PackageResolvableExpression.h>

#include <package/hpkg/PackageInfoAttributeValue.h>
#include <package/PackageInfo.h>
#include <package/PackageResolvable.h>


namespace BPackageKit {


/** @brief Human-readable operator symbols, indexed by BPackageResolvableOperator. */
const char*
BPackageResolvableExpression
::kOperatorNames[B_PACKAGE_RESOLVABLE_OP_ENUM_COUNT] = {
	"<",
	"<=",
	"==",
	"!=",
	">=",
	">",
};


/**
 * @brief Default-construct an uninitialised BPackageResolvableExpression.
 */
BPackageResolvableExpression::BPackageResolvableExpression()
	:
	fOperator(B_PACKAGE_RESOLVABLE_OP_ENUM_COUNT)
{
}


/**
 * @brief Construct a BPackageResolvableExpression from a low-level data struct.
 *
 * The name is converted to lower-case.
 *
 * @param data  Raw attribute data containing the name, operator, and version.
 */
BPackageResolvableExpression::BPackageResolvableExpression(
	const BPackageResolvableExpressionData& data)
	:
	fName(data.name),
	fOperator(data.op),
	fVersion(data.version)
{
	fName.ToLower();
}


/**
 * @brief Construct a BPackageResolvableExpression with explicit fields.
 *
 * The name is converted to lower-case.
 *
 * @param name       The name of the required resolvable.
 * @param _operator  The comparison operator to apply.
 * @param version    The version operand for the comparison.
 */
BPackageResolvableExpression::BPackageResolvableExpression(const BString& name,
	BPackageResolvableOperator _operator, const BPackageVersion& version)
	:
	fName(name),
	fOperator(_operator),
	fVersion(version)
{
	fName.ToLower();
}


/**
 * @brief Construct a BPackageResolvableExpression by parsing a string.
 *
 * Delegates to SetTo() to parse the expression.
 *
 * @param expressionString  The string to parse (e.g. "libfoo >= 1.2").
 */
BPackageResolvableExpression::BPackageResolvableExpression(
	const BString& expressionString)
	:
	fName(),
	fOperator(B_PACKAGE_RESOLVABLE_OP_ENUM_COUNT),
	fVersion()
{
	SetTo(expressionString);
}


/**
 * @brief Validate the expression's internal state.
 *
 * Both operator and version must either be set together or both absent.
 *
 * @return B_OK if the expression is valid, B_NO_INIT otherwise.
 */
status_t
BPackageResolvableExpression::InitCheck() const
{
	if (fName.Length() == 0)
		return B_NO_INIT;

	// either both or none of operator and version must be set
	if ((fOperator >= 0 && fOperator < B_PACKAGE_RESOLVABLE_OP_ENUM_COUNT)
		!= (fVersion.InitCheck() == B_OK))
		return B_NO_INIT;

	return B_OK;
}


/**
 * @brief Return the resolvable name.
 *
 * @return Reference to the lower-case name string.
 */
const BString&
BPackageResolvableExpression::Name() const
{
	return fName;
}


/**
 * @brief Return the comparison operator.
 *
 * @return The operator enum value; equals B_PACKAGE_RESOLVABLE_OP_ENUM_COUNT
 *         when no version constraint is set.
 */
BPackageResolvableOperator
BPackageResolvableExpression::Operator() const
{
	return fOperator;
}


/**
 * @brief Return the version operand of the expression.
 *
 * @return Reference to the version object.
 */
const BPackageVersion&
BPackageResolvableExpression::Version() const
{
	return fVersion;
}


/**
 * @brief Serialise the expression to a human-readable string.
 *
 * The version and operator are omitted when no version constraint is set.
 *
 * @return The formatted expression string.
 */
BString
BPackageResolvableExpression::ToString() const
{
	BString string = fName;

	if (fVersion.InitCheck() == B_OK)
		string << kOperatorNames[fOperator] << fVersion.ToString();

	return string;
}


/**
 * @brief Parse an expression string and replace the current state.
 *
 * @param expressionString  The string to parse.
 * @return B_OK on success, or an error code if parsing fails.
 */
status_t
BPackageResolvableExpression::SetTo(const BString& expressionString)
{
	fName.Truncate(0);
	fOperator = B_PACKAGE_RESOLVABLE_OP_ENUM_COUNT;
	fVersion.Clear();

	return BPackageInfo::ParseResolvableExpressionString(expressionString,
		*this);
}


/**
 * @brief Set the fields of this expression directly.
 *
 * The name is converted to lower-case.
 *
 * @param name       The resolvable name.
 * @param _operator  The comparison operator.
 * @param version    The version operand.
 */
void
BPackageResolvableExpression::SetTo(const BString& name,
	BPackageResolvableOperator _operator, const BPackageVersion& version)
{
	fName = name;
	fOperator = _operator;
	fVersion = version;

	fName.ToLower();
}


/**
 * @brief Reset all fields to their default (empty) state.
 */
void
BPackageResolvableExpression::Clear()
{
	fName.Truncate(0);
	fOperator = B_PACKAGE_RESOLVABLE_OP_ENUM_COUNT;
	fVersion.Clear();
}


/**
 * @brief Test whether a pair of versions satisfies this expression.
 *
 * When no version constraint is set, the expression always matches. The
 * compatible-version check implements the "compatible range" semantics: a
 * provide of version V that is compatible down to C matches a requirement of
 * version R when fVersion is within [C, V].
 *
 * @param version            The exact version of the candidate resolvable.
 * @param compatibleVersion  The minimum version still compatible with @a version.
 * @return True if the candidate satisfies the expression.
 */
bool
BPackageResolvableExpression::Matches(const BPackageVersion& version,
	const BPackageVersion& compatibleVersion) const
{
	// If no particular version is required, we always match.
	if (fVersion.InitCheck() != B_OK)
		return true;

	if (version.InitCheck() != B_OK)
		return false;

	int compare = version.Compare(fVersion);
	bool matches = false;
	switch (fOperator) {
		case B_PACKAGE_RESOLVABLE_OP_LESS:
			matches = compare < 0;
			break;
		case B_PACKAGE_RESOLVABLE_OP_LESS_EQUAL:
			matches = compare <= 0;
			break;
		case B_PACKAGE_RESOLVABLE_OP_EQUAL:
			matches = compare == 0;
			break;
		case B_PACKAGE_RESOLVABLE_OP_NOT_EQUAL:
			matches = compare != 0;
			break;
		case B_PACKAGE_RESOLVABLE_OP_GREATER_EQUAL:
			matches = compare >= 0;
			break;
		case B_PACKAGE_RESOLVABLE_OP_GREATER:
			matches = compare > 0;
			break;
		default:
			break;
	}
	if (!matches)
		return false;

	// Check compatible version. If not specified, the match must be exact.
	// Otherwise fVersion must be >= compatibleVersion.
	if (compatibleVersion.InitCheck() != B_OK)
		return compare == 0;

	// Since compatibleVersion <= version, we can save the comparison, if
	// version <= fVersion.
	return compare <= 0 || compatibleVersion.Compare(fVersion) <= 0;
}


/**
 * @brief Test whether a BPackageResolvable satisfies this expression.
 *
 * Both the name and the version/compatible-version pair must match.
 *
 * @param provides  The resolvable to test.
 * @return True if the resolvable satisfies this expression.
 */
bool
BPackageResolvableExpression::Matches(const BPackageResolvable& provides) const
{
	if (provides.Name() != fName)
		return false;

	return Matches(provides.Version(), provides.CompatibleVersion());
}


}	// namespace BPackageKit
