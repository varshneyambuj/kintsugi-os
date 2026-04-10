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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file Conditions.cpp
 *  @brief Implements launch condition evaluation including boolean logic, safe mode, file existence, network, and settings checks. */


#include "Conditions.h"

#include <stdio.h>

#include <driver_settings.h>
#include <Entry.h>
#include <File.h>
#include <ObjectList.h>
#include <Message.h>
#include <Path.h>
#include <StringList.h>

#include "NetworkWatcher.h"
#include "Utility.h"


class ConditionContainer : public Condition {
protected:
								ConditionContainer(const BMessage& args);
								ConditionContainer();

public:
			void				AddCondition(Condition* condition);

	virtual	bool				IsConstant(ConditionContext& context) const;

protected:
			void				AddConditionsToString(BString& string) const;

protected:
			BObjectList<Condition, true> fConditions;
};


class AndCondition : public ConditionContainer {
public:
								AndCondition(const BMessage& args);
								AndCondition();

	virtual	bool				Test(ConditionContext& context) const;
	virtual	BString				ToString() const;
};


class OrCondition : public ConditionContainer {
public:
								OrCondition(const BMessage& args);

	virtual	bool				Test(ConditionContext& context) const;
	virtual	bool				IsConstant(ConditionContext& context) const;

	virtual	BString				ToString() const;
};


class NotCondition : public ConditionContainer {
public:
								NotCondition(const BMessage& args);
								NotCondition();

	virtual	bool				Test(ConditionContext& context) const;
	virtual	BString				ToString() const;
};


class SafeModeCondition : public Condition {
public:
	virtual	bool				Test(ConditionContext& context) const;
	virtual	bool				IsConstant(ConditionContext& context) const;

	virtual	BString				ToString() const;
};


class ReadOnlyCondition : public Condition {
public:
								ReadOnlyCondition(const BMessage& args);

	virtual	bool				Test(ConditionContext& context) const;
	virtual	bool				IsConstant(ConditionContext& context) const;

	virtual	BString				ToString() const;

private:
			BString				fPath;
	mutable	bool				fIsReadOnly;
	mutable	bool				fTestPerformed;
};


class FileExistsCondition : public Condition {
public:
								FileExistsCondition(const BMessage& args);

	virtual	bool				Test(ConditionContext& context) const;
	virtual	BString				ToString() const;

private:
			BStringList			fPaths;
};


class NetworkAvailableCondition : public Condition {
public:
	virtual	bool				Test(ConditionContext& context) const;
	virtual	bool				IsConstant(ConditionContext& context) const;

	virtual	BString				ToString() const;
};


class SettingCondition : public Condition {
public:
								SettingCondition(const BMessage& args);

	virtual	bool				Test(ConditionContext& context) const;

	virtual	BString				ToString() const;

private:
			BPath				fPath;
			BString				fField;
			BString				fValue;
};


/**
 * @brief Factory function that instantiates the appropriate Condition subclass by name.
 *
 * Recognizes "and", "or", "not", "safemode", "read_only", "file_exists",
 * "network_available", and "setting".
 *
 * @param name The condition type name.
 * @param args The BMessage containing arguments for the condition.
 * @return A newly allocated Condition, or NULL if @a name is unrecognized.
 */
static Condition*
create_condition(const char* name, const BMessage& args)
{
	if (strcmp(name, "and") == 0 && !args.IsEmpty())
		return new AndCondition(args);
	if (strcmp(name, "or") == 0 && !args.IsEmpty())
		return new OrCondition(args);
	if (strcmp(name, "not") == 0 && !args.IsEmpty())
		return new NotCondition(args);

	if (strcmp(name, "safemode") == 0)
		return new SafeModeCondition();
	if (strcmp(name, "read_only") == 0)
		return new ReadOnlyCondition(args);
	if (strcmp(name, "file_exists") == 0)
		return new FileExistsCondition(args);
	if (strcmp(name, "network_available") == 0)
		return new NetworkAvailableCondition();
	if (strcmp(name, "setting") == 0)
		return new SettingCondition(args);

	return NULL;
}


// #pragma mark -


/** @brief Constructs the base condition. */
Condition::Condition()
{
}


/** @brief Destroys the condition. */
Condition::~Condition()
{
}


/**
 * @brief Returns whether this condition's result is invariant for the given context.
 *
 * The default implementation returns @c false (non-constant).
 *
 * @param context The condition evaluation context.
 * @return @c false by default.
 */
bool
Condition::IsConstant(ConditionContext& context) const
{
	return false;
}


// #pragma mark -


/**
 * @brief Constructs a condition container by parsing nested conditions from a BMessage.
 *
 * Iterates over all B_MESSAGE_TYPE fields in @a args, creating child
 * conditions via create_condition() and adding them to the internal list.
 *
 * @param args The BMessage containing nested condition definitions.
 */
