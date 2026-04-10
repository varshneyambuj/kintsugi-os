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
 *   Copyright 2007-2013, Haiku, Inc. All Rights Reserved.
 *   Authors: Ingo Weinhold <ingo_weinhold@gmx.de>
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DriverSettings.cpp
 * @brief Implementation of BDriverSettings, BDriverParameter, and
 *        BDriverParameterIterator for reading kernel driver settings files.
 *
 * These classes wrap the low-level driver_settings API to provide a
 * convenient C++ interface for loading, parsing, and querying hierarchical
 * key-value driver configuration data. BDriverSettings loads a settings
 * file (or parses a string), while BDriverParameter represents individual
 * parameter nodes that may themselves contain child parameters.
 *
 * @see BDriverSettings
 */

#include <DriverSettings.h>

#include <stdlib.h>
#include <string.h>

#include <new>

#include <driver_settings.h>
#include <Path.h>
#include <String.h>

#include <Referenceable.h>


// The parameter values that shall be evaluated to true.
static const char* const kTrueValueStrings[]
	= { "1", "true", "yes", "on", "enable", "enabled" };
static const int32 kTrueValueStringCount
	= sizeof(kTrueValueStrings) / sizeof(const char*);


namespace BPrivate {


// #pragma mark - BDriverParameterIterator


class BDriverParameterIterator::Delegate : public BReferenceable {
public:
								Delegate() : BReferenceable() {}
	virtual						~Delegate() {}

	virtual	Delegate*			Clone() const = 0;

