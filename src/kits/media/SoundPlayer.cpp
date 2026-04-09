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
 *   Copyright 2002-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marcus Overhagen
 *       Jérôme Duval
 */

/** @file SoundPlayer.cpp
 *  @brief Implements BSoundPlayer, a high-level interface for playing audio
 *         through the Media Kit using an internal BBufferProducer node.
 */


#include <SoundPlayer.h>

#include <math.h>
#include <string.h>

#include <Autolock.h>
#include <MediaRoster.h>
#include <ParameterWeb.h>
#include <Sound.h>
#include <TimeSource.h>

#include "SoundPlayNode.h"

#include "MediaDebug.h"


// Flags used internally in BSoundPlayer
enum {
	F_NODES_CONNECTED	= (1 << 0),
	F_HAS_DATA			= (1 << 1),
	F_IS_STARTED		= (1 << 2),
	F_MUST_RELEASE_MIXER = (1 << 3),
};


static BSoundPlayer::play_id sCurrentPlayID = 1;


/**
 * @brief Construct a BSoundPlayer using the default audio format and mixer.
 *
 * @param name                   Name for the internal player node.
 * @param playerFunction         Callback invoked each time a buffer needs to
 *                               be filled; may be NULL to use the default
 *                               BSound-based implementation.
 * @param eventNotifierFunction  Callback invoked on player events (start,
 *                               stop, sound done); may be NULL.
 * @param cookie                 Arbitrary pointer passed back in callbacks.
 */
BSoundPlayer::BSoundPlayer(const char* name, BufferPlayerFunc playerFunction,
	EventNotifierFunc eventNotifierFunction, void* cookie)
{
	CALLED();

	TRACE("BSoundPlayer::BSoundPlayer: default constructor used\n");

	media_multi_audio_format format = media_multi_audio_format::wildcard;

	_Init(NULL, &format, name, NULL, playerFunction, eventNotifierFunction,
		cookie);
}


/**
 * @brief Construct a BSoundPlayer with a specific raw audio format.
 *
 * @param _format                Desired raw audio format for the output.
 * @param name                   Name for the internal player node.
 * @param playerFunction         Buffer-fill callback; may be NULL.
 * @param eventNotifierFunction  Event notification callback; may be NULL.
 * @param cookie                 Arbitrary pointer passed back in callbacks.
 */
BSoundPlayer::BSoundPlayer(const media_raw_audio_format* _format,
	const char* name, BufferPlayerFunc playerFunction,
	EventNotifierFunc eventNotifierFunction, void* cookie)
{
	CALLED();

	TRACE("BSoundPlayer::BSoundPlayer: raw audio format constructor used\n");

	media_multi_audio_format format = media_multi_audio_format::wildcard;
	*(media_raw_audio_format*)&format = *_format;

#if DEBUG > 0
	char buf[100];
	media_format tmp; tmp.type = B_MEDIA_RAW_AUDIO; tmp.u.raw_audio = format;
	string_for_format(tmp, buf, sizeof(buf));
	TRACE("BSoundPlayer::BSoundPlayer: format %s\n", buf);
#endif

	_Init(NULL, &format, name, NULL, playerFunction, eventNotifierFunction,
		cookie);
}


/**
 * @brief Construct a BSoundPlayer connected to a specific media node and input.
 *
 * @param toNode                 Destination media node (must be a buffer consumer).
 * @param format                 Multi-channel audio format to negotiate.
 * @param name                   Name for the internal player node.
 * @param input                  Specific media input on @p toNode; may be NULL
 *                               to select a free input automatically.
 * @param playerFunction         Buffer-fill callback; may be NULL.
 * @param eventNotifierFunction  Event notification callback; may be NULL.
 * @param cookie                 Arbitrary pointer passed back in callbacks.
 */
BSoundPlayer::BSoundPlayer(const media_node& toNode,
	const media_multi_audio_format* format, const char* name,
	const media_input* input, BufferPlayerFunc playerFunction,
	EventNotifierFunc eventNotifierFunction, void* cookie)
{
	CALLED();

	TRACE("BSoundPlayer::BSoundPlayer: multi audio format constructor used\n");

	if ((toNode.kind & B_BUFFER_CONSUMER) == 0)
		debugger("BSoundPlayer: toNode must have B_BUFFER_CONSUMER kind!\n");

#if DEBUG > 0
	char buf[100];
	media_format tmp; tmp.type = B_MEDIA_RAW_AUDIO; tmp.u.raw_audio = *format;
	string_for_format(tmp, buf, sizeof(buf));
	TRACE("BSoundPlayer::BSoundPlayer: format %s\n", buf);
#endif

	_Init(&toNode, format, name, input, playerFunction, eventNotifierFunction,
		cookie);
}


