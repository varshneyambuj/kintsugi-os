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
 * Copyright 2009-2010, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file SoundPlayer.h
 *  @brief Defines BSoundPlayer for low-latency audio playback via the Media Kit.
 */

#ifndef _SOUND_PLAYER_H
#define _SOUND_PLAYER_H


#include <exception>

#include <BufferProducer.h>
#include <Locker.h>
#include <MediaDefs.h>


class BContinuousParameter;
class BParameterWeb;
class BSound;
namespace BPrivate {
	class SoundPlayNode;
}


/** @brief Exception thrown by BSoundPlayer on unrecoverable initialization errors. */
class sound_error : public std::exception {
			const char*			m_str_const;
public:
	/** @brief Constructs a sound_error with the given message string.
	 *  @param string Human-readable error description.
	 */
								sound_error(const char* string);

	/** @brief Returns the error message string.
	 *  @return Pointer to the error string.
	 */
			const char*			what() const throw();
};


/** @brief Plays audio data through the Media Kit with low latency.
 *
 *  BSoundPlayer connects directly to the audio mixer or a specified node
 *  and delivers audio via a callback function.  It also supports concurrent
 *  playback of multiple BSound objects.
 */
class BSoundPlayer {
public:
	/** @brief Notification codes delivered to the EventNotifierFunc callback. */
	enum sound_player_notification {
		B_STARTED = 1,   /**< The player has started. */
		B_STOPPED,        /**< The player has stopped. */
		B_SOUND_DONE      /**< A BSound has finished playing. */
	};

	/** @brief Callback type for supplying audio data to the player.
	 *
	 *  @param cookie The user-supplied cookie.
	 *  @param buffer Destination buffer to fill with audio data.
	 *  @param size Size of the buffer in bytes.
	 *  @param format The raw audio format of the buffer.
	 */
	typedef void (*BufferPlayerFunc)(void*, void* buffer, size_t size,
		const media_raw_audio_format& format);

	/** @brief Callback type for receiving transport-state notifications.
	 *
	 *  @param cookie The user-supplied cookie.
	 *  @param what One of the sound_player_notification values.
	 */
	typedef void (*EventNotifierFunc)(void*, sound_player_notification what,
		...);

public:
	/** @brief Constructs a BSoundPlayer connected to the default audio mixer.
	 *  @param name Optional node name.
	 *  @param playerFunction Optional buffer-fill callback.
	 *  @param eventNotifierFunction Optional event-notification callback.
	 *  @param cookie Arbitrary pointer passed to both callbacks.
	 */
								BSoundPlayer(const char* name = NULL,
									BufferPlayerFunc playerFunction = NULL,
									EventNotifierFunc
										eventNotifierFunction = NULL,
									void* cookie = NULL);

	/** @brief Constructs a BSoundPlayer with an explicit audio format.
	 *  @param format The raw audio format for playback.
	 *  @param name Optional node name.
	 *  @param playerFunction Optional buffer-fill callback.
	 *  @param eventNotifierFunction Optional event-notification callback.
	 *  @param cookie Arbitrary pointer passed to both callbacks.
	 */
								BSoundPlayer(
									const media_raw_audio_format* format,
									const char* name = NULL,
									BufferPlayerFunc playerFunction = NULL,
									EventNotifierFunc
										eventNotifierFunction = NULL,
									void* cookie = NULL);

	/** @brief Constructs a BSoundPlayer connected to a specific media node.
	 *  @param toNode The target consumer node (e.g. a specific output).
	 *  @param format Optional multi-channel audio format.
	 *  @param name Optional node name.
	 *  @param input Optional specific input on the target node.
	 *  @param playerFunction Optional buffer-fill callback.
	 *  @param eventNotifierFunction Optional event-notification callback.
	 *  @param cookie Arbitrary pointer passed to both callbacks.
	 */
								BSoundPlayer(
									const media_node& toNode,
									const media_multi_audio_format*
										format = NULL,
									const char* name = NULL,
									const media_input* input = NULL,
									BufferPlayerFunc playerFunction = NULL,
									EventNotifierFunc
										eventNotifierFunction = NULL,
									void* cookie = NULL);
	virtual						~BSoundPlayer();

	/** @brief Returns the initialization status.
	 *  @return B_OK if the player is ready, or an error code.
	 */
			status_t			InitCheck();

