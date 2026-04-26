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
 *   Copyright 2003-2006, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors in chronological order:
 *       Sikosis
 *       Jérôme Duval
 */


/**
 * @file Media.cpp
 * @brief BApplication entry point for the Media preflet.
 *
 * Restores the saved window frame from the user settings file (if any),
 * publishes the global MediaIcons cache to MediaListItem, and creates
 * the main MediaWindow. The @c main() routine refuses to enter the run
 * loop when InitCheck() reports that the media server could not be
 * contacted.
 *
 * @see MediaWindow, MediaIcons
 */


#include "Media.h"

#include <stdio.h>

#include <Catalog.h>
#include <Locale.h>
#include <StorageKit.h>
#include <String.h>


/**
 * @brief Constructs the application, restores the window frame, and
 *        creates the MediaWindow.
 *
 * The settings file is parsed line by line, skipping comment-style lines
 * that begin with @c '#'. The first @c rect = a,b,c,d directive whose
 * dimensions are at least as large as the default frame is honored.
 */
Media::Media()
	:
	BApplication("application/x-vnd.Haiku-Media"),
	fIcons(),
	fWindow(NULL)
{
	BRect rect(32, 64, 637, 462);

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(SETTINGS_FILE);
		BFile file(path.Path(),B_READ_ONLY);
		if (file.InitCheck()==B_OK) {
			char buffer[255];
			ssize_t size = 0;
			while ((size = file.Read(buffer, 255)) > 0) {
				int32 i = 0;
				while (buffer[i] == '#') {
					while (i < size && buffer[i] != '\n')
						i++;
					i++;
				}
				int32 a, b, c, d;
				const char* scanString = " rect = %li,%li,%li,%li";
				if (sscanf(&buffer[i], scanString, &a, &b, &c, &d) == 4) {
					if (c - a >= rect.IntegerWidth()) {
						rect.left = a;
						rect.right = c;
					}
					if (d - b >= rect.IntegerHeight()) {
						rect.top = b;
						rect.bottom = d;
					}
				}
			}
		}
	}

	MediaListItem::SetIcons(&fIcons);
	fWindow = new MediaWindow(rect);
}


/**
 * @brief Reports whether the MediaWindow successfully initialized the
 *        media services.
 *
 * @return @c B_OK when the window is ready, otherwise the underlying
 *         status code from MediaWindow::InitCheck(). When no window was
 *         constructed, returns @c B_OK trivially.
 */
status_t
Media::InitCheck()
{
	if (fWindow)
		return fWindow->InitCheck();
	return B_OK;
}


//	#pragma mark -


/**
 * @brief Process entry point: creates the Media app and runs its loop.
 *
 * @return Always zero.
 * @note When the application's InitCheck() reports an error (typically
 *       the user declined to launch the media server), Run() is skipped
 *       and the process exits cleanly.
 */
int
main(int, char**)
{
	Media app;
	if (app.InitCheck() == B_OK)
		app.Run();

	return 0;
}

