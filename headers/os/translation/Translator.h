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
 * This file incorporates work from the Haiku project:
 *   Copyright 2002-2009, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file Translator.h
 *  @brief BTranslator base class and make_nth_translator add-on entry point. */

#ifndef _TRANSLATOR_H
#define _TRANSLATOR_H


#include <Archivable.h>
#include <TranslationDefs.h>
#include <TranslatorRoster.h>


/** @brief Abstract base class for all Translation Kit translator add-ons.
 *
 *  Subclass BTranslator and implement the pure virtual methods to create a
 *  translator. Instances are reference-counted; use Acquire() and Release()
 *  instead of new/delete. */
class BTranslator : public BArchivable {
public:
								BTranslator();

	/** @brief Increments the reference count and returns this.
	 *  @return Pointer to this translator. */
			BTranslator*		Acquire();

	/** @brief Decrements the reference count; deletes the object when it reaches zero.
	 *  @return Pointer to this translator, or NULL if it was deleted. */
			BTranslator*		Release();

	/** @brief Returns the current reference count.
	 *  @return Reference count. */
			int32				ReferenceCount();

	/** @brief Returns the human-readable name of this translator.
	 *  @return Null-terminated name string. */
	virtual	const char*			TranslatorName() const = 0;

	/** @brief Returns a short description of this translator.
	 *  @return Null-terminated info string. */
	virtual const char*			TranslatorInfo() const = 0;

	/** @brief Returns the packed version number of this translator.
	 *  @return Version integer encoded with B_TRANSLATION_MAKE_VERSION(). */
	virtual int32				TranslatorVersion() const = 0;

	/** @brief Returns the array of input formats this translator can read.
	 *  @param _count Set to the number of elements in the returned array.
	 *  @return Pointer to an array of translation_format structs. */
	virtual const translation_format* InputFormats(int32* _count) const = 0;

	/** @brief Returns the array of output formats this translator can produce.
	 *  @param _count Set to the number of elements in the returned array.
	 *  @return Pointer to an array of translation_format structs. */
	virtual const translation_format* OutputFormats(int32* _count) const = 0;

	/** @brief Probes source to determine whether this translator can handle it.
	 *  @param source Input stream to inspect.
	 *  @param format Hint about the expected input format, or NULL.
	 *  @param extension Optional BMessage with additional parameters.
	 *  @param info Filled with recognition details on success.
	 *  @param outType Desired output type code.
	 *  @return B_OK if recognized, or an error code. */
	virtual status_t			Identify(BPositionIO* source,
									const translation_format* format,
									BMessage* extension, translator_info* info,
									uint32 outType) = 0;

	/** @brief Translates source into the requested output type and writes to destination.
	 *  @param source Input stream.
	 *  @param info Result from a prior Identify() call.
	 *  @param extension Optional BMessage with additional parameters.
	 *  @param outType Desired output type code.
	 *  @param destination Output stream.
	 *  @return B_OK on success, or an error code. */
	virtual status_t			Translate(BPositionIO* source,
									const translator_info* info,
									BMessage* extension, uint32 outType,
									BPositionIO* destination) = 0;

	/** @brief Creates an optional configuration view for this translator's settings.
	 *  @param extension Optional BMessage with context.
	 *  @param _view Set to the newly created view on success.
	 *  @param _extent Set to the preferred frame of the view.
	 *  @return B_OK on success, or B_NOT_SUPPORTED if no view is provided. */
	virtual status_t			MakeConfigurationView(BMessage* extension,
									BView** _view, BRect* _extent);

	/** @brief Fills a BMessage with the translator's current configuration.
	 *  @param extension BMessage to populate.
	 *  @return B_OK on success, or an error code. */
	virtual status_t			GetConfigurationMessage(BMessage* extension);

protected:
	virtual						~BTranslator();
		// this is protected because the object is deleted by the
		// Release() function instead of being deleted directly by
		// the user

private:
	friend class BTranslatorRoster::Private;

	virtual status_t			_Reserved_Translator_0(int32, void*);
	virtual status_t			_Reserved_Translator_1(int32, void*);
	virtual status_t			_Reserved_Translator_2(int32, void*);
	virtual status_t			_Reserved_Translator_3(int32, void*);
	virtual status_t			_Reserved_Translator_4(int32, void*);
	virtual status_t			_Reserved_Translator_5(int32, void*);
	virtual status_t			_Reserved_Translator_6(int32, void*);
	virtual status_t			_Reserved_Translator_7(int32, void*);

			BTranslatorRoster::Private* fOwningRoster;
			translator_id		fID;
			int32				fRefCount;

			uint32				_reserved[7];
};


/** @brief Add-on entry point; return a new BTranslator subclass instance for index n.
 *
 *  Called by the roster starting at n=0, incrementing n until NULL is returned.
 *  @param n Zero-based translator index.
 *  @param you image_id of the add-on.
 *  @param flags Reserved; ignore for now.
 *  @return A new BTranslator instance, or NULL when no more translators exist. */
extern "C" BTranslator* make_nth_translator(int32 n, image_id you,
	uint32 flags, ...);


#endif	// _TRANSLATOR_H