ConditionContainer::ConditionContainer(const BMessage& args)
	:
	fConditions(10)
{
	char* name;
	type_code type;
	int32 count;
	for (int32 index = 0; args.GetInfo(B_MESSAGE_TYPE, index, &name, &type,
			&count) == B_OK; index++) {
		BMessage message;
		for (int32 messageIndex = 0; args.FindMessage(name, messageIndex,
				&message) == B_OK; messageIndex++) {
			AddCondition(create_condition(name, message));
		}
	}
}


/** @brief Constructs an empty condition container. */
ConditionContainer::ConditionContainer()
	:
	fConditions(10)
{
}


/**
 * @brief Adds a child condition to this container.
 *
 * NULL conditions are silently ignored.
 *
 * @param condition The condition to add (takes ownership).
 */
void
ConditionContainer::AddCondition(Condition* condition)
{
	if (condition != NULL)
		fConditions.AddItem(condition);
}


/*!	A single constant failing condition makes this constant, too, otherwise,
	a single non-constant condition makes this non-constant as well.
*/
bool
ConditionContainer::IsConstant(ConditionContext& context) const
{
	bool fixed = true;
	for (int32 index = 0; index < fConditions.CountItems(); index++) {
		const Condition* condition = fConditions.ItemAt(index);
		if (condition->IsConstant(context)) {
			if (!condition->Test(context))
				return true;
		} else
			fixed = false;
	}
	return fixed;
}


/**
 * @brief Appends a bracketed, comma-separated list of child condition strings to @a string.
 *
 * @param string The output string to append to.
 */
void
ConditionContainer::AddConditionsToString(BString& string) const
{
	string += "[";

	for (int32 index = 0; index < fConditions.CountItems(); index++) {
		if (index != 0)
			string += ", ";
		string += fConditions.ItemAt(index)->ToString();
	}
	string += "]";
}


// #pragma mark - and


/**
 * @brief Constructs an AND condition from the given arguments message.
 *
 * @param args BMessage containing the child condition definitions.
 */
AndCondition::AndCondition(const BMessage& args)
	:
	ConditionContainer(args)
{
}


/** @brief Constructs an empty AND condition. */
AndCondition::AndCondition()
{
}


/**
 * @brief Tests all child conditions with short-circuit AND logic.
 *
 * @param context The condition evaluation context.
 * @return @c true if all children pass, @c false if any fails.
 */
bool
AndCondition::Test(ConditionContext& context) const
{
	for (int32 index = 0; index < fConditions.CountItems(); index++) {
		Condition* condition = fConditions.ItemAt(index);
		if (!condition->Test(context))
			return false;
	}
	return true;
}


/**
 * @brief Returns a human-readable string representation of this AND condition.
 *
 * @return A string of the form "and [child1, child2, ...]".
 */
BString
AndCondition::ToString() const
{
	BString string = "and ";
	ConditionContainer::AddConditionsToString(string);
	return string;
}


// #pragma mark - or


/**
 * @brief Constructs an OR condition from the given arguments message.
 *
 * @param args BMessage containing the child condition definitions.
 */
OrCondition::OrCondition(const BMessage& args)
	:
	ConditionContainer(args)
{
}


/**
 * @brief Tests child conditions with short-circuit OR logic.
 *
 * Returns @c true if any child passes. An empty OR condition returns @c true.
 *
 * @param context The condition evaluation context.
 * @return @c true if any child passes or the container is empty.
 */
bool
OrCondition::Test(ConditionContext& context) const
{
	if (fConditions.IsEmpty())
		return true;

	for (int32 index = 0; index < fConditions.CountItems(); index++) {
		Condition* condition = fConditions.ItemAt(index);
		if (condition->Test(context))
			return true;
	}
	return false;
}


/*!	If there is a single succeeding constant condition, this is constant, too.
	Otherwise, it is non-constant if there is a single non-constant condition.
*/
bool
OrCondition::IsConstant(ConditionContext& context) const
{
	bool fixed = true;
	for (int32 index = 0; index < fConditions.CountItems(); index++) {
		const Condition* condition = fConditions.ItemAt(index);
		if (condition->IsConstant(context)) {
			if (condition->Test(context))
				return true;
		} else
			fixed = false;
	}
	return fixed;
}


/**
 * @brief Returns a human-readable string representation of this OR condition.
 *
 * @return A string of the form "or [child1, child2, ...]".
 */
BString
OrCondition::ToString() const
{
	BString string = "or ";
	ConditionContainer::AddConditionsToString(string);
	return string;
}


// #pragma mark - or


/**
 * @brief Constructs a NOT condition from the given arguments message.
 *
 * @param args BMessage containing the child condition definition to negate.
 */