/**
 * @brief Destructor. Stops playback, disconnects nodes, and releases resources.
 */
BSoundPlayer::~BSoundPlayer()
{
	CALLED();

	if ((fFlags & F_IS_STARTED) != 0) {
		// block, but don't flush
		Stop(true, false);
	}

	status_t err;
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		TRACE("BSoundPlayer::~BSoundPlayer: Couldn't get BMediaRoster\n");
		goto cleanup;
	}

	if ((fFlags & F_NODES_CONNECTED) != 0) {
		// Ordinarily we'd stop *all* of the nodes in the chain before
		// disconnecting. However, our node is already stopped, and we can't
		// stop the System Mixer.
		// So, we just disconnect from it, and release our references to the
		// nodes that we're using. We *are* supposed to do that even for global
		// nodes like the Mixer.
		err = roster->Disconnect(fMediaOutput, fMediaInput);
		if (err != B_OK) {
			TRACE("BSoundPlayer::~BSoundPlayer: Error disconnecting nodes: "
				"%" B_PRId32 " (%s)\n", err, strerror(err));
		}
	}

	if ((fFlags & F_MUST_RELEASE_MIXER) != 0) {
		// Release the mixer as it was acquired
		// through BMediaRoster::GetAudioMixer()
		err = roster->ReleaseNode(fMediaInput.node);
		if (err != B_OK) {
			TRACE("BSoundPlayer::~BSoundPlayer: Error releasing input node: "
				"%" B_PRId32 " (%s)\n", err, strerror(err));
		}
	}

cleanup:
	// Dispose of the player node

	// We do not call BMediaRoster::ReleaseNode(), since
	// the player was created by using "new". We could
	// call BMediaRoster::UnregisterNode(), but this is
	// supposed to be done by BMediaNode destructor automatically.

	// The node is deleted by the Release() when ref count reach 0.
	// Since we are the sole owners, and no one acquired it
	// this should be the case. The Quit() synchronization
	// is handled by the DeleteHook inheritance.
	// NOTE: this might be crucial when using a BMediaEventLooper.
	if (fPlayerNode != NULL && fPlayerNode->Release() != NULL) {
		TRACE("BSoundPlayer::~BSoundPlayer: Error the producer node "
			"appears to be acquired by someone else than us!");
	}

	// do not delete fVolumeSlider, it belongs to the parameter web
	delete fParameterWeb;
}


/**
 * @brief Return the initialisation status of this BSoundPlayer.
 *
 * @return B_OK if the player is ready, or an error code.
 */
status_t
BSoundPlayer::InitCheck()
{
	CALLED();
	return fInitStatus;
}


/**
 * @brief Return the negotiated raw audio format.
 *
 * @return The media_raw_audio_format agreed upon during connection, or
 *         media_raw_audio_format::wildcard if not connected.
 */
media_raw_audio_format
BSoundPlayer::Format() const
{
	CALLED();

	if ((fFlags & F_NODES_CONNECTED) == 0)
		return media_raw_audio_format::wildcard;

	return fPlayerNode->Format();
}


/**
 * @brief Start audio playback.
 *
 * Starts the time source if necessary, then starts the internal player node
 * with enough lead time to fill the pipeline.
 *
 * @return B_OK on success, B_NO_INIT if not connected, or a Media Kit error.
 */
status_t
BSoundPlayer::Start()
{
	CALLED();

	if ((fFlags & F_NODES_CONNECTED) == 0)
		return B_NO_INIT;

	if ((fFlags & F_IS_STARTED) != 0)
		return B_OK;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (!roster) {
		TRACE("BSoundPlayer::Start: Couldn't get BMediaRoster\n");
		return B_ERROR;
	}

	if (!fPlayerNode->TimeSource()->IsRunning()) {
		roster->StartTimeSource(fPlayerNode->TimeSource()->Node(),
			fPlayerNode->TimeSource()->RealTime());
	}

	// Add latency and a few ms to the nodes current time to
	// make sure that we give the producer enough time to run
	// buffers through the node chain, otherwise it'll start
	// up already late

	status_t err = roster->StartNode(fPlayerNode->Node(),
		fPlayerNode->TimeSource()->Now() + Latency() + 5000);
	if (err != B_OK) {
		TRACE("BSoundPlayer::Start: StartNode failed, %" B_PRId32, err);
		return err;
	}

	if (fNotifierFunc != NULL)
		fNotifierFunc(fCookie, B_STARTED, this);

	SetHasData(true);
	atomic_or(&fFlags, F_IS_STARTED);

	return B_OK;
}


