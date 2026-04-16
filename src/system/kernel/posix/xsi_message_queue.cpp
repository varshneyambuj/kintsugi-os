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
 *   Copyright 2008-2023, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *   		Salvatore Benedetto <salvatore.benedetto@gmail.com>
 */

/**
 * @file xsi_message_queue.cpp
 * @brief XSI (System V) IPC message queue kernel implementation.
 *
 * Implements msgget(), msgctl(), msgsnd(), msgrcv() as defined by the
 * X/Open System Interfaces. Two hash tables are maintained: sIpcHashTable
 * maps the caller-visible key_t to an Ipc wrapper that carries the current
 * associated message queue id, and sMessageQueueHashTable maps integer
 * message queue ids onto XsiMessageQueue objects. Each queue owns a
 * doubly-linked list of queued_message entries, a permission record, a
 * mutex, and two ConditionVariables used to block senders (when the queue
 * is full or the global MAX_XSI_MESSAGE cap is reached) and receivers
 * (when they need a message that is not yet available). EIDRM is reported
 * back to waiters if the queue is destroyed while they sleep, which is
 * detected by snapshotting the queue's sequence number before blocking.
 */

#include <posix/xsi_message_queue.h>

#include <new>

#include <sys/ipc.h>
#include <sys/types.h>

#include <OS.h>

#include <kernel.h>
#include <syscall_restart.h>

#include <util/atomic.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <util/OpenHashTable.h>


#define TRACE_XSI_MSG_QUEUE
#ifdef TRACE_XSI_MSG_QUEUE
#	define TRACE(x)			dprintf x
#	define TRACE_ERROR(x)	dprintf x
#else
#	define TRACE(x)			/* nothing */
#	define TRACE_ERROR(x)	dprintf x
#endif


namespace {


/**
 * @brief One message sitting on a System V message queue.
 *
 * Stores the message type (the first long of the user buffer, per XSI) and
 * the raw payload that follows. The initOK flag tells callers whether the
 * constructor successfully copied in the user data.
 */
struct queued_message : DoublyLinkedListLinkImpl<queued_message> {
	/**
	 * @brief Copy a user-space message into a newly allocated kernel buffer.
	 *
	 * Splits the first sizeof(long) bytes into the type field and copies the
	 * remaining _length bytes into a heap buffer. On any copy failure the
	 * buffer is freed and initOK stays false.
	 *
	 * @param _message User pointer to the msgbuf (type followed by payload).
	 * @param _length Payload length in bytes (excluding the type prefix).
	 */
	queued_message(const void *_message, ssize_t _length)
		:
		initOK(false),
		length(_length)
	{
		message = (char *)malloc(sizeof(char) * _length);
		if (message == NULL)
			return;

		if (user_memcpy(&type, _message, sizeof(long)) != B_OK
			|| user_memcpy(message, (void *)((char *)_message + sizeof(long)),
			_length) != B_OK) {
			free(message);
			return;
		}
		initOK = true;
	}

	/**
	 * @brief Free the payload buffer if the constructor succeeded.
	 */
	~queued_message()
	{
		if (initOK)
			free(message);
	}

	/**
	 * @brief Copy this message back out to a user buffer for msgrcv().
	 *
	 * Writes the type word and up to _length bytes of payload. The caller is
	 * responsible for policing MSG_NOERROR / E2BIG before calling this.
	 *
	 * @param _message User pointer to receive type + payload.
	 * @param _length Maximum payload bytes the user buffer can accept.
	 * @return Actual payload bytes copied, or B_ERROR on copy failure.
	 */
	ssize_t copy_to_user_buffer(void *_message, ssize_t _length)
	{
		if (_length > length)
			_length = length;

		if (user_memcpy(_message, &type, sizeof(long)) != B_OK
			|| user_memcpy((void *)((char *)_message + sizeof(long)), message,
			_length) != B_OK)
			return B_ERROR;
		return _length;
	}

	bool		initOK;
	ssize_t		length;
	char		*message;
	long		type;
};

typedef DoublyLinkedList<queued_message> MessageQueue;

// Arbitrary limit
#define MAX_BYTES_PER_QUEUE		2048

/**
 * @brief A single System V message queue and its waiter bookkeeping.
 *
 * Wraps the msqid_ds public record together with the private linked list of
 * messages, the queue-level mutex, the byte-count accumulator used for the
 * msg_qbytes cap, and two ConditionVariables used to rendezvous senders
 * against space and receivers against messages.
 */
class XsiMessageQueue {
public:
	/**
	 * @brief Construct an empty queue, initialising all msqid_ds fields.
	 *
	 * @param flags Caller-supplied permission bits (low nine bits of flags).
	 */
	XsiMessageQueue(int flags)
		:
		fBytesInQueue(0)
	{
		mutex_init(&fLock, "XsiMessageQueue private mutex");
		fWaitingToReceive.Init(this, "XsiMessageQueue");
		fWaitingToSend.Init(this, "XsiMessageQueue");

		SetIpcKey((key_t)-1);
		SetPermissions(flags);
		// Initialize all fields to zero
		memset((void *)&fMessageQueue, 0, sizeof(struct msqid_ds));
		fMessageQueue.msg_ctime = (time_t)real_time_clock();
		fMessageQueue.msg_qbytes = MAX_BYTES_PER_QUEUE;
	}

