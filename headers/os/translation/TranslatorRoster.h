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
 *   Copyright 2002-2015, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file TranslatorRoster.h
 *  @brief BTranslatorRoster: central registry for discovering and invoking translators. */

#ifndef _TRANSLATOR_ROSTER_H
#define _TRANSLATOR_ROSTER_H


#include <Archivable.h>
#include <TranslationDefs.h>


struct translation_format;

class BBitmap;
class BMessage;
class BMessenger;
class BPositionIO;
class BQuery;
class BRect;
class BTranslator;
class BView;
struct entry_ref;


/** @brief RAII-style delegate that holds a reference to a BTranslator acquired from the roster.
 *
 *  Call Release() when finished to return the reference to the roster. */
class BTranslatorReleaseDelegate {
public:
								BTranslatorReleaseDelegate(BTranslator* translator);

	/** @brief Releases the translator reference held by this delegate. */
			void				Release();

private:
			BTranslator*		fUnderlying;
};


/** @brief Central registry that loads translator add-ons and brokers data conversion.
 *
 *  Use Default() to obtain the system-wide roster, or create a private instance and
 *  add translators with AddTranslators() / AddTranslator(). */
class BTranslatorRoster : public BArchivable {
public:
								BTranslatorRoster();
	/** @brief Constructs a roster from an archived BMessage (BArchivable protocol).
	 *  @param model Archive message previously created by Archive(). */
								BTranslatorRoster(BMessage* model);
	virtual						~BTranslatorRoster();

	/** @brief Returns the system-wide default translator roster.
	 *  @return Pointer to the shared default BTranslatorRoster; do not delete. */
	static	BTranslatorRoster*	Default();

	/** @brief Archives this roster into a BMessage.
	 *  @param into Destination BMessage.
	 *  @param deep If true, archive child objects recursively.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			Archive(BMessage* into, bool deep = true) const;

	/** @brief Instantiates a BTranslatorRoster from an archive message.
	 *  @param from Archive message.
	 *  @return New BTranslatorRoster, or NULL on failure. */
	static	BArchivable*		Instantiate(BMessage* from);

	/** @brief Loads translator add-ons from the default or a specified directory path.
	 *  @param loadPath Colon-separated list of directories, or NULL for defaults.
	 *  @return B_OK on success, or an error code. */
			status_t			AddTranslators(const char* loadPath = NULL);

	/** @brief Adds a single translator instance directly to the roster.
	 *  @param translator Translator to add; the roster acquires a reference.
	 *  @return B_OK on success, or an error code. */
			status_t			AddTranslator(BTranslator* translator);

