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
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009-2012, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file MediaNode.h
 *  @brief Defines BMediaNode, the base class for all Media Kit participants.
 */

#ifndef _MEDIA_NODE_H
#define _MEDIA_NODE_H


#include <MediaDefs.h>
#include <Point.h>

#include <new>


class BBufferConsumer;
class BBufferProducer;
class BControllable;
class BFileInterface;
class BMediaAddOn;
class BTimeSource;


/** @brief Lightweight descriptor identifying a registered media node instance. */
class media_node {
public:
								media_node();
								~media_node();

			media_node_id		node;  /**< Unique node ID assigned by the media server. */
			port_id				port;  /**< The node's control port. */
			uint32				kind;  /**< Bitmask of node_kind flags. */

	static const media_node		null;  /**< Represents an invalid/unset node. */

private:
			uint32				_reserved_[3];
};


/** @brief Describes one input connection endpoint of a media node. */
struct media_input {
								media_input();
								~media_input();

			media_node			node;        /**< The consumer node. */
			media_source		source;      /**< The connected producer source. */
			media_destination	destination; /**< This input's destination handle. */
			media_format		format;      /**< The format negotiated for this connection. */
			char				name[B_MEDIA_NAME_LENGTH]; /**< Human-readable input name. */

private:
			uint32				_reserved_media_input_[4];
};


/** @brief Describes one output connection endpoint of a media node. */
struct media_output {
								media_output();
								~media_output();

			media_node			node;        /**< The producer node. */
			media_source		source;      /**< This output's source handle. */
			media_destination	destination; /**< The connected consumer destination. */
			media_format		format;      /**< The format negotiated for this connection. */
			char				name[B_MEDIA_NAME_LENGTH]; /**< Human-readable output name. */

private:
			uint32				_reserved_media_output_[4];
};


/** @brief Information about a running (live) node as returned by BMediaRoster::GetLiveNodes(). */
struct live_node_info {
								live_node_info();
								~live_node_info();

			media_node			node;        /**< The live node. */
			BPoint				hint_point;  /**< Suggested position in a node graph view. */
			char				name[B_MEDIA_NAME_LENGTH]; /**< Node display name. */

private:
			char				reserved[160];
};


/** @brief Carries information about a completed or failed asynchronous media request. */
struct media_request_info {
			enum what_code {
				B_SET_VIDEO_CLIPPING_FOR = 1,
				B_REQUEST_FORMAT_CHANGE,
				B_SET_OUTPUT_ENABLED,
				B_SET_OUTPUT_BUFFERS_FOR,

				B_FORMAT_CHANGED = 4097
			};

			what_code			what;        /**< The type of request. */
			int32				change_tag;  /**< Tag matching the original request. */
			status_t			status;      /**< Result of the request. */
			void*				cookie;      /**< User-supplied cookie from the request. */
			void*				user_data;   /**< Additional user data. */
			media_source		source;      /**< Source involved in the request. */
			media_destination	destination; /**< Destination involved in the request. */
			media_format		format;      /**< Format associated with the request. */

			uint32				_reserved_[32];
};


/** @brief Carries named attribute data associated with a media node. */
struct media_node_attribute {
			enum {
				B_R40_COMPILED = 1,             /**< Node was compiled for R4.0. */
				B_USER_ATTRIBUTE_NAME = 0x1000000, /**< First user-defined attribute. */
				B_FIRST_USER_ATTRIBUTE
			};

			uint32				what;   /**< Attribute type identifier. */
			uint32				flags;  /**< Attribute flags. */
			int64				data;   /**< Attribute value. */
};


namespace BPrivate {
	namespace media {
		class TimeSourceObject;
		class SystemTimeSourceObject;
		class BMediaRosterEx;
	}
} // BPrivate::media


/** @brief The indirect base class for all Media Kit participants.
 *
 *  BMediaNode is the indirect base class for all Media Kit participants.
 *  However, you should use the more specific BBufferConsumer, BBufferProducer
 *  and others rather than BMediaNode directly.  It's OK to multiply inherit.
 */
class BMediaNode {
protected:
	// NOTE: Call Release() to destroy a node.
	virtual								~BMediaNode();

public:
			/** @brief Run mode that governs how a node handles timing constraints. */
			enum run_mode {
				B_OFFLINE = 1,
					// This mode has no realtime constraint.
				B_DECREASE_PRECISION,
					// When late, try to catch up by reducing quality.
				B_INCREASE_LATENCY,
					// When late, increase the presentation time offset.
				B_DROP_DATA,
					// When late, try to catch up by dropping buffers.
				B_RECORDING
					// For nodes on the receiving end of recording.
					// Buffers will always be late.
			};

	/** @brief Increments the reference count and returns this node.
	 *  @return This node pointer (for chaining).
	 */
			BMediaNode*			Acquire();

