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
 *   Copyright 2017, Dario Casalinuovo
 *   All rights reserved. Distributed under the terms of the MIT license.
 */

/** @file MediaStreamer.cpp
 *  @brief Internal helper that instantiates a streamer plugin and creates a BDataIO adapter for a URL.
 */


#include "MediaStreamer.h"

#include <stdio.h>
#include <string.h>

#include "MediaDebug.h"

#include "PluginManager.h"


/** @brief Constructs a MediaStreamer for the given URL.
 *         The adapter is not created until CreateAdapter() is called.
 *  @param url The URL to be streamed.
 */
MediaStreamer::MediaStreamer(BUrl url)
	:
	fStreamer(NULL)
{
	CALLED();

	fUrl = url;
}


/** @brief Destructor; destroys the streamer plugin if one was created. */
MediaStreamer::~MediaStreamer()
{
	CALLED();

	if (fStreamer != NULL)
		gPluginManager.DestroyStreamer(fStreamer);
}


/** @brief Creates a BDataIO adapter for the URL by instantiating the appropriate streamer plugin.
 *         If a previous streamer exists it is destroyed first.
 *  @param adapter Output pointer to receive the created BDataIO adapter.
 *  @return B_OK on success, or an error code from the plugin manager.
 */
status_t
MediaStreamer::CreateAdapter(BDataIO** adapter)
{
	CALLED();

	// NOTE: Consider splitting the streamer creation and
	// sniff in PluginManager.
	if (fStreamer != NULL)
		gPluginManager.DestroyStreamer(fStreamer);

	return gPluginManager.CreateStreamer(&fStreamer, fUrl, adapter);
}
