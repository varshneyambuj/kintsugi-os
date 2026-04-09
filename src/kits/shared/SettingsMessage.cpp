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
 *   Copyright 2008-2010, Stephan Aßmus <superstippi@gmx.de>.
 *   Copyright 1998, Eric Shepherd.
 *   All rights reserved. Distributed under the terms of the Be Sample Code
 *   license.
 *
 *   Authors:
 *       Stephan Aßmus
 *       Eric Shepherd
 */

/** @file SettingsMessage.cpp
 *  @brief Persistent BMessage subclass that automatically loads and saves its
 *         contents as a flattened message file, and notifies registered
 *         BMessenger listeners when individual values change.
 */

//! Be Newsletter Volume II, Issue 35; September 2, 1998 (Eric Shepherd)

#include "SettingsMessage.h"

#include <new>

#include <Autolock.h>
#include <Entry.h>
#include <File.h>
#include <Messenger.h>
#include <String.h>


/** @brief Constructs the settings message and attempts to load from disk.
 *
 *  The file path is formed as @p directory / @p filename. If the file does
 *  not exist or cannot be unflattened, fStatus reflects the error.
 *
 *  @param directory  A directory_which constant (e.g. B_USER_SETTINGS_DIRECTORY).
 *  @param filename   Name of the settings file within @p directory.
 */
SettingsMessage::SettingsMessage(directory_which directory,
		const char* filename)
	:
	BMessage('pref'),
	fListeners(0)
{
	fStatus = find_directory(directory, &fPath);

	if (fStatus == B_OK)
		fStatus = fPath.Append(filename);

	if (fStatus == B_OK)
		fStatus = Load();
}


/** @brief Destroys the settings message, saving it and freeing all listeners. */
SettingsMessage::~SettingsMessage()
{
	Save();

	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		delete reinterpret_cast<BMessenger*>(fListeners.ItemAtFast(i));
}


/** @brief Returns the initialisation status from construction.
 *  @return B_OK if the file was found and loaded, or an error code.
 */
status_t
SettingsMessage::InitCheck() const
{
	return fStatus;
}


/** @brief Loads (unflattens) the settings from disk.
 *
 *  The message lock is held during the operation.
 *
 *  @return B_OK on success, or an error code if the file cannot be opened or
 *          unflattened.
 */
status_t
SettingsMessage::Load()
{
	BAutolock _(this);

	BFile file(fPath.Path(), B_READ_ONLY);
	status_t status = file.InitCheck();

	if (status == B_OK)
		status = Unflatten(&file);

	return status;
}


/** @brief Saves (flattens) the settings to disk.
 *
 *  Creates or overwrites the settings file. The message lock is held during
 *  the operation.
 *
 *  @return B_OK on success, or an error code if the file cannot be opened or
 *          flattened.
 */
status_t
SettingsMessage::Save() const
{
	BAutolock _(const_cast<SettingsMessage*>(this));

	BFile file(fPath.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();

	if (status == B_OK)
		status = Flatten(&file);

	return status;
}


/** @brief Registers a BMessenger to receive SETTINGS_VALUE_CHANGED notifications.
 *
 *  The messenger is copied; the caller retains ownership of the original.
 *
 *  @param listener  The BMessenger to add.
 *  @return true on success, false if memory allocation fails.
 */
bool
SettingsMessage::AddListener(const BMessenger& listener)
{
	BAutolock _(this);

	BMessenger* listenerCopy = new(std::nothrow) BMessenger(listener);
	if (listenerCopy && fListeners.AddItem(listenerCopy))
		return true;
	delete listenerCopy;
	return false;
}


/** @brief Unregisters a previously added BMessenger listener.
 *
 *  Searches the listener list for an entry equal to @p listener and removes
 *  it. Does nothing if the messenger is not found.
 *
 *  @param listener  The BMessenger to remove.
 */
void
SettingsMessage::RemoveListener(const BMessenger& listener)
{
	BAutolock _(this);

	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--) {
		BMessenger* listenerItem = reinterpret_cast<BMessenger*>(
			fListeners.ItemAtFast(i));
		if (*listenerItem == listener) {
			fListeners.RemoveItem(i);
			delete listenerItem;
			return;
		}
	}
}


// #pragma mark -