	// Implemented after sXsiMessageCount is declared
	~XsiMessageQueue();

	/**
	 * @brief Drop the queue lock and block on a condition variable.
	 *
	 * The wait is always interruptible, so a signal converts the return into
	 * B_INTERRUPTED (reported up as EINTR). The caller re-acquires the lock
	 * explicitly after waking.
	 *
	 * @param queueEntry Pre-registered entry on fWaitingToReceive/fWaitingToSend.
	 * @param queueLocker Locker owning fLock; unlocked for the duration.
	 * @return B_OK on normal wake, B_INTERRUPTED on signal, or EIDRM if the
	 *         queue was destroyed.
	 */
	status_t BlockAndUnlock(ConditionVariableEntry *queueEntry, MutexLocker *queueLocker)
	{
		// Unlock the queue before blocking
		queueLocker->Unlock();
		return queueEntry->Wait(B_CAN_INTERRUPT);
	}

	/**
	 * @brief Apply a user-supplied msqid_ds to this queue (IPC_SET handler).
	 *
	 * Updates uid, gid, mode bits, msg_qbytes, and bumps msg_ctime.
	 *
	 * @param result User-supplied msqid_ds already copied into kernel memory.
	 */
	void DoIpcSet(struct msqid_ds *result)
	{
		fMessageQueue.msg_perm.uid = result->msg_perm.uid;
		fMessageQueue.msg_perm.gid = result->msg_perm.gid;
		fMessageQueue.msg_perm.mode = (fMessageQueue.msg_perm.mode & ~0x01ff)
			| (result->msg_perm.mode & 0x01ff);
		fMessageQueue.msg_qbytes = result->msg_qbytes;
		fMessageQueue.msg_ctime = (time_t)real_time_clock();
	}

	/**
	 * @brief Remove a waiter's entry without actually sleeping.
	 *
	 * Used on the EINTR path to unregister the entry from the condition
	 * variable by issuing a zero-timeout wait.
	 *
	 * @param queueEntry The entry to remove.
	 */
	void Dequeue(ConditionVariableEntry *queueEntry)
	{
		queueEntry->Wait(B_RELATIVE_TIMEOUT, 0);
	}

	/**
	 * @brief Register a waiter on the receive or send condition variable.
	 *
	 * @param queueEntry Entry to register.
	 * @param waitForMessage true for msgrcv() waiters, false for msgsnd().
	 */
	void Enqueue(ConditionVariableEntry *queueEntry, bool waitForMessage)
	{
		if (waitForMessage) {
			fWaitingToReceive.Add(queueEntry);
		} else {
			fWaitingToSend.Add(queueEntry);
		}
	}

	/**
	 * @brief Access the raw msqid_ds status record (used by IPC_STAT).
	 *
	 * @return Reference to the internal msqid_ds.
	 */
	struct msqid_ds &GetMessageQueue()
	{
		return fMessageQueue;
	}

	/**
	 * @brief Write-permission check honoring owner/group/other mode bits.
	 *
	 * Returns true when S_IWOTH is set, when the caller is root, when the
	 * caller owns the queue and S_IWUSR is set, or when the caller's gid
	 * matches and S_IWGRP is set.
	 *
	 * @return true if the caller may modify this queue.
	 */
	bool HasPermission() const
	{
		if ((fMessageQueue.msg_perm.mode & S_IWOTH) != 0)
			return true;

		uid_t uid = geteuid();
		if (uid == 0 || (uid == fMessageQueue.msg_perm.uid
			&& (fMessageQueue.msg_perm.mode & S_IWUSR) != 0))
			return true;

		gid_t gid = getegid();
		if (gid == fMessageQueue.msg_perm.gid
			&& (fMessageQueue.msg_perm.mode & S_IWGRP) != 0)
			return true;

		return false;
	}

	/**
	 * @brief Read-permission check (currently identical to HasPermission).
	 *
	 * @return true if the caller may read status from this queue.
	 */
	bool HasReadPermission() const
	{
		// TODO: fix this
		return HasPermission();
	}

	/**
	 * @brief Accessor for the kernel-assigned integer msqid.
	 *
	 * @return This queue's message queue id.
	 */
	int ID() const
	{
		return fID;
	}

	// Implemented after sXsiMessageCount is declared
	bool Insert(queued_message *message);

	/**
	 * @brief Accessor for the caller-facing key_t key this queue is bound to.
	 *
	 * @return Associated key, or (key_t)-1 for a private (IPC_PRIVATE) queue.
	 */
	key_t IpcKey() const
	{
		return fMessageQueue.msg_perm.key;
	}

	/**
	 * @brief Accessor for the queue-level mutex.
	 *
	 * @return Reference to the mutex guarding this queue's state.
	 */
	mutex &Lock()
	{
		return fLock;
	}

	/**
	 * @brief Current msg_qbytes cap (max bytes allowed in this queue).
	 *
	 * @return The cap in bytes.
	 */
	msglen_t MaxBytes() const
	{
		return fMessageQueue.msg_qbytes;
	}

