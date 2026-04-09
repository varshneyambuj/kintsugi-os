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
 *   AUTHOR: David McPaul
 *     FILE: TimeCode.cpp
 *    DESCR: Handles all TimeCode functions.
 */

/** @file TimeCode.cpp
 *  @brief Implements timecode conversion utilities and the BTimeCode class
 *         for representing SMPTE-style timecodes across multiple frame rates.
 */

#include <TimeCode.h>
#include "MediaDebug.h"
#include <string.h>

/**
 * @brief Convert microseconds to a timecode expressed as hours/minutes/seconds/frames.
 *
 * @param micros    Input time in microseconds.
 * @param hours     Receives the hours component.
 * @param minutes   Receives the minutes component.
 * @param seconds   Receives the seconds component.
 * @param frames    Receives the frames component.
 * @param code      Optional timecode descriptor; NULL defaults to NTSC 29.97.
 * @return B_OK on success.
 */
status_t us_to_timecode(bigtime_t micros, int * hours, int * minutes, int * seconds, int * frames, const timecode_info * code)
{
	int32 l_frames;

	CALLED();

	if (code) {
		// We have a valid timecode_info
		switch (code->type) {
			case	B_TIMECODE_DEFAULT:		// NTSC
			case	B_TIMECODE_30_DROP_2:	// NTSC
				l_frames = int32((((micros % 1000000) * 29.97) / 1000000) + (micros / 1000000 * 29.97));
				break;
			case	B_TIMECODE_30_DROP_4:	// Brazil
				l_frames = int32((((micros % 1000000) * 29.95) / 1000000) + (micros / 1000000 * 29.95));
				break;
			default:
				l_frames = (((micros % 1000000) * code->fps_div) / 1000000) + (micros / 1000000 * code->fps_div);
				break;
		};
	} else {
		// Convert us to frames
		l_frames = int32((((micros % 1000000) * 29.97) / 1000000) + (micros / 1000000 * 29.97));
	}

	return frames_to_timecode(l_frames, hours, minutes, seconds, frames, code);
}

/**
 * @brief Convert a timecode (hours/minutes/seconds/frames) to microseconds.
 *
 * @param hours    Hours component.
 * @param minutes  Minutes component.
 * @param seconds  Seconds component.
 * @param frames   Frames component.
 * @param micros   Receives the equivalent time in microseconds.
 * @param code     Optional timecode descriptor; NULL defaults to NTSC 29.97.
 * @return B_OK on success, B_ERROR if the intermediate frame conversion fails.
 */
status_t timecode_to_us(int hours, int minutes, int seconds, int frames, bigtime_t * micros, const timecode_info * code)
{
	int32 l_frames;

	CALLED();

	if (timecode_to_frames(hours, minutes, seconds, frames, &l_frames, code) == B_OK) {

		if (code) {
			// We have a valid timecode_info
			switch (code->type) {
				case	B_TIMECODE_DEFAULT:		// NTSC
				case	B_TIMECODE_30_DROP_2:	// NTSC
					*micros = bigtime_t(l_frames * ((1000000 / 29.97) + 0.5));
					break;
				case	B_TIMECODE_30_DROP_4:	// Brazil
					*micros = bigtime_t(l_frames * ((1000000 / 29.95) + 0.5));
					break;
				default:
					*micros = l_frames * 1000000 / code->fps_div;
					break;
			};

		} else {
			*micros = bigtime_t(l_frames * ((1000000 / 29.97) + 0.5));
		}

		return B_OK;
	}
	return B_ERROR;
}

/**
 * @brief Convert a linear frame count to a timecode (hours/minutes/seconds/frames).
 *
 * Handles drop-frame adjustments as specified by the timecode_info descriptor.
 *
 * @param l_frames  Linear frame count to convert.
 * @param hours     Receives the hours component.
 * @param minutes   Receives the minutes component.
 * @param seconds   Receives the seconds component.
 * @param frames    Receives the frames component.
 * @param code      Optional timecode descriptor; NULL defaults to NTSC drop-frame.
 * @return B_OK always.
 */
