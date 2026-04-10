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
 *   Copyright 2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */

/** @file PrintAddOnServerApplication.h
 *  @brief Server application that delegates print operations to printer driver add-ons. */

#ifndef PRINT_ADD_ON_SERVER_H
#define PRINT_ADD_ON_SERVER_H

#include <Application.h>
#include <Directory.h>
#include <File.h>
#include <Message.h>
#include <SupportDefs.h>

#include <PrintAddOnServerProtocol.h>


/** @brief Application that routes print requests to the appropriate printer driver add-on. */
class PrintAddOnServerApplication : public BApplication
{
public:
	/** @brief Construct the add-on server with the given application signature. */
					PrintAddOnServerApplication(const char* signature);
	/** @brief Dispatch incoming messages to the appropriate handler method. */
			void	MessageReceived(BMessage* message);

private:
			void		AddPrinter(BMessage* message);
			status_t	AddPrinter(const char* driver,
							const char* spoolFolderName);

			void		ConfigPage(BMessage* message);
			status_t	ConfigPage(const char* driver,
							BDirectory* spoolFolder,
							BMessage* settings);

			void		ConfigJob(BMessage* message);
			status_t	ConfigJob(const char* driver,
							BDirectory* spoolFolder,
							BMessage* settings);

			void		DefaultSettings(BMessage* message);
			status_t	DefaultSettings(const char* driver,
							BDirectory* spoolFolder,
							BMessage* settings);

			void		TakeJob(BMessage* message);
			status_t	TakeJob(const char* driver,
							const char* spoolFile,
							BDirectory* spoolFolder);

			void		SendReply(BMessage* message, status_t status);
			void		SendReply(BMessage* message, BMessage* reply);
};

#endif
