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
 *   Copyright 2013-2016, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Setting.cpp
 * @brief Implementations of the typed Setting hierarchy and its concrete
 *        @c *SettingImpl variants.
 *
 * Each Setting subclass identifies its @c setting_type and supplies a
 * default value. Concrete Impl classes (BoolSettingImpl, FloatSettingImpl,
 * OptionsSettingImpl, BoundedSettingImpl, RangeSettingImpl,
 * RectSettingImpl, StringSettingImpl) inherit from AbstractSetting to
 * carry the id/name pair and provide storage for the default value.
 */


#include "Setting.h"

#include <new>


// #pragma mark - Setting


/**
 * @brief Virtual destructor for the abstract Setting base class.
 */
Setting::~Setting()
{
}


// #pragma mark - BoolSetting


/**
 * @brief Returns the type tag identifying boolean settings.
 *
 * @return @c SETTING_TYPE_BOOL.
 */
setting_type
BoolSetting::Type() const
{
	return SETTING_TYPE_BOOL;
}


/**
 * @brief Returns the default value as a BVariant wrapping the bool default.
 *
 * @return BVariant carrying the result of DefaultBoolValue().
 */
BVariant
BoolSetting::DefaultValue() const
{
	return DefaultBoolValue();
}


// #pragma mark - FloatSetting


/**
 * @brief Returns the type tag identifying float settings.
 *
 * @return @c SETTING_TYPE_FLOAT.
 */
setting_type
FloatSetting::Type() const
{
	return SETTING_TYPE_FLOAT;
}


/**
 * @brief Returns the default value as a BVariant wrapping the float default.
 *
 * @return BVariant carrying the result of DefaultFloatValue().
 */
BVariant
FloatSetting::DefaultValue() const
{
	return DefaultFloatValue();
}


// #pragma mark - SettingsOption


/**
 * @brief Virtual destructor for the SettingsOption interface.
 */
SettingsOption::~SettingsOption()
{
}


// #pragma mark - OptionsSetting


/**
 * @brief Returns the type tag identifying options settings.
 *
 * @return @c SETTING_TYPE_OPTIONS.
 */
setting_type
OptionsSetting::Type() const
{
	return SETTING_TYPE_OPTIONS;
}


/**
 * @brief Returns the id of the default option as a BVariant string.
 *
 * @return BVariant referencing the default option's id, or empty when no
 *         default option is configured.
 */
BVariant
OptionsSetting::DefaultValue() const
{
	SettingsOption* option = DefaultOption();
	return option != NULL
		? BVariant(option->ID(), B_VARIANT_DONT_COPY_DATA) : BVariant();
}


// #pragma mark - BoundedSetting


/**
 * @brief Returns the type tag identifying bounded settings.
 *
 * @return @c SETTING_TYPE_BOUNDED.
 */
setting_type
BoundedSetting::Type() const
{
	return SETTING_TYPE_BOUNDED;
}


// #pragma mark - RangeSetting


/**
 * @brief Returns the type tag identifying range settings.
 *
 * @return @c SETTING_TYPE_RANGE.
 */
setting_type
RangeSetting::Type() const
{
	return SETTING_TYPE_RANGE;
}


// #pragma mark - RectSetting

/**
 * @brief Returns the type tag identifying rectangle settings.
 *
 * @return @c SETTING_TYPE_RECT.
 */
setting_type
RectSetting::Type() const
{
	return SETTING_TYPE_RECT;
}


/**
 * @brief Returns the default value as a BVariant wrapping the BRect default.
 *
 * @return BVariant carrying the result of DefaultRectValue().
 */
BVariant
RectSetting::DefaultValue() const
{
	return DefaultRectValue();
}


// #pragma mark - StringSetting


/**
 * @brief Returns the type tag identifying string settings.
 *
 * @return @c SETTING_TYPE_STRING.
 */
setting_type
StringSetting::Type() const
{
	return SETTING_TYPE_STRING;
}


/**
 * @brief Returns the default value as a BVariant wrapping the string default.
 *
 * @return BVariant referencing the default string's underlying buffer.
 */
BVariant
StringSetting::DefaultValue() const
{
	return DefaultStringValue().String();
}


// #pragma mark - AbstractSetting


/**
 * @brief Construct an AbstractSetting that carries an id/name pair.
 *
 * @param id    Stable identifier used in archives and lookups.
 * @param name  Human-readable name presented in UI.
 */
AbstractSetting::AbstractSetting(const BString& id, const BString& name)
	:
	fID(id),
	fName(name)
{
}


/**
 * @brief Returns the stable identifier.
 *
 * @return Pointer to the stored id string.
 */
const char*
AbstractSetting::ID() const
{
	return fID;
}


/**
 * @brief Returns the human-readable name.
 *
 * @return Pointer to the stored name string.
 */
