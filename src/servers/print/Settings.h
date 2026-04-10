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
 *   Copyright 2002-2006, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */

/** @file Settings.h
 *  @brief Per-application and per-printer settings for the print server. */

#ifndef _SETTINGS_H
#define _SETTINGS_H

#include "BeUtils.h"
#include "ObjectList.h"

#include <String.h>

/** @brief Stores the printer preference for a specific application. */
class AppSettings {
private:
	BString fMimeType; /**< Application MIME signature */
	BString fPrinter;  /**< Printer used by application (empty = default) */

public:
	/** @brief Construct app settings for the given MIME type and optional printer. */
	AppSettings(const char* mimeType, const char* printer = NULL);

	/** @brief Return the application MIME type. */
	const char* GetMimeType() const      { return fMimeType.String(); }
	/** @brief Check whether this app uses the system default printer. */
	bool UsesDefaultPrinter() const      { return fMimeType.Length() == 0; }
	/** @brief Return the assigned printer name. */
	const char* GetPrinter() const       { return fPrinter.String(); }
	/** @brief Set the printer for this application. */
	void SetPrinter(const char* printer) { fPrinter = printer; }
	/** @brief Reset to use the system default printer. */
	void SetDefaultPrinter()             { fPrinter = ""; }
};


/** @brief Stores default page and job settings for a specific printer. */
class PrinterSettings {
private:
	BString  fPrinter; /**< Printer name */
	BMessage fPageSettings; /**< Default page settings */
	BMessage fJobSettings;  /**< Default job settings */

public:
	/** @brief Construct printer settings with optional page and job defaults. */
	PrinterSettings(const char* printer, BMessage* pageSettings = NULL, BMessage* jobSettings = NULL);

	/** @brief Return the printer name. */
	const char* GetPrinter() const       { return fPrinter.String(); }
	/** @brief Return a pointer to the page settings message. */
	BMessage* GetPageSettings()          { return &fPageSettings; }
	/** @brief Return a pointer to the job settings message. */
	BMessage* GetJobSettings()           { return &fJobSettings; }

	/** @brief Set the printer name. */
	void SetPrinter(const char* p)       { fPrinter = p; }
	/** @brief Replace the page settings. */
	void SetPageSettings(BMessage* s)    { fPageSettings = *s; }
	/** @brief Replace the job settings. */
	void SetJobSettings(BMessage* s)     { fJobSettings = *s; }
};

/** @brief Singleton managing all print server settings including app and printer preferences. */
class Settings {
private:
	BObjectList<AppSettings>     fApps; /**< Per-application printer assignments */
	BObjectList<PrinterSettings> fPrinters; /**< Per-printer default settings */
	bool                         fUseConfigWindow; /**< Whether to show config window */
	BRect                        fConfigWindowFrame; /**< Saved config window position */
	BString                      fDefaultPrinter; /**< Name of the default printer */

	static Settings* sSingleton;
	Settings();

public:
	/** @brief Return the singleton settings instance, creating it if needed. */
	static Settings* GetSettings();
	/** @brief Destructor clearing the singleton pointer. */
	~Settings();

	/** @brief Return the number of per-app settings entries. */
	int AppSettingsCount() const           { return fApps.CountItems(); }
	/** @brief Return the app settings at the given index. */
	AppSettings* AppSettingsAt(int i)      { return fApps.ItemAt(i); }
	/** @brief Add a new per-app settings entry. */
	void AddAppSettings(AppSettings* s)    { fApps.AddItem(s); }
	/** @brief Remove and delete the per-app settings at the given index. */
	void RemoveAppSettings(int i);
	/** @brief Find per-app settings by MIME type. */
	AppSettings* FindAppSettings(const char* mimeType);

	/** @brief Return the number of per-printer settings entries. */
	int PrinterSettingsCount() const            { return fPrinters.CountItems(); }
	/** @brief Return the printer settings at the given index. */
	PrinterSettings* PrinterSettingsAt(int i)   { return fPrinters.ItemAt(i); }
	/** @brief Add a new per-printer settings entry. */
	void AddPrinterSettings(PrinterSettings* s) { fPrinters.AddItem(s); }
	/** @brief Remove and delete the per-printer settings at the given index. */
	void RemovePrinterSettings(int i);
	/** @brief Find per-printer settings by printer name. */
	PrinterSettings* FindPrinterSettings(const char* printer);

	/** @brief Check whether the config window is enabled. */
	bool UseConfigWindow() const          { return fUseConfigWindow; }
	/** @brief Enable or disable the config window. */
	void SetUseConfigWindow(bool b)       { fUseConfigWindow = b; }
	/** @brief Return the saved config window frame rectangle. */
	BRect ConfigWindowFrame() const       { return fConfigWindowFrame; }
	/** @brief Store the config window frame rectangle. */
	void SetConfigWindowFrame(BRect r)    { fConfigWindowFrame = r; }
	/** @brief Return the name of the default printer. */
	const char* DefaultPrinter() const    { return fDefaultPrinter.String(); }
	/** @brief Set the name of the default printer. */
	void SetDefaultPrinter(const char* n) { fDefaultPrinter = n; }

	/** @brief Serialize all settings to a file. */
	void Save(BFile* settings_file);
	/** @brief Deserialize all settings from a file. */
	void Load(BFile* settings_file);
};

#endif
