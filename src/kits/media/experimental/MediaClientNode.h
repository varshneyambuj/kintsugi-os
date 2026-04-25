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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2015, Dario Casalinuovo.
 */

/** @file MediaClientNode.h
    @brief Experimental media node implementation backing a BMediaClient,
           combining BBufferConsumer, BBufferProducer, and BMediaEventLooper. */

#ifndef _MEDIA_CLIENT_NODE_H
#define _MEDIA_CLIENT_NODE_H


#include <BufferConsumer.h>
#include <BufferProducer.h>
#include <Controllable.h>
#include <MediaDefs.h>
#include <MediaEventLooper.h>


namespace BPrivate { namespace media {


class BMediaClient;
class BMediaConnection;
class BMediaOutput;

/** @brief Internal media node owned by a BMediaClient that handles buffer
           production, consumption, and timed event scheduling for the client's
           input and output connections. */
class BMediaClientNode : public BBufferConsumer, public BBufferProducer,
	public BMediaEventLooper {
public:
							BMediaClientNode(const char* name,
								BMediaClient* owner,
								media_type type
									= B_MEDIA_UNKNOWN_TYPE);

	// Various useful stuff

			status_t		SendBuffer(BBuffer* buffer, BMediaConnection* conn);

protected:

	virtual	BMediaAddOn*	AddOn(int32* id) const;

	virtual void			NodeRegistered();

	virtual void			SetRunMode(run_mode mode);

	virtual	void			Start(bigtime_t performanceTime);

	virtual	void			Stop(bigtime_t performanceTime,
								bool immediate);

	virtual	void			Seek(bigtime_t mediaTime,
								bigtime_t performanceTime);

	virtual	void			TimeWarp(bigtime_t realTime,
								bigtime_t performanceTime);

	virtual	status_t		HandleMessage(int32 message,
								const void* data,
								size_t size);

	// BBufferConsumer

	virtual	status_t		AcceptFormat(const media_destination& dest,
								media_format* format);

	virtual	status_t		GetNextInput(int32* cookie,
								media_input* input);

	virtual	void			DisposeInputCookie(int32 cookie);

	virtual	void			BufferReceived(BBuffer* buffer);

	virtual	status_t		GetLatencyFor(const media_destination& dest,
								bigtime_t* latency,
								media_node_id* timesource);

	virtual	status_t		Connected(const media_source& source,
								const media_destination& dest,
								const media_format& format,
								media_input* outInput);

	virtual	void			Disconnected(const media_source& source,
								const media_destination& dest);

	virtual	status_t		FormatChanged(const media_source& source,
								const media_destination& consumer,
								int32 tag,
								const media_format& format);

	// BBufferProducer

	virtual 	status_t 	FormatSuggestionRequested(media_type type,
									int32 quality, media_format* format);
	virtual 	status_t 	FormatProposal(const media_source& source,
									media_format *format);
	virtual 	status_t 	FormatChangeRequested(const media_source& source,
									const media_destination& dest,
									media_format *format,
									int32* _deprecated_);
	virtual 	void 		LateNoticeReceived(const media_source& source,
									bigtime_t late,	bigtime_t when);
	virtual 	status_t	GetNextOutput(int32 *cookie, media_output *output);
	virtual 	status_t 	DisposeOutputCookie(int32 cookie);
	virtual 	status_t	SetBufferGroup(const media_source& source,
									BBufferGroup *group);
	virtual 	status_t 	PrepareToConnect(const media_source& source,
									const media_destination& dest,
									media_format *format,
									media_source *out_source,
									char *name);
	virtual 	void 		Connect(status_t status,
									const media_source& source,
									const media_destination& dest,
									const media_format &format,
									char* name);
	virtual		void 		Disconnect(const media_source& source,
									const media_destination& dest);
	virtual 	void 		EnableOutput(const media_source& source,
									bool enabled, int32* _deprecated_);
	virtual 	status_t 	GetLatency(bigtime_t *outLatency);
	virtual 	void 		LatencyChanged(	const media_source& source,
									const media_destination& dest,
									bigtime_t latency, uint32 flags);

				void 		ProducerDataStatus(const media_destination& dest,
								int32 status, bigtime_t when);
protected:
	virtual 	void 		HandleEvent(const media_timed_event *event,
									bigtime_t late,
									bool realTimeEvent=false);

	virtual					~BMediaClientNode();

private:
				void		_ScheduleConnections(bigtime_t eventTime);
				void		_HandleBuffer(BBuffer* buffer);
				void		_ProduceNewBuffer(const media_timed_event* event,
								bigtime_t late);
				BBuffer*	_GetNextBuffer(BMediaOutput* output,
								bigtime_t eventTime);

			BMediaClient*	fOwner;
			bigtime_t		fStartTime;
};

}
}

using namespace BPrivate::media;

#endif