	virtual	bool				HasNext() const = 0;
	virtual	BDriverParameter	Next() = 0;
};


/**
 * @brief Default constructor; creates an empty (exhausted) iterator.
 */
BDriverParameterIterator::BDriverParameterIterator()
	:
	fDelegate(NULL)
{
}


/**
 * @brief Constructs an iterator backed by the supplied delegate.
 *
 * @param delegate Heap-allocated delegate; the iterator takes a reference.
 */
BDriverParameterIterator::BDriverParameterIterator(Delegate* delegate)
	:
	fDelegate(delegate)
{
}


/**
 * @brief Copy constructor; shares or clones the delegate as needed.
 *
 * @param other The iterator to copy.
 */
BDriverParameterIterator::BDriverParameterIterator(
	const BDriverParameterIterator& other)
	:
	fDelegate(NULL)
{
	_SetTo(other.fDelegate, true);
}


/**
 * @brief Destructor. Releases the reference to the delegate.
 */
BDriverParameterIterator::~BDriverParameterIterator()
{
	_SetTo(NULL, false);
}


/**
 * @brief Returns whether there are more parameters to iterate over.
 *
 * @return true if at least one more parameter is available, false otherwise.
 */
bool
BDriverParameterIterator::HasNext() const
{
	return fDelegate != NULL ? fDelegate->HasNext() : false;
}


/**
 * @brief Returns the next BDriverParameter and advances the iterator.
 *
 * If the delegate is shared, it is cloned before advancing to preserve
 * copy-on-write semantics.
 *
 * @return The next BDriverParameter, or a default-constructed (invalid)
 *         BDriverParameter if the iterator is exhausted or an error occurs.
 */
BDriverParameter
BDriverParameterIterator::Next()
{
	if (fDelegate == NULL)
		return BDriverParameter();

	if (fDelegate->CountReferences() > 1) {
		Delegate* clone = fDelegate->Clone();
		if (clone == NULL)
			return BDriverParameter();
		_SetTo(clone, false);
	}

	return fDelegate->Next();
}


/**
 * @brief Assignment operator; shares the delegate from the right-hand side.
 *
 * @param other The iterator to assign from.
 * @return Reference to this iterator.
 */
BDriverParameterIterator&
BDriverParameterIterator::operator=(const BDriverParameterIterator& other)
{
	_SetTo(other.fDelegate, true);
	return *this;
}


/**
 * @brief Internal helper to swap the held delegate reference.
 *
 * @param delegate      New delegate to adopt (may be NULL).
 * @param addReference  If true, acquire a reference on the new delegate.
 */
void
BDriverParameterIterator::_SetTo(Delegate* delegate, bool addReference)
{
	if (fDelegate != NULL)
		fDelegate->ReleaseReference();
	fDelegate = delegate;
	if (fDelegate != NULL && addReference)
		fDelegate->AcquireReference();
}


// #pragma mark - BDriverParameterContainer


class BDriverParameterContainer::Iterator
	: public BDriverParameterIterator::Delegate {
public:
	Iterator(const driver_parameter* parameters, int32 count)
		:
		Delegate(),
		fParameters(parameters),
		fCount(count)
	{
	}

	virtual ~Iterator()
	{
	}

	virtual Delegate* Clone() const
	{
		return new(std::nothrow) Iterator(fParameters, fCount);
	}

	virtual bool HasNext() const
	{
		return fParameters != NULL && fCount > 0;
	}

	virtual	BDriverParameter Next()
	{
		if (fParameters == NULL || fCount <= 0)
			return BDriverParameter();

		fCount--;
		return BDriverParameter(fParameters++);
	}

private:
	const driver_parameter*	fParameters;
	int32					fCount;
};


class BDriverParameterContainer::NameIterator
	: public BDriverParameterIterator::Delegate {
public:
	NameIterator(const driver_parameter* parameters, int32 count,
		const BString& name)
		:
		Delegate(),
		fParameters(parameters),
		fCount(count),
		fName(name)
	{
		_FindNext(false);
	}

	virtual ~NameIterator()
	{
	}

	virtual Delegate* Clone() const
	{
		return new(std::nothrow) NameIterator(fParameters, fCount, fName);
	}

	virtual bool HasNext() const
	{
		return fParameters != NULL && fCount > 0;
	}

	virtual	BDriverParameter Next()
	{
		if (fParameters == NULL || fCount <= 0)
			return BDriverParameter();

		const driver_parameter* parameter = fParameters;
		_FindNext(true);
		return BDriverParameter(parameter);
	}

private:
	void _FindNext(bool skipCurrent)
	{
		if (fParameters == NULL || fCount < 1)
			return;
		if (skipCurrent) {
			fParameters++;
			fCount--;
		}
		while (fCount > 0 && fName != fParameters->name) {
			fParameters++;
			fCount--;
		}
	}

private:
	const driver_parameter*	fParameters;
	int32					fCount;
	BString					fName;
};


/**
 * @brief Default constructor.
 */
BDriverParameterContainer::BDriverParameterContainer()
{
}


/**
 * @brief Destructor.
 */
BDriverParameterContainer::~BDriverParameterContainer()
{
}


/**
 * @brief Returns the number of direct child parameters in this container.
 *
 * @return The parameter count, or 0 if the container is uninitialised.
 */
int32
BDriverParameterContainer::CountParameters() const
{
	int32 count;
	return GetParametersAndCount(count) != NULL ? count : 0;

}


/**
 * @brief Returns a pointer to the raw driver_parameter array.
 *
 * @return Pointer to the first parameter, or NULL if unavailable.
 */
const driver_parameter*
BDriverParameterContainer::Parameters() const
{
	int32 count;
	return GetParametersAndCount(count);
}


/**
 * @brief Returns the parameter at the given index.
 *
 * @param index Zero-based index into the parameter list.
 * @return The BDriverParameter at that index, or an invalid one if out of
 *         range.
 */
BDriverParameter
BDriverParameterContainer::ParameterAt(int32 index) const
{
	int32 count;
	const driver_parameter* parameters = GetParametersAndCount(count);
	if (parameters == NULL || index < 0 || index >= count)
		return BDriverParameter();

	return BDriverParameter(parameters + index);
}


/**
 * @brief Finds the first parameter with the given name.
 *
 * @param name       Name to search for.
 * @param _parameter If non-NULL and a match is found, filled with the result.
 * @return true if a matching parameter was found, false otherwise.
 */
bool
BDriverParameterContainer::FindParameter(const char* name,
	BDriverParameter* _parameter) const
{
	if (name == NULL)
		return false;

	int32 count;
	if (const driver_parameter* parameters = GetParametersAndCount(count)) {
		for (int32 i = 0; i < count; i++) {
			if (strcmp(name, parameters[i].name) == 0) {
				if (_parameter != NULL)
					_parameter->SetTo(parameters + i);
				return true;
			}
		}
	}
	return false;
}


/**
 * @brief Returns the first parameter with the given name.
 *
 * @param name Name to search for.
 * @return The matching BDriverParameter, or an invalid one if not found.
 */
BDriverParameter
BDriverParameterContainer::GetParameter(const char* name) const
{
	BDriverParameter parameter;
	FindParameter(name, &parameter);
	return parameter;
}


/**
 * @brief Returns an iterator over all direct child parameters.
 *
 * @return A BDriverParameterIterator positioned at the first parameter.
 */
BDriverParameterIterator
BDriverParameterContainer::ParameterIterator() const
{
	int32 count;
	if (const driver_parameter* parameters = GetParametersAndCount(count)) {
		if (Iterator* iterator = new(std::nothrow) Iterator(parameters, count))
			return BDriverParameterIterator(iterator);
	}
	return BDriverParameterIterator();
}


/**
 * @brief Returns an iterator over all direct child parameters with the given
 *        name.
 *
 * @param name The parameter name to filter by.
 * @return A BDriverParameterIterator positioned at the first matching
 *         parameter.
 */
BDriverParameterIterator
BDriverParameterContainer::ParameterIterator(const char* name) const
{
	int32 count;
	if (const driver_parameter* parameters = GetParametersAndCount(count)) {
		NameIterator* iterator
			= new(std::nothrow) NameIterator(parameters, count, name);
		if (iterator != NULL)
			return BDriverParameterIterator(iterator);
	}
	return BDriverParameterIterator();
}


/**
 * @brief Returns the string value of the first parameter with the given name.
 *
 * @param name         Name of the parameter to query.
 * @param unknownValue Value to return if the parameter does not exist.
 * @param noValue      Value to return if the parameter exists but has no value.
 * @return The parameter's first string value, or one of the fallback strings.
 */
const char*
BDriverParameterContainer::GetParameterValue(const char* name,
	const char* unknownValue, const char* noValue) const
{
	BDriverParameter parameter;
	if (!FindParameter(name, &parameter))
		return unknownValue;
	return parameter.ValueAt(0, noValue);
}


/**
 * @brief Returns the boolean value of the first parameter with the given name.
 *
 * @param name         Name of the parameter to query.
 * @param unknownValue Value to return if the parameter does not exist.
 * @param noValue      Value to return if the parameter exists but has no value.
 * @return The boolean interpretation of the parameter's first value.
 */
bool
BDriverParameterContainer::GetBoolParameterValue(const char* name,
	bool unknownValue, bool noValue) const
{
	BDriverParameter parameter;
	if (!FindParameter(name, &parameter))
		return unknownValue;
	return parameter.BoolValueAt(0, noValue);
}


/**
 * @brief Returns the int32 value of the first parameter with the given name.
 *
 * @param name         Name of the parameter to query.
 * @param unknownValue Value to return if the parameter does not exist.
 * @param noValue      Value to return if the parameter exists but has no value.
 * @return The int32 interpretation of the parameter's first value.
 */
int32
BDriverParameterContainer::GetInt32ParameterValue(const char* name,
	int32 unknownValue, int32 noValue) const
{
	BDriverParameter parameter;
	if (!FindParameter(name, &parameter))
		return unknownValue;
	return parameter.Int32ValueAt(0, noValue);
}


/**
 * @brief Returns the int64 value of the first parameter with the given name.
 *
 * @param name         Name of the parameter to query.
 * @param unknownValue Value to return if the parameter does not exist.
 * @param noValue      Value to return if the parameter exists but has no value.
 * @return The int64 interpretation of the parameter's first value.
 */
int64
BDriverParameterContainer::GetInt64ParameterValue(const char* name,
	int64 unknownValue, int64 noValue) const
{
	BDriverParameter parameter;
	if (!FindParameter(name, &parameter))
		return unknownValue;
	return parameter.Int64ValueAt(0, noValue);
}


// #pragma mark - BDriverSettings


/**
 * @brief Constructs an empty, uninitialised BDriverSettings object.
 */
BDriverSettings::BDriverSettings()
	:
	BDriverParameterContainer(),
	fSettingsHandle(NULL),
	fSettings(NULL)
{
}


/**
 * @brief Destructor. Calls Unset() to release the settings handle.
 */
BDriverSettings::~BDriverSettings()
{
	Unset();
}


/**
 * @brief Loads driver settings by driver name or absolute file path.
 *
 * Any previously loaded settings are released before the new ones are loaded.
 *
 * @param driverNameOrAbsolutePath Driver name (searched in standard locations)
 *                                 or an absolute path to a settings file.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the file is not found, or
 *         B_ERROR on other failures.
 */
status_t
BDriverSettings::Load(const char* driverNameOrAbsolutePath)
{
	Unset();

	fSettingsHandle = load_driver_settings(driverNameOrAbsolutePath);
	if (fSettingsHandle == NULL)
		return B_ENTRY_NOT_FOUND;

	fSettings = get_driver_settings(fSettingsHandle);
	if (fSettings == NULL) {
		Unset();
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Loads driver settings from an entry_ref.
 *
 * Resolves the ref to a path and delegates to Load(const char*).
 *
 * @param ref entry_ref identifying the settings file to load.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BDriverSettings::Load(const entry_ref& ref)
{
	Unset();

	BPath path;
	status_t error = path.SetTo(&ref);
	return error == B_OK ? Load(path.Path()) : error;
}


/**
 * @brief Parses driver settings from an in-memory string.
 *
 * Any previously loaded settings are released before parsing the new ones.
 *
 * @param string Null-terminated string containing the settings data.
 * @return B_OK on success, B_BAD_DATA if parsing fails, or B_ERROR on other
 *         failures.
 */
status_t
BDriverSettings::SetToString(const char* string)
{
	Unset();

	fSettingsHandle = parse_driver_settings_string(string);
	if (fSettingsHandle == NULL)
		return B_BAD_DATA;

	fSettings = get_driver_settings(fSettingsHandle);
	if (fSettings == NULL) {
		Unset();
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Releases the currently loaded settings handle and resets all fields.
 */
void
BDriverSettings::Unset()
{
	if (fSettingsHandle != NULL)
		unload_driver_settings(fSettingsHandle);

	fSettingsHandle = NULL;
	fSettings = NULL;
}


/**
 * @brief Returns the raw parameter array and its count for this settings node.
 *
 * @param _count Output parameter that receives the number of parameters.
 * @return Pointer to the first driver_parameter, or NULL if uninitialised.
 */
const driver_parameter*
BDriverSettings::GetParametersAndCount(int32& _count) const
{
	if (fSettings == NULL)
		return NULL;

	_count = fSettings->parameter_count;
	return fSettings->parameters;
}


// #pragma mark - BDriverParameter


/**
 * @brief Default constructor; creates an invalid (unset) parameter.
 */
BDriverParameter::BDriverParameter()
	:
	BDriverParameterContainer(),
	fParameter(NULL)
{
}


/**
 * @brief Constructs a BDriverParameter wrapping the given driver_parameter.
 *
 * @param parameter Pointer to the underlying driver_parameter structure.
 *                  The caller retains ownership.
 */
BDriverParameter::BDriverParameter(const driver_parameter* parameter)
	:
	BDriverParameterContainer(),
	fParameter(parameter)
{
}


/**
 * @brief Copy constructor.
 *
 * @param other The BDriverParameter to copy.
 */
BDriverParameter::BDriverParameter(const BDriverParameter& other)
	:
	BDriverParameterContainer(),
	fParameter(other.fParameter)
{
}


/**
 * @brief Destructor.
 */
BDriverParameter::~BDriverParameter()
{
}


/**
 * @brief Points this parameter at a different driver_parameter structure.
 *
 * @param parameter Pointer to the new driver_parameter (may be NULL).
 */
void
BDriverParameter::SetTo(const driver_parameter* parameter)
{
	fParameter = parameter;
}


/**
 * @brief Returns whether this parameter wraps a valid driver_parameter.
 *
 * @return true if valid (non-NULL), false otherwise.
 */
bool
BDriverParameter::IsValid() const
{
	return fParameter != NULL;
}


/**
 * @brief Returns the name of this parameter.
 *
 * @return The parameter name string, or NULL if invalid.
 */
const char*
BDriverParameter::Name() const
{
	return fParameter != NULL ? fParameter->name : NULL;
}


/**
 * @brief Returns the number of values associated with this parameter.
 *
 * @return Value count, or 0 if the parameter is invalid.
 */
int32
BDriverParameter::CountValues() const
{
	return fParameter != NULL ? fParameter->value_count : 0;
}


/**
 * @brief Returns the raw array of value strings.
 *
 * @return Pointer to the value strings, or NULL if the parameter is invalid.
 */
const char* const*
BDriverParameter::Values() const
{
	return fParameter != NULL ? fParameter->values : 0;
}


/**
 * @brief Returns the value string at the given index.
 *
 * @param index   Zero-based index of the value to retrieve.
 * @param noValue Fallback value if the index is out of range.
 * @return The value string at that index, or noValue.
 */
const char*
BDriverParameter::ValueAt(int32 index, const char* noValue) const
{
	if (fParameter == NULL || index < 0 || index >= fParameter->value_count)
		return noValue;
	return fParameter->values[index];
}


/**
 * @brief Returns the boolean interpretation of the value at the given index.
 *
 * Recognised true strings: "1", "true", "yes", "on", "enable", "enabled".
 *
 * @param index   Zero-based index of the value to interpret.
 * @param noValue Fallback value if the index is out of range.
 * @return true if the value matches a known true string, false otherwise.
 */
bool
BDriverParameter::BoolValueAt(int32 index, bool noValue) const
{
	const char* value = ValueAt(index, NULL);
	if (value == NULL)
		return noValue;

	for (int32 i = 0; i < kTrueValueStringCount; i++) {
		if (strcmp(value, kTrueValueStrings[i]) == 0)
			return true;
	}
	return false;
}


/**
 * @brief Returns the int32 interpretation of the value at the given index.
 *
 * @param index   Zero-based index of the value to interpret.
 * @param noValue Fallback value if the index is out of range.
 * @return The value parsed as a long integer.
 */
int32
BDriverParameter::Int32ValueAt(int32 index, int32 noValue) const
{
	const char* value = ValueAt(index, NULL);
	if (value == NULL)
		return noValue;
	return atol(value);
}


/**
 * @brief Returns the int64 interpretation of the value at the given index.
 *
 * @param index   Zero-based index of the value to interpret.
 * @param noValue Fallback value if the index is out of range.
 * @return The value parsed as a base-10 long long integer.
 */
int64
BDriverParameter::Int64ValueAt(int32 index, int64 noValue) const
{
	const char* value = ValueAt(index, NULL);
	if (value == NULL)
		return noValue;
	return strtoll(value, NULL, 10);
}


/**
 * @brief Returns the raw child parameter array and its count for this node.
 *
 * @param _count Output parameter that receives the number of child parameters.
 * @return Pointer to the first child driver_parameter, or NULL if invalid.
 */
const driver_parameter*
BDriverParameter::GetParametersAndCount(int32& _count) const
{
	if (fParameter == NULL)
		return NULL;

	_count = fParameter->parameter_count;
	return fParameter->parameters;
}


/**
 * @brief Assignment operator.
 *
 * @param other The BDriverParameter to copy from.
 * @return Reference to this parameter.
 */
BDriverParameter&
BDriverParameter::operator=(const BDriverParameter& other)
{
	fParameter = other.fParameter;
	return *this;
}


}	// namespace BPrivate