const char*
AbstractSetting::Name() const
{
	return fName;
}


// #pragma mark - BoolSettingImpl


/**
 * @brief Construct a concrete bool setting.
 *
 * @param id            Stable identifier.
 * @param name          Human-readable name.
 * @param defaultValue  Default value if no override is stored.
 */
BoolSettingImpl::BoolSettingImpl(const BString& id, const BString& name,
	bool defaultValue)
	:
	AbstractSetting(id, name),
	fDefaultValue(defaultValue)
{
}


/**
 * @brief Returns the configured default value.
 *
 * @return Default bool.
 */
bool
BoolSettingImpl::DefaultBoolValue() const
{
	return fDefaultValue;
}


// #pragma mark - FloatSettingImpl


/**
 * @brief Construct a concrete float setting.
 *
 * @param id            Stable identifier.
 * @param name          Human-readable name.
 * @param defaultValue  Default value if no override is stored.
 */
FloatSettingImpl::FloatSettingImpl(const BString& id, const BString& name,
	float defaultValue)
	:
	AbstractSetting(id, name),
	fDefaultValue(defaultValue)
{
}


/**
 * @brief Returns the configured default value.
 *
 * @return Default float.
 */
float
FloatSettingImpl::DefaultFloatValue() const
{
	return fDefaultValue;
}


// #pragma mark - OptionsSettingImpl


/**
 * @brief Inner SettingsOption implementation backed by an id/name pair.
 *
 * Used by OptionsSettingImpl::AddOption(id, name) to store options the
 * caller does not provide as their own SettingsOption subclass.
 */
class OptionsSettingImpl::Option : public SettingsOption {
public:
	/**
	 * @brief Construct an option.
	 *
	 * @param id    Stable id used for lookups.
	 * @param name  Human-readable label.
	 */
	Option(const BString& id, const BString& name)
	{
	}

	/** @brief Returns the option's stable id. */
	virtual const char* ID() const
	{
		return fID;
	}

	/** @brief Returns the option's human-readable name. */
	virtual const char* Name() const
	{
		return fName;
	}

private:
	BString	fID;
	BString	fName;
};


/**
 * @brief Construct an empty OptionsSettingImpl.
 *
 * @param id    Stable identifier.
 * @param name  Human-readable name.
 */
OptionsSettingImpl::OptionsSettingImpl(const BString& id, const BString& name)
	:
	AbstractSetting(id, name),
	fDefaultOption(NULL)
{
}


/**
 * @brief Releases the default option and every registered option reference.
 */
OptionsSettingImpl::~OptionsSettingImpl()
{
	SetDefaultOption(NULL);

	for (int32 i = 0; SettingsOption* option = fOptions.ItemAt(i); i++)
		option->ReleaseReference();
}


/**
 * @brief Returns the configured default option, or the first option.
 *
 * @return Pointer to the default option, or @c NULL when no options exist.
 */
SettingsOption*
OptionsSettingImpl::DefaultOption() const
{
	return fDefaultOption != NULL ? fDefaultOption : fOptions.ItemAt(0);
}


/**
 * @brief Returns the number of registered options.
 */
int32
OptionsSettingImpl::CountOptions() const
{
	return fOptions.CountItems();
}


/**
 * @brief Returns the option at @a index.
 *
 * @param index  Zero-based index.
 * @return Pointer to the option or @c NULL when @a index is out of range.
 */
SettingsOption*
OptionsSettingImpl::OptionAt(int32 index) const
{
	return fOptions.ItemAt(index);
}


/**
 * @brief Looks up an option by its stable id.
 *
 * @param id  Identifier to match.
 * @return Matching option, or @c NULL when not found.
 */
SettingsOption*
OptionsSettingImpl::OptionByID(const char* id) const
{
	for (int32 i = 0; SettingsOption* option = fOptions.ItemAt(i); i++) {
		if (strcmp(option->ID(), id) == 0)
			return option;
	}

	return NULL;
}


/**
 * @brief Appends @a option and acquires a reference on it.
 *
 * @param option  Option to add. The list takes a reference on success.
 * @return @c true on success, @c false on allocation failure.
 */
bool
OptionsSettingImpl::AddOption(SettingsOption* option)
{
	if (!fOptions.AddItem(option))
		return false;

	option->AcquireReference();
	return true;
}


/**
 * @brief Convenience overload that allocates a default option implementation.
 *
 * @param id    Stable id for the new option.
 * @param name  Human-readable label.
 * @return @c true on success, @c false on allocation/insertion failure.
 */
bool
OptionsSettingImpl::AddOption(const BString& id, const BString& name)
{
	Option* option = new(std::nothrow) Option(id, name);
	if (option == NULL)
		return false;
	BReference<Option> optionReference(option, true);

	return AddOption(option);
}


