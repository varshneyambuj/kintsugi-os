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

/** @file Log.cpp
 *  @brief Implements the launch daemon logging facility for job and event lifecycle tracking. */


#include "Log.h"

#include <OS.h>

#include "Events.h"
#include "Job.h"


/** @brief Maximum number of log items to retain before discarding the oldest. */
const size_t kMaxItems = 10000;


class AbstractJobLogItem : public LogItem {
public:
								AbstractJobLogItem(BaseJob* job);
	virtual						~AbstractJobLogItem();

	virtual status_t			GetParameter(BMessage& parameter) const;
	virtual	bool				Matches(const char* jobName,
									const char* eventName);

protected:
			BaseJob*			fJob;
};


class JobInitializedLogItem : public AbstractJobLogItem {
public:
								JobInitializedLogItem(Job* job);
	virtual						~JobInitializedLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
};


class JobIgnoredLogItem : public LogItem {
public:
								JobIgnoredLogItem(Job* job, status_t error);
	virtual						~JobIgnoredLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
	virtual status_t			GetParameter(BMessage& parameter) const;
	virtual	bool				Matches(const char* jobName,
									const char* eventName);

private:
			BString				fJobName;
			status_t			fError;
};


class JobSkippedLogItem : public LogItem {
public:
								JobSkippedLogItem(Job* job);
	virtual						~JobSkippedLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
	virtual status_t			GetParameter(BMessage& parameter) const;
	virtual	bool				Matches(const char* jobName,
									const char* eventName);

private:
			BString				fJobName;
};


class JobLaunchedLogItem : public AbstractJobLogItem {
public:
								JobLaunchedLogItem(Job* job, status_t status);
	virtual						~JobLaunchedLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
	virtual status_t			GetParameter(BMessage& parameter) const;

private:
			status_t			fStatus;
};


class JobTerminatedLogItem : public AbstractJobLogItem {
public:
								JobTerminatedLogItem(Job* job, status_t status);
	virtual						~JobTerminatedLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
	virtual status_t			GetParameter(BMessage& parameter) const;

private:
			status_t			fStatus;
};


class JobEnabledLogItem : public AbstractJobLogItem {
public:
								JobEnabledLogItem(Job* job, bool enabled);
	virtual						~JobEnabledLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
	virtual status_t			GetParameter(BMessage& parameter) const;

private:
			bool				fEnabled;
};


class JobStoppedLogItem : public AbstractJobLogItem {
public:
								JobStoppedLogItem(BaseJob* job, bool force);
	virtual						~JobStoppedLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
	virtual status_t			GetParameter(BMessage& parameter) const;

private:
			bool				fForce;
};


class EventLogItem : public AbstractJobLogItem {
public:
								EventLogItem(BaseJob* job, Event* event);
	virtual						~EventLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
	virtual status_t			GetParameter(BMessage& parameter) const;
	virtual	bool				Matches(const char* jobName,
									const char* eventName);

private:
			Event*				fEvent;
};


class AbstractExternalEventLogItem : public LogItem {
public:
								AbstractExternalEventLogItem(const char* name);
	virtual						~AbstractExternalEventLogItem();

	virtual status_t			GetParameter(BMessage& parameter) const;
	virtual	bool				Matches(const char* jobName,
									const char* eventName);

protected:
			BString				fEventName;
};


class ExternalEventLogItem : public AbstractExternalEventLogItem {
public:
								ExternalEventLogItem(const char* name);
	virtual						~ExternalEventLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
};


class ExternalEventRegisteredLogItem : public AbstractExternalEventLogItem {
public:
								ExternalEventRegisteredLogItem(
									const char* name);
	virtual						~ExternalEventRegisteredLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
};


class ExternalEventUnregisteredLogItem : public AbstractExternalEventLogItem {
public:
								ExternalEventUnregisteredLogItem(
									const char* name);
	virtual						~ExternalEventUnregisteredLogItem();

	virtual	LogItemType			Type() const;
	virtual status_t			GetMessage(BString& target) const;
};


// #pragma mark -


/** @brief Constructs a log item, recording the current system time. */
LogItem::LogItem()
	:
	fWhen(system_time())
{
}