	/** @brief Decrements the reference count and destroys the node when it reaches zero.
	 *  @return This node pointer if still alive, or NULL if destroyed.
	 */
			BMediaNode*			Release();

	/** @brief Returns the node's human-readable name.
	 *  @return The name string.
	 */
			const char*			Name() const;

	/** @brief Returns the system-assigned node ID.
	 *  @return The media_node_id.
	 */
			media_node_id		ID() const;

	/** @brief Returns the bitmask of node_kind flags.
	 *  @return The kinds bitmask.
	 */
			uint64				Kinds() const;

	/** @brief Returns a media_node descriptor for this node.
	 *  @return A media_node struct.
	 */
			media_node			Node() const;

	/** @brief Returns the current run mode.
	 *  @return The run_mode value.
	 */
			run_mode			RunMode() const;

	/** @brief Returns the time source governing this node's performance time.
	 *  @return Pointer to the BTimeSource.
	 */
			BTimeSource*		TimeSource() const;

	/** @brief Returns the port ID used to receive control messages.
	 *  @return The control port_id.
	 */
	virtual	port_id				ControlPort() const;

	/** @brief Returns the add-on that instantiated this node, or NULL if application-internal.
	 *  @param internalID On return, the flavor ID within the add-on.
	 *  @return Pointer to the BMediaAddOn, or NULL.
	 */
	virtual	BMediaAddOn*		AddOn(int32* internalID) const = 0;

	/** @brief Error codes sent as "be:node_id" fields in media notification messages. */
			enum node_error {
				// Note that these belong with the notifications in
				// MediaDefs.h! They are here to provide compiler type
				// checking in ReportError().
				B_NODE_FAILED_START					= 'TRI0',
				B_NODE_FAILED_STOP					= 'TRI1',
				B_NODE_FAILED_SEEK					= 'TRI2',
				B_NODE_FAILED_SET_RUN_MODE			= 'TRI3',
				B_NODE_FAILED_TIME_WARP				= 'TRI4',
				B_NODE_FAILED_PREROLL				= 'TRI5',
				B_NODE_FAILED_SET_TIME_SOURCE_FOR	= 'TRI6',
				B_NODE_IN_DISTRESS					= 'TRI7'
				// What field 'TRIA' and up are used in MediaDefs.h
			};

protected:
	/** @brief Broadcasts an error notification to all watchers.
	 *  @param what One of the node_error codes.
	 *  @param info Optional additional BMessage with error details.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ReportError(node_error what,
									const BMessage* info = NULL);

	/** @brief Notifies the system that this node has stopped.
	 *  @param performanceTime The performance time at which the node stopped.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			NodeStopped(bigtime_t performanceTime);

	/** @brief Called when a previously registered timer expires.
	 *  @param notifyPerformanceTime The performance time of the timer event.
	 *  @param cookie The cookie supplied when AddTimer() was called.
	 *  @param error B_OK on normal expiry, or an error code.
	 */
			void				TimerExpired(bigtime_t notifyPerformanceTime,
									int32 cookie, status_t error = B_OK);

	/** @brief Constructs a media node with the given name; reference count starts at 1.
	 *  @param name Human-readable name for the node.
	 */
	explicit					BMediaNode(const char* name);

	/** @brief Waits for a message to arrive on the control port.
	 *  @param waitUntil Absolute real time to wait until.
	 *  @param flags Reserved; pass 0.
	 *  @param _reserved_ Reserved; pass NULL.
	 *  @return B_OK when a message is received, or B_TIMED_OUT.
	 */
				status_t		WaitForMessage(bigtime_t waitUntil,
									uint32 flags = 0, void* _reserved_ = 0);

	/** @brief Called to start the node at the given performance time. */
	virtual	void				Start(bigtime_t atPerformanceTime);
	/** @brief Called to stop the node at the given performance time. */
	virtual	void				Stop(bigtime_t atPerformanceTime,
									bool immediate);
	/** @brief Called to seek the node to a new media position. */
	virtual	void				Seek(bigtime_t toMediaTime,
									bigtime_t atPerformanceTime);
	/** @brief Called to change the node's run mode. */
	virtual	void				SetRunMode(run_mode mode);
	/** @brief Called to warp the performance clock to a new value. */
	virtual	void				TimeWarp(bigtime_t atRealTime,
									bigtime_t toPerformanceTime);
	/** @brief Called to prepare the node for playback (optional pre-buffering). */
	virtual	void				Preroll();
	/** @brief Called to set a new time source for this node. */
	virtual	void				SetTimeSource(BTimeSource* timeSource);

public:
	/** @brief Dispatches an incoming port message to the appropriate handler.
	 *  @param message The message code.
	 *  @param data Pointer to the message payload.
	 *  @param size Size of the payload in bytes.
	 *  @return B_OK if handled, or an error code.
	 */
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

