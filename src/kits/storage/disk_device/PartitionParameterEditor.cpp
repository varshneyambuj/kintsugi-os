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
 *   Copyright 2013, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2009, Bryce Groff, brycegroff@gmail.com.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file PartitionParameterEditor.cpp
 * @brief Base class for GUI editors that collect partition parameters.
 *
 * BPartitionParameterEditor provides the interface that disk system add-ons
 * implement to present a user interface for gathering partition parameters
 * (e.g. during initialisation or child creation). The base class supplies
 * no-op or sensible-default implementations for all virtual methods so that
 * subclasses only need to override the methods relevant to their use case.
 *
 * @see BDiskSystemAddOn
 * @see BPartitionHandle
 */

#include <PartitionParameterEditor.h>

#include <String.h>
#include <View.h>


/**
 * @brief Constructs a BPartitionParameterEditor with no modification message.
 */
BPartitionParameterEditor::BPartitionParameterEditor()
	:
	fModificationMessage(NULL)
{
}


/**
 * @brief Destroys the BPartitionParameterEditor and frees the modification
 *        message if one was set.
 */
BPartitionParameterEditor::~BPartitionParameterEditor()
{
	delete fModificationMessage;
}


/**
 * @brief Sets the controls of the editor to match the parameters of the given
 *        partition.
 *
 * For B_CREATE_PARAMETER_EDITOR editors, the supplied partition is the parent
 * partition. The base class implementation is a no-op; subclasses should
 * override this to populate their controls.
 *
 * @param partition The partition whose current parameters should be reflected
 *                  in the editor's controls.
 */
void
BPartitionParameterEditor::SetTo(BPartition* partition)
{
}


/**
 * @brief Sets the modification message to be sent when a parameter changes.
 *
 * Takes ownership of the provided message. The message may include a string
 * field named "parameter" identifying which parameter changed.
 *
 * @param message The BMessage to send on parameter changes, or NULL to clear
 *                any previously set message.
 */
void
BPartitionParameterEditor::SetModificationMessage(BMessage* message)
{
	delete fModificationMessage;
	fModificationMessage = message;
}


/**
 * @brief Returns the currently set modification message.
 *
 * The returned pointer is owned by the editor; callers must not delete it.
 *
 * @return Pointer to the current modification BMessage, or NULL if none is set.
 */
BMessage*
BPartitionParameterEditor::ModificationMessage() const
{
	return fModificationMessage;
}


/**
 * @brief Returns a view containing the controls for editing parameters.
 *
 * To be overridden by derived classes. The returned BView is added to a
 * window temporarily and removed when editing is done; it remains owned by
 * the editor. Subsequent calls may return the same view or a new one.
 * The base class returns NULL, indicating no UI is required.
 *
 * @return A BView with the editing controls, or NULL if no UI is needed.
 */
BView*
BPartitionParameterEditor::View()
{
	return NULL;
}


/**
 * @brief Validates the parameters currently set in the editor.
 *
 * Called when the user confirms editing. Subclasses should override this to
 * verify that the current control state represents a valid parameter set and
 * return false if it does not. The base class always returns true.
 *
 * @return true if the current parameters are valid, false otherwise.
 */
bool
BPartitionParameterEditor::ValidateParameters() const
{
	return true;
}


/**
 * @brief Notifies the editor that an external parameter has changed.
 *
 * Allows the caller to push a new value for a named parameter into the
 * editor. Subclasses may accept the change and update their controls, or
 * reject it by returning an appropriate error code. The base class returns
 * B_NOT_SUPPORTED.
 *
 * @param name    The name of the changed parameter.
 * @param variant The new value for the parameter.
 * @return B_OK if the change was accepted, or an error code otherwise.
 */
status_t
BPartitionParameterEditor::ParameterChanged(const char* name,
	const BVariant& variant)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Returns the parameters currently represented by the editor's controls.
 *
 * To be overridden by derived classes. The base class sets parameters to an
 * empty string and returns B_OK.
 *
 * @param parameters Output BString to be set to the serialised parameter data.
 * @return B_OK on success, or an error code if the parameters could not be
 *         retrieved.
 */
status_t
BPartitionParameterEditor::GetParameters(BString& parameters)
{
	parameters.SetTo("");
	return B_OK;
}