	// Implemented after sXsiMessageCount is declared
	queued_message *Remove(long typeRequested);

	/**
	 * @brief Version counter used by waiters to detect IPC_RMID under them.
	 *
	 * @return Sequence number taken when this queue was created.
	 */
	uint32 SequenceNumber() const
	{
		return fSequenceNumber;
	}

	// Implemented after sMessageQueueHashTable is declared
	void SetID();

	/**
	 * @brief Change this queue's associated IPC key.
	 *
	 * @param key New key, or (key_t)-1 to mark the queue private.
	 */
	void SetIpcKey(key_t key)
	{
		fMessageQueue.msg_perm.key = key;
	}

	/**
	 * @brief Initialise owner/creator uid and gid plus the permission bits.
	 *
	 * Invariant: after this call the cuid/cgid fields are fixed for the
	 * lifetime of the queue and uid/gid are writable by subsequent IPC_SET.
	 *
	 * @param flags Low nine bits carry the permission mode.
	 */
	void SetPermissions(int flags)
	{
		fMessageQueue.msg_perm.uid = fMessageQueue.msg_perm.cuid = geteuid();
		fMessageQueue.msg_perm.gid = fMessageQueue.msg_perm.cgid = getegid();
		fMessageQueue.msg_perm.mode = (flags & 0x01ff);
	}

	/**
	 * @brief Wake receivers (all) or senders (one) depending on direction.
	 *
	 * Receivers are woken en-masse because arbitrary messageType selection
	 * means a single wake cannot know which thread will find a match.
	 * Senders get a single NotifyOne() because space freed by one Remove()
	 * only suffices for one Insert().
	 *
	 * @param waitForMessage true to wake receivers, false to wake a sender.
	 */
	void WakeUpThread(bool waitForMessage)
	{
		if (waitForMessage) {
			// Wake up all waiting thread for a message
			// TODO: this can cause starvation for any
			// very-unlucky-and-slow thread
			fWaitingToReceive.NotifyAll();
		} else {
			// Wake up only one thread waiting to send
			fWaitingToSend.NotifyOne();
		}
	}

	/**
	 * @brief Accessor used by the hash table for its intrusive chain link.
	 *
	 * @return Reference to the hash-link next pointer.
	 */
	XsiMessageQueue*& Link()
	{
		return fLink;
	}

private:
	msglen_t			fBytesInQueue;
	int					fID;
	mutex				fLock;
	MessageQueue		fMessage;
	struct msqid_ds		fMessageQueue;
	uint32				fSequenceNumber;

	ConditionVariable	fWaitingToReceive;
	ConditionVariable	fWaitingToSend;

	XsiMessageQueue*	fLink;
};


/**
 * @brief Open-hash-table policy keying XsiMessageQueue entries by msqid.
 */
struct MessageQueueHashTableDefinition {
	typedef int					KeyType;
	typedef XsiMessageQueue		ValueType;

	/**
	 * @brief Hash an integer msqid into a table slot.
	 *
	 * @param key msqid.
	 * @return Hash value.
	 */
	size_t HashKey (const int key) const
	{
		return (size_t)key;
	}

	/**
	 * @brief Hash an existing queue by its stored msqid.
	 *
	 * @param variable Queue entry.
	 * @return Hash value matching that of its id.
	 */
	size_t Hash(XsiMessageQueue *variable) const
	{
		return (size_t)variable->ID();
	}

	/**
	 * @brief Compare a lookup key against an entry by msqid equality.
	 *
	 * @param key Lookup msqid.
	 * @param variable Candidate entry.
	 * @return true when ids match.
	 */
	bool Compare(const int key, XsiMessageQueue *variable) const
	{
		return (int)key == (int)variable->ID();
	}

	/**
	 * @brief Return the chain-link field embedded in the queue.
	 *
	 * @param variable Entry whose link is needed.
	 * @return Reference to the hash-link next pointer.
	 */
	XsiMessageQueue*& GetLink(XsiMessageQueue *variable) const
	{
		return variable->Link();
	}
};


/**
 * @brief Key-to-msqid mapping stored in sIpcHashTable.
 *
 * Every non-IPC_PRIVATE key that has ever been requested has one of these;
 * it is deleted when the corresponding queue is removed via IPC_RMID.
 */
class Ipc {
public:
	/**
	 * @brief Construct an Ipc entry bound to a key with no queue yet.
	 *
	 * @param key XSI IPC key this entry represents.
	 */
	Ipc(key_t key)
		: fKey(key),
		fMessageQueueId(-1)
	{
	}

	/**
	 * @brief Accessor for the IPC key.
	 *
	 * @return The key this entry is associated with.
	 */
	key_t Key() const
	{
		return fKey;
	}

	/**
	 * @brief Accessor for the currently associated queue id (-1 if none).
	 *
	 * @return msqid, or -1 when unbound.
	 */
	int MessageQueueID() const
	{
		return fMessageQueueId;
	}

