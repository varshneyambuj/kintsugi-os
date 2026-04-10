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
   Copyright 2005, Ingo Weinhold, bonefish@users.sf.net. All rights reserved.
   Distributed under the terms of the MIT License.
 */

/** @file MessageDeliverer.h
 *  @brief Singleton message delivery service that batches and sends BMessages to target ports. */
#ifndef MESSAGE_DELIVERER_H
#define MESSAGE_DELIVERER_H

#include <Locker.h>
#include <Messenger.h>

struct messaging_target;

// MessagingTargetSet
/** @brief Abstract interface for iterating over a set of messaging targets. */
class MessagingTargetSet {
public:
	virtual ~MessagingTargetSet();

	/** @brief Returns whether more targets remain in the iteration. */
	virtual bool HasNext() const = 0;
	/** @brief Retrieves the next target port and token. */
	virtual bool Next(port_id &port, int32 &token) = 0;
	/** @brief Resets iteration to the first target. */
	virtual void Rewind() = 0;
};

// DefaultMessagingTargetSet
/** @brief Iterates over a C array of messaging_target structs. */
class DefaultMessagingTargetSet : public MessagingTargetSet {
public:
	DefaultMessagingTargetSet(const messaging_target *targets,
		int32 targetCount);
	virtual ~DefaultMessagingTargetSet();

	/** @brief Returns whether more targets remain in the iteration. */
	virtual bool HasNext() const;
	/** @brief Retrieves the next target port and token. */
	virtual bool Next(port_id &port, int32 &token);
	/** @brief Resets iteration to the first target. */
	virtual void Rewind();

private:
	const messaging_target	*fTargets;
	int32					fTargetCount;
	int32					fNextIndex;
};

// SingleMessagingTargetSet
/** @brief Wraps a single BMessenger or port/token pair as a target set. */
class SingleMessagingTargetSet : public MessagingTargetSet {
public:
	SingleMessagingTargetSet(BMessenger target);
	SingleMessagingTargetSet(port_id port, int32 token);
	virtual ~SingleMessagingTargetSet();

	/** @brief Returns whether more targets remain in the iteration. */
	virtual bool HasNext() const;
	/** @brief Retrieves the next target port and token. */
	virtual bool Next(port_id &port, int32 &token);
	/** @brief Resets iteration to the first target. */
	virtual void Rewind();

private:
	port_id				fPort;
	int32				fToken;
	bool				fAtBeginning;
};

// MessageDeliverer
/** @brief Singleton that queues and delivers BMessages to target ports via a background thread. */
class MessageDeliverer {
private:
	MessageDeliverer();
	~MessageDeliverer();

	status_t Init();

public:
	/** @brief Creates the singleton MessageDeliverer instance. */
	static status_t CreateDefault();
	/** @brief Destroys the singleton MessageDeliverer instance. */
	static void DeleteDefault();
	/** @brief Returns the singleton MessageDeliverer instance. */
	static MessageDeliverer *Default();

	/** @brief Queues a message for delivery to one or more targets. */
	status_t DeliverMessage(BMessage *message, BMessenger target,
		bigtime_t timeout = B_INFINITE_TIMEOUT);
	/** @brief Queues a message for delivery to one or more targets. */
	status_t DeliverMessage(BMessage *message, MessagingTargetSet &targets,
		bigtime_t timeout = B_INFINITE_TIMEOUT);
	/** @brief Queues a message for delivery to one or more targets. */
	status_t DeliverMessage(const void *message, int32 messageSize,
		MessagingTargetSet &targets, bigtime_t timeout = B_INFINITE_TIMEOUT);

private:
	class Message;
	class TargetMessage;
	class TargetMessageHandle;
	class TargetPort;
	struct TargetPortMap;

	TargetPort *_GetTargetPort(port_id portID, bool create = false);
	void _PutTargetPort(TargetPort *port);

	status_t _SendMessage(Message *message, port_id portID, int32 token);

	static int32 _DelivererThreadEntry(void *data);
	int32 _DelivererThread();

	static MessageDeliverer	*sDeliverer;

	BLocker			fLock;
	TargetPortMap	*fTargetPorts;
	thread_id		fDelivererThread;
	volatile bool	fTerminating;
};

#endif	// MESSAGE_DELIVERER_H