/** @brief Sets a boolean setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, bool value)
{
	status_t ret = ReplaceBool(name, value);
	if (ret != B_OK)
		ret = AddBool(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets an int8 setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, int8 value)
{
	status_t ret = ReplaceInt8(name, value);
	if (ret != B_OK)
		ret = AddInt8(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets an int16 setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, int16 value)
{
	status_t ret = ReplaceInt16(name, value);
	if (ret != B_OK)
		ret = AddInt16(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a uint16 setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, uint16 value)
{
	status_t ret = ReplaceUInt16(name, value);
	if (ret != B_OK)
		ret = AddUInt16(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets an int32 setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, int32 value)
{
	status_t ret = ReplaceInt32(name, value);
	if (ret != B_OK)
		ret = AddInt32(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a uint32 setting value, notifying listeners on success.
 *
 *  Also handles backwards compatibility: if the existing field has B_INT32_TYPE
 *  (written by older code) it is replaced with the proper uint32 type.
 *
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, uint32 value)
{
	status_t ret = ReplaceUInt32(name, value);
	if (ret != B_OK)
		ret = AddUInt32(name, value);
	if (ret == B_BAD_TYPE && HasData(name, B_INT32_TYPE)) {
		// For compatibility with older versions of this class, replace an int32
		RemoveData(name);
		ret = AddUInt32(name, value);
	}
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets an int64 setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, int64 value)
{
	status_t ret = ReplaceInt64(name, value);
	if (ret != B_OK)
		ret = AddInt64(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a uint64 setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, uint64 value)
{
	status_t ret = ReplaceUInt64(name, value);
	if (ret != B_OK)
		ret = AddUInt64(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a float setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, float value)
{
	status_t ret = ReplaceFloat(name, value);
	if (ret != B_OK)
		ret = AddFloat(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a double setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, double value)
{
	status_t ret = ReplaceDouble(name, value);
	if (ret != B_OK)
		ret = AddDouble(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a C-string setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New NUL-terminated string value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, const char* value)
{
	status_t ret = ReplaceString(name, value);
	if (ret != B_OK)
		ret = AddString(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a BString setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New string value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, const BString& value)
{
	status_t ret = ReplaceString(name, value);
	if (ret != B_OK)
		ret = AddString(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a BPoint setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New point value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, const BPoint& value)
{
	status_t ret = ReplacePoint(name, value);
	if (ret != B_OK)
		ret = AddPoint(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a BRect setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New rect value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, const BRect& value)
{
	status_t ret = ReplaceRect(name, value);
	if (ret != B_OK)
		ret = AddRect(name, value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets an entry_ref setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New entry_ref value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, const entry_ref& value)
{
	status_t ret = ReplaceRef(name, &value);
	if (ret != B_OK)
		ret = AddRef(name, &value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a BMessage setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  New BMessage value.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, const BMessage& value)
{
	status_t ret = ReplaceMessage(name, &value);
	if (ret != B_OK)
		ret = AddMessage(name, &value);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a BFlattenable setting value, notifying listeners on success.
 *  @param name   Field name.
 *  @param value  Pointer to a BFlattenable object.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, const BFlattenable* value)
{
	status_t ret = ReplaceFlat(name, const_cast<BFlattenable*>(value));
	if (ret != B_OK)
		ret = AddFlat(name, const_cast<BFlattenable*>(value));
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a raw typed data setting value, notifying listeners on success.
 *  @param name      Field name.
 *  @param type      Type code for the data.
 *  @param data      Pointer to the raw bytes.
 *  @param numBytes  Size of the data in bytes.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, type_code type, const void* data,
	ssize_t numBytes)
{
	status_t ret = ReplaceData(name, type, data, numBytes);
	if (ret != B_OK)
		ret = AddData(name, type, data, numBytes);
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


/** @brief Sets a BFont setting value, notifying listeners on success.
 *
 *  Stores the font as a sub-BMessage containing "family", "style", and "size"
 *  fields so it can be reloaded portably.
 *
 *  @param name   Field name.
 *  @param value  The BFont to store.
 *  @return B_OK on success, or an error code.
 */
status_t
SettingsMessage::SetValue(const char* name, const BFont& value)
{
	font_family family;
	font_style style;
	value.GetFamilyAndStyle(&family, &style);

	BMessage fontMessage;
	status_t ret = fontMessage.AddString("family", family);
	if (ret == B_OK)
		ret = fontMessage.AddString("style", style);
	if (ret == B_OK)
		ret = fontMessage.AddFloat("size", value.Size());

	if (ret == B_OK) {
		if (ReplaceMessage(name, &fontMessage) != B_OK)
			ret = AddMessage(name, &fontMessage);
	}
	if (ret == B_OK)
		_NotifyValueChanged(name);
	return ret;
}


// #pragma mark -