	/**
	 * @brief Bind this key entry to a queue by copying its id.
	 *
	 * @param messageQueue The queue to bind.
	 */
	void SetMessageQueueID(XsiMessageQueue *messageQueue)
	{
		fMessageQueueId = messageQueue->ID();
	}

	/**
	 * @brief Accessor used by the IPC hash table for its chain link.
	 *
	 * @return Reference to the hash-link next pointer.
	 */
	Ipc*& Link()
	{
		return fLink;
	}

private:
	key_t				fKey;
	int					fMessageQueueId;
	Ipc*				fLink;
};


/**
 * @brief Open-hash-table policy keying Ipc entries by key_t.
 */
struct IpcHashTableDefinition {
	typedef key_t	KeyType;
	typedef Ipc		ValueType;

	/**
	 * @brief Hash an IPC key_t into a table slot.
	 *
	 * @param key Lookup key.
	 * @return Hash value.
	 */
	size_t HashKey (const key_t key) const
	{
		return (size_t)(key);
	}

	/**
	 * @brief Hash an existing Ipc entry by its stored key.
	 *
	 * @param variable The Ipc entry.
	 * @return Hash value matching the entry's key.
	 */
	size_t Hash(Ipc *variable) const
	{
		return (size_t)HashKey(variable->Key());
	}

	/**
	 * @brief Compare a key against an entry's key for equality.
	 *
	 * @param key Lookup key.
	 * @param variable Candidate Ipc entry.
	 * @return true when the keys match.
	 */
	bool Compare(const key_t key, Ipc *variable) const
	{
		return (key_t)key == (key_t)variable->Key();
	}

	/**
	 * @brief Expose the chain-link field used by the hash table.
	 *
	 * @param variable Ipc entry whose link is needed.
	 * @return Reference to the hash-link next pointer.
	 */
	Ipc*& GetLink(Ipc *variable) const
	{
		return variable->Link();
	}
};

} // namespace


// Arbitrary limits
#define MAX_XSI_MESSAGE			4096
#define MAX_XSI_MESSAGE_QUEUE	1024
static BOpenHashTable<IpcHashTableDefinition> sIpcHashTable;
static BOpenHashTable<MessageQueueHashTableDefinition> sMessageQueueHashTable;

static mutex sIpcLock;
static mutex sXsiMessageQueueLock;

static uint32 sGlobalSequenceNumber = 1;
static int32 sXsiMessageCount = 0;
static int32 sXsiMessageQueueCount = 0;


//	#pragma mark -


/**
 * @brief Destroy the queue, waking any sleepers with EIDRM and freeing messages.
 *
 * Defined outside the class so it can reach the sXsiMessageCount global.
 * All threads blocked on either condition variable are notified with the
 * EIDRM error code so they can report removal to userland per XSI.
 */
XsiMessageQueue::~XsiMessageQueue()
{
	mutex_destroy(&fLock);

	// Wake up any threads still waiting
	fWaitingToReceive.NotifyAll(EIDRM);
	fWaitingToSend.NotifyAll(EIDRM);

	// Free up any remaining messages
	if (fMessageQueue.msg_qnum) {
		while (queued_message *message = fMessage.RemoveHead()) {
			atomic_add(&sXsiMessageCount, -1);
			delete message;
		}
	}
}


/**
 * @brief Try to append a message to the queue; report whether the caller must wait.
 *
 * Returns true (the caller should block) when either the byte budget would
 * be exceeded or the global MAX_XSI_MESSAGE cap is reached. Otherwise the
 * message is linked in, counters are updated, msg_lspid/msg_stime are
 * refreshed, and any waiting receivers are woken.
 *
 * @param message Pre-built queued_message transferring its storage into the queue.
 * @return true if the caller needs to wait for space, false if the message
 *         was successfully enqueued.
 */
bool
XsiMessageQueue::Insert(queued_message *message)
{
	// The only situation that would make us (potentially) wait
	// is that we exceed with bytes or with the total number of messages
	if (fBytesInQueue + message->length > fMessageQueue.msg_qbytes)
		return true;

	while (true) {
		int32 oldCount = atomic_get(&sXsiMessageCount);
		if (oldCount >= MAX_XSI_MESSAGE)
			return true;
		// If another thread updates the counter we keep
		// iterating
		if (atomic_test_and_set(&sXsiMessageCount, oldCount + 1, oldCount)
			== oldCount)
			break;
	}

	fMessage.Add(message);
	fMessageQueue.msg_qnum++;
	fMessageQueue.msg_lspid = getpid();
	fMessageQueue.msg_stime = real_time_clock();
	fBytesInQueue += message->length;

	WakeUpThread(true /* WaitForMessage */);
	return false;
}


/**
 * @brief Remove and return a message matching the XSI msgtyp semantics.
 *
 * Behaviour tracks msgrcv(): typeRequested == 0 picks the head, > 0 picks
 * the first message with a matching type, and < 0 picks the first message
 * whose type is <= |typeRequested| (lowest-type first). Updates the
 * msg_qnum/msg_rtime/msg_lrpid fields and wakes one sender on success.
 *
 * @param typeRequested XSI msgtyp selector.
 * @return Detached queued_message pointer, or NULL if nothing matched.
 */
