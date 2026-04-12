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
 *   Copyright 2022 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Niels Sascha Reedijk, niels.reedijk@gmail.com
 */


/**
 * @file HttpFields.cpp
 * @brief Implementation of BHttpFields and its nested FieldName and Field types.
 *
 * BHttpFields stores an ordered list of HTTP header fields (name–value pairs)
 * with case-insensitive name lookup.  BHttpFields::Field owns the raw
 * "Name: value" string so that interior string_views remain stable through
 * moves and copies of the container.
 *
 * @see BHttpRequest, HttpParser
 */


#include <HttpFields.h>

#include <algorithm>
#include <ctype.h>
#include <utility>

#include "HttpPrivate.h"

using namespace BPrivate::Network;


// #pragma mark -- utilities


/**
 * @brief Validate that a string contains only RFC 7230 legal header-value characters.
 *
 * Legal characters are HTAB, SP (32), visible ASCII (33–126), and any byte
 * with the sign bit set (value < 0 for signed char).
 *
 * @param string  The string_view to validate.
 * @return true if every character is valid; false if any control character is found.
 */
static inline bool
validate_value_string(const std::string_view& string)
{
	for (auto it = string.cbegin(); it < string.cend(); it++) {
		if ((*it >= 0 && *it < 32) || *it == 127 || *it == '\t')
			return false;
	}
	return true;
}


/**
 * @brief Case-insensitively compare two string_views for equality.
 *
 * @param a  First string to compare.
 * @param b  Second string to compare.
 * @return true if the strings are equal ignoring case.
 */
static inline bool
iequals(const std::string_view& a, const std::string_view& b)
{
	return std::equal(a.begin(), a.end(), b.begin(), b.end(),
		[](char a, char b) { return tolower(a) == tolower(b); });
}


/**
 * @brief Trim leading and trailing ASCII whitespace from a string_view.
 *
 * @param in  The string_view to trim.
 * @return A new string_view over the trimmed range, or an empty view if all whitespace.
 */
static inline std::string_view
trim(std::string_view in)
{
	auto left = in.begin();
	for (;; ++left) {
		if (left == in.end())
			return std::string_view();
		if (!isspace(*left))
			break;
	}

	auto right = in.end() - 1;
	for (; right > left && isspace(*right); --right)
		;

	return std::string_view(left, std::distance(left, right) + 1);
}


// #pragma mark -- BHttpFields::InvalidHeader


/**
 * @brief Construct an InvalidInput exception for a malformed HTTP field.
 *
 * @param origin  Null-terminated origin identifier string.
 * @param input   The field string that failed validation.
 */
BHttpFields::InvalidInput::InvalidInput(const char* origin, BString input)
	:
	BError(origin),
	input(std::move(input))
{
}


/**
 * @brief Return a human-readable description of the validation failure.
 *
 * @return "Invalid format or unsupported characters in input".
 */
const char*
BHttpFields::InvalidInput::Message() const noexcept
{
	return "Invalid format or unsupported characters in input";
}


/**
 * @brief Build a debug message appending the invalid input string.
 *
 * @return BString with origin, message, and the offending input on separate lines.
 */
BString
BHttpFields::InvalidInput::DebugMessage() const
{
	BString output = BError::DebugMessage();
	output << "\t " << input << "\n";
	return output;
}


// #pragma mark -- BHttpFields::Name


/**
 * @brief Construct a default (empty) FieldName.
 */
BHttpFields::FieldName::FieldName() noexcept
	:
	fName(std::string_view())
{
}


/**
 * @brief Construct a FieldName wrapping the given string_view.
 *
 * @param name  The string_view to wrap; must remain valid for the lifetime of this object.
 */
BHttpFields::FieldName::FieldName(const std::string_view& name) noexcept
	:
	fName(name)
{
}


/**
 * @brief Copy constructor.
 *
 * @param other  Source FieldName to copy.
 */
BHttpFields::FieldName::FieldName(const FieldName& other) noexcept = default;


/**
 * @brief Move constructor — leaves the source in an empty state.
 *
 * @param other  Source FieldName to move; its fName is set to an empty string_view.
 */
BHttpFields::FieldName::FieldName(FieldName&& other) noexcept
	:
	fName(std::move(other.fName))
{
	other.fName = std::string_view();
}