	/** @brief Identifies the best translator for the given source stream.
	 *  @param source Input stream to probe.
	 *  @param ioExtension Optional BMessage with hints; updated with results.
	 *  @param _info Filled with the best-match translator_info on success.
	 *  @param hintType Optional type-code hint for the input format.
	 *  @param hintMIME Optional MIME-type hint for the input format.
	 *  @param wantType Required output type code, or 0 for any.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			Identify(BPositionIO* source,
									BMessage* ioExtension,
									translator_info* _info, uint32 hintType = 0,
									const char* hintMIME = NULL,
									uint32 wantType = 0);

	/** @brief Returns all translators able to handle the source stream.
	 *  @param source Input stream to probe.
	 *  @param ioExtension Optional BMessage with hints.
	 *  @param _info Set to a newly allocated array of translator_info (caller must free[]).
	 *  @param _numInfo Set to the number of elements in _info.
	 *  @param hintType Optional type-code hint.
	 *  @param hintMIME Optional MIME-type hint.
	 *  @param wantType Required output type code, or 0 for any.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			GetTranslators(BPositionIO* source,
									BMessage* ioExtension,
									translator_info** _info, int32* _numInfo,
									uint32 hintType = 0,
									const char* hintMIME = NULL,
									uint32 wantType = 0);

	/** @brief Returns IDs for all translators currently loaded in the roster.
	 *  @param _list Set to a newly allocated array of translator IDs (caller must free[]).
	 *  @param _count Set to the number of elements in _list.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			GetAllTranslators(translator_id** _list,
									int32* _count);

	/** @brief Retrieves descriptive information about a specific translator.
	 *  @param translatorID Translator to query.
	 *  @param _name Set to the translator's name string (owned by the roster).
	 *  @param _info Set to the translator's info string (owned by the roster).
	 *  @param _version Set to the packed version integer.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			GetTranslatorInfo(translator_id translatorID,
									const char** _name, const char** _info,
									int32* _version);

	/** @brief Returns the input formats supported by a specific translator.
	 *  @param translatorID Translator to query.
	 *  @param _formats Set to the translator's input format array (owned by the translator).
	 *  @param _numFormats Set to the number of formats.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			GetInputFormats(translator_id translatorID,
									const translation_format** _formats,
									int32* _numFormats);

	/** @brief Returns the output formats supported by a specific translator.
	 *  @param translatorID Translator to query.
	 *  @param _formats Set to the translator's output format array (owned by the translator).
	 *  @param _numFormats Set to the number of formats.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			GetOutputFormats(translator_id translatorID,
									const translation_format** _formats,
									int32* _numFormats);

	/** @brief Translates source to the desired output type using auto-detected translator.
	 *  @param source Input stream.
	 *  @param info Identify result, or NULL to re-identify.
	 *  @param ioExtension Optional BMessage with parameters.
	 *  @param destination Output stream.
	 *  @param wantOutType Desired output type code.
	 *  @param hintType Optional input type hint.
	 *  @param hintMIME Optional input MIME hint.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			Translate(BPositionIO* source,
									const translator_info* info,
									BMessage* ioExtension,
									BPositionIO* destination,
									uint32 wantOutType, uint32 hintType = 0,
									const char* hintMIME = NULL);

	/** @brief Translates source using a specific translator identified by ID.
	 *  @param translatorID Translator to use.
	 *  @param source Input stream.
	 *  @param ioExtension Optional BMessage with parameters.
	 *  @param destination Output stream.
	 *  @param wantOutType Desired output type code.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			Translate(translator_id translatorID,
									BPositionIO* source, BMessage* ioExtension,
									BPositionIO* destination,
									uint32 wantOutType);

	/** @brief Creates the configuration view for a translator.
	 *  @param translatorID Translator to configure.
	 *  @param ioExtension Optional BMessage with context.
	 *  @param _view Set to the newly created configuration view.
	 *  @param _extent Set to the preferred frame of the view.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			MakeConfigurationView(
									translator_id translatorID,
									BMessage* ioExtension, BView** _view,
									BRect* _extent);

	/** @brief Retrieves the current configuration of a translator.
	 *  @param translatorID Translator to query.
	 *  @param ioExtension BMessage to populate with settings.
	 *  @return B_OK on success, or an error code. */
	virtual	status_t			GetConfigurationMessage(
									translator_id translatorID,
									BMessage* ioExtension);

	/** @brief Acquires a reference to a translator and returns a release delegate.
	 *  @param translatorID Translator to acquire.
	 *  @return A BTranslatorReleaseDelegate the caller must call Release() on, or NULL. */
			BTranslatorReleaseDelegate*	AcquireTranslator(int32 translatorID);

	/** @brief Returns the filesystem entry_ref for a translator's add-on file.
	 *  @param translatorID Translator to query.
	 *  @param ref Filled with the entry_ref on success.
	 *  @return B_OK on success, or an error code. */
			status_t			GetRefFor(translator_id translatorID,
									entry_ref* ref);

	/** @brief Returns whether the file at ref is a known translator add-on.
	 *  @param ref Reference to the file to check.
	 *  @return true if the file is a translator add-on. */
			bool				IsTranslator(entry_ref* ref);

	/** @brief Registers a BMessenger to receive translator change notifications.
	 *  @param target Messenger to notify on roster changes.
	 *  @return B_OK on success, or an error code. */
			status_t			StartWatching(BMessenger target);

	/** @brief Unregisters a previously registered change notification messenger.
	 *  @param target Messenger to remove.
	 *  @return B_OK on success, or an error code. */
			status_t			StopWatching(BMessenger target);

			class Private;

private:
			// unimplemented
								BTranslatorRoster(
									const BTranslatorRoster& other);
			BTranslatorRoster&	operator=(const BTranslatorRoster& other);

	virtual	void					ReservedTranslatorRoster1();
	virtual	void					ReservedTranslatorRoster2();
	virtual	void					ReservedTranslatorRoster3();
	virtual	void					ReservedTranslatorRoster4();
	virtual	void					ReservedTranslatorRoster5();
	virtual	void					ReservedTranslatorRoster6();

			void					_Initialize();

	static	const char*				Version(int32* outCurVersion,
										int32* outMinVersion,
										int32 inAppVersion);
				// for backward compatiblity only

private:
			friend class Private;

			Private*				fPrivate;
			int32					fUnused[6];

	static	BTranslatorRoster*		sDefaultRoster;
};


#endif	// _TRANSLATOR_ROSTER_H