	/** @brief Returns the current raw audio format used for playback.
	 *  @return The media_raw_audio_format.
	 */
			media_raw_audio_format Format() const;

	/** @brief Starts audio playback.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Start();

	/** @brief Stops audio playback.
	 *  @param block If true, block until playback has actually stopped.
	 *  @param flush If true, flush any pending buffers before stopping.
	 */
			void				Stop(bool block = true, bool flush = true);

	/** @brief Returns the current buffer-fill callback.
	 *  @return The BufferPlayerFunc pointer.
	 */
			BufferPlayerFunc	BufferPlayer() const;

	/** @brief Sets a new buffer-fill callback.
	 *  @param PlayBuffer The new callback function.
	 */
			void				SetBufferPlayer(void (*PlayBuffer)(void*,
									void* buffer, size_t size,
									const media_raw_audio_format& format));

	/** @brief Returns the current event-notification callback.
	 *  @return The EventNotifierFunc pointer.
	 */
			EventNotifierFunc	EventNotifier() const;

	/** @brief Sets a new event-notification callback.
	 *  @param eventNotifierFunction The new callback function.
	 */
			void				SetNotifier(
									EventNotifierFunc eventNotifierFunction);

	/** @brief Returns the user-supplied cookie passed to callbacks.
	 *  @return The cookie pointer.
	 */
			void*				Cookie() const;

	/** @brief Sets the user-supplied cookie passed to callbacks.
	 *  @param cookie The new cookie pointer.
	 */
			void				SetCookie(void* cookie);

	/** @brief Replaces all callbacks and the cookie atomically.
	 *  @param playerFunction New buffer-fill callback; NULL to clear.
	 *  @param eventNotifierFunction New event-notification callback; NULL to clear.
	 *  @param cookie New cookie pointer.
	 */
			void				SetCallbacks(
									BufferPlayerFunc playerFunction = NULL,
									EventNotifierFunc
										eventNotifierFunction = NULL,
									void* cookie = NULL);

	/** @brief Opaque identifier for a concurrent BSound playback instance. */
	typedef int32 play_id;

	/** @brief Returns the current performance time of the player.
	 *  @return Current performance time in microseconds.
	 */
			bigtime_t			CurrentTime();

	/** @brief Returns the estimated performance time for newly sent buffers.
	 *  @return Performance time in microseconds.
	 */
			bigtime_t			PerformanceTime();

	/** @brief Synchronously prerolls the player for immediate-start capability.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Preroll();

	/** @brief Starts playing a BSound object, optionally at a future time.
	 *  @param sound The BSound to play.
	 *  @param atTime Performance time to begin playback; 0 means immediately.
	 *  @return A play_id identifying this playback instance.
	 */
			play_id				StartPlaying(BSound* sound,
									bigtime_t atTime = 0);

	/** @brief Starts playing a BSound at a given time with a specific volume.
	 *  @param sound The BSound to play.
	 *  @param atTime Performance time to begin playback.
	 *  @param withVolume Volume in [0.0, 1.0].
	 *  @return A play_id identifying this playback instance.
	 */
			play_id 			StartPlaying(BSound* sound,
									bigtime_t atTime,
									float withVolume);

	/** @brief Changes the volume of a currently playing BSound.
	 *  @param sound The play_id to adjust.
	 *  @param newVolume New volume in [0.0, 1.0].
	 *  @return B_OK on success, or an error code.
	 */
			status_t 			SetSoundVolume(play_id sound, float newVolume);

	/** @brief Returns true if the given play_id is still active.
	 *  @param id The play_id to query.
	 *  @return True if playing.
	 */
			bool 				IsPlaying(play_id id);

	/** @brief Stops a currently playing BSound immediately.
	 *  @param id The play_id to stop.
	 *  @return B_OK on success, or an error code.
	 */
			status_t 			StopPlaying(play_id id);

	/** @brief Blocks until the given BSound finishes playing.
	 *  @param id The play_id to wait for.
	 *  @return B_OK when done, or an error code.
	 */
			status_t		 	WaitForSound(play_id id);

	/** @brief Returns the current master volume as a normalized value in [0, 1].
	 *  @return Volume in [0.0, 1.0].
	 */
			float 				Volume();

