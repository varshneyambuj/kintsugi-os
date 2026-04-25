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
 * Incorporates work from the Be Incorporated media kit headers, originally
 * Copyright 1995-97, Be Incorporated.
 */

/** @file OldAudioStream.h
    @brief Legacy R5 BADCStream and BDACStream classes wrapping the audio
           server's capture and playback buffer streams. Deprecated; retained
           for binary compatibility. */

#ifndef _AUDIO_STREAM_H
#define _AUDIO_STREAM_H


#include "OldBufferStream.h"


/* ================
   Class definition for BADCStream and BDACStream
   ================ */

/** @brief Legacy buffer-stream client for capturing audio from the ADC, with
           controls for input device, sampling rate, and mic boost. Deprecated. */
class BADCStream : public BAbstractBufferStream
{
public:

					BADCStream();
	virtual			~BADCStream();

	status_t		SetADCInput(int32 device);
	status_t		ADCInput(int32* device) const;

	status_t		SetSamplingRate(float sRate);
	status_t		SamplingRate(float* sRate) const;

	status_t		BoostMic(bool boost);
	bool			IsMicBoosted() const;

	status_t		SetStreamBuffers(size_t bufferSize, int32 bufferCount);

protected:

	virtual BMessenger*	Server() const; 		/* message pipe to server */
	virtual stream_id	StreamID() const;		/* stream identifier */

private:

virtual	void		_ReservedADCStream1();
virtual	void		_ReservedADCStream2();
virtual	void		_ReservedADCStream3();

	BMessenger*		fServer;
	stream_id		fStreamID;
	uint32			_reserved[4];
};


/** @brief Legacy buffer-stream client for playing audio through the DAC, with
           controls for sampling rate and per-device volume/enable. Deprecated. */
class BDACStream : public BAbstractBufferStream
{
public:

					BDACStream();
	virtual			~BDACStream();

	status_t		SetSamplingRate(float sRate);
	status_t		SamplingRate(float* sRate) const;

	status_t		SetVolume(int32 device,
							  float l_volume,
							  float r_volume);

	status_t		GetVolume(int32 device,
							  float *l_volume,
							  float *r_volume,
							  bool *enabled) const;

	status_t		EnableDevice(int32 device, bool enable);
	bool			IsDeviceEnabled(int32 device) const;

	status_t		SetStreamBuffers(size_t bufferSize, int32 bufferCount);

protected:

	virtual BMessenger*	Server() const; 		/* message pipe to server */
	virtual stream_id	StreamID() const;		/* stream identifier */

private:

virtual	void		_ReservedDACStream1();
virtual	void		_ReservedDACStream2();
virtual	void		_ReservedDACStream3();

	BMessenger*		fServer;
	stream_id		fStreamID;
	uint32			_reserved[4];
};

#endif			// #ifdef _AUDIO_STREAM_H
