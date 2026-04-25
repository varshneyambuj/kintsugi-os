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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Settings.cpp
 * @brief Generic value store backed by a SettingsDescription.
 *
 * Settings owns a BMessage of values keyed by setting id, lookups against
 * which fall back to per-Setting defaults when no override has been stored.
 * Listeners can subscribe to value-change notifications. The store is
 * thread-safe via an internal BLocker.
 */


#include "Settings.h"

#include <AutoLocker.h>

#include "SettingsDescription.h"


// #pragma mark - Settings


/**
 * @brief Construct a Settings store backed by @a description.
 *
 * Acquires a reference on @a description so it remains valid for the
 * lifetime of the Settings object.
 *
 * @param description  Schema describing the settings recognised by this store.
 */
Settings::Settings(SettingsDescription* description)
	:
	fLock("settings"),
	fDescription(description)
{
	fDescription->AcquireReference();
}


/**
 * @brief Destructor; releases the description reference.
 */
Settings::~Settings()
{
	fDescription->ReleaseReference();
}


/**
 * @brief Validates the lock used to serialise access to the value store.
 *
 * @return The lock's InitCheck() result.
 */
status_t
Settings::Init()
{
	return fLock.InitCheck();
}


/**
 * @brief Returns the value for @a setting, falling back to its default.
 *
 * Locks the store and looks up the field by Setting::ID(); if no override
 * is present the Setting's default value is returned.
 *
 * @param setting  Setting to query.
 * @return Stored value, or the Setting's default when nothing is stored.
 */
BVariant
Settings::Value(Setting* setting) const
{
	AutoLocker<BLocker> locker(fLock);

	BVariant value;
	return value.SetFromMessage(fValues, setting->ID()) == B_OK
		? value : setting->DefaultValue();
}


/**
 * @brief Returns the value for the setting with id @a settingID.
 *
 * Falls back to the registered default when nothing is stored. If
 * @a settingID is unknown the empty BVariant is returned.
 *
 * @param settingID  Setting identifier to look up.
 * @return Stored value, registered default, or empty BVariant.
 */
BVariant
Settings::Value(const char* settingID) const
{
	AutoLocker<BLocker> locker(fLock);

	BVariant value;
	if (value.SetFromMessage(fValues, settingID) == B_OK)
		return value;

	Setting* setting = fDescription->SettingByID(settingID);
	return setting != NULL ? setting->DefaultValue() : value;
}


/**
 * @brief Stores @a value as the override for @a setting.
 *
 * Replaces any previous stored value, then notifies all registered
 * listeners in reverse-registration order.
 *
 * @param setting  Setting whose value is being changed.
 * @param value    New value to store.
 * @return @c true on success, @c false when the value could not be added
 *         to the underlying BMessage.
 */
bool
Settings::SetValue(Setting* setting, const BVariant& value)
{
	AutoLocker<BLocker> locker(fLock);

	// remove the message field and re-add it with the new value
	const char* fieldName = setting->ID();
	fValues.RemoveName(fieldName);

	bool success = value.AddToMessage(fValues, fieldName) == B_OK;

	// notify the listeners
	int32 count = fListeners.CountItems();
	for (int32 i = count - 1; i >= 0; i--)
		fListeners.ItemAt(i)->SettingValueChanged(setting);

	return success;
}


/**
 * @brief Restores stored values from a BMessage produced earlier.
 *
 * Walks every Setting in the description, copying any value present in
 * @a message back into the store via SetValue() so listeners are notified.
 *
 * @param message  Source archive.
 * @return @c true if every applicable value was restored, @c false on the
 *         first SetValue() failure.
 */
bool
Settings::RestoreValues(const BMessage& message)
{
	AutoLocker<BLocker> locker(fLock);

	for (int32 i = 0; i < fDescription->CountSettings(); i++) {
		Setting* setting = fDescription->SettingAt(i);
		BVariant value;
		if (value.SetFromMessage(message, setting->ID()) == B_OK) {
			if (!SetValue(setting, value))
				return false;
		}
	}

	return true;
}


/**
 * @brief Resolves an OptionsSetting's stored id back to a SettingsOption.
 *
 * @param setting  Options-typed setting whose value is being queried.
 * @return Matching option, or the setting's default option when nothing is
 *         stored or the stored value is not a string.
 */
SettingsOption*
Settings::OptionValue(OptionsSetting* setting) const
{
	BVariant value = Value(setting);
	return value.Type() == B_STRING_TYPE
		? setting->OptionByID(value.ToString())
		: setting->DefaultOption();
}


/**
 * @brief Registers @a listener for value-change notifications.
 *
 * @param listener  Pointer to the listener; ownership remains with the caller.
 * @return @c true on success, @c false on allocation failure.
 */
bool
Settings::AddListener(Listener* listener)
{
	AutoLocker<BLocker> locker(fLock);
	return fListeners.AddItem(listener);
}


/**
 * @brief Unregisters a previously added listener.
 *
 * @param listener  Listener to remove.
 */
void
Settings::RemoveListener(Listener* listener)
{
	AutoLocker<BLocker> locker(fLock);
	fListeners.RemoveItem(listener);
}


// #pragma mark - Listener


/**
 * @brief Virtual destructor for the Settings::Listener interface.
 */
Settings::Listener::~Listener()
{
}