queued_message*
XsiMessageQueue::Remove(long typeRequested)
{
	queued_message *message = NULL;
	if (typeRequested < 0) {
		// Return first message of the lowest type
		// that is less than or equal to the absolute
		// value of type requested.
		MessageQueue::Iterator iterator = fMessage.GetIterator();
		while (iterator.HasNext()) {
			queued_message *current = iterator.Next();
			if (current->type <= -typeRequested) {
				message = iterator.Remove();
				break;
			}
		}
	} else if (typeRequested == 0) {
		// Return the first message on the queue
		message = fMessage.RemoveHead();
	} else {
		// Return the first message of type requested
		MessageQueue::Iterator iterator = fMessage.GetIterator();
		while (iterator.HasNext()) {
			queued_message *current = iterator.Next();
			if (current->type == typeRequested) {
				message = iterator.Remove();
				break;
			}
		}
	}

	if (message == NULL)
		return NULL;

	fMessageQueue.msg_qnum--;
	fMessageQueue.msg_lrpid = getpid();
	fMessageQueue.msg_rtime = real_time_clock();
	fBytesInQueue -= message->length;
	atomic_add(&sXsiMessageCount, -1);

	WakeUpThread(false /* WaitForMessage */);
	return message;
}


/**
 * @brief Assign a unique msqid and sequence number under the global lock.
 *
 * Invariant: the caller holds sXsiMessageQueueLock. Starts the id at the
 * current real-time clock and linearly probes forward until the id is not
 * already claimed, then advances the wrapping sGlobalSequenceNumber.
 */
void
XsiMessageQueue::SetID()
{
	fID = real_time_clock();
	// The lock is held before calling us
	while (true) {
		if (sMessageQueueHashTable.Lookup(fID) == NULL)
			break;
		fID++;
	}
	sGlobalSequenceNumber = (sGlobalSequenceNumber + 1) % UINT_MAX;
	fSequenceNumber = sGlobalSequenceNumber;
}


//	#pragma mark - Kernel exported API


/**
 * @brief One-time kernel initialiser for the XSI message queue subsystem.
 *
 * Initialises both hash tables and their guarding mutexes; panics if either
 * hash table fails to allocate.
 */
void
xsi_msg_init()
{
	// Initialize hash tables
	status_t status = sIpcHashTable.Init();
	if (status != B_OK)
		panic("xsi_msg_init() failed to initialize ipc hash table\n");
	status =  sMessageQueueHashTable.Init();
	if (status != B_OK)
		panic("xsi_msg_init() failed to initialize message queue hash table\n");

	mutex_init(&sIpcLock, "global POSIX message queue IPC table");
	mutex_init(&sXsiMessageQueueLock, "global POSIX xsi message queue table");
}


//	#pragma mark - Syscalls


/**
 * @brief Syscall entry point for msgctl().
 *
 * Dispatches IPC_STAT (copies msqid_ds out after a read-permission check),
 * IPC_SET (applies a user-supplied msqid_ds after a write-permission check,
 * forbidding non-root users from raising msg_qbytes), and IPC_RMID (removes
 * the queue and drops the sIpcHashTable entry; waiters are woken with EIDRM
 * via the destructor). The ipc and message-queue hash locks are released
 * early for the non-RMID path so that concurrent callers are not serialised
 * behind long IPC_SET userland copies.
 *
 * @param messageQueueID msqid returned by msgget().
 * @param command One of IPC_STAT, IPC_SET, IPC_RMID.
 * @param buffer User pointer to an msqid_ds (required for STAT and SET).
 * @return B_OK (0) on success, otherwise EINVAL, EPERM, EACCES, or
 *         B_BAD_ADDRESS.
 */
