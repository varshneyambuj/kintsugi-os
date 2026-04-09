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
 * Incorporates work originally licensed under the Be Sample Code License.
 * Copyright 2014-2016, Dario Casalinuovo.
 * Copyright 1999, Be Incorporated. All Rights Reserved.
 */

/** @file MediaRecorderNode.h
    @brief Internal media node backing BMediaRecorder buffer capture. */

#ifndef _MEDIA_RECORDER_NODE_H
#define _MEDIA_RECORDER_NODE_H


#include <BufferConsumer.h>
#include <MediaEventLooper.h>
#include <MediaRecorder.h>
#include <String.h>


namespace BPrivate { namespace media {


/** @brief Internal BBufferConsumer node that feeds captured buffers to a BMediaRecorder. */
class BMediaRecorderNode : public BMediaEventLooper,
	public BBufferConsumer {
public:
								BMediaRecorderNode(const char* name,
									BMediaRecorder* recorder,
									media_type type
										= B_MEDIA_UNKNOWN_TYPE);

			//	TODO these are not thread safe; we should fix that...
			/** @brief Sets the media format this node will accept from a producer.
			    @param format The media_format to accept. */
			void				SetAcceptedFormat(const media_format& format);

			/** @brief Returns the currently accepted media format.
			    @return Reference to the accepted media_format. */
			const media_format&	AcceptedFormat() const;

			/** @brief Fills in the single media input this node exposes.
			    @param input Output media_input descriptor. */
			void				GetInput(media_input* input);

			/** @brief Enables or disables delivery of incoming buffers to the recorder.
			    @param enabled true to enable buffer delivery, false to suppress it. */
			void				SetDataEnabled(bool enabled);

			/** @brief Switches between internal-connect and normal-connect modes.
			    @param connectMode true to use internal connect mode. */
			void				ActivateInternalConnect(bool connectMode);

protected:

	virtual	BMediaAddOn*		AddOn(int32* id) const;

	virtual void				NodeRegistered();

	virtual void				SetRunMode(run_mode mode);

	virtual void				HandleEvent(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent);

	virtual	void				Start(bigtime_t performanceTime);

	virtual	void				Stop(bigtime_t performanceTime,
									bool immediate);

	virtual	void				Seek(bigtime_t mediaTime,
									bigtime_t performanceTime);

	virtual	void				TimeWarp(bigtime_t realTime,
									bigtime_t performanceTime);

	virtual	status_t			HandleMessage(int32 message,
									const void* data,
									size_t size);

		// Someone, probably the producer, is asking you about
		// this format. Give your honest opinion, possibly
		// modifying *format. Do not ask upstream producer about
		//	the format, since he's synchronously waiting for your
		// reply.

	virtual	status_t			AcceptFormat(const media_destination& dest,
									media_format* format);

	virtual	status_t			GetNextInput(int32* cookie,
									media_input* outInput);

	virtual	void				DisposeInputCookie(int32 cookie);

	virtual	void				BufferReceived(BBuffer* buffer);

	virtual	void				ProducerDataStatus(
									const media_destination& destination,
									int32 status,
									bigtime_t performanceTime);

	virtual	status_t			GetLatencyFor(const media_destination& destination,
									bigtime_t* outLatency,
									media_node_id* outTimesource);

	virtual	status_t			Connected(const media_source& producer,
									const media_destination& where,
									const media_format& format,
									media_input* outInput);

	virtual	void				Disconnected(const media_source& producer,
									const media_destination& where);

	virtual	status_t			FormatChanged(const media_source& producer,
									const media_destination& consumer,
									int32 tag,
									const media_format& format);

protected:

	virtual						~BMediaRecorderNode();

			BMediaRecorder*		fRecorder;
			media_format		fOKFormat;
			media_input			fInput;
			BString				fName;
			bool				fConnectMode;
};

}
}

using namespace BPrivate::media;

#endif	//	_MEDIA_RECORDER_NODE_H