NotCondition::NotCondition(const BMessage& args)
	:
	ConditionContainer(args)
{
}


/** @brief Constructs an empty NOT condition. */
NotCondition::NotCondition()
{
}


/**
 * @brief Tests all child conditions and negates the result.
 *
 * Returns @c false if any child passes, @c true otherwise.
 *
 * @param context The condition evaluation context.
 * @return The negation of the children's combined result.
 */
bool
NotCondition::Test(ConditionContext& context) const
{
	for (int32 index = 0; index < fConditions.CountItems(); index++) {
		Condition* condition = fConditions.ItemAt(index);
		if (condition->Test(context))
			return false;
	}
	return true;
}


/**
 * @brief Returns a human-readable string representation of this NOT condition.
 *
 * @return A string of the form "not [child1, ...]".
 */
BString
NotCondition::ToString() const
{
	BString string = "not ";
	ConditionContainer::AddConditionsToString(string);
	return string;
}


// #pragma mark - safemode


/**
 * @brief Tests whether the system is booted in safe mode.
 *
 * @param context The condition evaluation context.
 * @return @c true if safe mode is active.
 */
bool
SafeModeCondition::Test(ConditionContext& context) const
{
	return context.IsSafeMode();
}


/**
 * @brief Safe mode never changes during a boot, so this is constant.
 *
 * @param context The condition evaluation context (unused).
 * @return @c true always.
 */
bool
SafeModeCondition::IsConstant(ConditionContext& context) const
{
	return true;
}


/**
 * @brief Returns the string "safemode".
 *
 * @return A BString containing "safemode".
 */
BString
SafeModeCondition::ToString() const
{
	return "safemode";
}


// #pragma mark - read_only


/**
 * @brief Constructs a read-only condition for the volume at the given path.
 *
 * @param args BMessage whose "args" string specifies the path to check.
 */
ReadOnlyCondition::ReadOnlyCondition(const BMessage& args)
	:
	fPath(args.GetString("args")),
	fIsReadOnly(false),
	fTestPerformed(false)
{
}


/**
 * @brief Tests whether the configured volume is read-only, caching the result.
 *
 * On first call, checks the boot volume (if path is empty or "/boot") or
 * the specified volume via Utility::IsReadOnlyVolume(). Subsequent calls
 * return the cached result.
 *
 * @param context The condition evaluation context.
 * @return @c true if the volume is read-only.
 */
bool
ReadOnlyCondition::Test(ConditionContext& context) const
{
	if (fTestPerformed)
		return fIsReadOnly;

	if (fPath.IsEmpty() || fPath == "/boot")
		fIsReadOnly = context.BootVolumeIsReadOnly();
	else
		fIsReadOnly = Utility::IsReadOnlyVolume(fPath);

	fTestPerformed = true;

	return fIsReadOnly;
}


/**
 * @brief A volume's read-only state does not change, so this is constant.
 *
 * @param context The condition evaluation context (unused).
 * @return @c true always.
 */
bool
ReadOnlyCondition::IsConstant(ConditionContext& context) const
{
	return true;
}


/**
 * @brief Returns a human-readable string for this read-only condition.
 *
 * @return A string of the form "readonly <path>".
 */
BString
ReadOnlyCondition::ToString() const
{
	BString string = "readonly ";
	string << fPath;
	return string;
}


// #pragma mark - file_exists


/**
 * @brief Constructs a file-exists condition from the given argument paths.
 *
 * Extracts all "args" strings from @a args and resolves home-directory
 * tokens via Utility::TranslatePath().
 *
 * @param args BMessage whose "args" strings are the filesystem paths to check.
 */
FileExistsCondition::FileExistsCondition(const BMessage& args)
{
	for (int32 index = 0;
			const char* path = args.GetString("args", index, NULL); index++) {
		fPaths.Add(Utility::TranslatePath(path));
	}
}


/**
 * @brief Tests whether all configured paths exist on the filesystem.
 *
 * @param context The condition evaluation context (unused).
 * @return @c true if every path exists, @c false if any is missing.
 */
bool
FileExistsCondition::Test(ConditionContext& context) const
{
	for (int32 index = 0; index < fPaths.CountStrings(); index++) {
		BEntry entry;
		if (entry.SetTo(fPaths.StringAt(index)) != B_OK
			|| !entry.Exists())
			return false;
	}
	return true;
}


/**
 * @brief Returns a human-readable string listing all paths checked by this condition.
 *
 * @return A string of the form "file_exists [path1, path2, ...]".
 */
BString
FileExistsCondition::ToString() const
{
	BString string = "file_exists [";
	for (int32 index = 0; index < fPaths.CountStrings(); index++) {
		if (index != 0)
			string << ", ";
		string << fPaths.StringAt(index);
	}
	string += "]";
	return string;
}


