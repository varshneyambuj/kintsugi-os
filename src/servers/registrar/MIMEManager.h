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
   MIMEManager.h
 */

/** @file MIMEManager.h
 *  @brief BLooper subclass that processes MIME database queries and type management requests. */
#ifndef MIME_MANAGER_H
#define MIME_MANAGER_H

#include <Looper.h>

#include <mime/Database.h>

#include "RegistrarThreadManager.h"


/** @brief Receives and handles MIME type database operations within its own message loop. */
class MIMEManager : public BLooper,
	private BPrivate::Storage::Mime::Database::NotificationListener {
public:
	MIMEManager();
	virtual ~MIMEManager();

	/** @brief Dispatches incoming MIME-related request messages. */
	virtual void MessageReceived(BMessage *message);

private:
	// Database::NotificationListener
	virtual status_t Notify(BMessage* message, const BMessenger& target);

private:
	class DatabaseLocker;

private:
	void HandleSetParam(BMessage *message);
	void HandleDeleteParam(BMessage *message);

private:
	BPrivate::Storage::Mime::Database fDatabase;
	DatabaseLocker* fDatabaseLocker;
	RegistrarThreadManager fThreadManager;
	BMessenger fManagerMessenger;
};

#endif	// MIME_MANAGER_H