/** @brief Returns a boolean setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
bool
SettingsMessage::GetValue(const char* name, bool defaultValue) const
{
	bool value;
	if (FindBool(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns an int8 setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
int8
SettingsMessage::GetValue(const char* name, int8 defaultValue) const
{
	int8 value;
	if (FindInt8(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns an int16 setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
int16
SettingsMessage::GetValue(const char* name, int16 defaultValue) const
{
	int16 value;
	if (FindInt16(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a uint16 setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
uint16
SettingsMessage::GetValue(const char* name, uint16 defaultValue) const
{
	uint16 value;
	if (FindUInt16(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns an int32 setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
int32
SettingsMessage::GetValue(const char* name, int32 defaultValue) const
{
	int32 value;
	if (FindInt32(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a uint32 setting value, or @p defaultValue if not found.
 *
 *  Also accepts a non-negative int32 field for backwards compatibility with
 *  older settings files.
 *
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
uint32
SettingsMessage::GetValue(const char* name, uint32 defaultValue) const
{
	uint32 value;
	if (FindUInt32(name, &value) == B_OK)
		return value;
	// For compatibility with older versions of this class, also accept an int32
	int32 signedValue;
	if (FindInt32(name, &signedValue) == B_OK && signedValue >= 0)
		return signedValue;
	return defaultValue;
}


/** @brief Returns an int64 setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
int64
SettingsMessage::GetValue(const char* name, int64 defaultValue) const
{
	int64 value;
	if (FindInt64(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a uint64 setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
uint64
SettingsMessage::GetValue(const char* name, uint64 defaultValue) const
{
	uint64 value;
	if (FindUInt64(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a float setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
float
SettingsMessage::GetValue(const char* name, float defaultValue) const
{
	float value;
	if (FindFloat(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a double setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
double
SettingsMessage::GetValue(const char* name, double defaultValue) const
{
	double value;
	if (FindDouble(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a BString setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
BString
SettingsMessage::GetValue(const char* name, const BString& defaultValue) const
{
	BString value;
	if (FindString(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a C-string setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return Pointer to the stored string (valid for the lifetime of this object),
 *          or @p defaultValue.
 */
const char*
SettingsMessage::GetValue(const char* name, const char* defaultValue) const
{
	const char* value;
	if (FindString(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a BPoint setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
BPoint
SettingsMessage::GetValue(const char *name, BPoint defaultValue) const
{
	BPoint value;
	if (FindPoint(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a BRect setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
BRect
SettingsMessage::GetValue(const char* name, BRect defaultValue) const
{
	BRect value;
	if (FindRect(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns an entry_ref setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
entry_ref
SettingsMessage::GetValue(const char* name, const entry_ref& defaultValue) const
{
	entry_ref value;
	if (FindRef(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a BMessage setting value, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return The stored value or @p defaultValue.
 */
BMessage
SettingsMessage::GetValue(const char* name, const BMessage& defaultValue) const
{
	BMessage value;
	if (FindMessage(name, &value) != B_OK)
		return defaultValue;
	return value;
}


/** @brief Returns a BFont setting value, or @p defaultValue if not found.
 *
 *  Reconstructs the BFont from the "family", "style", and "size" sub-fields
 *  stored by SetValue(const char*, const BFont&).
 *
 *  @param name          Field name.
 *  @param defaultValue  Value to return when the field is absent or invalid.
 *  @return The reconstructed BFont or @p defaultValue.
 */
BFont
SettingsMessage::GetValue(const char* name, const BFont& defaultValue) const
{
	BMessage fontMessage;
	if (FindMessage(name, &fontMessage) != B_OK)
		return defaultValue;

	const char* family;
	const char* style;
	float size;
	if (fontMessage.FindString("family", &family) != B_OK
		|| fontMessage.FindString("style", &style) != B_OK
		|| fontMessage.FindFloat("size", &size) != B_OK) {
		return defaultValue;
	}

	BFont value;
	if (value.SetFamilyAndStyle(family, style) != B_OK)
		return defaultValue;

	value.SetSize(size);

	return value;
}


/** @brief Returns a raw typed data pointer, or @p defaultValue if not found.
 *  @param name          Field name.
 *  @param type          Expected type code of the field.
 *  @param numBytes      Expected size of the data in bytes.
 *  @param defaultValue  Value to return when the field is absent.
 *  @return Pointer to the raw data or @p defaultValue.
 */
void*
SettingsMessage::GetValue(const char* name, type_code type, ssize_t numBytes,
		const void** defaultValue) const
{
	void* value;
	if (FindData(name, type, (const void**)&value, &numBytes) != B_OK)
		return defaultValue;
	return value;
}


// #pragma mark - private

/** @brief Sends a SETTINGS_VALUE_CHANGED notification to all registered listeners.
 *
 *  Includes the field name and its current value (first entry) in the
 *  notification message.
 *
 *  @param name  The name of the field whose value changed.
 */
void
SettingsMessage::_NotifyValueChanged(const char* name) const
{
	BMessage message(SETTINGS_VALUE_CHANGED);
	message.AddString("name", name);

	// Add the value of that name to the notification.
	type_code type;
	if (GetInfo(name, &type) == B_OK) {
		const void* data;
		ssize_t numBytes;
		if (FindData(name, type, &data, &numBytes) == B_OK)
			message.AddData("value", type, data, numBytes);
	}

	int32 count = fListeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		BMessenger* listener = reinterpret_cast<BMessenger*>(
			fListeners.ItemAtFast(i));
		listener->SendMessage(&message);
	}
}