/**
 * @brief Copy assignment operator.
 *
 * @param other  Source FieldName to copy.
 * @return Reference to this object.
 */
BHttpFields::FieldName& BHttpFields::FieldName::operator=(
	const BHttpFields::FieldName& other) noexcept = default;


/**
 * @brief Move assignment operator — leaves the source in an empty state.
 *
 * @param other  Source FieldName to move.
 * @return Reference to this object.
 */
BHttpFields::FieldName&
BHttpFields::FieldName::operator=(BHttpFields::FieldName&& other) noexcept
{
	fName = std::move(other.fName);
	other.fName = std::string_view();
	return *this;
}


/**
 * @brief Case-insensitive equality comparison with a BString.
 *
 * @param other  BString to compare against.
 * @return true if the names are equal ignoring case.
 */
bool
BHttpFields::FieldName::operator==(const BString& other) const noexcept
{
	return iequals(fName, std::string_view(other.String()));
}


/**
 * @brief Case-insensitive equality comparison with a string_view.
 *
 * @param other  string_view to compare against.
 * @return true if the names are equal ignoring case.
 */
bool
BHttpFields::FieldName::operator==(const std::string_view& other) const noexcept
{
	return iequals(fName, other);
}


/**
 * @brief Case-insensitive equality comparison with another FieldName.
 *
 * @param other  FieldName to compare against.
 * @return true if the names are equal ignoring case.
 */
bool
BHttpFields::FieldName::operator==(const BHttpFields::FieldName& other) const noexcept
{
	return iequals(fName, other.fName);
}


/**
 * @brief Implicit conversion to string_view.
 *
 * @return The wrapped string_view.
 */
BHttpFields::FieldName::operator std::string_view() const
{
	return fName;
}


// #pragma mark -- BHttpFields::Field


/**
 * @brief Construct a default (empty) Field.
 */
BHttpFields::Field::Field() noexcept
	:
	fName(std::string_view()),
	fValue(std::string_view())
{
}


/**
 * @brief Construct a Field from separate name and value string_views.
 *
 * Builds the canonical "name: value" raw string and stores interior
 * string_views pointing into it.
 *
 * @param name   HTTP field name; must pass RFC 7230 token validation.
 * @param value  HTTP field value; must contain only legal characters.
 */
BHttpFields::Field::Field(const std::string_view& name, const std::string_view& value)
{
	if (name.length() == 0 || !validate_http_token_string(name))
		throw BHttpFields::InvalidInput(__PRETTY_FUNCTION__, BString(name.data(), name.size()));
	if (value.length() == 0 || !validate_value_string(value))
		throw BHttpFields::InvalidInput(__PRETTY_FUNCTION__, BString(value.data(), value.length()));

	BString rawField(name.data(), name.size());
	rawField << ": ";
	rawField.Append(value.data(), value.size());

	fName = std::string_view(rawField.String(), name.size());
	fValue = std::string_view(rawField.String() + name.size() + 2, value.size());
	fRawField = std::move(rawField);
}


/**
 * @brief Construct a Field by parsing a pre-formatted "name: value" BString.
 *
 * Takes ownership of \a field and stores interior string_views into it.
 *
 * @param field  BString in "name: value" format; modified (moved from) on success.
 */
BHttpFields::Field::Field(BString& field)
{
	// Check if the input contains a key, a separator and a value.
	auto separatorIndex = field.FindFirst(':');
	if (separatorIndex <= 0)
		throw BHttpFields::InvalidInput(__PRETTY_FUNCTION__, field);

	// Get the name and the value. Remove whitespace around the value.
	auto name = std::string_view(field.String(), separatorIndex);
	auto value = trim(std::string_view(field.String() + separatorIndex + 1));

	if (name.length() == 0 || !validate_http_token_string(name))
		throw BHttpFields::InvalidInput(__PRETTY_FUNCTION__, BString(name.data(), name.size()));
	if (value.length() == 0 || !validate_value_string(value))
		throw BHttpFields::InvalidInput(__PRETTY_FUNCTION__, BString(value.data(), value.length()));

	fRawField = std::move(field);
	fName = name;
	fValue = value;
}


/**
 * @brief Copy constructor — re-establishes interior string_views into the copied raw string.
 *
 * @param other  Source Field to copy.
 */
