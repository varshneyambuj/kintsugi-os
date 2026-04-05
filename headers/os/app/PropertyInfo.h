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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2009-2010 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT license.
 */
#ifndef _PROPERTY_INFO_H
#define _PROPERTY_INFO_H

/**
 * @file PropertyInfo.h
 * @brief Defines BPropertyInfo and supporting structures for scripting property metadata.
 */

#include <BeBuild.h>
#include <Flattenable.h>
#include <SupportDefs.h>
#include <TypeConstants.h>


class BMessage;


/**
 * @brief Represents a compound type composed of name/type field pairs.
 *
 * A compound_type is used within property_info to describe properties that
 * consist of multiple named fields, each with its own type code.
 */
struct compound_type {
	/**
	 * @brief A single name/type pair within a compound type.
	 */
	struct field_pair {
		const char*	name;	/**< The field name. */
		type_code	type;	/**< The field's type code. */
	};
	field_pair	pairs[5];	/**< Array of up to 5 field pairs. */
};


/**
 * @brief Describes a single scripting property and the commands it supports.
 *
 * Each property_info entry defines a named property along with the scripting
 * commands (e.g., B_GET_PROPERTY, B_SET_PROPERTY) and specifiers (e.g.,
 * B_DIRECT_SPECIFIER) that it responds to.
 *
 * @see BPropertyInfo
 */
struct property_info {
	const char*		name;			/**< The property name. */
	uint32			commands[10];	/**< Supported scripting commands (zero-terminated). */
	uint32			specifiers[10];	/**< Supported specifier types (zero-terminated). */
	const char*		usage;			/**< Human-readable usage description. */
	uint32			extra_data;		/**< Application-defined extra data. */
	uint32			types[10];		/**< Supported type codes (zero-terminated). */
	compound_type	ctypes[3];		/**< Compound type definitions. */
	uint32			_reserved[10];	/**< Reserved for future use. */
};


/**
 * @enum value_kind
 * @brief Distinguishes between command values and type code values in value_info.
 */
enum value_kind {
	B_COMMAND_KIND = 0,		/**< The value represents a scripting command constant. */
	B_TYPE_CODE_KIND = 1	/**< The value represents a type code constant. */
};


/**
 * @brief Describes a named value constant used in scripting.
 *
 * value_info entries provide human-readable documentation for command
 * constants or type codes that a handler supports.
 *
 * @see BPropertyInfo
 */
struct value_info {
	const char*		name;			/**< The value name. */
	uint32			value;			/**< The numeric value (command or type code). */
	value_kind		kind;			/**< Whether this is a command or type code. */
	const char*		usage;			/**< Human-readable usage description. */
	uint32			extra_data;		/**< Application-defined extra data. */
	uint32			_reserved[10];	/**< Reserved for future use. */
};


/**
 * @brief Provides scripting property metadata for a BHandler.
 *
 * BPropertyInfo implements the BFlattenable interface and holds arrays of
 * property_info and value_info structures that describe the scripting
 * properties and values a handler supports. It is used by the scripting
 * framework to match incoming scripting messages to the appropriate handler.
 *
 * @see BHandler::GetSupportedSuites()
 * @see property_info
 * @see value_info
 */
class BPropertyInfo : public BFlattenable {
public:
	/**
	 * @brief Constructs a BPropertyInfo with property and value descriptions.
	 *
	 * @param prop          An array of property_info structures, terminated by
	 *                      an entry with a NULL name. May be NULL.
	 * @param value         An array of value_info structures, terminated by
	 *                      an entry with a NULL name. May be NULL.
	 * @param freeOnDelete  If true, the prop and value arrays are freed when
	 *                      this object is destroyed.
	 */
								BPropertyInfo(property_info* prop = NULL,
									value_info* value = NULL,
									bool freeOnDelete = false);

	/**
	 * @brief Destructor.
	 *
	 * Frees the property and value arrays if freeOnDelete was set to true.
	 */
	virtual						~BPropertyInfo();

