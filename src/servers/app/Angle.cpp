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
 *   Copyright (c) 2001-2002, Haiku, Inc.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the "Software"),
 *   to deal in the Software without restriction, including without limitation
 *   the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *   and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *   DEALINGS IN THE SOFTWARE.
 *
 *   File Name:   Angle.cpp
 *   Author:      DarkWyrm <bpmagic@columbus.rr.com>
 *   Description: Angle class for speeding up trig functions
 */

/** @file Angle.cpp
    @brief Angle class providing fast look-up-table-based trigonometric operations. */

#include "Angle.h"
#include <math.h>

#ifndef ANGLE_PI
	#define ANGLE_PI 3.14159265358979323846
#endif

static bool sTablesInitialized = false;
static float sSinTable[360];
static float sCosTable[360];
static float sTanTable[360];

/** @brief Constructs an Angle with the given value in degrees.
    @param angle Value in degrees. The look-up tables are initialised if needed. */
Angle::Angle(float angle)
	: fAngleValue(angle)
{
	_InitTrigTables();
}

/** @brief Default constructor. Initialises the angle to 0 degrees. */
Angle::Angle()
	: fAngleValue(0)
{
	_InitTrigTables();
}

/** @brief Destructor. */
Angle::~Angle()
{
}

/** @brief Constrains the angle value to the range [0, 360). */
void
Angle::Normalize()
{
	// if the value of the angle is >=360 or <0, make it so that it is
	// within those bounds
    fAngleValue = fmodf(fAngleValue, 360);
    if (fAngleValue < 0)
        fAngleValue += 360;
}

/** @brief Returns the sine of the angle using a pre-computed look-up table.
    @return The sine of the current angle value. */
float
Angle::Sine()
{
	return sSinTable[(int)fAngleValue];
}

/** @brief Computes an Angle whose sine equals the given value via reverse look-up.
    @param value A number in the range [0, 1].
    @return The corresponding angle, or Angle(0) if value is out of range. */
Angle
Angle::InvSine(float value)
{
	// Returns the inverse sine of a value in the range 0 <= value <= 1 via
	//	reverse-lookup any value out of range causes the function to return 0

	// Filter out bad values
	value = fabs(value);

	if (value > 1)
		return Angle(0);

	uint16 i = 90;
	while (value < sSinTable[i])
		i--;

	// current sSinTable[i] is less than value. Pick the degree value which is closer
	// to the passed value
	if ((value - sSinTable[i]) > (sSinTable[i + 1] - value))
		return Angle(i + 1);

	return Angle(i);		// value is closer to previous
}


/** @brief Returns the cosine of the angle using a pre-computed look-up table.
    @return The cosine of the current angle value. */
float
Angle::Cosine(void)
{
	return sCosTable[(int)fAngleValue];
}

/** @brief Computes an Angle whose cosine equals the given value via reverse look-up.
    @param value A number in the range [0, 1].
    @return The corresponding angle, or Angle(0) if value is out of range. */
Angle
Angle::InvCosine(float value)
{
	// Returns the inverse cosine of a value in the range 0 <= value <= 1 via
	//	reverse-lookup any value out of range causes the function to return 0

	// Filter out bad values
	value = fabs(value);

	if (value > 1)
		return 0;

	uint16 i = 90;
	while (value > sCosTable[i])
		i--;

	// current sCosTable[i] is less than value. Pick the degree value which is closer
	// to the passed value
	if ((value - sCosTable[i]) < (sCosTable[i + 1] - value))
		return Angle(i + 1);

	return Angle(i);		// value is closer to previous
}

/** @brief Returns the tangent of the angle using a pre-computed look-up table.
    @param status Optional pointer set to 0 if the tangent is undefined (90 or 270 degrees).
    @return The tangent value, or 0.0 if the angle is 90 or 270 degrees. */
float
Angle::Tangent(int *status)
{
	if (fAngleValue == 90 || fAngleValue == 270) {
		if (status)
			*status = 0;
		return 0.0;
	}

	return sTanTable[(int)fAngleValue];
}

/** @brief Computes an Angle whose tangent equals the given value via reverse look-up.
    @param value A number in the range [0, 1].
    @return The corresponding angle, or Angle(0) if value is out of range. */
Angle
Angle::InvTangent(float value)
{
	// Filter out bad values
	value = fabs(value);

	if (value > 1)
		return Angle(0);

	uint16 i = 90;
	while (value > sTanTable[i])
		i--;

	if ((value - sTanTable[i]) < (sTanTable[i+1] - value))
		return Angle(i+1);

	return Angle(i);		// value is closer to previous
}

/** @brief Returns the quadrant number (1–4) that the angle falls in.
    @return
    - 1: 0 <= angle < 90
    - 2: 90 <= angle < 180
    - 3: 180 <= angle < 270
    - 4: 270 <= angle < 360 */