/**
 * @brief Stop audio playback.
 *
 * @param block  If true, wait until the node has fully stopped and all
 *               in-flight buffers have been played.
 * @param flush  Reserved; currently ignored.
 */
void
BSoundPlayer::Stop(bool block, bool flush)
{
	CALLED();

	TRACE("BSoundPlayer::Stop: block %d, flush %d\n", (int)block, (int)flush);

	if ((fFlags & F_NODES_CONNECTED) == 0)
		return;

	// TODO: flush is ignored

	if ((fFlags & F_IS_STARTED) != 0) {
		BMediaRoster* roster = BMediaRoster::Roster();
		if (roster == NULL) {
			TRACE("BSoundPlayer::Stop: Couldn't get BMediaRoster\n");
			return;
		}

		roster->StopNode(fPlayerNode->Node(), 0, true);

		atomic_and(&fFlags, ~F_IS_STARTED);
	}

	if (block) {
		// wait until the node is stopped
		int tries;
		for (tries = 250; fPlayerNode->IsPlaying() && tries != 0; tries--)
			snooze(2000);

		DEBUG_ONLY(if (tries == 0)
			TRACE("BSoundPlayer::Stop: waiting for node stop failed\n"));

		// Wait until all buffers on the way to the physical output have been
		// played
		snooze(Latency() + 2000);
	}

	if (fNotifierFunc)
		fNotifierFunc(fCookie, B_STOPPED, this);

}


/**
 * @brief Query the total end-to-end latency of the audio pipeline.
 *
 * @return Latency in microseconds, or 0 if not connected or on error.
 */
bigtime_t
BSoundPlayer::Latency()
{
	CALLED();

	if ((fFlags & F_NODES_CONNECTED) == 0)
		return 0;

	BMediaRoster *roster = BMediaRoster::Roster();
	if (!roster) {
		TRACE("BSoundPlayer::Latency: Couldn't get BMediaRoster\n");
		return 0;
	}

	bigtime_t latency;
	status_t err = roster->GetLatencyFor(fMediaOutput.node, &latency);
	if (err != B_OK) {
		TRACE("BSoundPlayer::Latency: GetLatencyFor failed %" B_PRId32
			" (%s)\n", err, strerror(err));
		return 0;
	}

	TRACE("BSoundPlayer::Latency: latency is %" B_PRId64 "\n", latency);

	return latency;
}


/**
 * @brief Mark whether there is audio data available for playback.
 *
 * When set to false the internal buffer callback will fill buffers with silence.
 *
 * @param hasData  true if data is available, false to indicate silence.
 */
void
BSoundPlayer::SetHasData(bool hasData)
{
	CALLED();
	if (hasData)
		atomic_or(&fFlags, F_HAS_DATA);
	else
		atomic_and(&fFlags, ~F_HAS_DATA);
}


/**
 * @brief Return whether audio data is currently available.
 *
 * @return true if audio data is available, false if silence should be output.
 */
bool
BSoundPlayer::HasData()
{
	CALLED();
	return (atomic_get(&fFlags) & F_HAS_DATA) != 0;
}


/**
 * @brief Return the current buffer-fill callback function.
 *
 * @return The BufferPlayerFunc callback, or NULL if none is set.
 */
BSoundPlayer::BufferPlayerFunc
BSoundPlayer::BufferPlayer() const
{
	CALLED();
	return fPlayBufferFunc;
}


/**
 * @brief Replace the buffer-fill callback function.
 *
 * @param playerFunction  New callback to use; may be NULL to disable custom filling.
 */
void
BSoundPlayer::SetBufferPlayer(BufferPlayerFunc playerFunction)
{
	CALLED();
	BAutolock _(fLocker);

	fPlayBufferFunc = playerFunction;
}


/**
 * @brief Return the current event-notification callback function.
 *
 * @return The EventNotifierFunc callback, or NULL if none is set.
 */
BSoundPlayer::EventNotifierFunc
BSoundPlayer::EventNotifier() const
{
	CALLED();
	return fNotifierFunc;
}


/**
 * @brief Replace the event-notification callback function.
 *
 * @param eventNotifierFunction  New callback to use; may be NULL.
 */
