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
 *   Copyright 2002, Marcus Overhagen. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file OldAudioModule.cpp
 * @brief Stub implementations for the deprecated BeOS R4 audio module API.
 *
 * Contains unimplemented bodies for BAudioEvent, BDACRenderer, BAudioFileStream,
 * and BADCSource — classes that formed the pre-R5 audio rendering pipeline.
 * All methods call UNIMPLEMENTED() and return default values. This code is
 * compiled only for GCC 2 builds that need ABI compatibility with BeIDE.
 *
 * @see OldMediaModule.cpp, OldAudioStream.cpp
 */


// This is deprecated API that is not even implemented - no need to export
// it on a GCC4 build (BeIDE needs it to run, though, so it's worthwhile for
// GCC2)
#if __GNUC__ < 3


#include "OldAudioModule.h"

#include <MediaDebug.h>


/*************************************************************
 * public BAudioEvent
 *************************************************************/

/**
 * @brief Constructs a BAudioEvent with sample data (unimplemented).
 *
 * @param frames   Number of audio frames in this event.
 * @param stereo   true for stereo, false for mono.
 * @param samples  Pointer to the PCM sample data.
 */
BAudioEvent::BAudioEvent(int32 frames, bool stereo, float *samples)
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BAudioEvent (unimplemented).
 */
