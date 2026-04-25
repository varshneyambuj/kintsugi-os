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
 * MIT License. Copyright 2015, Rene Gollent.
 */

/** @file TeamSignalSettings.h
    @brief Per-team default and custom signal-disposition configuration. */

#ifndef TEAM_SIGNAL_SETTINGS_H
#define TEAM_SIGNAL_SETTINGS_H

#include <Message.h>

#include "SignalDispositionTypes.h"


/**
 * @brief Stores the default disposition and per-signal overrides used by the
 *        debugger when a debugged team receives signals.
 */
class TeamSignalSettings {
public:
								TeamSignalSettings();
	virtual						~TeamSignalSettings();

			TeamSignalSettings&
								operator=(
									const TeamSignalSettings& other);
									// throws std::bad_alloc;

			const char*			ID() const;
			status_t			SetTo(const BMessage& archive);
			status_t			WriteTo(BMessage& archive) const;
			void				Unset();

			void				SetDefaultSignalDisposition(int32 disposition);
			int32				DefaultSignalDisposition() const;

			int32				CountCustomSignalDispositions() const;
			status_t			AddCustomSignalDisposition(int32 signal,
									int32 disposition);
			status_t			RemoveCustomSignalDispositionAt(int32 index);
			status_t			GetCustomSignalDispositionAt(int32 index,
									int32& signal, int32& disposition) const;

	virtual	TeamSignalSettings*
								Clone() const;

private:
	BMessage					fValues;
};


#endif	// TEAM_SIGNAL_SETTINGS_H