	/**
	 * @brief Finds the property matching a scripting message.
	 *
	 * Searches the property_info array for a property that matches the
	 * given message's command, specifier, and property name.
	 *
	 * @param msg        The scripting message to match.
	 * @param index      The specifier index within the message.
	 * @param specifier  The specifier message.
	 * @param form       The specifier form (e.g., B_DIRECT_SPECIFIER).
	 * @param prop       The property name to match.
	 * @param data       Optional user data pointer.
	 * @return The index of the matching property_info entry, or -1 if not found.
	 */
	virtual	int32				FindMatch(BMessage* msg, int32 index,
									BMessage* specifier, int32 form,
									const char* prop, void* data = NULL) const;

	/**
	 * @brief Returns whether this flattenable has a fixed size.
	 *
	 * @return Always returns false; BPropertyInfo has variable size.
	 */
	virtual	bool				IsFixedSize() const;

	/**
	 * @brief Returns the type code for flattened BPropertyInfo data.
	 *
	 * @return The type code B_PROPERTY_INFO_TYPE.
	 */
	virtual	type_code			TypeCode() const;

	/**
	 * @brief Returns the size needed to flatten this object.
	 *
	 * @return The number of bytes required.
	 */
	virtual	ssize_t				FlattenedSize() const;

	/**
	 * @brief Flattens the property info into a buffer.
	 *
	 * @param buffer  The buffer to write the flattened data into.
	 * @param size    The size of the buffer in bytes.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			Flatten(void* buffer, ssize_t size) const;

	/**
	 * @brief Checks whether a given type code is compatible for unflattening.
	 *
	 * @param code  The type code to check.
	 * @return true if the type code is compatible, false otherwise.
	 */
	virtual	bool				AllowsTypeCode(type_code code) const;

	/**
	 * @brief Restores the property info from a flattened buffer.
	 *
	 * @param code    The type code of the flattened data.
	 * @param buffer  The buffer containing the flattened data.
	 * @param size    The size of the buffer in bytes.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			Unflatten(type_code code, const void* buffer,
									ssize_t size);

	/**
	 * @brief Returns the array of property descriptions.
	 *
	 * @return A pointer to the property_info array, or NULL if none.
	 */
		const property_info*	Properties() const;

	/**
	 * @brief Returns the array of value descriptions.
	 *
	 * @return A pointer to the value_info array, or NULL if none.
	 */
		const value_info*		Values() const;

	/**
	 * @brief Returns the number of property descriptions.
	 *
	 * @return The number of entries in the property_info array.
	 */
		int32					CountProperties() const;

	/**
	 * @brief Returns the number of value descriptions.
	 *
	 * @return The number of entries in the value_info array.
	 */
		int32					CountValues() const;

	/**
	 * @brief Prints the property and value info to standard output.
	 *
	 * Useful for debugging scripting support.
	 */
		void					PrintToStream() const;

protected:
	/**
	 * @brief Checks whether a command matches a property_info entry.
	 *
	 * @param what   The scripting command to check.
	 * @param index  The index within the commands array to check.
	 * @param info   The property_info entry to match against.
	 * @return true if the command matches, false otherwise.
	 */
	static	bool				FindCommand(uint32 what, int32 index,
									property_info* info);

	/**
	 * @brief Checks whether a specifier form matches a property_info entry.
	 *
	 * @param form  The specifier form to check.
	 * @param info  The property_info entry to match against.
	 * @return true if the specifier matches, false otherwise.
	 */
	static	bool				FindSpecifier(uint32 form, property_info* info);

private:
	virtual	void				_ReservedPropertyInfo1();
	virtual	void				_ReservedPropertyInfo2();
	virtual	void				_ReservedPropertyInfo3();
	virtual	void				_ReservedPropertyInfo4();

								BPropertyInfo(const BPropertyInfo& other);
			BPropertyInfo&		operator=(const BPropertyInfo& other);
			void				FreeMem();

			property_info*		fPropInfo;
			value_info*			fValueInfo;
			int32				fPropCount;
			bool				fInHeap;
			uint16				fValueCount;
			uint32				_reserved[4];
};


#endif	/* _PROPERTY_INFO_H */
