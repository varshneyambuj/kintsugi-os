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
 *   Copyright 2002-2015, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Wilber
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file Translator.cpp
 * @brief Abstract base class for Translation Kit translators
 *
 * Implements BTranslator, the reference-counted base class from which all
 * translator add-ons (and BFuncTranslator) derive. The reference counting
 * allows multiple owners (e.g. BTranslatorRoster plus application code) to
 * share a translator safely. When the count reaches zero and the translator
 * belongs to a roster, deletion is dispatched through the roster's looper to
 * ensure thread-safe image unloading.
 *
 * @see FuncTranslator.cpp, TranslatorRoster.cpp
 */


#include "TranslatorRosterPrivate.h"

#include <Translator.h>


/**
 * @brief Constructs a BTranslator with an initial reference count of 1.
 *
 * The translator is not associated with any roster until it is registered
 * via BTranslatorRoster::AddTranslator().
 */
BTranslator::BTranslator()
	:
	fOwningRoster(NULL),
	fID(0),
	fRefCount(1)
{
}


/** @brief Destroys the BTranslator. */
BTranslator::~BTranslator()
{
}


/**
 * @brief Increments the reference count and returns this translator.
 *
 * Call Release() when you are done with the pointer returned here.
 *
 * @return Pointer to this translator if the refcount was positive before the
 *     increment, or NULL if the object was already being destroyed.
 */
BTranslator *BTranslator::Acquire()
{
	if (atomic_add(&fRefCount, 1) > 0)
		return this;

	return NULL;
}


/**
 * @brief Decrements the reference count and destroys the translator if zero.
 *
 * When the count drops to zero and the translator belongs to a roster, a
 * B_DELETE_TRANSLATOR message is sent to the roster's looper so that the
 * add-on image can be unloaded safely in the correct thread context. If
 * there is no owning roster, the object is deleted directly.
 *
 * @return Pointer to this translator if references remain, or NULL if the
 *     object was just deleted or the deletion was dispatched to a roster.
 */
BTranslator *BTranslator::Release()
{
	int32 oldValue = atomic_add(&fRefCount, -1);
	if (oldValue > 1)
		return this;

	if (fOwningRoster == NULL) {
		delete this;
		return NULL;
	}

	// If we have ever been part of a roster, notify the roster to delete us
	// and unload our image in a thread-safe way
	BMessage deleteRequest(B_DELETE_TRANSLATOR);

	deleteRequest.AddPointer("ptr", this);
	deleteRequest.AddInt32("id", fID);

	BMessenger sender(fOwningRoster);
	sender.SendMessage(&deleteRequest);

	return NULL;
}


/**
 * @brief Returns the current reference count.
 * @return The number of outstanding references to this translator.
 */
int32
BTranslator::ReferenceCount()
{
	return fRefCount;
}


/**
 * @brief Creates a configuration view for the translator (optional).
 *
 * Subclasses may override this to provide a BView for the "Translations"
 * control panel or other settings UI. The default implementation returns
 * B_ERROR to indicate no configuration view is available.
 *
 * @param ioExtension Optional extension message with additional parameters.
 * @param outView Set to the newly created configuration BView.
 * @param outExtent Set to the preferred bounds for the view.
 * @return B_OK on success, or B_ERROR if not supported.
 */
status_t
BTranslator::MakeConfigurationView(BMessage* ioExtension,
	BView** outView, BRect* outExtent)
{
	return B_ERROR;
}


/**
 * @brief Retrieves the current configuration settings for the translator (optional).
 *
 * Subclasses may override this to populate \a ioExtension with the current
 * translator settings as name/value pairs. The default implementation returns
 * B_ERROR to indicate no configuration is available.
 *
 * @param ioExtension BMessage to be populated with the translator's settings.
 * @return B_OK on success, or B_ERROR if not supported.
 */
status_t
BTranslator::GetConfigurationMessage(BMessage* ioExtension)
{
	return B_ERROR;
}


//	#pragma mark - FBC protection


status_t BTranslator::_Reserved_Translator_0(int32 n, void *p) { return B_ERROR; }
status_t BTranslator::_Reserved_Translator_1(int32 n, void *p) { return B_ERROR; }
status_t BTranslator::_Reserved_Translator_2(int32 n, void *p) { return B_ERROR; }
status_t BTranslator::_Reserved_Translator_3(int32 n, void *p) { return B_ERROR; }
status_t BTranslator::_Reserved_Translator_4(int32 n, void *p) { return B_ERROR; }
status_t BTranslator::_Reserved_Translator_5(int32 n, void *p) { return B_ERROR; }
status_t BTranslator::_Reserved_Translator_6(int32 n, void *p) { return B_ERROR; }
status_t BTranslator::_Reserved_Translator_7(int32 n, void *p) { return B_ERROR; }