status_t frames_to_timecode(int32 l_frames, int * hours, int * minutes, int * seconds, int * frames, const timecode_info * code)
{
	int fps_div;
	int total_mins;
	int32 extra_frames;
	int32 extra_frames2;

	CALLED();

	if (code) {
		// We have a valid timecode_info so use it
		fps_div = code->fps_div;

		if (code->every_nth > 0) {
			// Handle Drop Frames format

			total_mins = l_frames / fps_div / 60;

			// "every_nth" minute we gain "drop_frames" "except_nth" minute
			extra_frames = code->drop_frames*(total_mins/code->every_nth) - code->drop_frames*(total_mins/code->except_nth);
			l_frames += extra_frames;

			total_mins = l_frames / fps_div / 60;
			extra_frames2 = code->drop_frames*(total_mins/code->every_nth) - code->drop_frames*(total_mins/code->except_nth);

			// Gaining frames may mean that we gain more frames so we keep adjusting until no more to adjust
			while (extra_frames != extra_frames2) {
				l_frames += extra_frames2 - extra_frames;
				extra_frames = extra_frames2;

				total_mins = l_frames / fps_div / 60;
				extra_frames2 = code->drop_frames*(total_mins/code->every_nth) - code->drop_frames*(total_mins/code->except_nth);
			}

			// l_frames should now include all adjusted frames
		}
	} else {
		// no timecode_info so set default NTSC :-(
		fps_div = 30;		// NTSC Default
		total_mins = l_frames / fps_div / 60;

		// "every_nth" minute we gain "drop_frames" "except_nth" minute
		extra_frames = 2*(total_mins) - 2*(total_mins/10);
		l_frames += extra_frames;

		total_mins = l_frames / fps_div / 60;
		extra_frames2 = 2*(total_mins) - 2*(total_mins/10);

		// Gaining frames may mean that we gain more frames so we keep adjusting until no more to adjust
		while (extra_frames != extra_frames2) {
			l_frames += extra_frames2 - extra_frames;
			extra_frames = extra_frames2;

			total_mins = l_frames / fps_div / 60;
			extra_frames2 = 2*(total_mins) - 2*(total_mins/10);
		}
	}

	// convert frames to seconds leaving the remainder as frames
	*seconds = l_frames / fps_div;		// DIV
	*frames = l_frames % fps_div;		// MOD

	// Normalise to Hours Minutes Seconds Frames
	*minutes = *seconds / 60;
	*seconds = *seconds % 60;

	*hours = *minutes / 60;
	*minutes = *minutes % 60;

	return B_OK;
}

/**
 * @brief Convert a timecode (hours/minutes/seconds/frames) to a linear frame count.
 *
 * Applies drop-frame correction as specified by the timecode_info descriptor.
 *
 * @param hours     Hours component.
 * @param minutes   Minutes component.
 * @param seconds   Seconds component.
 * @param frames    Frames component.
 * @param l_frames  Receives the linear frame count.
 * @param code      Optional timecode descriptor; NULL defaults to NTSC drop-frame.
 * @return B_OK always.
 */
status_t timecode_to_frames(int hours, int minutes, int seconds, int frames, int32 * l_frames, const timecode_info * code)
{
	int fps_div;
	int total_mins;
	int32 extra_frames;

	CALLED();

	if (code) {
		// We have a valid timecode_info
		fps_div = code->fps_div;

		total_mins = (hours * 60) + minutes;
		*l_frames = (total_mins * 60) + seconds;
		*l_frames = (*l_frames * fps_div) + frames;

		// Adjust "every_nth" minute we lose "drop_frames" "except_nth" minute
		if (code->every_nth > 0) {
			extra_frames = code->drop_frames*(total_mins/code->every_nth) - code->drop_frames*(total_mins/code->except_nth);

			*l_frames = *l_frames - extra_frames;
		}
	} else {

		total_mins = (hours * 60) + minutes;
		*l_frames = (total_mins * 60) + seconds;
		*l_frames = (*l_frames * 30) + frames;

		// Adjust "every_nth" minute we lose "drop_frames" "except_nth" minute
		extra_frames = 2*(total_mins) - 2*(total_mins/10);

		*l_frames = *l_frames - extra_frames;
	}

	return B_OK;
}

