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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2009, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Artur Wyszynski <harakash@gmail.com>
 */


/**
 * @file Gradient.cpp
 * @brief Implementation of BGradient, the abstract base class for gradient fills
 *
 * BGradient defines the color stop list shared by all gradient types. Concrete
 * subclasses (BGradientLinear, BGradientRadial, etc.) add geometry-specific
 * parameters. Gradients are used with BView drawing calls.
 *
 * @see BGradientLinear, BGradientRadial, BGradientConic, BView
 */


#include "Gradient.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>

#include <DataIO.h>
#include <Message.h>

#include <AutoDeleter.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <GradientDiamond.h>
#include <GradientConic.h>


/**
 * @brief Construct a ColorStop from an rgb_color and a normalized offset.
 *
 * @param c The RGBA color value for this stop.
 * @param o The position of this stop along the gradient, in the range [0, 255].
 */
BGradient::ColorStop::ColorStop(const rgb_color c, float o)
{
	color.red = c.red;
	color.green = c.green;
	color.blue = c.blue;
	color.alpha = c.alpha;
	offset = o;
}


/**
 * @brief Construct a ColorStop from individual RGBA components and a normalized offset.
 *
 * @param r Red component (0–255).
 * @param g Green component (0–255).
 * @param b Blue component (0–255).
 * @param a Alpha component (0–255).
 * @param o The position of this stop along the gradient, in the range [0, 255].
 */
BGradient::ColorStop::ColorStop(uint8 r, uint8 g, uint8 b, uint8 a, float o)
{
	color.red = r;
	color.green = g;
	color.blue = b;
	color.alpha = a;
	offset = o;
}


/**
 * @brief Copy-construct a ColorStop from another ColorStop.
 *
 * @param other The ColorStop to copy.
 */
BGradient::ColorStop::ColorStop(const ColorStop& other)
{
	color.red = other.color.red;
	color.green = other.color.green;
	color.blue = other.color.blue;
	color.alpha = other.color.alpha;
	offset = other.offset;
}


/**
 * @brief Construct a default ColorStop with opaque black at offset 0.
 */
BGradient::ColorStop::ColorStop()
{
	color.red = 0;
	color.green = 0;
	color.blue = 0;
	color.alpha = 255;
	offset = 0;
}


/**
 * @brief Test whether two ColorStop objects differ in any field.
 *
 * @param other The ColorStop to compare against.
 * @return true if any color component or the offset differs; false if identical.
 */
bool
BGradient::ColorStop::operator!=(const ColorStop& other) const
{
	return color.red != other.color.red ||
	color.green != other.color.green ||
	color.blue != other.color.blue ||
	color.alpha != other.color.alpha ||
	offset != other.offset;
}


/**
 * @brief Comparator used by stable_sort to order ColorStop pointers by ascending offset.
 *
 * @param left  Pointer to the left-hand ColorStop.
 * @param right Pointer to the right-hand ColorStop.
 * @return true if left->offset is strictly less than right->offset.
 */
static bool
sort_color_stops_by_offset(const BGradient::ColorStop* left,
	const BGradient::ColorStop* right)
{
	return left->offset < right->offset;
}


// #pragma mark -


/**
 * @brief Construct a default BGradient with no color stops and type TYPE_NONE.
 *
 * The color stop list is pre-allocated for four entries as a reasonable
 * default capacity.
 */
BGradient::BGradient()
	: BArchivable(),
	fColorStops(4),
	fType(TYPE_NONE)
{
}


/**
 * @brief Copy-construct a BGradient, duplicating all color stops and geometry.
 *
 * @param other The gradient to copy from.
 */
BGradient::BGradient(const BGradient& other)
	: BArchivable(),
	fColorStops(std::max((int32)4, other.CountColorStops()))
{
	*this = other;
}


/**
 * @brief Construct a BGradient by unarchiving a previously archived BMessage.
 *
 * Restores color stops and all geometry fields for every supported gradient
 * type from the message. Missing fields default to 0.0.
 *
 * @param archive The BMessage containing the archived gradient state.
 * @note If @p archive is NULL the gradient is left in its default state
 *       (TYPE_NONE, no color stops).
 * @see Archive()
 */