BHttpFields::Field::Field(const BHttpFields::Field& other)
	:
	fName(std::string_view()),
	fValue(std::string_view())
{
	if (other.IsEmpty()) {
		fRawField = BString();
		fName = std::string_view();
		fValue = std::string_view();
	} else {
		fRawField = other.fRawField;
		auto nameSize = other.Name().fName.size();
		auto valueOffset = other.fValue.data() - other.fRawField.value().String();
		fName = std::string_view((*fRawField).String(), nameSize);
		fValue = std::string_view((*fRawField).String() + valueOffset, other.fValue.size());
	}
}


/**
 * @brief Move constructor — transfers the raw string and adjusts string_views.
 *
 * @param other  Source Field to move; its string_views are cleared.
 */
BHttpFields::Field::Field(BHttpFields::Field&& other) noexcept
	:
	fRawField(std::move(other.fRawField)),
	fName(std::move(other.fName)),
	fValue(std::move(other.fValue))
{
	other.fName.fName = std::string_view();
	other.fValue = std::string_view();
}


/**
 * @brief Copy assignment operator — re-establishes interior string_views.
 *
 * @param other  Source Field to copy.
 * @return Reference to this object.
 */
BHttpFields::Field&
BHttpFields::Field::operator=(const BHttpFields::Field& other)
{
	if (other.IsEmpty()) {
		fRawField = BString();
		fName = std::string_view();
		fValue = std::string_view();
	} else {
		fRawField = other.fRawField;
		auto nameSize = other.Name().fName.size();
		auto valueOffset = other.fValue.data() - other.fRawField.value().String();
		fName = std::string_view((*fRawField).String(), nameSize);
		fValue = std::string_view((*fRawField).String() + valueOffset, other.fValue.size());
	}
	return *this;
}


/**
 * @brief Move assignment operator — transfers the raw string and clears the source.
 *
 * @param other  Source Field to move.
 * @return Reference to this object.
 */
BHttpFields::Field&
BHttpFields::Field::operator=(BHttpFields::Field&& other) noexcept
{
	fRawField = std::move(other.fRawField);
	fName = std::move(other.fName);
	other.fName.fName = std::string_view();
	fValue = std::move(other.fValue);
	fValue = std::string_view();
	return *this;
}


/**
 * @brief Return a const reference to the field name wrapper.
 *
 * @return Const reference to the FieldName for case-insensitive comparisons.
 */
const BHttpFields::FieldName&
BHttpFields::Field::Name() const noexcept
{
	return fName;
}


/**
 * @brief Return the field value as a string_view.
 *
 * @return string_view pointing into the raw field string.
 */
std::string_view
BHttpFields::Field::Value() const noexcept
{
	return fValue;
}


/**
 * @brief Return the full "name: value" raw field string as a string_view.
 *
 * @return string_view over the raw field, or an empty view if this Field is empty.
 */
std::string_view
BHttpFields::Field::RawField() const noexcept
{
	if (fRawField)
		return std::string_view((*fRawField).String(), (*fRawField).Length());
	else
		return std::string_view();
}


/**
 * @brief Return whether this Field holds no data.
 *
 * @return true if the raw field string is absent (default-constructed or moved-from).
 */
bool
BHttpFields::Field::IsEmpty() const noexcept
{
	// The object is either fully empty, or it has data, so we only have to check fValue.
	return !fRawField.has_value();
}


// #pragma mark -- BHttpFields


/**
 * @brief Construct an empty BHttpFields container.
 */
BHttpFields::BHttpFields()
{
}


/**
 * @brief Construct a BHttpFields container pre-populated from an initialiser list.
 *
 * @param fields  Initialiser list of Field objects to add.
 */
BHttpFields::BHttpFields(std::initializer_list<BHttpFields::Field> fields)
{
	AddFields(fields);
}


/**
 * @brief Copy constructor.
 *
 * @param other  Source BHttpFields to copy.
 */
BHttpFields::BHttpFields(const BHttpFields& other) = default;


/**
 * @brief Move constructor — leaves the source with an empty field list.
 *
 * @param other  Source BHttpFields to move.
 */
BHttpFields::BHttpFields(BHttpFields&& other)
	:
	fFields(std::move(other.fFields))
{
	// Explicitly clear the other list, as the C++ standard does not specify that the other list
	// will be empty.
	other.fFields.clear();
}