void
BSoundPlayer::SetNotifier(EventNotifierFunc eventNotifierFunction)
{
	CALLED();
	BAutolock _(fLocker);

	fNotifierFunc = eventNotifierFunction;
}


/**
 * @brief Return the cookie value passed to callbacks.
 *
 * @return The cookie pointer.
 */
void*
BSoundPlayer::Cookie() const
{
	CALLED();
	return fCookie;
}


/**
 * @brief Set the cookie value passed to callbacks.
 *
 * @param cookie  New cookie pointer.
 */
void
BSoundPlayer::SetCookie(void *cookie)
{
	CALLED();
	BAutolock _(fLocker);

	fCookie = cookie;
}


/**
 * @brief Atomically set the buffer-player, notifier, and cookie.
 *
 * @param playerFunction         New buffer-fill callback.
 * @param eventNotifierFunction  New event-notification callback.
 * @param cookie                 New cookie pointer.
 */
void
BSoundPlayer::SetCallbacks(BufferPlayerFunc playerFunction,
	EventNotifierFunc eventNotifierFunction, void* cookie)
{
	CALLED();
	BAutolock _(fLocker);

	SetBufferPlayer(playerFunction);
	SetNotifier(eventNotifierFunction);
	SetCookie(cookie);
}


/*!	The BeBook is inaccurate about the meaning of this function.
	The probably best interpretation is to return the time that
	has elapsed since playing was started, whichs seems to match
	"CurrentTime() returns the current media time"
*/
/**
 * @brief Return the current media time elapsed since playback started.
 *
 * @return Microseconds elapsed since Start() was called, or 0 if not connected.
 */
bigtime_t
BSoundPlayer::CurrentTime()
{
	if ((fFlags & F_NODES_CONNECTED) == 0)
		return 0;

	return fPlayerNode->CurrentTime();
}


/*!	Returns the current performance time of the sound player node
	being used by the BSoundPlayer. Will return B_ERROR if the
	BSoundPlayer object hasn't been properly initialized.
*/
/**
 * @brief Return the current performance time of the internal player node.
 *
 * @return Performance time in microseconds, or B_ERROR if not connected.
 */
bigtime_t
BSoundPlayer::PerformanceTime()
{
	if ((fFlags & F_NODES_CONNECTED) == 0)
		return (bigtime_t) B_ERROR;

	return fPlayerNode->TimeSource()->Now();
}


/**
 * @brief Preroll the internal player node so it is ready to start immediately.
 *
 * @return B_OK on success, B_NO_INIT if not connected, or a Media Kit error.
 */
status_t
BSoundPlayer::Preroll()
{
	CALLED();

	if ((fFlags & F_NODES_CONNECTED) == 0)
		return B_NO_INIT;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		TRACE("BSoundPlayer::Preroll: Couldn't get BMediaRoster\n");
		return B_ERROR;
	}

	status_t err = roster->PrerollNode(fMediaOutput.node);
	if (err != B_OK) {
		TRACE("BSoundPlayer::Preroll: Error while PrerollNode: %"
			B_PRId32 " (%s)\n", err, strerror(err));
		return err;
	}

	return B_OK;
}


/**
 * @brief Begin playing a BSound at a specified time (volume defaults to 1.0).
 *
 * @param sound   The BSound object to play.
 * @param atTime  Performance time at which playback should begin.
 * @return A play_id handle identifying this playback, or a negative error code.
 */
BSoundPlayer::play_id
BSoundPlayer::StartPlaying(BSound* sound, bigtime_t atTime)
{
	return StartPlaying(sound, atTime, 1.0);
}


/**
 * @brief Begin playing a BSound at a specified time and volume.
 *
 * @param sound       The BSound object to play.
 * @param atTime      Performance time at which playback should begin.
 * @param withVolume  Playback volume as a linear factor (1.0 = full volume).
 * @return A play_id handle identifying this playback, or a negative error code.
 */
BSoundPlayer::play_id
BSoundPlayer::StartPlaying(BSound* sound, bigtime_t atTime, float withVolume)
{
	CALLED();

	// TODO: support the at_time and with_volume parameters
	playing_sound* item = (playing_sound*)malloc(sizeof(playing_sound));
	if (item == NULL)
		return B_NO_MEMORY;

	item->current_offset = 0;
	item->sound = sound;
	item->id = atomic_add(&sCurrentPlayID, 1);
	item->delta = 0;
	item->rate = 0;
	item->volume = withVolume;

	if (!fLocker.Lock()) {
		free(item);
		return B_ERROR;
	}

	sound->AcquireRef();
	item->next = fPlayingSounds;
	fPlayingSounds = item;
	fLocker.Unlock();

	SetHasData(true);
	return item->id;
}