int
_user_xsi_msgctl(int messageQueueID, int command, struct msqid_ds *buffer)
{
	TRACE(("xsi_msgctl: messageQueueID = %d, command = %d\n", messageQueueID, command));
	MutexLocker ipcHashLocker(sIpcLock);
	MutexLocker messageQueueHashLocker(sXsiMessageQueueLock);
	XsiMessageQueue *messageQueue = sMessageQueueHashTable.Lookup(messageQueueID);
	if (messageQueue == NULL) {
		TRACE(("xsi_msgctl: message queue id %d not valid\n", messageQueueID));
		return EINVAL;
	}
	if (buffer != NULL && !IS_USER_ADDRESS(buffer)) {
		TRACE(("xsi_msgctl: buffer address is not valid\n"));
		return B_BAD_ADDRESS;
	}

	// Lock the message queue itself and release both the ipc hash table lock
	// and the message queue hash table lock _only_ if the command it's not
	// IPC_RMID, this prevents undesidered situation from happening while
	// (hopefully) improving the concurrency.
	MutexLocker messageQueueLocker;
	if (command != IPC_RMID) {
		messageQueueLocker.SetTo(&messageQueue->Lock(), false);
		messageQueueHashLocker.Unlock();
		ipcHashLocker.Unlock();
	} else
		// Since we are going to delete the message queue object
		// along with its mutex, we can't use a MutexLocker object,
		// as the mutex itself won't exist on function exit
		mutex_lock(&messageQueue->Lock());

	switch (command) {
		case IPC_STAT: {
			if (!messageQueue->HasReadPermission()) {
				TRACE(("xsi_msgctl: calling process has not read "
					"permission on message queue %d, key %d\n", messageQueueID,
					(int)messageQueue->IpcKey()));
				return EACCES;
			}
			struct msqid_ds msg = messageQueue->GetMessageQueue();
			if (user_memcpy(buffer, &msg, sizeof(struct msqid_ds)) < B_OK) {
				TRACE_ERROR(("xsi_msgctl: user_memcpy failed\n"));
				return B_BAD_ADDRESS;
			}
			break;
		}

		case IPC_SET: {
			if (!messageQueue->HasPermission()) {
				TRACE(("xsi_msgctl: calling process has not permission "
					"on message queue %d, key %d\n", messageQueueID,
					(int)messageQueue->IpcKey()));
				return EPERM;
			}
			struct msqid_ds msg;
			if (user_memcpy(&msg, buffer, sizeof(struct msqid_ds)) < B_OK) {
				TRACE_ERROR(("xsi_msgctl: user_memcpy failed\n"));
				return B_BAD_ADDRESS;
			}
			if (msg.msg_qbytes > messageQueue->MaxBytes() && getuid() != 0) {
				TRACE(("xsi_msgctl: user does not have permission to "
					"increase the maximum number of bytes allowed on queue\n"));
				return EPERM;
			}
			if (msg.msg_qbytes == 0) {
				TRACE(("xsi_msgctl: can't set msg_qbytes to 0!\n"));
				return EINVAL;
			}

			messageQueue->DoIpcSet(&msg);
			break;
		}

		case IPC_RMID: {
			// If this was the command, we are still holding the message
			// queue hash table lock along with the ipc one, but not the
			// message queue lock itself. This prevents other process
			// to try and acquire a destroyed mutex
			if (!messageQueue->HasPermission()) {
				TRACE(("xsi_msgctl: calling process has not permission "
					"on message queue %d, key %d\n", messageQueueID,
					(int)messageQueue->IpcKey()));
				return EPERM;
			}
			key_t key = messageQueue->IpcKey();
			Ipc *ipcKey = NULL;
			if (key != -1) {
				ipcKey = sIpcHashTable.Lookup(key);
				sIpcHashTable.Remove(ipcKey);
			}
			sMessageQueueHashTable.Remove(messageQueue);
			// Wake up of any threads waiting on this
			// queue happens in destructor
			if (key != -1)
				delete ipcKey;
			atomic_add(&sXsiMessageQueueCount, -1);

			delete messageQueue;
			break;
		}

		default:
			TRACE_ERROR(("xsi_semctl: command %d not valid\n", command));
			return EINVAL;
	}

	return B_OK;
}


/**
 * @brief Syscall entry point for msgget().
 *
 * Looks up (or allocates) the Ipc record for the key, honours IPC_CREAT and
 * IPC_EXCL semantics, and creates a new XsiMessageQueue when required.
 * IPC_PRIVATE always forces creation of a fresh, unkeyed queue.
 *
 * @param key XSI key or IPC_PRIVATE.
 * @param flags Permission bits plus IPC_CREAT / IPC_EXCL.
 * @return The new or existing msqid on success, or a negative errno
 *         (ENOENT, EEXIST, EACCES, ENOSPC, ENOMEM).
 */
int
_user_xsi_msgget(key_t key, int flags)
{
	TRACE(("xsi_msgget: key = %d, flags = %d\n", (int)key, flags));
	XsiMessageQueue *messageQueue = NULL;
	Ipc *ipcKey = NULL;
	// Default assumptions
	bool isPrivate = true;
	bool create = true;

	if (key != IPC_PRIVATE) {
		isPrivate = false;
		// Check if key already exist, if it does it already has a message
		// queue associated with it
		ipcKey = sIpcHashTable.Lookup(key);
		if (ipcKey == NULL || ipcKey->MessageQueueID() == -1) {
			if (!(flags & IPC_CREAT)) {
				TRACE(("xsi_msgget: key %d does not exist, but the "
					"caller did not ask for creation\n", (int)key));
				return ENOENT;
			}
			if (ipcKey == NULL) {
				ipcKey = new(std::nothrow) Ipc(key);
				if (ipcKey == NULL) {
					TRACE(("xsi_msgget: failed to create new Ipc object "
						"for key %d\n", (int)key));
					return ENOMEM;
				}
				sIpcHashTable.Insert(ipcKey);
			}
		} else {
			// The IPC key exist and it already has a message queue
			if ((flags & IPC_CREAT) && (flags & IPC_EXCL)) {
				TRACE_ERROR(("xsi_msgget: key %d already exist\n", (int)key));
				return EEXIST;
			}
			int messageQueueID = ipcKey->MessageQueueID();

			MutexLocker _(sXsiMessageQueueLock);
			messageQueue = sMessageQueueHashTable.Lookup(messageQueueID);
			if (!messageQueue->HasPermission()) {
				TRACE(("xsi_msgget: calling process has not permission "
					"on message queue %d, key %d\n", messageQueue->ID(),
					(int)key));
				return EACCES;
			}
			create = false;
		}
	}

	if (create) {
		// Create a new message queue for this key
		if (atomic_get(&sXsiMessageQueueCount) >= MAX_XSI_MESSAGE_QUEUE) {
			TRACE_ERROR(("xsi_msgget: reached limit of maximun number of "
				"message queues\n"));
			return ENOSPC;
		}

		messageQueue = new(std::nothrow) XsiMessageQueue(flags);
		if (messageQueue == NULL) {
			TRACE_ERROR(("xsi_msgget: failed to allocate new xsi "
				"message queue\n"));
			return ENOMEM;
		}
		atomic_add(&sXsiMessageQueueCount, 1);

		MutexLocker _(sXsiMessageQueueLock);
		messageQueue->SetID();
		if (isPrivate)
			messageQueue->SetIpcKey((key_t)-1);
		else {
			messageQueue->SetIpcKey(key);
			ipcKey->SetMessageQueueID(messageQueue);
		}
		sMessageQueueHashTable.Insert(messageQueue);
	}

	return messageQueue->ID();
}