BGradient::BGradient(BMessage* archive)
	: BArchivable(archive),
	fColorStops(4),
	fType(TYPE_NONE)
{
	if (!archive)
		return;

	// color stops
	ColorStop stop;
	for (int32 i = 0; archive->FindFloat("offset", i, &stop.offset) >= B_OK; i++) {
		if (archive->FindInt32("color", i, (int32*)&stop.color) >= B_OK)
			AddColorStop(stop, i);
		else
			break;
	}
	if (archive->FindInt32("type", (int32*)&fType) < B_OK)
		fType = TYPE_LINEAR;

	// linear
	if (archive->FindFloat("linear_x1", (float*)&fData.linear.x1) < B_OK)
		fData.linear.x1 = 0.0f;
	if (archive->FindFloat("linear_y1", (float*)&fData.linear.y1) < B_OK)
		fData.linear.y1 = 0.0f;
	if (archive->FindFloat("linear_x2", (float*)&fData.linear.x2) < B_OK)
		fData.linear.x2 = 0.0f;
	if (archive->FindFloat("linear_y2", (float*)&fData.linear.y2) < B_OK)
		fData.linear.y2 = 0.0f;

	// radial
	if (archive->FindFloat("radial_cx", (float*)&fData.radial.cx) < B_OK)
		fData.radial.cx = 0.0f;
	if (archive->FindFloat("radial_cy", (float*)&fData.radial.cy) < B_OK)
		fData.radial.cy = 0.0f;
	if (archive->FindFloat("radial_radius", (float*)&fData.radial.radius) < B_OK)
		fData.radial.radius = 0.0f;

	// radial focus
	if (archive->FindFloat("radial_f_cx", (float*)&fData.radial_focus.cx) < B_OK)
		fData.radial_focus.cx = 0.0f;
	if (archive->FindFloat("radial_f_cy", (float*)&fData.radial_focus.cy) < B_OK)
		fData.radial_focus.cy = 0.0f;
	if (archive->FindFloat("radial_f_fx", (float*)&fData.radial_focus.fx) < B_OK)
		fData.radial_focus.fx = 0.0f;
	if (archive->FindFloat("radial_f_fy", (float*)&fData.radial_focus.fy) < B_OK)
		fData.radial_focus.fy = 0.0f;
	if (archive->FindFloat("radial_f_radius", (float*)&fData.radial_focus.radius) < B_OK)
		fData.radial_focus.radius = 0.0f;

	// diamond
	if (archive->FindFloat("diamond_cx", (float*)&fData.diamond.cx) < B_OK)
		fData.diamond.cx = 0.0f;
	if (archive->FindFloat("diamond_cy", (float*)&fData.diamond.cy) < B_OK)
		fData.diamond.cy = 0.0f;

	// conic
	if (archive->FindFloat("conic_cx", (float*)&fData.conic.cx) < B_OK)
		fData.conic.cx = 0.0f;
	if (archive->FindFloat("conic_cy", (float*)&fData.conic.cy) < B_OK)
		fData.conic.cy = 0.0f;
	if (archive->FindFloat("conic_angle", (float*)&fData.conic.angle) < B_OK)
		fData.conic.angle = 0.0f;
}


/**
 * @brief Destroy the BGradient and free all color stops.
 *
 * Calls MakeEmpty() to delete every heap-allocated ColorStop before the
 * object itself is released.
 */
BGradient::~BGradient()
{
	MakeEmpty();
}


/**
 * @brief Archive the gradient into a BMessage.
 *
 * Serializes all color stops (color and offset fields) followed by the
 * gradient type and every geometry field for all supported gradient types.
 * The class name "BGradient" is appended last so that Instantiate() can
 * identify the archive.
 *
 * @param into The message to write into; must not be NULL.
 * @param deep If true, child objects are archived recursively (forwarded to
 *             BArchivable::Archive()).
 * @return B_OK on success, or a negative error code on the first failure.
 * @see BGradient(BMessage*)
 */