/**
 * @brief Adjust the playback volume of a currently playing sound.
 *
 * @param id        play_id returned by StartPlaying().
 * @param newVolume New linear volume factor.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if @p id is not active.
 */
status_t
BSoundPlayer::SetSoundVolume(play_id id, float newVolume)
{
	CALLED();
	if (!fLocker.Lock())
		return B_ERROR;

	playing_sound *item = fPlayingSounds;
	while (item) {
		if (item->id == id) {
			item->volume = newVolume;
			fLocker.Unlock();
			return B_OK;
		}

		item = item->next;
	}

	fLocker.Unlock();
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Check whether a given sound handle is still actively playing.
 *
 * @param id  play_id returned by StartPlaying().
 * @return true if still playing, false otherwise.
 */
bool
BSoundPlayer::IsPlaying(play_id id)
{
	CALLED();
	if (!fLocker.Lock())
		return B_ERROR;

	playing_sound *item = fPlayingSounds;
	while (item) {
		if (item->id == id) {
			fLocker.Unlock();
			return true;
		}

		item = item->next;
	}

	fLocker.Unlock();
	return false;
}


/**
 * @brief Immediately stop a currently playing sound.
 *
 * Notifies waiters and fires the B_SOUND_DONE event.
 *
 * @param id  play_id returned by StartPlaying().
 * @return B_OK on success, B_ENTRY_NOT_FOUND if @p id is not active.
 */
status_t
BSoundPlayer::StopPlaying(play_id id)
{
	CALLED();
	if (!fLocker.Lock())
		return B_ERROR;

	playing_sound** link = &fPlayingSounds;
	playing_sound* item = fPlayingSounds;

	while (item != NULL) {
		if (item->id == id) {
			*link = item->next;
			sem_id waitSem = item->wait_sem;
			item->sound->ReleaseRef();
			free(item);
			fLocker.Unlock();

			_NotifySoundDone(id, true);
			if (waitSem >= 0)
				release_sem(waitSem);

			return B_OK;
		}

		link = &item->next;
		item = item->next;
	}

	fLocker.Unlock();
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Block until the specified sound has finished playing.
 *
 * @param id  play_id returned by StartPlaying().
 * @return B_OK when the sound finishes, B_ENTRY_NOT_FOUND if @p id is not active.
 */
status_t
BSoundPlayer::WaitForSound(play_id id)
{
	CALLED();
	if (!fLocker.Lock())
		return B_ERROR;

	playing_sound* item = fPlayingSounds;
	while (item != NULL) {
		if (item->id == id) {
			sem_id waitSem = item->wait_sem;
			if (waitSem < 0)
				waitSem = item->wait_sem = create_sem(0, "wait for sound");

			fLocker.Unlock();
			return acquire_sem(waitSem);
		}

		item = item->next;
	}

	fLocker.Unlock();
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Return the current master output volume as a linear factor.
 *
 * @return Linear volume (e.g. 1.0 = 0 dB, 0.5 ≈ -6 dB).
 */
float
BSoundPlayer::Volume()
{
	CALLED();
	return pow(10.0, VolumeDB(true) / 20.0);
}


/**
 * @brief Set the master output volume using a linear factor.
 *
 * @param newVolume  Linear volume factor to set.
 */
void
BSoundPlayer::SetVolume(float newVolume)
{
	CALLED();
	SetVolumeDB(20.0 * log10(newVolume));
}


/**
 * @brief Return the current master output volume in decibels.
 *
 * @param forcePoll  If true, always query the hardware; if false, use a cached
 *                   value that is at most 500 ms old.
 * @return Volume in dB, or -94.0 (silence) if no volume slider is available.
 */
float
BSoundPlayer::VolumeDB(bool forcePoll)
{
	CALLED();
	if (!fVolumeSlider)
		return -94.0f; // silence

	if (!forcePoll && system_time() - fLastVolumeUpdate < 500000)
		return fVolumeDB;

	int32 count = fVolumeSlider->CountChannels();
	float values[count];
	size_t size = count * sizeof(float);
	fVolumeSlider->GetValue(&values, &size, NULL);
	fLastVolumeUpdate = system_time();
	fVolumeDB = values[0];

	return values[0];
}


/**
 * @brief Set the master output volume in decibels.
 *
 * Clamps the value to the supported range of the hardware volume slider.
 *
 * @param volumeDB  Target volume in decibels.
 */
void
BSoundPlayer::SetVolumeDB(float volumeDB)
{
	CALLED();
	if (!fVolumeSlider)
		return;

	float minDB = fVolumeSlider->MinValue();
	float maxDB = fVolumeSlider->MaxValue();
	if (volumeDB < minDB)
		volumeDB = minDB;
	if (volumeDB > maxDB)
		volumeDB = maxDB;

	int count = fVolumeSlider->CountChannels();
	float values[count];
	for (int i = 0; i < count; i++)
		values[i] = volumeDB;
	fVolumeSlider->SetValue(values, sizeof(float) * count, 0);

	fVolumeDB = volumeDB;
	fLastVolumeUpdate = system_time();
}


/**
 * @brief Retrieve information about the volume control parameter.
 *
 * @param _node        If non-NULL, receives the media node controlling volume.
 * @param _parameterID If non-NULL, receives the parameter ID of the volume slider.
 * @param _minDB       If non-NULL, receives the minimum volume in dB.
 * @param _maxDB       If non-NULL, receives the maximum volume in dB.
 * @return B_OK on success, B_NO_INIT if no volume slider was found.
 */
status_t
BSoundPlayer::GetVolumeInfo(media_node* _node, int32* _parameterID,
	float* _minDB, float* _maxDB)
{
	CALLED();
	if (fVolumeSlider == NULL)
		return B_NO_INIT;

	if (_node != NULL)
		*_node = fMediaInput.node;
	if (_parameterID != NULL)
		*_parameterID = fVolumeSlider->ID();
	if (_minDB != NULL)
		*_minDB = fVolumeSlider->MinValue();
	if (_maxDB != NULL)
		*_maxDB = fVolumeSlider->MaxValue();

	return B_OK;
}


// #pragma mark - protected BSoundPlayer


/**
 * @brief Set the initialisation error code (for use by subclasses).
 *
 * @param error  Error code to store as the initialisation status.
 */
void
BSoundPlayer::SetInitError(status_t error)
{
	CALLED();
	fInitStatus = error;
}


// #pragma mark - private BSoundPlayer


/**
 * @brief Default internal buffer-fill callback used when no custom callback is set.
 *
 * Reads audio data from the head of the fPlayingSounds list and copies it into
 * the buffer. Calls StopPlaying() when a sound runs out of data.
 *
 * @param cookie  Pointer to the owning BSoundPlayer.
 * @param buffer  Destination audio buffer to fill.
 * @param size    Size of @p buffer in bytes.
 * @param format  Raw audio format of @p buffer.
 */
void
BSoundPlayer::_SoundPlayBufferFunc(void *cookie, void *buffer, size_t size,
	const media_raw_audio_format &format)
{
	// TODO: support more than one sound and make use of the format parameter
	BSoundPlayer *player = (BSoundPlayer *)cookie;
	if (!player->fLocker.Lock()) {
		memset(buffer, 0, size);
		return;
	}

	playing_sound *sound = player->fPlayingSounds;
	if (sound == NULL) {
		player->SetHasData(false);
		player->fLocker.Unlock();
		memset(buffer, 0, size);
		return;
	}

	size_t used = 0;
	if (!sound->sound->GetDataAt(sound->current_offset, buffer, size, &used)) {
		// will take care of removing the item and notifying others
		player->StopPlaying(sound->id);
		player->fLocker.Unlock();
		memset(buffer, 0, size);
		return;
	}

	sound->current_offset += used;
	player->fLocker.Unlock();

	if (used < size)
		memset((uint8 *)buffer + used, 0, size - used);
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSoundPlayer::_Reserved_SoundPlayer_0(void*, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSoundPlayer::_Reserved_SoundPlayer_1(void*, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSoundPlayer::_Reserved_SoundPlayer_2(void*, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSoundPlayer::_Reserved_SoundPlayer_3(void*, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSoundPlayer::_Reserved_SoundPlayer_4(void*, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSoundPlayer::_Reserved_SoundPlayer_5(void*, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSoundPlayer::_Reserved_SoundPlayer_6(void*, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSoundPlayer::_Reserved_SoundPlayer_7(void*, ...) { return B_ERROR; }


/**
 * @brief Shared initialisation routine called by all constructors.
 *
 * Creates the internal SoundPlayNode, registers it, negotiates the connection
 * with the target mixer node, and locates the volume slider.
 *
 * @param node                   Optional target node; NULL uses the system mixer.
 * @param format                 Desired audio format (wildcards allowed).
 * @param name                   Node name.
 * @param input                  Optional specific input to connect to.
 * @param playerFunction         Buffer-fill callback.
 * @param eventNotifierFunction  Event notification callback.
 * @param cookie                 Cookie for callbacks.
 */
void
BSoundPlayer::_Init(const media_node* node,
	const media_multi_audio_format* format, const char* name,
	const media_input* input, BufferPlayerFunc playerFunction,
	EventNotifierFunc eventNotifierFunction, void* cookie)
{
	CALLED();
	fPlayingSounds = NULL;
	fWaitingSounds = NULL;

	fPlayerNode = NULL;
	if (playerFunction == NULL) {
		fPlayBufferFunc = _SoundPlayBufferFunc;
		fCookie = this;
	} else {
		fPlayBufferFunc = playerFunction;
		fCookie = cookie;
	}

	fNotifierFunc = eventNotifierFunction;
	fVolumeDB = 0.0f;
	fFlags = 0;
	fInitStatus = B_ERROR;
	fParameterWeb = NULL;
	fVolumeSlider = NULL;
	fLastVolumeUpdate = 0;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		TRACE("BSoundPlayer::_Init: Couldn't get BMediaRoster\n");
		return;
	}

	// The inputNode that our player node will be
	// connected with is either supplied by the user
	// or the system audio mixer
	media_node inputNode;
	if (node) {
		inputNode = *node;
	} else {
		fInitStatus = roster->GetAudioMixer(&inputNode);
		if (fInitStatus != B_OK) {
			TRACE("BSoundPlayer::_Init: Couldn't GetAudioMixer\n");
			return;
		}
		fFlags |= F_MUST_RELEASE_MIXER;
	}

	media_output _output;
	media_input _input;
	int32 inputCount;
	int32 outputCount;
	media_format tryFormat;

	// Create the player node and register it
	fPlayerNode = new BPrivate::SoundPlayNode(name, this);
	fInitStatus = roster->RegisterNode(fPlayerNode);
	if (fInitStatus != B_OK) {
		TRACE("BSoundPlayer::_Init: Couldn't RegisterNode: %s\n",
			strerror(fInitStatus));
		return;
	}

	// set the producer's time source to be the "default" time source,
	// which the system audio mixer uses too.
	media_node timeSource;
	fInitStatus = roster->GetTimeSource(&timeSource);
	if (fInitStatus != B_OK) {
		TRACE("BSoundPlayer::_Init: Couldn't GetTimeSource: %s\n",
			strerror(fInitStatus));
		return;
	}
	fInitStatus = roster->SetTimeSourceFor(fPlayerNode->Node().node,
		timeSource.node);
	if (fInitStatus != B_OK) {
		TRACE("BSoundPlayer::_Init: Couldn't SetTimeSourceFor: %s\n",
			strerror(fInitStatus));
		return;
	}

	// find a free media_input
	if (!input) {
		fInitStatus = roster->GetFreeInputsFor(inputNode, &_input, 1,
			&inputCount, B_MEDIA_RAW_AUDIO);
		if (fInitStatus != B_OK) {
			TRACE("BSoundPlayer::_Init: Couldn't GetFreeInputsFor: %s\n",
				strerror(fInitStatus));
			return;
		}
		if (inputCount < 1) {
			TRACE("BSoundPlayer::_Init: Couldn't find a free input\n");
			fInitStatus = B_ERROR;
			return;
		}
	} else {
		_input = *input;
	}

	// find a free media_output
	fInitStatus = roster->GetFreeOutputsFor(fPlayerNode->Node(), &_output, 1,
		&outputCount, B_MEDIA_RAW_AUDIO);
	if (fInitStatus != B_OK) {
		TRACE("BSoundPlayer::_Init: Couldn't GetFreeOutputsFor: %s\n",
			strerror(fInitStatus));
		return;
	}
	if (outputCount < 1) {
		TRACE("BSoundPlayer::_Init: Couldn't find a free output\n");
		fInitStatus = B_ERROR;
		return;
	}

	// Set an appropriate run mode for the producer
	fInitStatus = roster->SetRunModeNode(fPlayerNode->Node(),
		BMediaNode::B_INCREASE_LATENCY);
	if (fInitStatus != B_OK) {
		TRACE("BSoundPlayer::_Init: Couldn't SetRunModeNode: %s\n",
			strerror(fInitStatus));
		return;
	}

	// setup our requested format (can still have many wildcards)
	tryFormat.type = B_MEDIA_RAW_AUDIO;
	tryFormat.u.raw_audio = *format;

#if DEBUG > 0
	char buf[100];
	string_for_format(tryFormat, buf, sizeof(buf));
	TRACE("BSoundPlayer::_Init: trying to connect with format %s\n", buf);
#endif

	// and connect the nodes
	fInitStatus = roster->Connect(_output.source, _input.destination,
		&tryFormat, &fMediaOutput, &fMediaInput);
	if (fInitStatus != B_OK) {
		TRACE("BSoundPlayer::_Init: Couldn't Connect: %s\n",
			strerror(fInitStatus));
		return;
	}

	fFlags |= F_NODES_CONNECTED;

	_GetVolumeSlider();

	TRACE("BSoundPlayer node %" B_PRId32 " has timesource %" B_PRId32 "\n",
		fPlayerNode->Node().node, fPlayerNode->TimeSource()->Node().node);
}


/**
 * @brief Fire the B_SOUND_DONE event for a completed or stopped sound.
 *
 * @param id         The play_id of the sound that finished.
 * @param gotToPlay  true if the sound was stopped manually, false if it ran out.
 */
void
BSoundPlayer::_NotifySoundDone(play_id id, bool gotToPlay)
{
	CALLED();
	Notify(B_SOUND_DONE, id, gotToPlay);
}


/**
 * @brief Locate the gain parameter in the mixer's parameter web and cache it.
 *
 * Searches the fMediaInput node's parameter web for a B_GAIN continuous
 * parameter that corresponds to our destination and stores it in fVolumeSlider.
 */
void
BSoundPlayer::_GetVolumeSlider()
{
	CALLED();

	ASSERT(fVolumeSlider == NULL);

	BMediaRoster *roster = BMediaRoster::CurrentRoster();
	if (!roster) {
		TRACE("BSoundPlayer::_GetVolumeSlider failed to get BMediaRoster");
		return;
	}

	if (!fParameterWeb && roster->GetParameterWebFor(fMediaInput.node, &fParameterWeb) < B_OK) {
		TRACE("BSoundPlayer::_GetVolumeSlider couldn't get parameter web");
		return;
	}

	int count = fParameterWeb->CountParameters();
	for (int i = 0; i < count; i++) {
		BParameter *parameter = fParameterWeb->ParameterAt(i);
		if (parameter->Type() != BParameter::B_CONTINUOUS_PARAMETER)
			continue;
		if ((parameter->ID() >> 16) != fMediaInput.destination.id)
			continue;
		if  (strcmp(parameter->Kind(), B_GAIN) != 0)
			continue;
		fVolumeSlider = (BContinuousParameter *)parameter;
		break;
	}

#if DEBUG >0
	if (!fVolumeSlider) {
		TRACE("BSoundPlayer::_GetVolumeSlider couldn't find volume control");
	}
#endif
}


/**
 * @brief Deliver a sound_player_notification event to the registered notifier.
 *
 * @param what  The notification type (e.g. B_STARTED, B_STOPPED, B_SOUND_DONE).
 */
void
BSoundPlayer::Notify(sound_player_notification what, ...)
{
	CALLED();
	if (fLocker.Lock()) {
		if (fNotifierFunc)
			(*fNotifierFunc)(fCookie, what);
		fLocker.Unlock();
	}
}


/**
 * @brief Invoke the registered buffer-fill callback under the player lock.
 *
 * @param buffer  Audio buffer to fill.
 * @param size    Size of @p buffer in bytes.
 * @param format  Raw audio format of @p buffer.
 */
void
BSoundPlayer::PlayBuffer(void* buffer, size_t size,
	const media_raw_audio_format& format)
{
	if (fLocker.Lock()) {
		if (fPlayBufferFunc)
			(*fPlayBufferFunc)(fCookie, buffer, size, format);
		fLocker.Unlock();
	}
}


// #pragma mark - public sound_error


/**
 * @brief Construct a sound_error with the given description string.
 *
 * @param string  Human-readable description of the error.
 */
sound_error::sound_error(const char* string)
{
	m_str_const = string;
}


/**
 * @brief Return the human-readable error description.
 *
 * @return Pointer to the error string supplied at construction.
 */
const char*
sound_error::what() const throw()
{
	return m_str_const;
}
