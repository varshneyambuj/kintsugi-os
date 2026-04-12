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
 * @file OldAudioStream.cpp
 * @brief Stub implementations for the deprecated BeOS R4 audio stream classes.
 *
 * Contains unimplemented bodies for BADCStream (analog-to-digital capture) and
 * BDACStream (digital-to-analog playback) — the low-level stream API that was
 * superseded by BMediaNode in BeOS R5. All methods call UNIMPLEMENTED() and
 * return default values. Compiled only for GCC 2 builds.
 *
 * @see OldAudioModule.cpp, OldBufferStream.cpp
 */


// This is deprecated API that is not even implemented - no need to export
// it on a GCC4 build (BeIDE needs it to run, though, so it's worthwhile for
// GCC2)
#if __GNUC__ < 3


#include "OldAudioStream.h"

#include <MediaDebug.h>


/*************************************************************
 * public BADCStream
 *************************************************************/

/**
 * @brief Constructs a BADCStream for analog-to-digital capture (unimplemented).
 */
BADCStream::BADCStream()
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BADCStream (unimplemented).
 */
BADCStream::~BADCStream()
{
	UNIMPLEMENTED();
}


/**
 * @brief Selects the ADC input device (unimplemented).
 *
 * @param device  Device identifier for the desired ADC input.
 * @return B_ERROR always.
 */
status_t
BADCStream::SetADCInput(int32 device)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Retrieves the currently selected ADC input device (unimplemented).
 *
 * @param device  Output pointer for the current device identifier.
 * @return B_ERROR always.
 */
status_t
BADCStream::ADCInput(int32 *device) const
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Sets the ADC sampling rate (unimplemented).
 *
 * @param sRate  Desired sampling rate in Hz.
 * @return B_ERROR always.
 */
status_t
BADCStream::SetSamplingRate(float sRate)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Retrieves the current ADC sampling rate (unimplemented).
 *
 * @param sRate  Output pointer for the sampling rate in Hz.
 * @return B_ERROR always.
 */
status_t
BADCStream::SamplingRate(float *sRate) const
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Enables or disables microphone boost on the ADC input (unimplemented).
 *
 * @param boost  true to enable boost, false to disable.
 * @return B_ERROR always.
 */
status_t
BADCStream::BoostMic(bool boost)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns whether microphone boost is currently enabled (unimplemented).
 *
 * @return false always.
 */
bool
BADCStream::IsMicBoosted() const
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Configures the ADC stream buffer layout (unimplemented).
 *
 * @param bufferSize   Size of each capture buffer in bytes.
 * @param bufferCount  Number of buffers in the stream ring.
 * @return B_ERROR always.
 */
status_t
BADCStream::SetStreamBuffers(size_t bufferSize,
							 int32 bufferCount)
{
	UNIMPLEMENTED();

	return B_ERROR;
}

/*************************************************************
 * protected BADCStream
 *************************************************************/


/**
 * @brief Returns the BMessenger connected to the media server (unimplemented).
 *
 * @return NULL always.
 */
BMessenger *
BADCStream::Server() const
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Returns the server-side stream identifier (unimplemented).
 *
 * @return 0 always.
 */
stream_id
BADCStream::StreamID() const
{
	UNIMPLEMENTED();

	return 0;
}

/*************************************************************
 * private BADCStream
 *************************************************************/


/**
 * @brief Reserved virtual method slot 1 (unimplemented).
 */
void
BADCStream::_ReservedADCStream1()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 2 (unimplemented).
 */
void
BADCStream::_ReservedADCStream2()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 3 (unimplemented).
 */
void
BADCStream::_ReservedADCStream3()
{
	UNIMPLEMENTED();
}

/*************************************************************
 * public BDACStream
 *************************************************************/

/**
 * @brief Constructs a BDACStream for digital-to-analog playback (unimplemented).
 */
BDACStream::BDACStream()
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BDACStream (unimplemented).
 */
BDACStream::~BDACStream()
{
	UNIMPLEMENTED();
}


/**
 * @brief Sets the DAC output sampling rate (unimplemented).
 *
 * @param sRate  Desired sampling rate in Hz.
 * @return B_ERROR always.
 */
status_t
BDACStream::SetSamplingRate(float sRate)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Retrieves the current DAC output sampling rate (unimplemented).
 *
 * @param sRate  Output pointer for the sampling rate in Hz.
 * @return B_ERROR always.
 */
status_t
BDACStream::SamplingRate(float *sRate) const
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Sets the left and right channel volumes for a DAC output device (unimplemented).
 *
 * @param device    Device identifier.
 * @param l_volume  Left channel volume (0.0 to 1.0).
 * @param r_volume  Right channel volume (0.0 to 1.0).
 * @return B_ERROR always.
 */
status_t
BDACStream::SetVolume(int32 device,
					  float l_volume,
					  float r_volume)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Retrieves left/right volumes and enabled state for a DAC device (unimplemented).
 *
 * @param device    Device identifier.
 * @param l_volume  Output pointer for the left channel volume.
 * @param r_volume  Output pointer for the right channel volume.
 * @param enabled   Output pointer for the enabled state.
 * @return B_ERROR always.
 */
status_t
BDACStream::GetVolume(int32 device,
					  float *l_volume,
					  float *r_volume,
					  bool *enabled) const
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Enables or disables a DAC output device (unimplemented).
 *
 * @param device  Device identifier.
 * @param enable  true to enable, false to disable.
 * @return B_ERROR always.
 */
status_t
BDACStream::EnableDevice(int32 device,
						 bool enable)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns whether a DAC output device is currently enabled (unimplemented).
 *
 * @param device  Device identifier.
 * @return false always.
 */
bool
BDACStream::IsDeviceEnabled(int32 device) const
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Configures the DAC stream buffer layout (unimplemented).
 *
 * @param bufferSize   Size of each playback buffer in bytes.
 * @param bufferCount  Number of buffers in the stream ring.
 * @return B_ERROR always.
 */
status_t
BDACStream::SetStreamBuffers(size_t bufferSize,
							 int32 bufferCount)
{
	UNIMPLEMENTED();

	return B_ERROR;
}

/*************************************************************
 * protected BDACStream
 *************************************************************/

/**
 * @brief Returns the BMessenger connected to the media server (unimplemented).
 *
 * @return NULL always.
 */
BMessenger *
BDACStream::Server() const
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Returns the server-side stream identifier for this DAC stream (unimplemented).
 *
 * @return 0 always.
 */
stream_id
BDACStream::StreamID() const
{
	UNIMPLEMENTED();

	return 0;
}

/*************************************************************
 * private BDACStream
 *************************************************************/

/**
 * @brief Reserved virtual method slot 1 (unimplemented).
 */
void
BDACStream::_ReservedDACStream1()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 2 (unimplemented).
 */
void
BDACStream::_ReservedDACStream2()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 3 (unimplemented).
 */
void
BDACStream::_ReservedDACStream3()
{
	UNIMPLEMENTED();
}


#endif	// __GNUC__ < 3
