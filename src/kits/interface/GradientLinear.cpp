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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2008, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Artur Wyszynski <harakash@gmail.com>
 */


/**
 * @file GradientLinear.cpp
 * @brief Implementation of BGradientLinear, a linear gradient fill
 *
 * BGradientLinear defines a gradient that transitions along a straight line from
 * a start point to an end point. Used as a fill pattern in BView drawing operations.
 *
 * @see BGradient, BView
 */


#include <Point.h>
#include <Gradient.h>
#include <GradientLinear.h>


/**
 * @brief Construct a default linear gradient from the origin to the origin.
 *
 * Initializes both start and end points to (0, 0) and sets the gradient type
 * to TYPE_LINEAR.
 */
BGradientLinear::BGradientLinear()
{
	fData.linear.x1 = 0.0f;
	fData.linear.y1 = 0.0f;
	fData.linear.x2 = 0.0f;
	fData.linear.y2 = 0.0f;
	fType = TYPE_LINEAR;
}


/**
 * @brief Construct a linear gradient between two BPoint endpoints.
 *
 * @param start The start point of the gradient line; maps to offset 0.
 * @param end   The end point of the gradient line; maps to offset 255.
 */
BGradientLinear::BGradientLinear(const BPoint& start, const BPoint& end)
{
	fData.linear.x1 = start.x;
	fData.linear.y1 = start.y;
	fData.linear.x2 = end.x;
	fData.linear.y2 = end.y;
	fType = TYPE_LINEAR;
}


/**
 * @brief Construct a linear gradient between two points given as explicit coordinates.
 *
 * @param x1 X coordinate of the start point.
 * @param y1 Y coordinate of the start point.
 * @param x2 X coordinate of the end point.
 * @param y2 Y coordinate of the end point.
 */
BGradientLinear::BGradientLinear(float x1, float y1, float x2, float y2)
{
	fData.linear.x1 = x1;
	fData.linear.y1 = y1;
	fData.linear.x2 = x2;
	fData.linear.y2 = y2;
	fType = TYPE_LINEAR;
}


/**
 * @brief Return the start point of the gradient line.
 *
 * @return The start point as a BPoint.
 */
BPoint
BGradientLinear::Start() const
{
	return BPoint(fData.linear.x1, fData.linear.y1);
}


/**
 * @brief Set the start point of the gradient line from a BPoint.
 *
 * @param start The new start point.
 */
void
BGradientLinear::SetStart(const BPoint& start)
{
	fData.linear.x1 = start.x;
	fData.linear.y1 = start.y;
}


/**
 * @brief Set the start point of the gradient line from explicit coordinates.
 *
 * @param x New X coordinate of the start point.
 * @param y New Y coordinate of the start point.
 */
void
BGradientLinear::SetStart(float x, float y)
{
	fData.linear.x1 = x;
	fData.linear.y1 = y;
}


/**
 * @brief Return the end point of the gradient line.
 *
 * @return The end point as a BPoint.
 */
BPoint
BGradientLinear::End() const
{
	return BPoint(fData.linear.x2, fData.linear.y2);
}


/**
 * @brief Set the end point of the gradient line from a BPoint.
 *
 * @param end The new end point.
 */
void
BGradientLinear::SetEnd(const BPoint& end)
{
	fData.linear.x2 = end.x;
	fData.linear.y2 = end.y;
}


/**
 * @brief Set the end point of the gradient line from explicit coordinates.
 *
 * @param x New X coordinate of the end point.
 * @param y New Y coordinate of the end point.
 */
void
BGradientLinear::SetEnd(float x, float y)
{
	fData.linear.x2 = x;
	fData.linear.y2 = y;
}
