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
 * MIT License. Copyright 2013, Rene Gollent.
 */

/** @file TeamFileManagerSettings.h
    @brief Per-team source-path remapping configuration. */

#ifndef TEAM_FILE_MANAGER_SETTINGS_H
#define TEAM_FILE_MANAGER_SETTINGS_H

#include <Message.h>


/**
 * @brief Settings holding source-path remappings for a debugged team.
 *
 * Each mapping pairs a source path embedded in debug info with the on-disk
 * path the file actually resolves to.
 */
class TeamFileManagerSettings {
public:
								TeamFileManagerSettings();
	virtual						~TeamFileManagerSettings();

			TeamFileManagerSettings&
								operator=(
									const TeamFileManagerSettings& other);
									// throws std::bad_alloc;

			const char*			ID() const;
			status_t			SetTo(const BMessage& archive);
			status_t			WriteTo(BMessage& archive) const;

			int32				CountSourceMappings() const;
			status_t			AddSourceMapping(const BString& sourcePath,
									const BString& locatedPath);
			status_t			RemoveSourceMappingAt(int32 index);
			status_t			GetSourceMappingAt(int32 index,
									BString& sourcePath, BString& locatedPath);

	virtual	TeamFileManagerSettings*
								Clone() const;
									// throws std::bad_alloc

private:
	BMessage					fValues;
};


#endif	// TEAM_FILE_MANAGER_SETTINGS_H