// #pragma mark - network_available


/**
 * @brief Tests whether a usable network interface is currently available.
 *
 * @param context The condition evaluation context (unused).
 * @return @c true if the network is available.
 */
bool
NetworkAvailableCondition::Test(ConditionContext& context) const
{
	return NetworkWatcher::NetworkAvailable(false);
}


/**
 * @brief Network availability can change at any time, so this is non-constant.
 *
 * @param context The condition evaluation context (unused).
 * @return @c false always.
 */
bool
NetworkAvailableCondition::IsConstant(ConditionContext& context) const
{
	return false;
}


/**
 * @brief Returns the string "network_available".
 *
 * @return A BString containing "network_available".
 */
BString
NetworkAvailableCondition::ToString() const
{
	return "network_available";
}


// #pragma mark - setting


/**
 * @brief Constructs a setting condition that checks a field value in a settings file.
 *
 * Extracts the settings file path, field name, and expected value from
 * the "args" strings in @a args.
 *
 * @param args BMessage with "args" strings: path (index 0), field (index 1),
 *             and optional value (index 2).
 */
SettingCondition::SettingCondition(const BMessage& args)
{
	fPath.SetTo(Utility::TranslatePath(args.GetString("args", 0, NULL)));
	fField = args.GetString("args", 1, NULL);
	fValue = args.GetString("args", 2, NULL);
}


/**
 * @brief Tests whether the configured setting matches the expected value.
 *
 * First attempts to open the file as a flattened BMessage and check the
 * field by type (bool or string). If that fails, falls back to parsing
 * the file as driver settings and performing a text search.
 *
 * @param context The condition evaluation context (unused).
 * @return @c true if the setting matches the expected value, @c false otherwise.
 */
bool
SettingCondition::Test(ConditionContext& context) const
{
	BFile file(fPath.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;

	BMessage settings;
	if (settings.Unflatten(&file) == B_OK) {
		type_code type;
		int32 count;
		if (settings.GetInfo(fField, &type, &count) == B_OK) {
			switch (type) {
				case B_BOOL_TYPE:
				{
					bool value = settings.GetBool(fField);
					bool expect = fValue.IsEmpty();
					if (!expect) {
						expect = fValue == "true" || fValue == "yes"
							|| fValue == "on" || fValue == "1";
					}
					return value == expect;
				}
				case B_STRING_TYPE:
				{
					BString value = settings.GetString(fField);
					if (fValue.IsEmpty() && !value.IsEmpty())
						return true;

					return fValue == value;
				}
			}
		}
		return false;
	}

	void* handle = load_driver_settings(fPath.Path());
	if (handle != NULL) {
		char buffer[512];
		size_t bufferSize = sizeof(buffer);
		if (get_driver_settings_string(handle, buffer, &bufferSize, true) == B_OK) {
			BString pattern(fField);
			if (!fValue.IsEmpty()) {
				pattern << " = ";
				pattern << fValue;
			}
			return strstr(buffer, pattern.String()) != NULL;
		}
		unload_driver_settings(handle);
	}

	return false;
}


/**
 * @brief Returns a human-readable string describing this setting condition.
 *
 * @return A string of the form "setting file <path>, field <field>[, value <value>]".
 */
BString
SettingCondition::ToString() const
{
	BString string = "setting file ";
	string << fPath.Path() << ", field " << fField;
	if (!fValue.IsEmpty())
		string << ", value " << fValue;

	return string;
}


// #pragma mark -


/**
 * @brief Creates a compound condition from a BMessage by wrapping it in an AND.
 *
 * @param message The BMessage containing condition definitions.
 * @return A newly allocated Condition tree, or NULL on failure.
 */
/*static*/ Condition*
Conditions::FromMessage(const BMessage& message)
{
	return create_condition("and", message);
}


/**
 * @brief Wraps an existing condition with a "not safemode" guard.
 *
 * If @a condition is already an AndCondition, appends a "not safemode"
 * child to it. Otherwise, creates a new AndCondition containing both
 * the original condition and the "not safemode" guard.
 *
 * @param condition The existing condition to augment (may be NULL).
 * @return The augmented condition tree.
 */
/*static*/ Condition*
Conditions::AddNotSafeMode(Condition* condition)
{
	AndCondition* andCondition = dynamic_cast<AndCondition*>(condition);
	if (andCondition == NULL)
		andCondition = new AndCondition();
	if (andCondition != condition && condition != NULL)
		andCondition->AddCondition(condition);

	NotCondition* notCondition = new NotCondition();
	notCondition->AddCondition(new SafeModeCondition());

	andCondition->AddCondition(notCondition);
	return andCondition;
}
