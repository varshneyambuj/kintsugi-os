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
 * MIT License. Copyright 2007, Haiku.
 * Original authors: Oliver Ruiz Dorantes, Ryan Leavengood.
 */

/** @file MessagedItem.h
    @brief BStringItem variant carrying an attached BMessage payload. */

#ifndef MESSAGED_ITEM_H
#define MESSAGED_ITEM_H


#include <Window.h>
#include <ListItem.h>
#include <Message.h>
#include <ListItem.h>


/**
 * @brief A BStringItem that owns an associated BMessage.
 *
 * Used by list views that need to send a context-rich message when the
 * item is invoked. The item takes ownership of the BMessage and frees it
 * on destruction.
 */
class MessagedItem : public BStringItem {
	public:
		/**
		 * @brief Creates a list item that carries an attached BMessage.
		 *
		 * @param label       Visible text shown in the list.
		 * @param information BMessage taken over by the item; freed in the
		 *                    destructor.
		 */
		MessagedItem(const char* label, BMessage* information) : BStringItem(label)
		{
			fMessage = information;
		}

		/** @brief Destructor; frees the attached BMessage. */
		~MessagedItem()
		{
			delete fMessage;
		}

		/**
		 * @brief Returns the attached BMessage (still owned by the item).
		 *
		 * @return Pointer to the BMessage, or NULL if none was provided.
		 */
		BMessage* getMessage()
		{
			return fMessage;
		}

	protected:
		BMessage*   fMessage;

};


#endif	/* MESSAGED_ITEM_H */