status_t
BGradient::Archive(BMessage* into, bool deep) const
{
	status_t ret = BArchivable::Archive(into, deep);

	// color steps
	if (ret >= B_OK) {
		for (int32 i = 0; ColorStop* stop = ColorStopAt(i); i++) {
			ret = into->AddInt32("color", (const uint32&)stop->color);
			if (ret < B_OK)
				break;
			ret = into->AddFloat("offset", stop->offset);
			if (ret < B_OK)
				break;
		}
	}
	// gradient type
	if (ret >= B_OK)
		ret = into->AddInt32("type", (int32)fType);

	// linear
	if (ret >= B_OK)
		ret = into->AddFloat("linear_x1", (float)fData.linear.x1);
	if (ret >= B_OK)
		ret = into->AddFloat("linear_y1", (float)fData.linear.y1);
	if (ret >= B_OK)
		ret = into->AddFloat("linear_x2", (float)fData.linear.x2);
	if (ret >= B_OK)
		ret = into->AddFloat("linear_y2", (float)fData.linear.y2);

	// radial
	if (ret >= B_OK)
		ret = into->AddFloat("radial_cx", (float)fData.radial.cx);
	if (ret >= B_OK)
		ret = into->AddFloat("radial_cy", (float)fData.radial.cy);
	if (ret >= B_OK)
		ret = into->AddFloat("radial_radius", (float)fData.radial.radius);

	// radial focus
	if (ret >= B_OK)
		ret = into->AddFloat("radial_f_cx", (float)fData.radial_focus.cx);
	if (ret >= B_OK)
		ret = into->AddFloat("radial_f_cy", (float)fData.radial_focus.cy);
	if (ret >= B_OK)
		ret = into->AddFloat("radial_f_fx", (float)fData.radial_focus.fx);
	if (ret >= B_OK)
		ret = into->AddFloat("radial_f_fy", (float)fData.radial_focus.fy);
	if (ret >= B_OK)
		ret = into->AddFloat("radial_f_radius", (float)fData.radial_focus.radius);

	// diamond
	if (ret >= B_OK)
		ret = into->AddFloat("diamond_cx", (float)fData.diamond.cx);
	if (ret >= B_OK)
		ret = into->AddFloat("diamond_cy", (float)fData.diamond.cy);

	// conic
	if (ret >= B_OK)
		ret = into->AddFloat("conic_cx", (float)fData.conic.cx);
	if (ret >= B_OK)
		ret = into->AddFloat("conic_cy", (float)fData.conic.cy);
	if (ret >= B_OK)
		ret = into->AddFloat("conic_angle", (float)fData.conic.angle);

	// finish off
	if (ret >= B_OK)
		ret = into->AddString("class", "BGradient");

	return ret;
}


/**
 * @brief Assign the state of another BGradient to this object.
 *
 * Copies the color stop list and all geometry fields from @p other, replacing
 * any existing state.  Self-assignment is handled safely.
 *
 * @param other The gradient to copy from.
 * @return A reference to this gradient.
 */
BGradient&
BGradient::operator=(const BGradient& other)
{
	if (&other == this)
		return *this;

	SetColorStops(other);
	fType = other.fType;
	switch (fType) {
		case TYPE_LINEAR:
			fData.linear = other.fData.linear;
			break;
		case TYPE_RADIAL:
			fData.radial = other.fData.radial;
			break;
		case TYPE_RADIAL_FOCUS:
			fData.radial_focus = other.fData.radial_focus;
			break;
		case TYPE_DIAMOND:
			fData.diamond = other.fData.diamond;
			break;
		case TYPE_CONIC:
			fData.conic = other.fData.conic;
			break;
		case TYPE_NONE:
			break;
	}
	return *this;
}


/**
 * @brief Test whether two gradients are identical in type and color stops.
 *
 * @param other The gradient to compare against.
 * @return true if the type and every color stop are equal; false otherwise.
 * @see ColorStopsAreEqual()
 */
bool
BGradient::operator==(const BGradient& other) const
{
	return ((other.GetType() == GetType()) && ColorStopsAreEqual(other));
}


/**
 * @brief Test whether two gradients differ in type or any color stop.
 *
 * @param other The gradient to compare against.
 * @return true if the gradients are not equal; false if they are identical.
 */
bool
BGradient::operator!=(const BGradient& other) const
{
	return !(*this == other);
}


/**
 * @brief Check whether the color stop lists of two gradients are identical.
 *
 * Compares the count, type, and each individual ColorStop in order.
 *
 * @param other The gradient whose color stops are compared with this one's.
 * @return true if both gradients have the same type and identical stop lists.
 */
bool
BGradient::ColorStopsAreEqual(const BGradient& other) const
{
	int32 count = CountColorStops();
	if (count == other.CountColorStops() &&
		fType == other.fType) {

		bool equal = true;
		for (int32 i = 0; i < count; i++) {
			ColorStop* ourStop = ColorStopAtFast(i);
			ColorStop* otherStop = other.ColorStopAtFast(i);
			if (*ourStop != *otherStop) {
				equal = false;
				break;
			}
		}
		return equal;
	}
	return false;
}