/**
 * @brief Populate a timecode_info structure for the specified timecode type.
 *
 * Sets the name, fps_div, drop_frames, every_nth, and except_nth fields
 * appropriate for the given type.
 *
 * @param type          The timecode type (e.g. B_TIMECODE_30_DROP_2 for NTSC).
 * @param out_timecode  Receives the filled-in timecode_info structure.
 * @return B_OK always.
 */
status_t get_timecode_description(timecode_type type, timecode_info * out_timecode)
{
	CALLED();

	out_timecode->type = type;
	strncpy(out_timecode->format,"%.2ih:%.2im:%.2is.%.2i",31);
	out_timecode->every_nth = 0;
	out_timecode->except_nth = 0;

	switch (type) {
		case	B_TIMECODE_100:
				strncpy(out_timecode->name,"100 FPS",31);
				out_timecode->fps_div = 100;
				break;
		case	B_TIMECODE_75:			// CD
				strncpy(out_timecode->name,"CD",31);
				out_timecode->fps_div = 75;
				break;
		case	B_TIMECODE_30:			// MIDI
				strncpy(out_timecode->name,"MIDI",31);
				out_timecode->fps_div = 30;
				break;
		case	B_TIMECODE_30_DROP_2:	// NTSC
				strncpy(out_timecode->name,"NTSC",31);
				out_timecode->fps_div = 30;	// This is supposed to be 29.97fps but can be simulated using the drop frame format below
				out_timecode->drop_frames = 2; // Drop 2 frames
				out_timecode->every_nth = 1;	// every 1 minutes
				out_timecode->except_nth = 10;	// except every 10 minutes
				break;
		case	B_TIMECODE_30_DROP_4:	// Brazil
				strncpy(out_timecode->name,"NTSC Brazil",31);
				out_timecode->fps_div = 30;
				out_timecode->drop_frames = 4; // Drop 4 frames
				out_timecode->every_nth = 1;	// every 1 minutes
				out_timecode->except_nth = 10;	// except every 10 minutes
				break;
		case	B_TIMECODE_25:			// PAL
				strncpy(out_timecode->name,"PAL",31);
				out_timecode->fps_div = 25;
				break;
		case	B_TIMECODE_24:			// Film
				strncpy(out_timecode->name,"FILM",31);
				out_timecode->fps_div = 24;
				break;
		case	B_TIMECODE_18:			// Super8
				strncpy(out_timecode->name,"Super 8",31);
				out_timecode->fps_div = 18;
				break;
		default:
				strncpy(out_timecode->name,"NTSC",31);
				out_timecode->fps_div = 30;	// This is supposed to be 29.97fps but can be simulated using the drop frame format below
				out_timecode->drop_frames = 2; // Drop 2 frames
				out_timecode->every_nth = 1;	// every 1 minutes
				out_timecode->except_nth = 10;	// except every 10 minutes
				break;
	}

	return B_OK;
}

/**
 * @brief Return the number of supported timecode types.
 *
 * @return The count of timecode types (currently 8).
 */
status_t count_timecodes()
{
	CALLED();
	return 8;	// Is this right?
}

/*************************************************************
 * public BTimeCode
 *************************************************************/


/**
 * @brief Default constructor. Does not initialise timecode data.
 */
BTimeCode::BTimeCode()
{
	CALLED();
}


/**
 * @brief Construct a BTimeCode from a microsecond value and a timecode type.
 *
 * @param us    Time in microseconds.
 * @param type  Timecode type (e.g. B_TIMECODE_30_DROP_2 for NTSC).
 */