/** @brief Destroys the log item. */
LogItem::~LogItem()
{
}


/**
 * @brief Returns the log message as a BString.
 *
 * @return The formatted message text.
 */
BString
LogItem::Message() const
{
	BString message;
	GetMessage(message);
	return message;
}


// #pragma mark - Log


/** @brief Constructs an empty log with a mutex for thread-safe access. */
Log::Log()
	:
	fCount(0)
{
	mutex_init(&fLock, "log lock");
}


/**
 * @brief Adds a log item, evicting the oldest if the maximum count is reached.
 *
 * @param item The log item to add (takes ownership).
 */
void
Log::Add(LogItem* item)
{
	MutexLocker locker(fLock);
	if (fCount == kMaxItems)
		fItems.Remove(fItems.First());
	else
		fCount++;

	fItems.Add(item);
}


/**
 * @brief Logs that a job was initialized.
 *
 * @param job The job that was initialized.
 */
void
Log::JobInitialized(Job* job)
{
	LogItem* item = new(std::nothrow) JobInitializedLogItem(job);
	if (item != NULL)
		Add(item);
	else
		debug_printf("Initialized job \"%s\"\n", job->Name());
}


/**
 * @brief Logs that a job was ignored due to an error.
 *
 * @param job    The job that was ignored.
 * @param status The error code that caused the job to be ignored.
 */
void
Log::JobIgnored(Job* job, status_t status)
{
	LogItem* item = new(std::nothrow) JobIgnoredLogItem(job, status);
	if (item != NULL)
		Add(item);
	else {
		debug_printf("Ignored job \"%s\": %s\n", job->Name(),
			strerror(status));
	}
}


/**
 * @brief Logs that a job was skipped (e.g. due to an unmet condition).
 *
 * @param job The job that was skipped.
 */
void
Log::JobSkipped(Job* job)
{
	LogItem* item = new(std::nothrow) JobSkippedLogItem(job);
	if (item != NULL)
		Add(item);
	else {
		debug_printf("Skipped job \"%s\"\n", job->Name());
	}
}


/**
 * @brief Logs a job launch attempt and its result status.
 *
 * @param job    The job that was launched.
 * @param status The result of the launch (B_OK on success).
 */
void
Log::JobLaunched(Job* job, status_t status)
{
	LogItem* item = new(std::nothrow) JobLaunchedLogItem(job, status);
	if (item != NULL)
		Add(item);
	else {
		debug_printf("Launched job \"%s\": %s\n", job->Name(),
			strerror(status));
	}
}


/**
 * @brief Logs that a job's process was terminated.
 *
 * @param job    The job whose team terminated.
 * @param status The termination status.
 */
void
Log::JobTerminated(Job* job, status_t status)
{
	LogItem* item = new(std::nothrow) JobTerminatedLogItem(job, status);
	if (item != NULL)
		Add(item);
	else {
		debug_printf("Terminated job \"%s\": %s\n", job->Name(),
			strerror(status));
	}
}


/**
 * @brief Logs that a job's enabled state changed.
 *
 * @param job     The job whose state changed.
 * @param enabled @c true if now enabled, @c false if disabled.
 */
void
Log::JobEnabled(Job* job, bool enabled)
{
	LogItem* item = new(std::nothrow) JobEnabledLogItem(job, enabled);
	if (item != NULL)
		Add(item);
	else
		debug_printf("Enabled job \"%s\": %d\n", job->Name(), enabled);
}


/**
 * @brief Logs that a job was stopped.
 *
 * @param job   The job that was stopped.
 * @param force @c true if it was a forced stop.
 */
void
Log::JobStopped(BaseJob* job, bool force)
{
	LogItem* item = new(std::nothrow) JobStoppedLogItem(job, force);
	if (item != NULL)
		Add(item);
	else
		debug_printf("Stopped job \"%s\"\n", job->Name());
}


/**
 * @brief Logs that an event was triggered for a job.
 *
 * @param job   The job that owns the event.
 * @param event The event that was triggered.
 */