	/** @brief Sets the master volume as a normalized value.
	 *  @param volume New volume in [0.0, 1.0].
	 */
			void 				SetVolume(float volume);

	/** @brief Returns the master volume in decibels, optionally refreshing the cached value.
	 *  @param forcePoll If true, query the mixer rather than using a cached value.
	 *  @return Volume in dB.
	 */
			float				VolumeDB(bool forcePoll = false);

	/** @brief Sets the master volume in decibels.
	 *  @param dB New volume level in dB.
	 */
			void				SetVolumeDB(float dB);

	/** @brief Retrieves information about the volume control parameter.
	 *  @param _node On return, the media node hosting the volume control.
	 *  @param _parameterID On return, the parameter ID of the volume control.
	 *  @param _minDB On return, the minimum volume in dB.
	 *  @param _maxDB On return, the maximum volume in dB.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetVolumeInfo(media_node* _node,
									int32* _parameterID, float* _minDB,
									float* _maxDB);

	/** @brief Returns the total playback latency in microseconds.
	 *  @return Latency in microseconds.
	 */
			bigtime_t			Latency();

	/** @brief Returns true if there is data available to play.
	 *  @return True if data is available.
	 */
	virtual	bool				HasData();

	/** @brief Signals whether data is available (affects B_SOUND_DONE notification timing).
	 *  @param hasData True to indicate data is available.
	 */
			void				SetHasData(bool hasData);

	// TODO: Needs Perform() method for FBC!

protected:

	/** @brief Sets the initialization error code; call from constructor if setup fails.
	 *  @param error The error code to record.
	 */
			void				SetInitError(status_t error);

private:
	static	void				_SoundPlayBufferFunc(void* cookie,
									void* buffer, size_t size,
									const media_raw_audio_format& format);

	// FBC padding
	virtual	status_t			_Reserved_SoundPlayer_0(void*, ...);
	virtual	status_t			_Reserved_SoundPlayer_1(void*, ...);
	virtual	status_t			_Reserved_SoundPlayer_2(void*, ...);
	virtual	status_t			_Reserved_SoundPlayer_3(void*, ...);
	virtual	status_t			_Reserved_SoundPlayer_4(void*, ...);
	virtual	status_t			_Reserved_SoundPlayer_5(void*, ...);
	virtual	status_t			_Reserved_SoundPlayer_6(void*, ...);
	virtual	status_t			_Reserved_SoundPlayer_7(void*, ...);

			void				_Init(const media_node* node,
									const media_multi_audio_format* format,
									const char* name, const media_input* input,
									BufferPlayerFunc playerFunction,
									EventNotifierFunc eventNotifierFunction,
									void* cookie);
			void				_GetVolumeSlider();

			void				_NotifySoundDone(play_id sound, bool gotToPlay);

	// TODO: those two shouldn't be virtual
	virtual	void				Notify(sound_player_notification what, ...);
	virtual	void				PlayBuffer(void* buffer, size_t size,
									const media_raw_audio_format& format);

private:
	friend class BPrivate::SoundPlayNode;

	/** @brief Internal tracking structure for currently playing BSound instances. */
	struct playing_sound {
		playing_sound*	next;
		off_t			current_offset;
		BSound*			sound;
		play_id			id;
		int32			delta;
		int32			rate;
		sem_id			wait_sem;
		float			volume;
	};

	/** @brief Internal tracking structure for BSound instances waiting to start. */
	struct waiting_sound {
		waiting_sound*	next;
		bigtime_t		start_time;
		BSound*			sound;
		play_id			id;
		int32			rate;
		float			volume;
	};

			BPrivate::SoundPlayNode* fPlayerNode;

			playing_sound*		fPlayingSounds;
			waiting_sound*		fWaitingSounds;

			BufferPlayerFunc	fPlayBufferFunc;
			EventNotifierFunc	fNotifierFunc;

			BLocker				fLocker;
			float				fVolumeDB;
			media_input			fMediaInput;
				// Usually the system mixer
			media_output		fMediaOutput;
				// The playback node
			void*				fCookie;
				// Opaque handle passed to hooks
			int32				fFlags;

			status_t			fInitStatus;
			BContinuousParameter* fVolumeSlider;
			bigtime_t			fLastVolumeUpdate;
			BParameterWeb*		fParameterWeb;

			uint32				_reserved[15];
};

#endif // _SOUND_PLAYER_H