/**
 * @brief Replace this gradient's color stop list with a copy of another gradient's.
 *
 * Clears all existing stops via MakeEmpty() before adding new ones.
 *
 * @param other The gradient whose color stops are copied into this object.
 */
void
BGradient::SetColorStops(const BGradient& other)
{
	MakeEmpty();
	for (int32 i = 0; ColorStop* stop = other.ColorStopAt(i); i++)
		AddColorStop(*stop, i);
}


/**
 * @brief Add a color at the correct sorted position in the stop list.
 *
 * The stop is inserted so that the list remains sorted by ascending offset.
 * Offsets outside [0, 255] are rejected.
 *
 * @param color  The RGBA color value for the new stop.
 * @param offset Position along the gradient in the range [0, 255].
 * @return The index at which the stop was inserted, or -1 on failure.
 */
int32
BGradient::AddColor(const rgb_color& color, float offset)
{
	// Out of bounds stops would crash the app_server
	if (offset < 0.f || offset > 255.f)
		return -1;

	// find the correct index (sorted by offset)
	ColorStop* stop = new ColorStop(color, offset);
	int32 index = 0;
	int32 count = CountColorStops();
	for (; index < count; index++) {
		ColorStop* s = ColorStopAtFast(index);
		if (s->offset > stop->offset)
			break;
	}
	if (!fColorStops.AddItem((void*)stop, index)) {
		delete stop;
		return -1;
	}
	return index;
}


/**
 * @brief Insert a copy of a ColorStop at an explicit index.
 *
 * @param colorStop The stop to copy and insert.
 * @param index     The position in the list at which to insert the stop.
 * @return true on success; false if memory allocation or list insertion fails.
 */
bool
BGradient::AddColorStop(const ColorStop& colorStop, int32 index)
{
	ColorStop* stop = new ColorStop(colorStop);
	if (!fColorStops.AddItem((void*)stop, index)) {
		delete stop;
		return false;
	}
	return true;
}


/**
 * @brief Remove and delete the color stop at the given index.
 *
 * @param index Zero-based index of the stop to remove.
 * @return true if the stop was found and removed; false if the index is invalid.
 */
bool
BGradient::RemoveColor(int32 index)
{
	ColorStop* stop = (ColorStop*)fColorStops.RemoveItem(index);
	if (!stop) {
		return false;
	}
	delete stop;
	return true;
}


/**
 * @brief Replace the color and offset of an existing stop.
 *
 * The stop is only modified when the new value differs from the existing one.
 *
 * @param index Zero-based index of the stop to modify.
 * @param color The new ColorStop value (color + offset) to apply.
 * @return true if the stop existed and its value changed; false otherwise.
 */
bool
BGradient::SetColorStop(int32 index, const ColorStop& color)
{
	if (ColorStop* stop = ColorStopAt(index)) {
		if (*stop != color) {
			stop->color = color.color;
			stop->offset = color.offset;
			return true;
		}
	}
	return false;
}


/**
 * @brief Change the RGBA color of an existing stop, leaving its offset unchanged.
 *
 * @param index Zero-based index of the stop to modify.
 * @param color The new rgb_color value.
 * @return true if the stop existed and its color changed; false otherwise.
 */
bool
BGradient::SetColor(int32 index, const rgb_color& color)
{
	ColorStop* stop = ColorStopAt(index);
	if (stop && stop->color != color) {
		stop->color = color;
		return true;
	}
	return false;
}


/**
 * @brief Change the offset of an existing stop, leaving its color unchanged.
 *
 * @param index  Zero-based index of the stop to modify.
 * @param offset The new offset value in the range [0, 255].
 * @return true if the stop existed and its offset changed; false otherwise.
 * @note Changing the offset does not automatically re-sort the stop list.
 *       Call SortColorStopsByOffset() afterwards if sorted order is required.
 */
bool
BGradient::SetOffset(int32 index, float offset)
{
	ColorStop* stop = ColorStopAt(index);
	if (stop && stop->offset != offset) {
		stop->offset = offset;
		return true;
	}
	return false;
}


/**
 * @brief Return the number of color stops in the gradient.
 *
 * @return The count of color stops currently held by this gradient.
 */
int32
BGradient::CountColorStops() const
{
	return fColorStops.CountItems();
}