	/** @brief Handles a message that was not recognized by the node or its superclasses.
	 *  @param code The unrecognized message code.
	 *  @param buffer Pointer to the message data.
	 *  @param size Size of the message data in bytes.
	 */
			void				HandleBadMessage(int32 code,
									const void* buffer, size_t size);

	/** @brief Adds a node kind flag to this node's kinds bitmask.
	 *  @param kind The node_kind flag to add.
	 */
			void				AddNodeKind(uint64 kind);

	/** @brief Allocates memory from the system heap. */
			void*				operator new(size_t size);
	/** @brief Allocates memory using the no-throw policy. */
			void*				operator new(size_t size,
									const std::nothrow_t&) throw();
	/** @brief Frees memory allocated by operator new. */
			void				operator delete(void* ptr);
	/** @brief Frees memory allocated by the no-throw new. */
			void				operator delete(void* ptr,
									const std::nothrow_t&) throw();

protected:
	/** @brief Called when a pending request has completed or failed.
	 *  @param info Details about the completed request.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			RequestCompleted(
									const media_request_info & info);

	/** @brief Called by the framework to delete this node object.
	 *  @param node The node to delete (same as this).
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			DeleteHook(BMediaNode* node);

	/** @brief Called after the node has been registered with the media server. */
	virtual	void				NodeRegistered();

public:

	/** @brief Fills in node-specific attribute data.
	 *  @param _attributes Array to receive attribute entries.
	 *  @param inMaxCount Capacity of the array.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetNodeAttributes(
									media_node_attribute* _attributes,
									size_t inMaxCount);

	/** @brief Schedules a timer event at the given performance time.
	 *  @param atPerformanceTime The performance time to fire the timer.
	 *  @param cookie An arbitrary value returned in TimerExpired().
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			AddTimer(bigtime_t atPerformanceTime,
									int32 cookie);

private:
			friend class BTimeSource;
			friend class BMediaRoster;
			friend class BBufferProducer;
			friend class BPrivate::media::TimeSourceObject;
			friend class BPrivate::media::SystemTimeSourceObject;
			friend class BPrivate::media::BMediaRosterEx;

			// Deprecated in BeOS R4.1
			int32				IncrementChangeTag();
			int32				ChangeTag();
			int32				MintChangeTag();
			status_t			ApplyChangeTag(int32 previouslyReserved);

private:
	// FBC padding and forbidden methods
			status_t			_Reserved_MediaNode_0(void*);
				// RequestCompletionHook()
			status_t			_Reserved_MediaNode_1(void*);
				// DeleteHook()
			status_t			_Reserved_MediaNode_2(void*);
				// NodeRegistered()
			status_t			_Reserved_MediaNode_3(void*);
				// GetNodeAttributes()
			status_t			_Reserved_MediaNode_4(void*);
				// AddTimer()
	virtual	status_t			_Reserved_MediaNode_5(void*);
	virtual	status_t			_Reserved_MediaNode_6(void*);
	virtual	status_t			_Reserved_MediaNode_7(void*);
	virtual	status_t			_Reserved_MediaNode_8(void*);
	virtual	status_t			_Reserved_MediaNode_9(void*);
	virtual	status_t			_Reserved_MediaNode_10(void*);
	virtual	status_t			_Reserved_MediaNode_11(void*);
	virtual	status_t			_Reserved_MediaNode_12(void*);
	virtual	status_t			_Reserved_MediaNode_13(void*);
	virtual	status_t			_Reserved_MediaNode_14(void*);
	virtual	status_t			_Reserved_MediaNode_15(void*);

								BMediaNode();
								BMediaNode(const BMediaNode& other);
			BMediaNode&			operator=(const BMediaNode& other);

private:
								BMediaNode(const char* name,
									media_node_id id, uint32 kinds);

			void				_InitObject(const char* name,
									media_node_id id, uint64 kinds);

private:
			media_node_id		fNodeID;
			BTimeSource*		fTimeSource;
			int32				fRefCount;
			char				fName[B_MEDIA_NAME_LENGTH];
			run_mode			fRunMode;

			int32				_reserved[2];

			uint64				fKinds;
			media_node_id		fTimeSourceID;

			BBufferProducer*	fProducerThis;
			BBufferConsumer*	fConsumerThis;
			BFileInterface*		fFileInterfaceThis;
			BControllable*		fControllableThis;
			BTimeSource*		fTimeSourceThis;

			bool				_reservedBool[4];

	mutable	port_id				fControlPort;

			uint32				_reserved_media_node_[8];

protected:
	static	int32				NewChangeTag();
		// for use by BBufferConsumer, mostly

private:
	// NOTE: Dont' rename this one, it's static and needed for binary
	// compatibility
	static	int32 _m_changeTag;
		// not to be confused with _mChangeCount
};


#endif // _MEDIA_NODE_H
