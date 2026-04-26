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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Utility.cpp
 * @brief Small numeric helpers used by the Screen preferences app.
 */


#include "Utility.h"
#include <math.h>


/**
 * @brief Round @a n to @a max decimal places.
 *
 * Multiplies @a n by 10^@a max, rounds to the nearest integer using a +0.5
 * bias, then divides back. Used to clamp displayed refresh rates to a
 * tidy fractional precision.
 *
 * @param n    Value to round.
 * @param max  Number of decimal digits to preserve.
 * @return     The rounded value.
 */
float round(float n, int32 max)
{
	max = (int32)pow(10, (float)max);

	n *= max;
	n += 0.5;

	int32 tmp = (int32)floor(n);
	return (float)tmp / (max);
}