BTimeCode::BTimeCode(bigtime_t us,
					 timecode_type type)
{
	CALLED();
	if (SetType(type) == B_OK) {
		SetMicroseconds(us);
	}
}


/**
 * @brief Copy constructor.
 *
 * @param clone  The BTimeCode object to copy.
 */
BTimeCode::BTimeCode(const BTimeCode &clone)
{
	CALLED();
	if (SetType(clone.Type()) == B_OK) {
		SetData(clone.Hours(),clone.Minutes(),clone.Seconds(),clone.Frames());
	}
}


/**
 * @brief Construct a BTimeCode from explicit H:M:S:F components.
 *
 * @param hours    Hours component.
 * @param minutes  Minutes component.
 * @param seconds  Seconds component.
 * @param frames   Frames component.
 * @param type     Timecode type.
 */
BTimeCode::BTimeCode(int hours,
					 int minutes,
					 int seconds,
					 int frames,
					 timecode_type type)
{
	CALLED();
	if (SetType(type) == B_OK) {
		SetData(hours,minutes,seconds,frames);
	}
}


/**
 * @brief Destructor.
 */
BTimeCode::~BTimeCode()
{
	CALLED();
}


/**
 * @brief Set all timecode components at once.
 *
 * @param hours    New hours value.
 * @param minutes  New minutes value.
 * @param seconds  New seconds value.
 * @param frames   New frames value.
 */
void
BTimeCode::SetData(int hours,
				   int minutes,
				   int seconds,
				   int frames)
{
	CALLED();
	fHours = hours;
	fMinutes = minutes;
	fSeconds = seconds;
	fFrames = frames;
}


/**
 * @brief Change the timecode type and update the internal descriptor.
 *
 * @param type  New timecode type.
 * @return B_OK on success, or an error from get_timecode_description().
 */
status_t
BTimeCode::SetType(timecode_type type)
{
	CALLED();

	return get_timecode_description(type, &fInfo);
}


/**
 * @brief Set the timecode value from a microsecond timestamp.
 *
 * @param us  Time in microseconds.
 */
void
BTimeCode::SetMicroseconds(bigtime_t us)
{
	CALLED();

	us_to_timecode(us, &fHours, &fMinutes, &fSeconds, &fFrames, &fInfo);
}


/**
 * @brief Set the timecode value from a linear frame count.
 *
 * @param linear_frames  Linear frame count.
 */
void
BTimeCode::SetLinearFrames(int32 linear_frames)
{
	CALLED();

	frames_to_timecode(linear_frames, &fHours, &fMinutes, &fSeconds, &fFrames, &fInfo);
}


/**
 * @brief Assignment operator.
 *
 * @param clone  The BTimeCode to copy from.
 * @return Reference to this object.
 */
BTimeCode &
BTimeCode::operator=(const BTimeCode &clone)
{
	CALLED();
	fHours = clone.Hours();
	fMinutes = clone.Minutes();
	fSeconds = clone.Seconds();
	fFrames = clone.Frames();
	SetType(clone.Type());

	return *this;
}


/**
 * @brief Equality comparison by linear frame count.
 *
 * @param other  The BTimeCode to compare to.
 * @return true if both timecodes represent the same linear frame.
 */
bool
BTimeCode::operator==(const BTimeCode &other) const
{
	CALLED();

	return ((this->LinearFrames()) == (other.LinearFrames()));
}


/**
 * @brief Less-than comparison by linear frame count.
 *
 * @param other  The BTimeCode to compare to.
 * @return true if this timecode is earlier than @p other.
 */
bool
BTimeCode::operator<(const BTimeCode &other) const
{
	CALLED();

	return ((this->LinearFrames()) < (other.LinearFrames()));
}


/**
 * @brief In-place addition of another timecode's linear frame count.
 *
 * @param other  Timecode whose frame count is added to this one.
 * @return Reference to this object.
 */