void
Log::EventTriggered(BaseJob* job, Event* event)
{
	LogItem* item = new(std::nothrow) EventLogItem(job, event);
	if (item != NULL)
		Add(item);
	else {
		debug_printf("Event triggered for \"%s\": %s\n", job->Name(),
			event->ToString().String());
	}
}


/**
 * @brief Logs that an external event was triggered.
 *
 * @param name The name of the external event.
 */
void
Log::ExternalEventTriggered(const char* name)
{
	LogItem* item = new(std::nothrow) ExternalEventLogItem(name);
	if (item != NULL)
		Add(item);
	else
		debug_printf("External event triggered: %s\n", name);
}


/**
 * @brief Logs that an external event was registered.
 *
 * @param name The name of the external event.
 */
void
Log::ExternalEventRegistered(const char* name)
{
	LogItem* item = new(std::nothrow) ExternalEventRegisteredLogItem(name);
	if (item != NULL)
		Add(item);
	else
		debug_printf("External event registered: %s\n", name);
}


/**
 * @brief Logs that an external event was unregistered.
 *
 * @param name The name of the external event.
 */
void
Log::ExternalEventUnregistered(const char* name)
{
	LogItem* item = new(std::nothrow) ExternalEventUnregisteredLogItem(name);
	if (item != NULL)
		Add(item);
	else
		debug_printf("External event unregistered: %s\n", name);
}


// #pragma mark - AbstractJobLogItem


/**
 * @brief Constructs an abstract job log item referencing the given job.
 *
 * @param job The job this log item is about.
 */
AbstractJobLogItem::AbstractJobLogItem(BaseJob* job)
	:
	fJob(job)
{
}


/** @brief Destroys the abstract job log item. */
AbstractJobLogItem::~AbstractJobLogItem()
{
}


/**
 * @brief Adds the job name to the parameter message.
 *
 * @param parameter The output BMessage.
 * @return B_OK on success.
 */
status_t
AbstractJobLogItem::GetParameter(BMessage& parameter) const
{
	return parameter.AddString("job", fJob->Name());
}


/**
 * @brief Tests whether this log item matches the given job or event filter.
 *
 * @param jobName   Job name filter (NULL matches all).
 * @param eventName Event name filter (unused in this base class).
 * @return @c true if this item matches the filter criteria.
 */
bool
AbstractJobLogItem::Matches(const char* jobName, const char* eventName)
{
	if (jobName == NULL && eventName == NULL)
		return true;

	if (jobName != NULL && strcmp(fJob->Name(), jobName) == 0)
		return true;

	return false;
}


// #pragma mark - JobInitializedLogItem


/** @brief Constructs a job-initialized log item. */
JobInitializedLogItem::JobInitializedLogItem(Job* job)
	:
	AbstractJobLogItem(job)
{
}


/** @brief Destroys the job-initialized log item. */
JobInitializedLogItem::~JobInitializedLogItem()
{
}


/** @brief Returns kJobInitialized. */
LogItemType
JobInitializedLogItem::Type() const
{
	return kJobInitialized;
}


/**
 * @brief Formats the log message indicating the job was initialized.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
JobInitializedLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("Job \"%s\" initialized.", fJob->Name());
	return B_OK;
}


// #pragma mark - JobIgnoredLogItem


/**
 * @brief Constructs a job-ignored log item with the error that caused it.
 *
 * @param job   The ignored job (name is copied).
 * @param error The error code.
 */
JobIgnoredLogItem::JobIgnoredLogItem(Job* job, status_t error)
	:
	fJobName(job->Name()),
	fError(error)
{
}


/** @brief Destroys the job-ignored log item. */
JobIgnoredLogItem::~JobIgnoredLogItem()
{
}


/** @brief Returns kJobIgnored. */
LogItemType
JobIgnoredLogItem::Type() const
{
	return kJobIgnored;
}


/**
 * @brief Formats the log message indicating the job was ignored and why.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
JobIgnoredLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("Ignored job \"%s\" due %s", fJobName.String(),
		strerror(fError));
	return B_OK;
}


/**
 * @brief Adds the job name and error code to the parameter message.
 *
 * @param parameter The output BMessage.
 * @return B_OK on success.
 */