BAudioEvent::~BAudioEvent()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the start time of this audio event (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BAudioEvent::Start()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the start time of this audio event (unimplemented).
 *
 * @param time  The new start time.
 */
void
BAudioEvent::SetStart(mk_time)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the duration of this audio event (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BAudioEvent::Duration()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the number of audio frames in this event (unimplemented).
 *
 * @return 0 always.
 */
int32
BAudioEvent::Frames()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns a pointer to the PCM sample data (unimplemented).
 *
 * @return NULL always.
 */
float *
BAudioEvent::Samples()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Returns the number of channels in this event (unimplemented).
 *
 * @return 0 always.
 */
int32
BAudioEvent::ChannelCount()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the gain level for this event (unimplemented).
 *
 * @return 0.0f always.
 */
float
BAudioEvent::Gain()
{
	UNIMPLEMENTED();

	return 0.0f;
}


/**
 * @brief Sets the gain level for this event (unimplemented).
 *
 * @param gain  Desired gain value.
 */
void
BAudioEvent::SetGain(float)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the output destination for this event (unimplemented).
 *
 * @return 0 always.
 */
int32
BAudioEvent::Destination()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the output destination for this event (unimplemented).
 *
 * @param dest  Destination identifier.
 */
void
BAudioEvent::SetDestination(int32)
{
	UNIMPLEMENTED();
}


/**
 * @brief Mixes this event into a destination buffer (unimplemented).
 *
 * @param dst     Destination float buffer.
 * @param frames  Number of frames to mix.
 * @param time    Media time offset for mixing.
 * @return false always.
 */
bool
BAudioEvent::MixIn(float *dst, int32 frames, mk_time time)
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Creates a copy of this audio event (unimplemented).
 *
 * @return NULL always.
 */
BMediaEvent *
BAudioEvent::Clone()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Returns the capture time of this event (unimplemented).
 *
 * @return 0 always.
 */
bigtime_t
BAudioEvent::CaptureTime()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the capture time of this event (unimplemented).
 *
 * @param time  The capture timestamp.
 */
void
BAudioEvent::SetCaptureTime(bigtime_t)
{
	UNIMPLEMENTED();
}


/*************************************************************
 * public BDACRenderer
 *************************************************************/

/**
 * @brief Constructs a BDACRenderer with the given name (unimplemented).
 *
 * @param name  Human-readable name for this renderer.
 */
BDACRenderer::BDACRenderer(const char *name)
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BDACRenderer (unimplemented).
 */
BDACRenderer::~BDACRenderer()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the DAC output rate in units per second (unimplemented).
 *
 * @return 0 always.
 */
mk_rate
BDACRenderer::Units()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the DAC hardware latency (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BDACRenderer::Latency()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the start time of the renderer (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BDACRenderer::Start()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the duration of the renderer's active range (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BDACRenderer::Duration()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the time base associated with this renderer (unimplemented).
 *
 * @return NULL always.
 */
BTimeBase *
BDACRenderer::TimeBase()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Opens the DAC output device (unimplemented).
 */
void
BDACRenderer::Open()
{
	UNIMPLEMENTED();
}


/**
 * @brief Closes the DAC output device (unimplemented).
 */
void
BDACRenderer::Close()
{
	UNIMPLEMENTED();
}


/**
 * @brief Wakes the renderer thread to process pending audio (unimplemented).
 */
void
BDACRenderer::Wakeup()
{
	UNIMPLEMENTED();
}


/**
 * @brief Notifies the renderer of a transport state change (unimplemented).
 *
 * @param time    Media time of the transport change.
 * @param rate    New playback rate.
 * @param status  New transport status.
 */
void
BDACRenderer::TransportChanged(mk_time time, mk_rate rate,
	transport_status status)
{
	UNIMPLEMENTED();
}


/**
 * @brief Notifies the renderer that the stream configuration changed (unimplemented).
 */
void
BDACRenderer::StreamChanged()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the media channel associated with this renderer (unimplemented).
 *
 * @return NULL always.
 */
BMediaChannel *
BDACRenderer::Channel()
{
	UNIMPLEMENTED();
	return NULL;
}

/*************************************************************
 * private BDACRenderer
 *************************************************************/


/**
 * @brief Internal DAC write callback used by the audio driver (unimplemented).
 *
 * @param arg     Renderer instance pointer.
 * @param buf     Output audio buffer.
 * @param bytes   Number of bytes to write.
 * @param header  Audio buffer header.
 * @return false always.
 */
bool
BDACRenderer::_WriteDAC(void *arg, char *buf, uint32 bytes, void *header)
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Writes 16-bit PCM frames to the DAC hardware (unimplemented).
 *
 * @param buf     Pointer to short PCM samples.
 * @param frames  Number of frames to write.
 * @param header  Audio buffer header.
 * @return false always.
 */
bool
BDACRenderer::WriteDAC(short *buf, int32 frames, audio_buffer_header *header)
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Mixes all active audio segments starting at a given time (unimplemented).
 *
 * @param start  Media time from which to mix active segments.
 * @return false always.
 */
bool
BDACRenderer::MixActiveSegments(mk_time start)
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Mixes the internal event list down to a 16-bit output buffer (unimplemented).
 *
 * @param dst  Destination 16-bit PCM buffer.
 */
void
BDACRenderer::MixOutput(short *dst)
{
	UNIMPLEMENTED();
}


/*************************************************************
 * public BAudioFileStream
 *************************************************************/

/**
 * @brief Constructs a BAudioFileStream reading from a BFile (unimplemented).
 *
 * @param channel  The BMediaChannel to associate with this stream.
 * @param file     The BFile to read audio data from.
 * @param start    Media time offset at which playback begins.
 */
BAudioFileStream::BAudioFileStream(BMediaChannel *channel, BFile *file,
	mk_time start)
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BAudioFileStream (unimplemented).
 */
BAudioFileStream::~BAudioFileStream()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the next audio event from this file stream (unimplemented).
 *
 * @param channel  The channel requesting the event.
 * @return NULL always.
 */
BMediaEvent *
BAudioFileStream::GetEvent(BMediaChannel *channel)
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Peeks at the next event without advancing the read position (unimplemented).
 *
 * @param channel  The channel requesting the peek.
 * @param asap     Deadline time for the requested event.
 * @return NULL always.
 */
BMediaEvent *
BAudioFileStream::PeekEvent(BMediaChannel *channel, mk_time asap)
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Seeks the file stream to a given media time (unimplemented).
 *
 * @param channel  The channel requesting the seek.
 * @param time     Target media time.
 * @return B_ERROR always.
 */
status_t
BAudioFileStream::SeekToTime(BMediaChannel *channel, mk_time time)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Sets the stream's start time offset (unimplemented).
 *
 * @param start  New start time in media ticks.
 */
void
BAudioFileStream::SetStart(mk_time start)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the capture time of the stream (unimplemented).
 *
 * @return 0 always.
 */
bigtime_t
BAudioFileStream::CaptureTime()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the media channel associated with this stream (unimplemented).
 *
 * @return NULL always.
 */
BMediaChannel *
BAudioFileStream::Channel()
{
	UNIMPLEMENTED();
	return NULL;
}

/*************************************************************
 * public BADCSource
 *************************************************************/

/**
 * @brief Constructs a BADCSource capturing from the ADC (unimplemented).
 *
 * @param channel  The BMediaChannel to associate with this source.
 * @param start    Media time at which capture begins.
 */
BADCSource::BADCSource(BMediaChannel *channel, mk_time start)
	:
	fEventLock("BADCSource lock")
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BADCSource (unimplemented).
 */
BADCSource::~BADCSource()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the next captured audio event (unimplemented).
 *
 * @param channel  The channel requesting the event.
 * @return NULL always.
 */
BMediaEvent *
BADCSource::GetEvent(BMediaChannel *channel)
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Peeks at the next captured event without consuming it (unimplemented).
 *
 * @param channel  The channel requesting the peek.
 * @param asap     Deadline time for the requested event.
 * @return NULL always.
 */
BMediaEvent *
BADCSource::PeekEvent(BMediaChannel *channel, mk_time asap)
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Seeks the ADC source to a given media time (unimplemented).
 *
 * @param channel  The channel requesting the seek.
 * @param time     Target media time.
 * @return B_ERROR always.
 */
status_t
BADCSource::SeekToTime(BMediaChannel *channel, mk_time time)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Sets the capture start time for this source (unimplemented).
 *
 * @param start  New start time in media ticks.
 */
void
BADCSource::SetStart(mk_time start)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the media channel associated with this source (unimplemented).
 *
 * @return NULL always.
 */
BMediaChannel *
BADCSource::Channel()
{
	UNIMPLEMENTED();
	return NULL;
}

/*************************************************************
 * private BADCSource
 *************************************************************/

/**
 * @brief Internal ADC read callback used by the audio driver (unimplemented).
 *
 * @param arg     Source instance pointer.
 * @param buf     Input audio buffer.
 * @param bytes   Number of bytes available.
 * @param header  Audio buffer header.
 * @return false always.
 */
bool
BADCSource::_ReadADC(void *arg, char *buf, uint32 bytes, void *header)
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Reads 16-bit PCM frames from the ADC hardware (unimplemented).
 *
 * @param buf     Destination short PCM buffer.
 * @param frames  Number of frames to read.
 * @param header  Audio buffer header.
 */
void
BADCSource::ReadADC(short *buf, int32 frames, audio_buffer_header *header)
{
	UNIMPLEMENTED();
}


#endif	// __GNUC__ < 3