/**
 * @brief Syscall entry point for msgrcv().
 *
 * Sleeps on the queue's receive condition variable until a message matching
 * messageType appears, unless IPC_NOWAIT is set (in which case ENOMSG is
 * returned on an empty queue). Detects queue removal underneath a sleeper
 * by comparing the captured sequence number against the post-wake state
 * and returns EIDRM; signals convert into EINTR. Oversized messages are
 * put back and reported as E2BIG unless MSG_NOERROR asked for truncation.
 *
 * @param messageQueueID msqid.
 * @param messagePointer User buffer receiving type + payload.
 * @param messageSize Max payload bytes the user buffer can accept.
 * @param messageType XSI msgtyp selector (see Remove()).
 * @param messageFlags IPC_NOWAIT | MSG_NOERROR.
 * @return Number of payload bytes received, or a negative errno
 *         (EINVAL, EACCES, EAGAIN/ENOMSG, EINTR, EIDRM, E2BIG,
 *         B_BAD_ADDRESS).
 */
ssize_t
_user_xsi_msgrcv(int messageQueueID, void *messagePointer,
	size_t messageSize, long messageType, int messageFlags)
{
	TRACE(("xsi_msgrcv: messageQueueID = %d, messageSize = %ld\n",
		messageQueueID, messageSize));
	MutexLocker messageQueueHashLocker(sXsiMessageQueueLock);
	XsiMessageQueue *messageQueue = sMessageQueueHashTable.Lookup(messageQueueID);
	if (messageQueue == NULL) {
		TRACE(("xsi_msgrcv: message queue id %d not valid\n",
			messageQueueID));
		return EINVAL;
	}
	MutexLocker messageQueueLocker(messageQueue->Lock());
	messageQueueHashLocker.Unlock();

	if (messageSize > MAX_BYTES_PER_QUEUE) {
		TRACE_ERROR(("xsi_msgrcv: message size is out of range\n"));
		return EINVAL;
	}
	if (!messageQueue->HasPermission()) {
		TRACE(("xsi_msgrcv: calling process has not permission "
			"on message queue id %d, key %d\n", messageQueueID,
			(int)messageQueue->IpcKey()));
		return EACCES;
	}
	if (!IS_USER_ADDRESS(messagePointer)) {
		TRACE(("xsi_msgrcv: message address is not valid\n"));
		return B_BAD_ADDRESS;
	}

	queued_message *message = NULL;
	while (true) {
		message = messageQueue->Remove(messageType);

		if (message == NULL && !(messageFlags & IPC_NOWAIT)) {
			// We are going to sleep
			ConditionVariableEntry queueEntry;
			messageQueue->Enqueue(&queueEntry, /* waitForMessage */ true);

			uint32 sequenceNumber = messageQueue->SequenceNumber();

			TRACE(("xsi_msgrcv: thread %d going to sleep\n", (int)thread_get_current_thread_id()));
			status_t result
				= messageQueue->BlockAndUnlock(&queueEntry, &messageQueueLocker);
			TRACE(("xsi_msgrcv: thread %d back to life\n", (int)thread_get_current_thread_id()));

			messageQueueHashLocker.Lock();
			messageQueue = sMessageQueueHashTable.Lookup(messageQueueID);
			if (result == EIDRM || messageQueue == NULL || (messageQueue != NULL
				&& sequenceNumber != messageQueue->SequenceNumber())) {
				TRACE(("xsi_msgrcv: message queue id %d (sequence = "
					"%" B_PRIu32 ") got destroyed\n", messageQueueID,
					sequenceNumber));
				return EIDRM;
			} else if (result == B_INTERRUPTED) {
				TRACE(("xsi_msgrcv: thread %d got interrupted while "
					"waiting on message queue %d\n", (int)thread_get_current_thread_id(),
					messageQueueID));
				messageQueue->Dequeue(&queueEntry);
				return EINTR;
			} else {
				messageQueueLocker.Lock();
				messageQueueHashLocker.Unlock();
			}
		} else if (message == NULL) {
			// There is not message of type requested and
			// we can't wait
			return ENOMSG;
		} else {
			// Message received correctly (so far)
			if ((ssize_t)messageSize < message->length
				&& !(messageFlags & MSG_NOERROR)) {
				TRACE_ERROR(("xsi_msgrcv: message too big!\n"));
				// Put the message back inside. Since we hold the
				// queue message lock, not one else could have filled
				// up the queue meanwhile
				messageQueue->Insert(message);
				return E2BIG;
			}

			ssize_t result
				= message->copy_to_user_buffer(messagePointer, messageSize);
			if (result < 0) {
				messageQueue->Insert(message);
				return B_BAD_ADDRESS;
			}

			delete message;
			TRACE(("xsi_msgrcv: message received correctly\n"));
			return result;
		}
	}

	return B_OK;
}


