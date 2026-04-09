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
 * Incorporates work originally licensed under the MIT License.
 * Copyright (c) 2001-2002, Haiku, Inc.
 * Original author: DarkWyrm <bpmagic@columbus.rr.com>
 */

/** @file Angle.h
    @brief Angle class that accelerates trigonometric computations using lookup tables. */

#ifndef _ANGLE_H_
#define _ANGLE_H_

#include <GraphicsDefs.h>

/** @brief Represents an angle in degrees and provides fast trig operations via lookup tables. */
class Angle {
public:
						Angle(float angle);
						Angle();
virtual					~Angle();

		/** @brief Normalises the angle value to the range [0, 360). */
		void			Normalize();

		/** @brief Returns the sine of this angle.
		    @return Sine value in [-1.0, 1.0]. */
		float			Sine(void);

		/** @brief Returns the angle whose sine equals the given value.
		    @param value Sine value in [-1.0, 1.0].
		    @return Angle in degrees. */
		Angle			InvSine(float value);

		/** @brief Returns the cosine of this angle.
		    @return Cosine value in [-1.0, 1.0]. */
		float			Cosine(void);

		/** @brief Returns the angle whose cosine equals the given value.
		    @param value Cosine value in [-1.0, 1.0].
		    @return Angle in degrees. */
		Angle			InvCosine(float value);

		/** @brief Returns the tangent of this angle.
		    @param status Optional pointer set to a non-zero value if the
		                  result is undefined (e.g. at 90 or 270 degrees).
		    @return Tangent value. */
		float			Tangent(int *status=NULL);

		/** @brief Returns the angle whose tangent equals the given value.
		    @param value The tangent value.
		    @return Angle in degrees. */
		Angle			InvTangent(float value);

		/** @brief Returns which trigonometric quadrant (1-4) this angle falls in.
		    @return Quadrant number in [1, 4]. */
		uint8			Quadrant(void);

		/** @brief Returns a copy of this angle constrained to [-180, 180].
		    @return Constrained angle. */
		Angle			Constrain180(void);

		/** @brief Returns a copy of this angle constrained to [-90, 90].
		    @return Constrained angle. */
		Angle			Constrain90(void);

		/** @brief Sets the angle value in degrees.
		    @param angle New angle value. */
		void			SetValue(float angle);

		/** @brief Returns the current angle value in degrees.
		    @return Angle in degrees. */
		float			Value(void) const;

		Angle			&operator=(const Angle &from);
		Angle			&operator=(const float &from);
		Angle			&operator=(const long &from);
		Angle			&operator=(const int &from);

		bool			operator==(const Angle &from);
		bool			operator!=(const Angle &from);
		bool			operator<(const Angle &from);
		bool			operator>(const Angle &from);
		bool			operator>=(const Angle &from);
		bool			operator<=(const Angle &from);

protected:
		/** @brief Populates the internal sine/cosine lookup tables on first use. */
		void			_InitTrigTables(void);

		float			fAngleValue;
};

#endif
