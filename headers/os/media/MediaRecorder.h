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
 * Copyright 2014, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file MediaRecorder.h
 *  @brief Defines BMediaRecorder for capturing media data from a node output.
 */

#ifndef _MEDIA_RECORDER_H
#define _MEDIA_RECORDER_H


#include <MediaDefs.h>
#include <MediaNode.h>


namespace BPrivate { namespace media {
class BMediaRecorderNode;
}
}


/** @brief Captures raw media data from a connected producer node.
 *
 *  BMediaRecorder connects to a media producer, receives buffers, and delivers
 *  them to a user-supplied callback function.  It manages the underlying
 *  consumer node automatically.
 */
class BMediaRecorder {
public:
	/** @brief Notification codes delivered to the NotifyFunc callback. */
	enum notification {
		B_WILL_START = 1,	// performance_time
		B_WILL_STOP,		// performance_time immediate
		B_WILL_SEEK,		// performance_time media_time
		B_WILL_TIMEWARP,	// real_time performance_time
	};

	/** @brief Callback invoked for each received media buffer.
	 *
	 *  @param cookie The user-supplied cookie.
	 *  @param timestamp The performance time of the buffer.
	 *  @param data Pointer to the raw buffer data.
	 *  @param size Size of the buffer data in bytes.
	 *  @param format The format of the received data.
	 */
	typedef void				(*ProcessFunc)(void* cookie,
									bigtime_t timestamp, void* data,
									size_t size, const media_format& format);

	/** @brief Callback invoked for transport-state change notifications.
	 *
	 *  @param cookie The user-supplied cookie.
	 *  @param what One of the notification enum values.
	 */
	typedef void				(*NotifyFunc)(void* cookie,
									notification what, ...);

public:
	/** @brief Constructs the recorder and creates the internal consumer node.
	 *  @param name Human-readable name for the recorder node.
	 *  @param type The media type to capture (default B_MEDIA_UNKNOWN_TYPE).
	 */
								BMediaRecorder(const char* name,
									media_type type
										= B_MEDIA_UNKNOWN_TYPE);

	virtual						~BMediaRecorder();

	/** @brief Returns the initialization status.
	 *  @return B_OK if the recorder is ready, or an error code.
	 */
			status_t			InitCheck() const;

	/** @brief Sets the callback functions called for data and notifications.
	 *  @param recordFunc Callback for received buffers; NULL clears it.
	 *  @param notifyFunc Callback for transport events; NULL clears it.
	 *  @param cookie Arbitrary pointer passed to both callbacks.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetHooks(ProcessFunc recordFunc = NULL,
									NotifyFunc notifyFunc = NULL,
									void* cookie = NULL);

	/** @brief Sets the format this recorder will accept from a producer.
	 *  @param format The desired media format.
	 */
			void				SetAcceptedFormat(
									const media_format& format);

	/** @brief Returns the format this recorder has accepted.
	 *  @return A reference to the accepted media_format.
	 */
			const media_format&	AcceptedFormat() const;

	/** @brief Starts capturing; the producer must already be connected.
	 *  @param force If true, starts even if already running.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t			Start(bool force = false);

	/** @brief Stops capturing.
	 *  @param force If true, stops immediately without waiting.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t			Stop(bool force = false);

	/** @brief Connects to the best available producer matching the given format.
	 *  @param format The desired format for the connection.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t			Connect(const media_format& format);

	/** @brief Connects to a specific dormant node.
	 *  @param dormantInfo Identifies the dormant node to instantiate and connect to.
	 *  @param format The desired format for the connection.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t			Connect(const dormant_node_info& dormantInfo,
									const media_format& format);

	/** @brief Connects to a specific live media node.
	 *  @param node The producer node to connect to.
	 *  @param output Optional specific output on the producer; NULL for any.
	 *  @param format Optional format hint; NULL for negotiated default.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t			Connect(const media_node& node,
									const media_output* output = NULL,
									const media_format* format = NULL);

	/** @brief Disconnects from the current producer.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t			Disconnect();

	/** @brief Returns true if the recorder is currently connected to a producer. */
			bool				IsConnected() const;

	/** @brief Returns true if the recorder is currently capturing. */
			bool				IsRunning() const;

	/** @brief Returns the format of the currently active connection.
	 *  @return A reference to the negotiated media_format.
	 */
			const media_format&	Format() const;

protected:
	/** @brief Returns the producer source this recorder is connected to. */
			const media_source&	MediaSource() const;

	/** @brief Returns the internal consumer input for this recorder. */
			const media_input	MediaInput() const;

	/** @brief Called when a buffer is received from the producer.
	 *  @param buffer Pointer to the raw buffer data.
	 *  @param size Size of the buffer in bytes.
	 *  @param header The media header for this buffer.
	 */
	virtual	void				BufferReceived(void* buffer, size_t size,
									const media_header& header);

	/** @brief Completes the connection setup after a source has been selected.
	 *  @param outputSource The source to connect to.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetUpConnection(media_source outputSource);

private:

			status_t			_Connect(const media_node& mediaNode,
									const media_output* output,
									const media_format& format);

	virtual	void				_ReservedMediaRecorder0();
	virtual	void				_ReservedMediaRecorder1();
	virtual	void				_ReservedMediaRecorder2();
	virtual	void				_ReservedMediaRecorder3();
	virtual	void				_ReservedMediaRecorder4();
	virtual	void				_ReservedMediaRecorder5();
	virtual	void				_ReservedMediaRecorder6();
	virtual	void				_ReservedMediaRecorder7();

			status_t			fInitErr;

			bool				fConnected;
			bool				fRunning;
			bool				fReleaseOutputNode;

			ProcessFunc			fRecordHook;
			NotifyFunc			fNotifyHook;

			media_node			fOutputNode;
			media_source		fOutputSource;

			BPrivate::media::BMediaRecorderNode* fNode;

			void*				fBufferCookie;
			uint32				fPadding[32];

			friend class		BPrivate::media::BMediaRecorderNode;
};

#endif	//	_MEDIA_RECORDER_H