status_t
JobIgnoredLogItem::GetParameter(BMessage& parameter) const
{
	status_t status = parameter.AddString("job", fJobName);
	if (status == B_OK)
		status = parameter.AddInt32("error", fError);
	return status;
}


/**
 * @brief Tests whether this log item matches the given filter criteria.
 *
 * @param jobName   Job name filter (NULL matches all).
 * @param eventName Event name filter (unused).
 * @return @c true if this item matches.
 */
bool
JobIgnoredLogItem::Matches(const char* jobName, const char* eventName)
{
	if (jobName == NULL && eventName == NULL)
		return true;

	if (jobName != NULL && fJobName == jobName)
		return true;

	return false;
}


// #pragma mark - JobSkippedLogItem


/** @brief Constructs a job-skipped log item (copies the job name). */
JobSkippedLogItem::JobSkippedLogItem(Job* job)
	:
	fJobName(job->Name())
{
}


/** @brief Destroys the job-skipped log item. */
JobSkippedLogItem::~JobSkippedLogItem()
{
}


/** @brief Returns kJobSkipped. */
LogItemType
JobSkippedLogItem::Type() const
{
	return kJobSkipped;
}


/**
 * @brief Formats the log message indicating the job was skipped.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
JobSkippedLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("Skipped job \"%s\"", fJobName.String());
	return B_OK;
}


/**
 * @brief Adds the job name to the parameter message.
 *
 * @param parameter The output BMessage.
 * @return B_OK on success.
 */
status_t
JobSkippedLogItem::GetParameter(BMessage& parameter) const
{
	return parameter.AddString("job", fJobName);
}


/**
 * @brief Tests whether this log item matches the given filter criteria.
 *
 * @param jobName   Job name filter (NULL matches all).
 * @param eventName Event name filter (unused).
 * @return @c true if this item matches.
 */
bool
JobSkippedLogItem::Matches(const char* jobName, const char* eventName)
{
	if (jobName == NULL && eventName == NULL)
		return true;

	if (jobName != NULL && fJobName == jobName)
		return true;

	return false;
}


// #pragma mark - JobLaunchedLogItem


/**
 * @brief Constructs a job-launched log item with the launch status.
 *
 * @param job    The launched job.
 * @param status The launch result.
 */
JobLaunchedLogItem::JobLaunchedLogItem(Job* job, status_t status)
	:
	AbstractJobLogItem(job),
	fStatus(status)
{
}


/** @brief Destroys the job-launched log item. */
JobLaunchedLogItem::~JobLaunchedLogItem()
{
}


/** @brief Returns kJobLaunched. */
LogItemType
JobLaunchedLogItem::Type() const
{
	return kJobLaunched;
}


/**
 * @brief Formats the log message with the job name and launch status.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
JobLaunchedLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("Job \"%s\" launched: %s", fJob->Name(),
		strerror(fStatus));
	return B_OK;
}


/**
 * @brief Adds the job name and launch status to the parameter message.
 *
 * @param parameter The output BMessage.
 * @return B_OK on success.
 */
status_t
JobLaunchedLogItem::GetParameter(BMessage& parameter) const
{
	status_t status = AbstractJobLogItem::GetParameter(parameter);
	if (status == B_OK)
		status = parameter.AddInt32("status", fStatus);
	return status;
}


// #pragma mark - JobTerminatedLogItem


/**
 * @brief Constructs a job-terminated log item with the termination status.
 *
 * @param job    The terminated job.
 * @param status The termination status.
 */
JobTerminatedLogItem::JobTerminatedLogItem(Job* job, status_t status)
	:
	AbstractJobLogItem(job),
	fStatus(status)
{
}


/** @brief Destroys the job-terminated log item. */
JobTerminatedLogItem::~JobTerminatedLogItem()
{
}


/** @brief Returns kJobTerminated. */
LogItemType
JobTerminatedLogItem::Type() const
{
	return kJobTerminated;
}


