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
 * Copyright 2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file TimeCode.h
 *  @brief Defines timecode types and conversion utilities for SMPTE-style time codes.
 */

#ifndef _TIME_CODE_H
#define _TIME_CODE_H


#include <SupportDefs.h>


/** @brief Identifies the frame-rate convention used by a time code. */
// Time code is always in the form HH:MM:SS:FF, it's the definition of "FF"
// that varies
enum timecode_type {
	B_TIMECODE_DEFAULT,
	B_TIMECODE_100,
	B_TIMECODE_75,			/* CD */
	B_TIMECODE_30,			/* MIDI */
	B_TIMECODE_30_DROP_2,	/* NTSC */
	B_TIMECODE_30_DROP_4,	/* Brazil */
	B_TIMECODE_25,			/* PAL */
	B_TIMECODE_24,			/* Film */
	B_TIMECODE_18			/* Super8 */
};


/** @brief Describes the parameters of a specific time code type. */
struct timecode_info {
	timecode_type	type;        /**< The time code type. */
	int				drop_frames; /**< Number of frames dropped per drop-frame interval. */
	int				every_nth;   /**< Interval at which frames are dropped. */
	int				except_nth;  /**< Exception interval within every_nth. */
	int				fps_div;     /**< Frames-per-second divisor. */
	char			name[32];    /**< Human-readable name for display (e.g. "30 drop"). */
	char			format[32];  /**< sprintf format string for HH:MM:SS:FF display. */

	char			_reserved_[64];
};


/** @brief Converts a microsecond time value to HH:MM:SS:FF components.
 *  @param micros The time in microseconds.
 *  @param hours On return, hours component.
 *  @param minutes On return, minutes component.
 *  @param seconds On return, seconds component.
 *  @param frames On return, frames component.
 *  @param code Time code parameters; NULL uses the default type.
 *  @return B_OK on success, or an error code.
 */
status_t us_to_timecode(bigtime_t micros, int* hours, int* minutes,
	int* seconds, int* frames, const timecode_info* code = NULL);

/** @brief Converts HH:MM:SS:FF components to a microsecond time value.
 *  @param hours Hours component.
 *  @param minutes Minutes component.
 *  @param seconds Seconds component.
 *  @param frames Frames component.
 *  @param micros On return, the equivalent time in microseconds.
 *  @param code Time code parameters; NULL uses the default type.
 *  @return B_OK on success, or an error code.
 */
status_t timecode_to_us(int hours, int minutes, int seconds, int frames,
	bigtime_t* micros, const timecode_info* code = NULL);

/** @brief Converts a linear frame count to HH:MM:SS:FF components.
 *  @param l_frames Linear frame count since the time code epoch.
 *  @param hours On return, hours component.
 *  @param minutes On return, minutes component.
 *  @param seconds On return, seconds component.
 *  @param frames On return, frames component.
 *  @param code Time code parameters; NULL uses the default type.
 *  @return B_OK on success, or an error code.
 */
status_t frames_to_timecode(int32 l_frames, int* hours, int* minutes,
	int* seconds, int* frames, const timecode_info* code = NULL);

/** @brief Converts HH:MM:SS:FF components to a linear frame count.
 *  @param hours Hours component.
 *  @param minutes Minutes component.
 *  @param seconds Seconds component.
 *  @param frames Frames component.
 *  @param lFrames On return, the linear frame count.
 *  @param code Time code parameters; NULL uses the default type.
 *  @return B_OK on success, or an error code.
 */
status_t timecode_to_frames(int hours, int minutes, int seconds, int frames,
	int32* lFrames, const timecode_info* code = NULL);

/** @brief Fills a timecode_info structure for the given time code type.
 *  @param type The time code type to look up.
 *  @param _timecode On return, the timecode_info for that type.
 *  @return B_OK on success, or an error code.
 */
status_t get_timecode_description(timecode_type type,
	timecode_info* _timecode);

/** @brief Returns the total number of supported time code types.
 *  @return Number of time code types.
 */
status_t count_timecodes();


/** @brief An object-oriented wrapper around a SMPTE-style time code value.
 *
 *  BTimeCode stores a time position as HH:MM:SS:FF along with the active
 *  timecode_type and provides arithmetic, comparison, and conversion operations.
 */
class BTimeCode {
public:
	/** @brief Default constructor; initializes to 00:00:00:00 with B_TIMECODE_DEFAULT. */
								BTimeCode();