BTimeCode &
BTimeCode::operator+=(const BTimeCode &other)
{
	CALLED();

	this->SetLinearFrames(this->LinearFrames() + other.LinearFrames());

	return *this;
}


/**
 * @brief In-place subtraction of another timecode's linear frame count.
 *
 * @param other  Timecode whose frame count is subtracted from this one.
 * @return Reference to this object.
 */
BTimeCode &
BTimeCode::operator-=(const BTimeCode &other)
{
	CALLED();

	this->SetLinearFrames(this->LinearFrames() - other.LinearFrames());

	return *this;
}


/**
 * @brief Addition operator returning a new BTimeCode.
 *
 * @param other  Timecode to add.
 * @return New BTimeCode representing the sum.
 */
BTimeCode
BTimeCode::operator+(const BTimeCode &other) const
{
	CALLED();
	BTimeCode tc(*this);
	tc += other;
	return tc;
}


/**
 * @brief Subtraction operator returning a new BTimeCode.
 *
 * @param other  Timecode to subtract.
 * @return New BTimeCode representing the difference.
 */
BTimeCode
BTimeCode::operator-(const BTimeCode &other) const
{
	CALLED();
	BTimeCode tc(*this);
	tc -= other;
	return tc;
}


/**
 * @brief Return the hours component.
 *
 * @return Hours.
 */
int
BTimeCode::Hours() const
{
	CALLED();

	return fHours;
}


/**
 * @brief Return the minutes component.
 *
 * @return Minutes.
 */
int
BTimeCode::Minutes() const
{
	CALLED();

	return fMinutes;
}


/**
 * @brief Return the seconds component.
 *
 * @return Seconds.
 */
int
BTimeCode::Seconds() const
{
	CALLED();

	return fSeconds;
}


/**
 * @brief Return the frames component.
 *
 * @return Frames.
 */
int
BTimeCode::Frames() const
{
	CALLED();

	return fFrames;
}


/**
 * @brief Return the timecode type.
 *
 * @return The timecode_type value stored in the internal descriptor.
 */
timecode_type
BTimeCode::Type() const
{
	CALLED();

	return fInfo.type;
}


/**
 * @brief Retrieve all timecode components and the type in one call.
 *
 * @param out_hours    Receives the hours component.
 * @param out_minutes  Receives the minutes component.
 * @param out_seconds  Receives the seconds component.
 * @param out_frames   Receives the frames component.
 * @param out_type     Receives the timecode type.
 */
void
BTimeCode::GetData(int *out_hours,
				   int *out_minutes,
				   int *out_seconds,
				   int *out_frames,
				   timecode_type *out_type) const
{
	CALLED();
	*out_hours = fHours;
	*out_minutes = fMinutes;
	*out_seconds = fSeconds;
	*out_frames = fFrames;
	*out_type = fInfo.type;
}


/**
 * @brief Convert this timecode to microseconds.
 *
 * @return Equivalent time in microseconds, or 0 on error.
 */
bigtime_t
BTimeCode::Microseconds() const
{
	bigtime_t us;

	CALLED();

	if (timecode_to_us(fHours,fMinutes,fSeconds,fFrames, &us, &fInfo) == B_OK) {
		return us;
	}

	return 0;
}


/**
 * @brief Convert this timecode to a linear frame count.
 *
 * @return Linear frame count, or 0 on error.
 */
int32
BTimeCode::LinearFrames() const
{
	int32 linear_frames;

	CALLED();

	if (timecode_to_frames(fHours,fMinutes,fSeconds,fFrames,&linear_frames,&fInfo) == B_OK) {
		return linear_frames;
	}

	return 0;
}


/**
 * @brief Format this timecode into a string using the type's format template.
 *
 * @param str  Destination buffer (must be large enough for the formatted result).
 */
void
BTimeCode::GetString(char *str) const
{
	CALLED();
	sprintf(str,fInfo.format, fHours, fMinutes, fSeconds, fFrames);
}
