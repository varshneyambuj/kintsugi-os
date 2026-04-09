/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 */

/** @file ClickTarget.h
    @brief Identifies the app-server element that received a mouse click. */

#ifndef CLICK_TARGET_H
#define CLICK_TARGET_H


#include <TokenSpace.h>


/** @brief Identifies a mouse click target in the app server.

    Used to discriminate between different targets in order to filter
    multi-clicks. A click on a different target resets the click count.
*/
class ClickTarget {
public:
	/** @brief Enumerates the possible kinds of click targets. */
	enum Type {
		TYPE_INVALID,
		TYPE_WINDOW_CONTENTS,
		TYPE_WINDOW_DECORATOR
	};

public:
	/** @brief Constructs an invalid (unset) ClickTarget. */
	ClickTarget()
		:
		fType(TYPE_INVALID),
		fWindow(B_NULL_TOKEN),
		fWindowElement(0)
	{
	}

	/** @brief Constructs a ClickTarget identifying a specific window element.
	    @param type Kind of target (contents or decorator).
	    @param window Token of the window that was clicked.
	    @param windowElement Sub-element identifier within the window. */
	ClickTarget(Type type, int32 window, int32 windowElement)
		:
		fType(type),
		fWindow(window),
		fWindowElement(windowElement)
	{
	}

	/** @brief Returns whether this target identifies a real click location.
	    @return true if the target type is not TYPE_INVALID. */
	bool IsValid() const
	{
		return fType != TYPE_INVALID;
	}

	/** @brief Returns the kind of element that was clicked.
	    @return One of the Type enum values. */
	Type GetType() const
	{
		return fType;
	}

	/** @brief Returns the token of the window that was clicked.
	    @return Window token. */
	int32 WindowToken() const
	{
		return fWindow;
	}

	/** @brief Returns the sub-element identifier within the window.
	    @return Window element identifier. */
	int32 WindowElement() const
	{
		return fWindowElement;
	}

	/** @brief Returns true if both targets describe the same click location.
	    @param other The target to compare with.
	    @return true if identical. */
	bool operator==(const ClickTarget& other) const
	{
		return fType == other.fType && fWindow == other.fWindow
			&& fWindowElement == other.fWindowElement;
	}

	/** @brief Returns true if the targets describe different click locations.
	    @param other The target to compare with.
	    @return true if different. */
	bool operator!=(const ClickTarget& other) const
	{
		return !(*this == other);
	}

private:
	Type	fType;
	int32	fWindow;
	int32	fWindowElement;
};


#endif	// CLICK_TARGET_H