/**
 * @brief Destructor.
 */
BHttpFields::~BHttpFields() noexcept
{
}


/**
 * @brief Copy assignment operator.
 *
 * @param other  Source BHttpFields to copy.
 * @return Reference to this object.
 */
BHttpFields& BHttpFields::operator=(const BHttpFields& other) = default;


/**
 * @brief Move assignment operator — leaves the source with an empty field list.
 *
 * @param other  Source BHttpFields to move.
 * @return Reference to this object.
 */
BHttpFields&
BHttpFields::operator=(BHttpFields&& other) noexcept
{
	fFields = std::move(other.fFields);

	// Explicitly clear the other list, as the C++ standard does not specify that the other list
	// will be empty.
	other.fFields.clear();
	return *this;
}


/**
 * @brief Return the field at \a index by position.
 *
 * @param index  Zero-based position in the field list.
 * @return Const reference to the Field at that position.
 */
const BHttpFields::Field&
BHttpFields::operator[](size_t index) const
{
	if (index >= fFields.size())
		throw BRuntimeError(__PRETTY_FUNCTION__, "Index out of bounds");
	auto it = fFields.cbegin();
	std::advance(it, index);
	return *it;
}


/**
 * @brief Append a field specified by separate name and value string_views.
 *
 * @param name   Field name string_view.
 * @param value  Field value string_view.
 */
void
BHttpFields::AddField(const std::string_view& name, const std::string_view& value)
{
	fFields.emplace_back(name, value);
}


/**
 * @brief Append a field parsed from a pre-formatted "name: value" BString.
 *
 * @param field  BString in "name: value" format.
 */
void
BHttpFields::AddField(BString& field)
{
	fFields.emplace_back(field);
}


/**
 * @brief Append multiple fields from an initialiser list, skipping empty entries.
 *
 * @param fields  Initialiser list of Field objects.
 */
void
BHttpFields::AddFields(std::initializer_list<Field> fields)
{
	for (auto& field: fields) {
		if (!field.IsEmpty())
			fFields.push_back(std::move(field));
	}
}


/**
 * @brief Remove all fields with the given name (case-insensitive).
 *
 * @param name  Field name to remove.
 */
void
BHttpFields::RemoveField(const std::string_view& name) noexcept
{
	for (auto it = FindField(name); it != end(); it = FindField(name)) {
		fFields.erase(it);
	}
}


/**
 * @brief Remove the field pointed to by \a it.
 *
 * @param it  Valid iterator into this container.
 */
void
BHttpFields::RemoveField(ConstIterator it) noexcept
{
	fFields.erase(it);
}


/**
 * @brief Remove all fields from the container.
 */
void
BHttpFields::MakeEmpty() noexcept
{
	fFields.clear();
}


/**
 * @brief Find the first field with the given name (case-insensitive).
 *
 * @param name  Field name to search for.
 * @return Iterator to the first matching field, or end() if not found.
 */
BHttpFields::ConstIterator
BHttpFields::FindField(const std::string_view& name) const noexcept
{
	for (auto it = fFields.cbegin(); it != fFields.cend(); it++) {
		if ((*it).Name() == name)
			return it;
	}
	return fFields.cend();
}


/**
 * @brief Return the total number of fields in the container.
 *
 * @return Field count.
 */
size_t
BHttpFields::CountFields() const noexcept
{
	return fFields.size();
}


/**
 * @brief Count the number of fields with a given name (case-insensitive).
 *
 * @param name  Field name to count.
 * @return Number of fields matching \a name.
 */
size_t
BHttpFields::CountFields(const std::string_view& name) const noexcept
{
	size_t count = 0;
	for (auto it = fFields.cbegin(); it != fFields.cend(); it++) {
		if ((*it).Name() == name)
			count += 1;
	}
	return count;
}


/**
 * @brief Return a const iterator to the first field.
 *
 * @return ConstIterator pointing to the first element.
 */
BHttpFields::ConstIterator
BHttpFields::begin() const noexcept
{
	return fFields.cbegin();
}


/**
 * @brief Return a const iterator past the last field.
 *
 * @return ConstIterator representing the end sentinel.
 */
BHttpFields::ConstIterator
BHttpFields::end() const noexcept
{
	return fFields.cend();
}
