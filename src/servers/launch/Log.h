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
 *   Copyright 2017-2018, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file Log.h
 *  @brief In-memory log of launch daemon job and event lifecycle events. */

#ifndef LOG_H
#define LOG_H


#include <String.h>

#include <locks.h>
#include <util/DoublyLinkedList.h>


class BMessage;

class BaseJob;
class Event;
class Job;


enum LogItemType {
	kJobInitialized,
	kJobIgnored,
	kJobSkipped,
	kJobLaunched,
	kJobTerminated,
	kJobEnabled,
	kJobStopped,
	kEvent,
	kExternalEvent,
	kExternalEventRegistered,
	kExternalEventUnregistered,
};


class LogItem : public DoublyLinkedListLinkImpl<LogItem> {
public:
public:
								LogItem();
	virtual						~LogItem();

			bigtime_t			When()
									{ return fWhen; }
			BString				Message() const;

	virtual	LogItemType			Type() const = 0;
	virtual status_t			GetMessage(BString& target) const = 0;
	virtual status_t			GetParameter(BMessage& parameter) const = 0;
	virtual	bool				Matches(const char* jobName,
									const char* eventName) = 0;

private:
			bigtime_t			fWhen;
};


typedef DoublyLinkedList<LogItem> LogItemList;


class Log {
public:
								Log();

			void				Add(LogItem* item);

			LogItemList::Iterator
								Iterator()
									{ return fItems.GetIterator(); }

			mutex&				Lock()
									{ return fLock; }

			void				JobInitialized(Job* job);
			void				JobIgnored(Job* job, status_t status);

			void				JobSkipped(Job* job);
			void				JobLaunched(Job* job, status_t status);
			void				JobTerminated(Job* job, status_t status);

			void				JobEnabled(Job* job, bool enabled);
			void				JobStopped(BaseJob* job, bool force);

			void				EventTriggered(BaseJob* job, Event* event);

			void				ExternalEventTriggered(const char* name);
			void				ExternalEventRegistered(const char* name);
			void				ExternalEventUnregistered(const char* name);

private:
			mutex				fLock;
			LogItemList			fItems;
			size_t				fCount;
};


#endif // LOG_H
