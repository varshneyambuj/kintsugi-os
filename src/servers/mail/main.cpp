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
   Copyright 2007-2012, Haiku, Inc. All rights reserved.
   Copyright 2001-2002 Dr. Zoidberg Enterprises. All rights reserved.
   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
   Distributed under the terms of the MIT License.
 */
/** @file main.cpp
 *  @brief Entry point for the mail daemon server application. */
#include "MailDaemonApplication.h"


int
main(int argc, const char** argv)
{
	bool remakeMIMETypes = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-E") == 0) {
			if (!BMailSettings().DaemonAutoStarts())
				return 0;
		}
		if (strcmp(argv[i], "-M") == 0)
			remakeMIMETypes = true;
	}

	MailDaemonApplication app;
	if (remakeMIMETypes)
		app.MakeMimeTypes(true);
	app.Run();
	return 0;
}