/**
 * @brief Return a pointer to the color stop at the given index, with bounds checking.
 *
 * @param index Zero-based index of the desired stop.
 * @return Pointer to the ColorStop, or NULL if @p index is out of range.
 * @see ColorStopAtFast()
 */
BGradient::ColorStop*
BGradient::ColorStopAt(int32 index) const
{
	return (ColorStop*)fColorStops.ItemAt(index);
}


/**
 * @brief Return a pointer to the color stop at the given index without bounds checking.
 *
 * This is the fast variant; the caller must ensure @p index is valid.
 *
 * @param index Zero-based index of the desired stop.
 * @return Pointer to the ColorStop.
 * @see ColorStopAt()
 */
BGradient::ColorStop*
BGradient::ColorStopAtFast(int32 index) const
{
	return (ColorStop*)fColorStops.ItemAtFast(index);
}


/**
 * @brief Return a pointer to the first element of the internal color stop array.
 *
 * Useful for passing the stop array directly to a renderer. Returns NULL when
 * the gradient has no stops.
 *
 * @return Pointer to the first ColorStop, or NULL if the list is empty.
 */
BGradient::ColorStop*
BGradient::ColorStops() const
{
	if (CountColorStops() > 0) {
		return (ColorStop*) fColorStops.Items();
	}
	return NULL;
}


/**
 * @brief Sort the color stops in ascending order of their offset values.
 *
 * Uses a stable sort so that stops with identical offsets retain their
 * original relative order, which allows sharp color transitions to be
 * represented by two consecutive stops at the same offset.
 */
void
BGradient::SortColorStopsByOffset()
{
	// Use stable sort: stops with the same offset will retain their original
	// order. This can be used to have sharp color changes in the gradient.
	// BList.SortItems() uses qsort(), which isn't stable, and sometimes swaps
	// such stops.
	const BGradient::ColorStop** first = (const BGradient::ColorStop**)fColorStops.Items();
	const BGradient::ColorStop** last = first + fColorStops.CountItems();
	std::stable_sort(first, last, sort_color_stops_by_offset);
}


/**
 * @brief Delete all color stops and reset the list to empty.
 *
 * Each heap-allocated ColorStop is deleted before the internal list is cleared.
 */
void
BGradient::MakeEmpty()
{
	int32 count = CountColorStops();
	for (int32 i = 0; i < count; i++)
		delete ColorStopAtFast(i);
	fColorStops.MakeEmpty();
}


/**
 * @brief Serialize the gradient to a binary stream.
 *
 * Writes the gradient type, stop count, each ColorStop, and the
 * geometry fields for the active gradient type. The stream is not
 * rewound before writing.
 *
 * @param stream The destination stream; must not be NULL.
 * @return B_OK on success.
 * @see Unflatten()
 */
status_t
BGradient::Flatten(BDataIO* stream) const
{
	int32 stopCount = CountColorStops();
	stream->Write(&fType, sizeof(Type));
	stream->Write(&stopCount, sizeof(int32));
	if (stopCount > 0) {
		for (int i = 0; i < stopCount; i++) {
			stream->Write(ColorStopAtFast(i),
				sizeof(ColorStop));
		}
	}

	switch (fType) {
		case TYPE_LINEAR:
			stream->Write(&fData.linear.x1, sizeof(float));
			stream->Write(&fData.linear.y1, sizeof(float));
			stream->Write(&fData.linear.x2, sizeof(float));
			stream->Write(&fData.linear.y2, sizeof(float));
			break;
		case TYPE_RADIAL:
			stream->Write(&fData.radial.cx, sizeof(float));
			stream->Write(&fData.radial.cy, sizeof(float));
			stream->Write(&fData.radial.radius, sizeof(float));
			break;
		case TYPE_RADIAL_FOCUS:
			stream->Write(&fData.radial_focus.cx, sizeof(float));
			stream->Write(&fData.radial_focus.cy, sizeof(float));
			stream->Write(&fData.radial_focus.fx, sizeof(float));
			stream->Write(&fData.radial_focus.fy, sizeof(float));
			stream->Write(&fData.radial_focus.radius, sizeof(float));
			break;
		case TYPE_DIAMOND:
			stream->Write(&fData.diamond.cx, sizeof(float));
			stream->Write(&fData.diamond.cy, sizeof(float));
			break;
		case TYPE_CONIC:
			stream->Write(&fData.conic.cx, sizeof(float));
			stream->Write(&fData.conic.cy, sizeof(float));
			stream->Write(&fData.conic.angle, sizeof(float));
			break;
		case TYPE_NONE:
			break;
	}
	return B_OK;
}