	/** @brief Constructs from a microsecond time value.
	 *  @param microSeconds Time in microseconds.
	 *  @param type The time code convention to use.
	 */
								BTimeCode(bigtime_t microSeconds,
									timecode_type type = B_TIMECODE_DEFAULT);

	/** @brief Copy constructor.
	 *  @param other The BTimeCode to copy.
	 */
								BTimeCode(const BTimeCode& other);

	/** @brief Constructs from explicit HH:MM:SS:FF components.
	 *  @param hours Hours.
	 *  @param minutes Minutes.
	 *  @param seconds Seconds.
	 *  @param frames Frames.
	 *  @param type The time code convention to use.
	 */
								BTimeCode(int hours, int minutes, int seconds,
									int frames,
									timecode_type type = B_TIMECODE_DEFAULT);
								~BTimeCode();

	/** @brief Sets all four time components without changing the type.
	 *  @param hours Hours.
	 *  @param minutes Minutes.
	 *  @param seconds Seconds.
	 *  @param frames Frames.
	 */
			void				SetData(int hours, int minutes, int seconds,
									int frames);

	/** @brief Changes the active time code type and recomputes the components.
	 *  @param type The new timecode_type.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetType(timecode_type type);

	/** @brief Sets the time from a microsecond value.
	 *  @param microSeconds The new time in microseconds.
	 */
			void				SetMicroseconds(bigtime_t microSeconds);

	/** @brief Sets the time from a linear frame count.
	 *  @param linearFrames Linear frame count.
	 */
			void				SetLinearFrames(int32 linearFrames);

	/** @brief Assigns another BTimeCode to this object.
	 *  @param other The source BTimeCode.
	 *  @return Reference to this object.
	 */
			BTimeCode&			operator=(const BTimeCode& other);

	/** @brief Returns true if this time code equals another.
	 *  @param other The BTimeCode to compare with.
	 *  @return True if equal.
	 */
			bool				operator==(const BTimeCode& other) const;

	/** @brief Returns true if this time code is less than another.
	 *  @param other The BTimeCode to compare with.
	 *  @return True if less than.
	 */
			bool				operator<(const BTimeCode& other) const;

	/** @brief Adds another time code to this one in place.
	 *  @param other The BTimeCode to add.
	 *  @return Reference to this object.
	 */
			BTimeCode&			operator+=(const BTimeCode& other);

	/** @brief Subtracts another time code from this one in place.
	 *  @param other The BTimeCode to subtract.
	 *  @return Reference to this object.
	 */
			BTimeCode&			operator-=(const BTimeCode& other);

	/** @brief Returns the sum of this time code and another.
	 *  @param other The BTimeCode to add.
	 *  @return The resulting BTimeCode.
	 */
			BTimeCode			operator+(const BTimeCode& other) const;

	/** @brief Returns the difference between this time code and another.
	 *  @param other The BTimeCode to subtract.
	 *  @return The resulting BTimeCode.
	 */
			BTimeCode			operator-(const BTimeCode& other) const;

	/** @brief Returns the hours component.
	 *  @return Hours value.
	 */
			int					Hours() const;

	/** @brief Returns the minutes component.
	 *  @return Minutes value.
	 */
			int					Minutes() const;

	/** @brief Returns the seconds component.
	 *  @return Seconds value.
	 */
			int					Seconds() const;

	/** @brief Returns the frames component.
	 *  @return Frames value.
	 */
			int					Frames() const;

	/** @brief Returns the active time code type.
	 *  @return The timecode_type.
	 */
			timecode_type		Type() const;

	/** @brief Retrieves all components and optionally the type.
	 *  @param _hours On return, hours.
	 *  @param _minutes On return, minutes.
	 *  @param _seconds On return, seconds.
	 *  @param _frames On return, frames.
	 *  @param _type If non-NULL, receives the timecode_type.
	 */
			void				GetData(int* _hours, int* _minutes,
									int* _seconds, int* _frames,
									timecode_type* _type = NULL) const;

	/** @brief Returns the time value in microseconds.
	 *  @return Time in microseconds.
	 */
			bigtime_t			Microseconds() const;

	/** @brief Returns the time as a linear frame count.
	 *  @return Linear frame count.
	 */
			int32				LinearFrames() const;

	/** @brief Formats the time code as a HH:MM:SS:FF string.
	 *  @param string Destination buffer; must be at least 24 bytes.
	 */
			// Make sure the passed buffer is at least 24 bytes large.
			void				GetString(char* string) const;

private:
			int					fHours;
			int					fMinutes;
			int					fSeconds;
			int					fFrames;
			timecode_info		fInfo;
};

#endif // _TIME_CODE_H
