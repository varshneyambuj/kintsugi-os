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
 * MIT License. Copyright 2017, Haiku Inc.
 * Original authors: Brian Hill.
 */

/** @file RepositoriesSettings.h
    @brief Persistent settings store for the Repositories preflet. */

#ifndef REPOSITORIES_SETTINGS_H
#define REPOSITORIES_SETTINGS_H


#include <File.h>
#include <Message.h>
#include <Path.h>
#include <Point.h>
#include <Rect.h>
#include <String.h>
#include <StringList.h>


/**
 * @brief Reads and writes the preflet's persistent state.
 *
 * Wraps a flattened BMessage at `~/config/settings/Repositories_settings`
 * exposing accessors for the window frame and the user's preserved
 * repository list (name and URL pairs).
 */
class RepositoriesSettings {
public:
							RepositoriesSettings();
	BRect					GetFrame();
	void					SetFrame(BRect frame);
	status_t				GetRepositories(int32& repoCount,
								BStringList& nameList, BStringList& urlList);
	void					SetRepositories(BStringList& nameList,
								BStringList& urlList);

private:
	BMessage				_ReadFromFile();
	status_t				_SaveToFile(BMessage settings);

	BPath					fFilePath;
	BFile					fFile;
	status_t				fInitStatus;
};


#endif