uint8
Angle::Quadrant()
{
	// We can get away with not doing extra value checks because of the order in
	// which the checks are done.
	if (fAngleValue < 90)
		return 1;

	if (fAngleValue < 180)
		return 2;

	if (fAngleValue < 270)
		return 3;

	return 4;
}

/** @brief Returns the angle constrained to the range [0, 180).
    @return A new Angle in the range [0, 180). */
Angle
Angle::Constrain180()
{
	// Constrains angle to 0 <= angle < 180
	if (fAngleValue < 180)
		return Angle(fAngleValue);

	float value = fmodf(fAngleValue, 180);;
	if (value < 0)
		value += 180;
	return Angle(value);
}

/** @brief Returns the angle constrained to the range [0, 90).
    @return A new Angle in the range [0, 90). */
Angle
Angle::Constrain90()
{
	// Constrains angle to 0 <= angle < 90
	if (fAngleValue < 90)
		return Angle(fAngleValue);

	float value = fmodf(fAngleValue, 90);;
	if (value < 0)
		value += 90;
	return Angle(value);
}

/** @brief Sets the angle to the given value and normalises it into [0, 360).
    @param angle Value in degrees. */
void
Angle::SetValue(float angle)
{
	fAngleValue = angle;
	Normalize();
}


/** @brief Returns the current angle value in degrees without normalisation.
    @return The angle in degrees. */
float
Angle::Value() const
{
	return fAngleValue;
}

/** @brief Initialises the global sine, cosine, and tangent look-up tables if not yet done. */
void
Angle::_InitTrigTables()
{
	if (sTablesInitialized)
		return;
	sTablesInitialized = true;

	for(int32 i = 0; i < 90; i++) {
		double currentRadian = (i * ANGLE_PI) / 180.0;

		// Get these so that we can do some superfast assignments
		double sinValue = sin(currentRadian);
		double cosValue = cos(currentRadian);

		// Do 4 assignments, taking advantage of sin/cos symmetry
		sSinTable[i] = sinValue;
		sSinTable[i + 90] = cosValue;
		sSinTable[i + 180] = sinValue * -1;
		sSinTable[i + 270] = cosValue * -1;

		sCosTable[i] = cosValue;
		sCosTable[i + 90] = sinValue * -1;
		sCosTable[i + 180] = cosValue * -1;
		sCosTable[i + 270] = sinValue;

		double tanValue = sinValue / cosValue;

		sTanTable[i] = tanValue;
		sTanTable[i + 90] = tanValue;
		sTanTable[i + 180] = tanValue;
		sTanTable[i + 270] = tanValue;
	}
}


/** @brief Assignment operator from another Angle.
    @param from The source Angle.
    @return Reference to this Angle. */
Angle&
Angle::operator=(const Angle &from)
{
	fAngleValue = from.fAngleValue;
	return *this;
}


/** @brief Assignment operator from a float value in degrees.
    @param from The float value to assign.
    @return Reference to this Angle. */
Angle&
Angle::operator=(const float &from)
{
	fAngleValue = from;
	return *this;
}


/** @brief Assignment operator from a long integer value in degrees.
    @param from The long value to assign.
    @return Reference to this Angle. */
Angle&
Angle::operator=(const long &from)
{
	fAngleValue = (float)from;
	return *this;
}


/** @brief Assignment operator from an int value in degrees.
    @param from The integer value to assign.
    @return Reference to this Angle. */
Angle&
Angle::operator=(const int &from)
{
	fAngleValue = (float)from;
	return *this;
}


/** @brief Equality comparison operator.
    @param from The Angle to compare against.
    @return true if both angles have exactly the same value. */
bool
Angle::operator==(const Angle &from)
{
	return (fAngleValue == from.fAngleValue);
}


/** @brief Inequality comparison operator.
    @param from The Angle to compare against.
    @return true if the angles have different values. */
bool
Angle::operator!=(const Angle &from)
{
	return (fAngleValue != from.fAngleValue);
}


/** @brief Greater-than comparison operator.
    @param from The Angle to compare against.
    @return true if this angle is greater than from. */
bool
Angle::operator>(const Angle &from)
{
	return (fAngleValue > from.fAngleValue);
}


/** @brief Less-than comparison operator.
    @param from The Angle to compare against.
    @return true if this angle is less than from. */
bool
Angle::operator<(const Angle &from)
{
	return (fAngleValue < from.fAngleValue);
}


/** @brief Greater-than-or-equal comparison operator.
    @param from The Angle to compare against.
    @return true if this angle is greater than or equal to from. */
bool
Angle::operator>=(const Angle &from)
{
	return (fAngleValue >= from.fAngleValue);
}


/** @brief Less-than-or-equal comparison operator.
    @param from The Angle to compare against.
    @return true if this angle is less than or equal to from. */
bool
Angle::operator<=(const Angle &from)
{
	return (fAngleValue <= from.fAngleValue);
}