/**
 * @brief Sets the option treated as the default for this setting.
 *
 * Releases the previous default reference, if any, and acquires a new one.
 *
 * @param option  New default, or @c NULL to clear it.
 */
void
OptionsSettingImpl::SetDefaultOption(SettingsOption* option)
{
	if (option == fDefaultOption)
		return;

	if (fDefaultOption != NULL)
		fDefaultOption->ReleaseReference();

	fDefaultOption = option;

	if (fDefaultOption != NULL)
		fDefaultOption->AcquireReference();
}


// #pragma mark - RangeSettingImpl


/**
 * @brief Construct a bounded setting with an inclusive range and a default.
 *
 * @param id            Stable identifier.
 * @param name          Human-readable name.
 * @param lowerBound    Minimum allowed value.
 * @param upperBound    Maximum allowed value.
 * @param defaultValue  Value used when no override is stored.
 */
BoundedSettingImpl::BoundedSettingImpl(const BString& id, const BString& name,
	const BVariant& lowerBound, const BVariant& upperBound,
	const BVariant& defaultValue)
	:
	AbstractSetting(id, name),
	fLowerBound(lowerBound),
	fUpperBound(upperBound),
	fDefaultValue(defaultValue)
{
}


/**
 * @brief Returns the configured default value.
 */
BVariant
BoundedSettingImpl::DefaultValue() const
{
	return fDefaultValue;
}


/**
 * @brief Returns the inclusive lower bound.
 */
BVariant
BoundedSettingImpl::LowerBound() const
{
	return fLowerBound;
}


/**
 * @brief Returns the inclusive upper bound.
 */
BVariant
BoundedSettingImpl::UpperBound() const
{
	return fUpperBound;
}


// #pragma mark - RangeSettingImpl


/**
 * @brief Construct a range setting describing a (lower, upper) value pair.
 *
 * @param id          Stable identifier.
 * @param name        Human-readable name.
 * @param lowerBound  Minimum allowed value.
 * @param upperBound  Maximum allowed value.
 * @param lowerValue  Default lower selected value.
 * @param upperValue  Default upper selected value.
 */
RangeSettingImpl::RangeSettingImpl(const BString& id, const BString& name,
	const BVariant& lowerBound, const BVariant& upperBound,
	const BVariant& lowerValue, const BVariant& upperValue)
	:
	AbstractSetting(id, name),
	fLowerBound(lowerBound),
	fUpperBound(upperBound),
	fLowerValue(lowerValue),
	fUpperValue(upperValue)
{
}


/**
 * @brief Returns an empty BVariant.
 *
 * @note A range setting represents a pair of values, which BVariant cannot
 *       readily encode in a single instance. Use LowerValue()/UpperValue()
 *       instead.
 */
BVariant
RangeSettingImpl::DefaultValue() const
{
	// this one doesn't really make sense for RangeSetting since it
	// describes a pair of values, which BVariant can't readily
	// represent.
	return BVariant();
}


/**
 * @brief Returns the inclusive lower bound.
 */
BVariant
RangeSettingImpl::LowerBound() const
{
	return fLowerBound;
}


/**
 * @brief Returns the inclusive upper bound.
 */
BVariant
RangeSettingImpl::UpperBound() const
{
	return fUpperBound;
}


/**
 * @brief Returns the default lower selected value.
 */
BVariant
RangeSettingImpl::LowerValue() const
{
	return fLowerValue;
}


/**
 * @brief Returns the default upper selected value.
 */
BVariant
RangeSettingImpl::UpperValue() const
{
	return fUpperValue;
}


// #pragma mark - RectSettingImpl


/**
 * @brief Construct a rectangle-typed setting with a default BRect.
 *
 * @param id            Stable identifier.
 * @param name          Human-readable name.
 * @param defaultValue  Default rectangle.
 */
RectSettingImpl::RectSettingImpl(const BString& id, const BString& name,
	const BRect& defaultValue)
	:
	AbstractSetting(id, name),
	fDefaultValue(defaultValue)
{
}


/**
 * @brief Returns the configured default rectangle.
 */
BRect
RectSettingImpl::DefaultRectValue() const
{
	return fDefaultValue;
}


// #pragma mark - StringSettingImpl


/**
 * @brief Construct a string-typed setting with a default BString.
 *
 * @param id            Stable identifier.
 * @param name          Human-readable name.
 * @param defaultValue  Default string.
 */
StringSettingImpl::StringSettingImpl(const BString& id, const BString& name,
	const BString& defaultValue)
	:
	AbstractSetting(id, name),
	fDefaultValue(defaultValue)
{
}


/**
 * @brief Returns the configured default string.
 *
 * @return Reference to the stored BString.
 */
const BString&
StringSettingImpl::DefaultStringValue() const
{
	return fDefaultValue;
}