/**
 * @brief Syscall entry point for msgsnd().
 *
 * Copies the message from user space, then loops trying to Insert(). If
 * Insert() reports that the queue is full, the caller either blocks on
 * the send condition variable (default) or returns EAGAIN when IPC_NOWAIT
 * was requested. EINTR is returned on signal, EIDRM if the queue is
 * destroyed while blocked.
 *
 * @param messageQueueID msqid.
 * @param messagePointer User buffer containing type + payload.
 * @param messageSize Payload length (excluding the type prefix).
 * @param messageFlags IPC_NOWAIT or 0.
 * @return 0 on success, or a negative errno (EINVAL, EACCES, EAGAIN,
 *         EINTR, EIDRM, ENOMEM, B_BAD_ADDRESS).
 */
int
_user_xsi_msgsnd(int messageQueueID, const void *messagePointer,
	size_t messageSize, int messageFlags)
{
	TRACE(("xsi_msgsnd: messageQueueID = %d, messageSize = %ld\n",
		messageQueueID, messageSize));
	MutexLocker messageQueueHashLocker(sXsiMessageQueueLock);
	XsiMessageQueue *messageQueue = sMessageQueueHashTable.Lookup(messageQueueID);
	if (messageQueue == NULL) {
		TRACE(("xsi_msgsnd: message queue id %d not valid\n",
			messageQueueID));
		return EINVAL;
	}
	MutexLocker messageQueueLocker(messageQueue->Lock());
	messageQueueHashLocker.Unlock();

	if (messageSize > MAX_BYTES_PER_QUEUE) {
		TRACE_ERROR(("xsi_msgsnd: message size is out of range\n"));
		return EINVAL;
	}
	if (!messageQueue->HasPermission()) {
		TRACE(("xsi_msgsnd: calling process has not permission "
			"on message queue id %d, key %d\n", messageQueueID,
			(int)messageQueue->IpcKey()));
		return EACCES;
	}
	if (!IS_USER_ADDRESS(messagePointer)) {
		TRACE(("xsi_msgsnd: message address is not valid\n"));
		return B_BAD_ADDRESS;
	}

	queued_message *message
		= new(std::nothrow) queued_message(messagePointer, messageSize);
	if (message == NULL || message->initOK != true) {
		TRACE_ERROR(("xsi_msgsnd: failed to create new message to queue\n"));
		delete message;
		return ENOMEM;
	}

	bool notSent = true;
	status_t result = B_OK;
	while (notSent) {
		bool goToSleep = messageQueue->Insert(message);

		if (goToSleep && !(messageFlags & IPC_NOWAIT)) {
			// We are going to sleep
			ConditionVariableEntry queueEntry;
			messageQueue->Enqueue(&queueEntry, /* waitForMessage */ false);

			uint32 sequenceNumber = messageQueue->SequenceNumber();

			TRACE(("xsi_msgsnd: thread %d going to sleep\n", (int)thread_get_current_thread_id()));
			result = messageQueue->BlockAndUnlock(&queueEntry, &messageQueueLocker);
			TRACE(("xsi_msgsnd: thread %d back to life\n", (int)thread_get_current_thread_id()));

			messageQueueHashLocker.Lock();
			messageQueue = sMessageQueueHashTable.Lookup(messageQueueID);
			if (result == EIDRM || messageQueue == NULL || (messageQueue != NULL
				&& sequenceNumber != messageQueue->SequenceNumber())) {
				TRACE(("xsi_msgsnd: message queue id %d (sequence = "
					"%" B_PRIu32 ") got destroyed\n", messageQueueID,
					sequenceNumber));
				delete message;
				notSent = false;
				result = EIDRM;
			} else if (result == B_INTERRUPTED) {
				TRACE(("xsi_msgsnd: thread %d got interrupted while "
					"waiting on message queue %d\n", (int)thread_get_current_thread_id(),
					messageQueueID));
				messageQueue->Dequeue(&queueEntry);
				delete message;
				notSent = false;
				result = EINTR;
			} else {
				messageQueueLocker.Lock();
				messageQueueHashLocker.Unlock();
			}
		} else if (goToSleep) {
			// We did not send the message and we can't wait
			delete message;
			notSent = false;
			result = EAGAIN;
		} else {
			// Message delivered correctly
			TRACE(("xsi_msgsnd: message sent correctly\n"));
			notSent = false;
		}
	}

	return result;
}