/**
 * @brief Formats the log message with the job name and termination status.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
JobTerminatedLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("Job \"%s\" terminated: %s", fJob->Name(),
		strerror(fStatus));
	return B_OK;
}


/**
 * @brief Adds the job name and termination status to the parameter message.
 *
 * @param parameter The output BMessage.
 * @return B_OK on success.
 */
status_t
JobTerminatedLogItem::GetParameter(BMessage& parameter) const
{
	status_t status = AbstractJobLogItem::GetParameter(parameter);
	if (status == B_OK)
		status = parameter.AddInt32("status", fStatus);
	return status;
}


// #pragma mark - JobEnabledLogItem


/**
 * @brief Constructs a job-enabled log item.
 *
 * @param job     The job whose enabled state changed.
 * @param enabled The new enabled state.
 */
JobEnabledLogItem::JobEnabledLogItem(Job* job, bool enabled)
	:
	AbstractJobLogItem(job),
	fEnabled(enabled)
{
}


/** @brief Destroys the job-enabled log item. */
JobEnabledLogItem::~JobEnabledLogItem()
{
}


/** @brief Returns kJobEnabled. */
LogItemType
JobEnabledLogItem::Type() const
{
	return kJobEnabled;
}


/**
 * @brief Formats the log message indicating the job was enabled or disabled.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
JobEnabledLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("Job \"%s\" %sabled", fJob->Name(),
		fEnabled ? "en" : "dis");
	return B_OK;
}


/**
 * @brief Adds the job name and enabled flag to the parameter message.
 *
 * @param parameter The output BMessage.
 * @return B_OK on success.
 */
status_t
JobEnabledLogItem::GetParameter(BMessage& parameter) const
{
	status_t status = AbstractJobLogItem::GetParameter(parameter);
	if (status == B_OK)
		status = parameter.AddBool("enabled", fEnabled);
	return status;
}


// #pragma mark - JobStoppedLogItem


/**
 * @brief Constructs a job-stopped log item.
 *
 * @param job   The stopped job.
 * @param force Whether the stop was forced.
 */
JobStoppedLogItem::JobStoppedLogItem(BaseJob* job, bool force)
	:
	AbstractJobLogItem(job),
	fForce(force)
{
}


/** @brief Destroys the job-stopped log item. */
JobStoppedLogItem::~JobStoppedLogItem()
{
}


/** @brief Returns kJobStopped. */
LogItemType
JobStoppedLogItem::Type() const
{
	return kJobStopped;
}


/**
 * @brief Formats the log message indicating the job was stopped (possibly forced).
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
JobStoppedLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("Job \"%s\" %sstopped", fJob->Name(),
		fForce ? "force " : "");
	return B_OK;
}


/**
 * @brief Adds the job name and force flag to the parameter message.
 *
 * @param parameter The output BMessage.
 * @return B_OK on success.
 */
status_t
JobStoppedLogItem::GetParameter(BMessage& parameter) const
{
	status_t status = AbstractJobLogItem::GetParameter(parameter);
	if (status == B_OK)
		status = parameter.AddBool("force", fForce);
	return status;
}


// #pragma mark - EventLogItem


/**
 * @brief Constructs an event log item for a triggered event.
 *
 * @param job   The job that owns the event.
 * @param event The event that was triggered.
 */
EventLogItem::EventLogItem(BaseJob* job, Event* event)
	:
	AbstractJobLogItem(job),
	fEvent(event)
{
}


/** @brief Destroys the event log item. */
EventLogItem::~EventLogItem()
{
}


/** @brief Returns kEvent. */
LogItemType
EventLogItem::Type() const
{
	return kEvent;
}


/**
 * @brief Formats the log message with job name and event description.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
EventLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("Event triggered \"%s\": \"%s\"", fJob->Name(),
		fEvent->ToString().String());
	return B_OK;
}


/**
 * @brief Adds the job name and event description to the parameter message.
 *
 * @param parameter The output BMessage.
 * @return B_OK on success.
 */
status_t
EventLogItem::GetParameter(BMessage& parameter) const
{
	status_t status = AbstractJobLogItem::GetParameter(parameter);
	if (status == B_OK)
		status = parameter.AddString("event", fEvent->ToString());
	return status;
}