/**
 * @brief Allocate the concrete BGradient subclass appropriate for the given type.
 *
 * @param type The gradient type identifying which subclass to create.
 * @return A newly allocated gradient object, or NULL if allocation fails or the
 *         type is unrecognized.
 */
static BGradient*
gradient_for_type(BGradient::Type type)
{
	switch (type) {
		case BGradient::TYPE_LINEAR:
			return new (std::nothrow) BGradientLinear();
		case BGradient::TYPE_RADIAL:
			return new (std::nothrow) BGradientRadial();
		case BGradient::TYPE_RADIAL_FOCUS:
			return new (std::nothrow) BGradientRadialFocus();
		case BGradient::TYPE_DIAMOND:
			return new (std::nothrow) BGradientDiamond();
		case BGradient::TYPE_CONIC:
			return new (std::nothrow) BGradientConic();
		case BGradient::TYPE_NONE:
			return new (std::nothrow) BGradient();
	}
	return NULL;
}


/**
 * @brief Deserialize a gradient from a binary stream previously written by Flatten().
 *
 * Reads the type and stop count first, then allocates the matching subclass via
 * gradient_for_type(), populates its color stops, and finally reads the
 * geometry fields for the active type. On success @p output is set to the
 * newly allocated gradient and ownership is transferred to the caller.
 *
 * @param[out] output Receives the newly allocated gradient on success; set to
 *                    NULL on entry and on failure.
 * @param      stream The source stream positioned at the start of a flattened
 *                    gradient; must not be NULL.
 * @return B_OK on success, B_NO_MEMORY if allocation fails, or a negative
 *         stream error code on read failure.
 * @see Flatten()
 */
status_t
BGradient::Unflatten(BGradient *&output, BDataIO* stream)
{
	output = NULL;
	Type gradientType;
	int32 colorsCount;
	stream->Read(&gradientType, sizeof(Type));
	status_t status = stream->Read(&colorsCount, sizeof(int32));
	if (status < B_OK)
		return status;

	ObjectDeleter<BGradient> gradient(gradient_for_type(gradientType));
	if (!gradient.IsSet())
		return B_NO_MEMORY;

	if (colorsCount > 0) {
		ColorStop stop;
		for (int i = 0; i < colorsCount; i++) {
			if ((status = stream->Read(&stop, sizeof(ColorStop))) < B_OK)
				return status;
			if (!gradient->AddColorStop(stop, i))
				return B_NO_MEMORY;
		}
	}

	switch (gradientType) {
		case TYPE_LINEAR:
			stream->Read(&gradient->fData.linear.x1, sizeof(float));
			stream->Read(&gradient->fData.linear.y1, sizeof(float));
			stream->Read(&gradient->fData.linear.x2, sizeof(float));
			if ((status = stream->Read(&gradient->fData.linear.y2, sizeof(float))) < B_OK)
				return status;
			break;
		case TYPE_RADIAL:
			stream->Read(&gradient->fData.radial.cx, sizeof(float));
			stream->Read(&gradient->fData.radial.cy, sizeof(float));
			if ((stream->Read(&gradient->fData.radial.radius, sizeof(float))) < B_OK)
				return status;
			break;
		case TYPE_RADIAL_FOCUS:
			stream->Read(&gradient->fData.radial_focus.cx, sizeof(float));
			stream->Read(&gradient->fData.radial_focus.cy, sizeof(float));
			stream->Read(&gradient->fData.radial_focus.fx, sizeof(float));
			stream->Read(&gradient->fData.radial_focus.fy, sizeof(float));
			if ((stream->Read(&gradient->fData.radial_focus.radius, sizeof(float))) < B_OK)
				return status;
			break;
		case TYPE_DIAMOND:
			stream->Read(&gradient->fData.diamond.cx, sizeof(float));
			if ((stream->Read(&gradient->fData.diamond.cy, sizeof(float))) < B_OK)
				return status;
			break;
		case TYPE_CONIC:
			stream->Read(&gradient->fData.conic.cx, sizeof(float));
			stream->Read(&gradient->fData.conic.cy, sizeof(float));
			if ((stream->Read(&gradient->fData.conic.angle, sizeof(float))) < B_OK)
				return status;
			break;
		case TYPE_NONE:
			break;
	}

	output = gradient.Detach();
	return B_OK;
}