/**
 * @brief Tests whether this event log item matches the given filter criteria.
 *
 * @param jobName   Job name filter (NULL matches all).
 * @param eventName Event name substring filter (NULL matches all).
 * @return @c true if this item matches both filters.
 */
bool
EventLogItem::Matches(const char* jobName, const char* eventName)
{
	if (eventName != NULL && strstr(fEvent->ToString(), eventName) == NULL)
		return false;

	return AbstractJobLogItem::Matches(jobName, NULL);
}


// #pragma mark - ExternalEventLogItem


/**
 * @brief Constructs an abstract external event log item.
 *
 * @param name The external event name.
 */
AbstractExternalEventLogItem::AbstractExternalEventLogItem(const char* name)
	:
	fEventName(name)
{
}


/** @brief Destroys the abstract external event log item. */
AbstractExternalEventLogItem::~AbstractExternalEventLogItem()
{
}


/**
 * @brief Adds the event name to the parameter message.
 *
 * @param parameter The output BMessage.
 * @return B_OK on success.
 */
status_t
AbstractExternalEventLogItem::GetParameter(BMessage& parameter) const
{
	return parameter.AddString("event", fEventName);
}


/**
 * @brief Tests whether this external event log item matches the given filter criteria.
 *
 * @param jobName   Job name filter (unused for external events).
 * @param eventName Event name substring filter (NULL matches all).
 * @return @c true if this item matches.
 */
bool
AbstractExternalEventLogItem::Matches(const char* jobName,
	const char* eventName)
{
	if (jobName == NULL && eventName == NULL)
		return true;

	if (eventName != NULL && strstr(fEventName.String(), eventName) != NULL)
		return true;

	return false;
}


// #pragma mark - ExternalEventLogItem


/** @brief Constructs an external event triggered log item. */
ExternalEventLogItem::ExternalEventLogItem(const char* name)
	:
	AbstractExternalEventLogItem(name)
{
}


/** @brief Destroys the external event log item. */
ExternalEventLogItem::~ExternalEventLogItem()
{
}


/** @brief Returns kExternalEvent. */
LogItemType
ExternalEventLogItem::Type() const
{
	return kExternalEvent;
}


/**
 * @brief Formats the log message indicating an external event was triggered.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
ExternalEventLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("External event triggered: \"%s\"",
		fEventName.String());
	return B_OK;
}


// #pragma mark - ExternalEventRegisteredLogItem


/** @brief Constructs an external event registered log item. */
ExternalEventRegisteredLogItem::ExternalEventRegisteredLogItem(const char* name)
	:
	AbstractExternalEventLogItem(name)
{
}


/** @brief Destroys the external event registered log item. */
ExternalEventRegisteredLogItem::~ExternalEventRegisteredLogItem()
{
}


/** @brief Returns kExternalEventRegistered. */
LogItemType
ExternalEventRegisteredLogItem::Type() const
{
	return kExternalEventRegistered;
}


/**
 * @brief Formats the log message indicating an external event was registered.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
ExternalEventRegisteredLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("External event registered: \"%s\"",
		fEventName.String());
	return B_OK;
}


// #pragma mark - ExternalEventUnregisteredLogItem


/** @brief Constructs an external event unregistered log item. */
ExternalEventUnregisteredLogItem::ExternalEventUnregisteredLogItem(
	const char* name)
	:
	AbstractExternalEventLogItem(name)
{
}


/** @brief Destroys the external event unregistered log item. */
ExternalEventUnregisteredLogItem::~ExternalEventUnregisteredLogItem()
{
}


/** @brief Returns kExternalEventUnregistered. */
LogItemType
ExternalEventUnregisteredLogItem::Type() const
{
	return kExternalEventUnregistered;
}


/**
 * @brief Formats the log message indicating an external event was unregistered.
 *
 * @param target Output string to receive the message.
 * @return B_OK always.
 */
status_t
ExternalEventUnregisteredLogItem::GetMessage(BString& target) const
{
	target.SetToFormat("External event unregistered: \"%s\"",
		fEventName.String());
	return B_OK;
}
