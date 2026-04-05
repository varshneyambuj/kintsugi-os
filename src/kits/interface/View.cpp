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
 *   Copyright 2001-2019 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Axel Dörfler, axeld@pinc-software.de
 *       Adrian Oanca, adioanca@cotty.iren.ro
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 *       Julian Harnath, julian.harnath@rwth-aachen.de
 *       Joseph Groover, looncraz@looncraz.net
 */


/**
 * @file View.cpp
 * @brief Implementation of BView, the fundamental drawing and event-handling unit
 *
 * BView is the base class for all visible interface elements. It manages a
 * coordinate system, a tree of child views, drawing state, event hooks, focus,
 * cursors, and layout participation. BView communicates with the app_server via
 * a command buffer for efficient batching of drawing calls.
 *
 * @see BWindow, BLayout, BScrollBar, BFont
 */


#include <View.h>

#include <algorithm>
#include <new>

#include <math.h>
#include <stdio.h>

#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Cursor.h>
#include <File.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <GradientDiamond.h>
#include <GradientConic.h>
#include <InterfaceDefs.h>
#include <Layout.h>
#include <LayoutContext.h>
#include <LayoutUtils.h>
#include <MenuBar.h>
#include <Message.h>
#include <MessageQueue.h>
#include <ObjectList.h>
#include <Picture.h>
#include <Point.h>
#include <Polygon.h>
#include <PropertyInfo.h>
#include <Region.h>
#include <ScrollBar.h>
#include <Shape.h>
#include <Shelf.h>
#include <String.h>
#include <Window.h>

#include <AppMisc.h>
#include <AppServerLink.h>
#include <binary_compatibility/Interface.h>
#include <binary_compatibility/Support.h>
#include <MessagePrivate.h>
#include <MessageUtils.h>
#include <PortLink.h>
#include <ServerProtocol.h>
#include <ServerProtocolStructs.h>
#include <ShapePrivate.h>
#include <ToolTip.h>
#include <ToolTipManager.h>
#include <TokenSpace.h>
#include <ViewPrivate.h>

using std::nothrow;

//#define DEBUG_BVIEW
#ifdef DEBUG_BVIEW
#	include <stdio.h>
#	define STRACE(x) printf x
#	define BVTRACE _PrintToStream()
#else
#	define STRACE(x) ;
#	define BVTRACE ;
#endif


/**
 * @brief Scripting property table for BView.
 *
 * Defines the properties exposed via the BeOS scripting protocol, including
 * "Frame", "Hidden", "Shelf", and "View" (by count, index, reverse-index, or name).
 *
 * @see BView::ResolveSpecifier(), BView::GetSupportedSuites()
 */
static property_info sViewPropInfo[] = {
	{ "Frame", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER, 0 }, "The view's frame rectangle.", 0,
		{ B_RECT_TYPE }
	},
	{ "Hidden", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER, 0 }, "Whether or not the view is hidden.",
		0, { B_BOOL_TYPE }
	},
	{ "Shelf", { 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Directs the scripting message to the "
			"shelf.", 0
	},
	{ "View", { B_COUNT_PROPERTIES, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns the number of child views.", 0,
		{ B_INT32_TYPE }
	},
	{ "View", { 0 },
		{ B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, B_NAME_SPECIFIER, 0 },
		"Directs the scripting message to the specified view.", 0
	},

	{ 0 }
};


//	#pragma mark -


/**
 * @brief Converts an rgb_color to a host-endian uint32.
 *
 * rgb_color is always stored in RGBA order regardless of host endianness.
 * This helper reinterprets the four bytes as a 32-bit integer in host byte order,
 * which is suitable for packing into protocol messages.
 *
 * @param color The colour value to convert.
 * @return A uint32 encoding of the colour in host byte order.
 */
static inline uint32
get_uint32_color(rgb_color color)
{
	return B_BENDIAN_TO_HOST_INT32(*(uint32*)&color);
		// rgb_color is always in rgba format, no matter what endian;
		// we always return the int32 value in host endian.
}


/**
 * @brief Converts a host-endian uint32 to an rgb_color.
 *
 * Reverses get_uint32_color(): converts the integer to big-endian byte order
 * and reinterprets it as an rgb_color struct.
 *
 * @param value The host-endian encoded colour integer.
 * @return The corresponding rgb_color value.
 */
static inline rgb_color
get_rgb_color(uint32 value)
{
	value = B_HOST_TO_BENDIAN_INT32(value);
	return *(rgb_color*)&value;
}


//	#pragma mark -


namespace BPrivate {

/**
 * @brief Initialises all ViewState fields to their default (clean-slate) values.
 *
 * Sets the pen to the origin with size 1, colours to black/white/panel-background,
 * drawing mode to B_OP_COPY, scale to 1.0, and marks all state flags as valid
 * except B_VIEW_CLIP_REGION_BIT, which must always be fetched from the app_server.
 */
ViewState::ViewState()
{
	pen_location.Set(0, 0);
	pen_size = 1.0;

	// NOTE: the clipping_region is empty
	// on construction but it is not used yet,
	// we avoid having to keep track of it via
	// this flag
	clipping_region_used = false;

	high_color = (rgb_color){ 0, 0, 0, 255 };
	low_color = (rgb_color){ 255, 255, 255, 255 };
	view_color = low_color;
	which_view_color = B_NO_COLOR;
	which_view_color_tint = B_NO_TINT;

	which_high_color = B_NO_COLOR;
	which_high_color_tint = B_NO_TINT;

	which_low_color = B_NO_COLOR;
	which_low_color_tint = B_NO_TINT;

	pattern = B_SOLID_HIGH;
	drawing_mode = B_OP_COPY;

	origin.Set(0, 0);

	line_join = B_MITER_JOIN;
	line_cap = B_BUTT_CAP;
	miter_limit = B_DEFAULT_MITER_LIMIT;
	fill_rule = B_NONZERO;

	alpha_source_mode = B_PIXEL_ALPHA;
	alpha_function_mode = B_ALPHA_OVERLAY;

	scale = 1.0;

	font = *be_plain_font;
	font_flags = font.Flags();
	font_aliasing = false;

	parent_composite_transform.Reset();
	parent_composite_scale = 1.0f;
	parent_composite_origin.Set(0, 0);

	// We only keep the B_VIEW_CLIP_REGION_BIT flag invalidated,
	// because we should get the clipping region from app_server.
	// The other flags do not need to be included because the data they
	// represent is already in sync with app_server - app_server uses the
	// same init (default) values.
	valid_flags = ~B_VIEW_CLIP_REGION_BIT;

	archiving_flags = B_VIEW_FRAME_BIT | B_VIEW_RESIZE_BIT;
}


/**
 * @brief Sends only the font portion of the view state to the app_server.
 *
 * Starts an AS_VIEW_SET_FONT_STATE message and attaches only those font
 * attributes whose bits are set in @c font_flags, minimising traffic on the
 * port link.
 *
 * @param link The PortLink connected to the owning window's server channel.
 */
void
ViewState::UpdateServerFontState(BPrivate::PortLink &link)
{
	link.StartMessage(AS_VIEW_SET_FONT_STATE);
	link.Attach<uint16>(font_flags);
		// always present

	if (font_flags & B_FONT_FAMILY_AND_STYLE)
		link.Attach<uint32>(font.FamilyAndStyle());

	if (font_flags & B_FONT_SIZE)
		link.Attach<float>(font.Size());

	if (font_flags & B_FONT_SHEAR)
		link.Attach<float>(font.Shear());

	if (font_flags & B_FONT_ROTATION)
		link.Attach<float>(font.Rotation());

	if (font_flags & B_FONT_FALSE_BOLD_WIDTH)
		link.Attach<float>(font.FalseBoldWidth());

	if (font_flags & B_FONT_SPACING)
		link.Attach<uint8>(font.Spacing());

	if (font_flags & B_FONT_ENCODING)
		link.Attach<uint8>(font.Encoding());

	if (font_flags & B_FONT_FACE)
		link.Attach<uint16>(font.Face());

	if (font_flags & B_FONT_FLAGS)
		link.Attach<uint32>(font.Flags());
}


/**
 * @brief Sends the complete view state (font + drawing state) to the app_server.
 *
 * First calls UpdateServerFontState(), then packs all remaining drawing-state
 * fields (pen, colours, mode, scale, transforms, clipping region) into an
 * AS_VIEW_SET_STATE message.  After flushing, all valid flags except
 * B_VIEW_CLIP_REGION_BIT are marked valid on the client side.
 *
 * @param link The PortLink connected to the owning window's server channel.
 */
void
ViewState::UpdateServerState(BPrivate::PortLink &link)
{
	UpdateServerFontState(link);

	link.StartMessage(AS_VIEW_SET_STATE);

	ViewSetStateInfo info;
	info.penLocation = pen_location;
	info.penSize = pen_size;
	info.highColor = high_color;
	info.lowColor = low_color;
	info.whichHighColor = which_high_color;
	info.whichLowColor = which_low_color;
	info.whichHighColorTint = which_high_color_tint;
	info.whichLowColorTint = which_low_color_tint;
	info.pattern = pattern;
	info.drawingMode = drawing_mode;
	info.origin = origin;
	info.scale = scale;
	info.lineJoin = line_join;
	info.lineCap = line_cap;
	info.miterLimit = miter_limit;
	info.fillRule = fill_rule;
	info.alphaSourceMode = alpha_source_mode;
	info.alphaFunctionMode = alpha_function_mode;
	info.fontAntialiasing = font_aliasing;
	link.Attach<ViewSetStateInfo>(info);

	// BAffineTransform is transmitted as a double array
	double _transform[6];
	if (transform.Flatten(_transform, sizeof(_transform)) != B_OK)
		return;
	link.Attach<double[6]>(_transform);

	// we send the 'local' clipping region... if we have one...
	link.Attach<bool>(clipping_region_used);
	if (clipping_region_used)
		link.AttachRegion(clipping_region);

	// Although we might have a 'local' clipping region, when we call
	// BView::GetClippingRegion() we ask for the 'global' one and it
	// is kept on server, so we must invalidate B_VIEW_CLIP_REGION_BIT flag

	valid_flags = ~B_VIEW_CLIP_REGION_BIT;
}


/**
 * @brief Refreshes the local view state by querying the app_server.
 *
 * Sends AS_VIEW_GET_STATE and reads back the full ViewGetStateInfo structure,
 * including font settings, pen, colours, drawing mode, transforms, and the
 * user clipping region.  On success, valid_flags is updated to reflect the
 * newly synchronised fields (excluding B_VIEW_CLIP_REGION_BIT and
 * B_VIEW_PARENT_COMPOSITE_BIT, which remain managed separately).
 *
 * @param link The PortLink connected to the owning window's server channel.
 */
void
ViewState::UpdateFrom(BPrivate::PortLink &link)
{
	link.StartMessage(AS_VIEW_GET_STATE);

	int32 code;
	if (link.FlushWithReply(code) != B_OK
		|| code != B_OK)
		return;

	ViewGetStateInfo info;
	link.Read<ViewGetStateInfo>(&info);

	// set view's font state
	font_flags = B_FONT_ALL;
	font.SetFamilyAndStyle(info.fontID);
	font.SetSize(info.fontSize);
	font.SetShear(info.fontShear);
	font.SetRotation(info.fontRotation);
	font.SetFalseBoldWidth(info.fontFalseBoldWidth);
	font.SetSpacing(info.fontSpacing);
	font.SetEncoding(info.fontEncoding);
	font.SetFace(info.fontFace);
	font.SetFlags(info.fontFlags);

	// set view's state
	pen_location = info.viewStateInfo.penLocation;
	pen_size = info.viewStateInfo.penSize;
	high_color = info.viewStateInfo.highColor;
	low_color = info.viewStateInfo.lowColor;
	pattern = info.viewStateInfo.pattern;
	drawing_mode = info.viewStateInfo.drawingMode;
	origin = info.viewStateInfo.origin;
	scale = info.viewStateInfo.scale;
	line_join = info.viewStateInfo.lineJoin;
	line_cap = info.viewStateInfo.lineCap;
	miter_limit = info.viewStateInfo.miterLimit;
	fill_rule = info.viewStateInfo.fillRule;
	alpha_source_mode = info.viewStateInfo.alphaSourceMode;
	alpha_function_mode = info.viewStateInfo.alphaFunctionMode;
	font_aliasing = info.viewStateInfo.fontAntialiasing;

	// BAffineTransform is transmitted as a double array
	double _transform[6];
	link.Read<double[6]>(&_transform);
	if (transform.Unflatten(B_AFFINE_TRANSFORM_TYPE, _transform,
		sizeof(_transform)) != B_OK) {
		return;
	}

	// read the user clipping
	// (that's NOT the current View visible clipping but the additional
	// user specified clipping!)
	link.Read<bool>(&clipping_region_used);
	if (clipping_region_used)
		link.ReadRegion(&clipping_region);
	else
		clipping_region.MakeEmpty();

	valid_flags = ~(B_VIEW_CLIP_REGION_BIT | B_VIEW_PARENT_COMPOSITE_BIT)
		| (valid_flags & B_VIEW_PARENT_COMPOSITE_BIT);
}

}	// namespace BPrivate


//	#pragma mark -


// archiving constants
namespace {
	/** @brief Archive field name for the three layout sizes {min, max, preferred}. */
	const char* const kSizesField = "BView:sizes";
		// kSizesField = {min, max, pref}
	/** @brief Archive field name for the view's explicit layout alignment. */
	const char* const kAlignmentField = "BView:alignment";
	/** @brief Archive field name for the view's attached BLayout object. */
	const char* const kLayoutField = "BView:layout";
}


/**
 * @brief Private aggregate holding all layout-related state for a BView.
 *
 * Stores explicit size hints, alignment, the attached BLayout, the current
 * BLayoutContext, all BLayoutItems that reference this view, and several
 * boolean flags that drive the layout invalidation and re-layout cycle.
 *
 * @see BView::SetLayout(), BView::InvalidateLayout(), BView::DoLayout()
 */
struct BView::LayoutData {
	/**
	 * @brief Initialises the layout data to a clean, layout-pending state.
	 *
	 * All explicit sizes and alignment are set to their "unset" defaults.
	 * @c fLayoutValid and @c fMinMaxValid start as @c true so that the first
	 * explicit InvalidateLayout() call initiates a real re-layout pass.
	 */
	LayoutData()
		:
		fMinSize(),
		fMaxSize(),
		fPreferredSize(),
		fAlignment(),
		fLayoutInvalidationDisabled(0),
		fLayout(NULL),
		fLayoutContext(NULL),
		fLayoutItems(5),
		fLayoutValid(true),		// TODO: Rethink these initial values!
		fMinMaxValid(true),		//
		fLayoutInProgress(false),
		fNeedsRelayout(true)
	{
	}

	/**
	 * @brief Writes explicit size and alignment hints into an archive message.
	 *
	 * Stores the minimum, maximum, and preferred sizes (under kSizesField) and
	 * the alignment (under kAlignmentField) so they survive archiving.
	 *
	 * @param archive The destination message.
	 * @return B_OK on success, or the first error encountered.
	 */
	status_t
	AddDataToArchive(BMessage* archive)
	{
		status_t err = archive->AddSize(kSizesField, fMinSize);

		if (err == B_OK)
			err = archive->AddSize(kSizesField, fMaxSize);

		if (err == B_OK)
			err = archive->AddSize(kSizesField, fPreferredSize);

		if (err == B_OK)
			err = archive->AddAlignment(kAlignmentField, fAlignment);

		return err;
	}

	/**
	 * @brief Restores explicit size and alignment hints from an archive message.
	 *
	 * Reads the minimum, maximum, and preferred sizes (at indices 0, 1, 2 of
	 * kSizesField) and the alignment from kAlignmentField.  Missing fields are
	 * silently left at their default values.
	 *
	 * @param archive The source message previously written by AddDataToArchive().
	 */
	void
	PopulateFromArchive(BMessage* archive)
	{
		archive->FindSize(kSizesField, 0, &fMinSize);
		archive->FindSize(kSizesField, 1, &fMaxSize);
		archive->FindSize(kSizesField, 2, &fPreferredSize);
		archive->FindAlignment(kAlignmentField, &fAlignment);
	}

	/** @brief Explicitly set minimum size, or an "unset" BSize by default. */
	BSize			fMinSize;
	/** @brief Explicitly set maximum size, or an "unset" BSize by default. */
	BSize			fMaxSize;
	/** @brief Explicitly set preferred size, or an "unset" BSize by default. */
	BSize			fPreferredSize;
	/** @brief Explicitly set alignment within the parent layout cell. */
	BAlignment		fAlignment;
	/** @brief Reference count preventing layout invalidation while positive. */
	int				fLayoutInvalidationDisabled;
	/** @brief The BLayout managing this view's children, or NULL. */
	BLayout*		fLayout;
	/** @brief The active BLayoutContext during a layout pass, or NULL. */
	BLayoutContext*	fLayoutContext;
	/** @brief All BLayoutItems that represent this view in parent layouts. */
	BObjectList<BLayoutItem> fLayoutItems;
	/** @brief True when the current layout pass results are still valid. */
	bool			fLayoutValid;
	/** @brief True when cached min/max sizes are still valid. */
	bool			fMinMaxValid;
	/** @brief True while a layout pass is actively executing (re-entry guard). */
	bool			fLayoutInProgress;
	/** @brief True when the view must be re-laid out at the next opportunity. */
	bool			fNeedsRelayout;
};


/**
 * @brief Constructs a layout-aware BView with no explicit frame rectangle.
 *
 * The view is given a zero-area frame (0,0,-1,-1) and B_FOLLOW_NONE resizing
 * mode.  B_SUPPORTS_LAYOUT is automatically added to @p flags.  If @p layout
 * is non-NULL it is immediately installed via SetLayout().
 *
 * @param name    The handler/view name.
 * @param flags   View flags (e.g. B_WILL_DRAW); B_SUPPORTS_LAYOUT is ORed in.
 * @param layout  Optional BLayout to attach, or NULL.
 */
BView::BView(const char* name, uint32 flags, BLayout* layout)
	:
	BHandler(name)
{
	_InitData(BRect(0, 0, -1, -1), name, B_FOLLOW_NONE,
		flags | B_SUPPORTS_LAYOUT);
	SetLayout(layout);
}


/**
 * @brief Constructs a BView with an explicit frame rectangle and resizing mode.
 *
 * This is the classic BeOS constructor for views that participate in manual
 * (non-layout) view hierarchies.  Frame coordinates are rounded to integers.
 *
 * @param frame        The view's frame in parent coordinates.
 * @param name         The handler/view name.
 * @param resizingMode How the view resizes with its parent (B_FOLLOW_* constants).
 * @param flags        View flags (e.g. B_WILL_DRAW, B_FRAME_EVENTS).
 */
BView::BView(BRect frame, const char* name, uint32 resizingMode, uint32 flags)
	:
	BHandler(name)
{
	_InitData(frame, name, resizingMode, flags);
}


/**
 * @brief Unarchives a BView from a BMessage previously produced by Archive().
 *
 * Restores the frame, resizing mode, flags, font, colours, drawing state,
 * layout hints, and visibility level from the archive.  Child views are
 * recursively unarchived via BUnarchiver when the archive is managed, or
 * directly via AddChild() otherwise.
 *
 * @param archive The source BMessage; must not be NULL.
 *
 * @see BView::Archive(), BView::AllUnarchived()
 */
BView::BView(BMessage* archive)
	:
	BHandler(BUnarchiver::PrepareArchive(archive))
{
	BUnarchiver unarchiver(archive);
	if (!archive)
		debugger("BView cannot be constructed from a NULL archive.");

	BRect frame;
	archive->FindRect("_frame", &frame);

	uint32 resizingMode;
	if (archive->FindInt32("_resize_mode", (int32*)&resizingMode) != B_OK)
		resizingMode = 0;

	uint32 flags;
	if (archive->FindInt32("_flags", (int32*)&flags) != B_OK)
		flags = 0;

	_InitData(frame, Name(), resizingMode, flags);

	font_family family;
	font_style style;
	if (archive->FindString("_fname", 0, (const char**)&family) == B_OK
		&& archive->FindString("_fname", 1, (const char**)&style) == B_OK) {
		BFont font;
		font.SetFamilyAndStyle(family, style);

		float size;
		if (archive->FindFloat("_fflt", 0, &size) == B_OK)
			font.SetSize(size);

		float shear;
		if (archive->FindFloat("_fflt", 1, &shear) == B_OK
			&& shear >= 45.0 && shear <= 135.0)
			font.SetShear(shear);

		float rotation;
		if (archive->FindFloat("_fflt", 2, &rotation) == B_OK
			&& rotation >=0 && rotation <= 360)
			font.SetRotation(rotation);

		SetFont(&font, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE
			| B_FONT_SHEAR | B_FONT_ROTATION);
	}

	int32 color = 0;
	if (archive->FindInt32("_color", 0, &color) == B_OK)
		SetHighColor(get_rgb_color(color));
	if (archive->FindInt32("_color", 1, &color) == B_OK)
		SetLowColor(get_rgb_color(color));
	if (archive->FindInt32("_color", 2, &color) == B_OK)
		SetViewColor(get_rgb_color(color));

	float tint = B_NO_TINT;
	if (archive->FindInt32("_uicolor", 0, &color) == B_OK
		&& color != B_NO_COLOR) {
		if (archive->FindFloat("_uitint", 0, &tint) != B_OK)
			tint = B_NO_TINT;

		SetHighUIColor((color_which)color, tint);
	}
	if (archive->FindInt32("_uicolor", 1, &color) == B_OK
		&& color != B_NO_COLOR) {
		if (archive->FindFloat("_uitint", 1, &tint) != B_OK)
			tint = B_NO_TINT;

		SetLowUIColor((color_which)color, tint);
	}
	if (archive->FindInt32("_uicolor", 2, &color) == B_OK
		&& color != B_NO_COLOR) {
		if (archive->FindFloat("_uitint", 2, &tint) != B_OK)
			tint = B_NO_TINT;

		SetViewUIColor((color_which)color, tint);
	}

	uint32 evMask;
	uint32 options;
	if (archive->FindInt32("_evmask", 0, (int32*)&evMask) == B_OK
		&& archive->FindInt32("_evmask", 1, (int32*)&options) == B_OK)
		SetEventMask(evMask, options);

	BPoint origin;
	if (archive->FindPoint("_origin", &origin) == B_OK)
		SetOrigin(origin);

	float scale;
	if (archive->FindFloat("_scale", &scale) == B_OK)
		SetScale(scale);

	BAffineTransform transform;
	if (archive->FindFlat("_transform", &transform) == B_OK)
		SetTransform(transform);

	float penSize;
	if (archive->FindFloat("_psize", &penSize) == B_OK)
		SetPenSize(penSize);

	BPoint penLocation;
	if (archive->FindPoint("_ploc", &penLocation) == B_OK)
		MovePenTo(penLocation);

	int16 lineCap;
	int16 lineJoin;
	float lineMiter;
	if (archive->FindInt16("_lmcapjoin", 0, &lineCap) == B_OK
		&& archive->FindInt16("_lmcapjoin", 1, &lineJoin) == B_OK
		&& archive->FindFloat("_lmmiter", &lineMiter) == B_OK)
		SetLineMode((cap_mode)lineCap, (join_mode)lineJoin, lineMiter);

	int16 fillRule;
	if (archive->FindInt16("_fillrule", &fillRule) == B_OK)
		SetFillRule(fillRule);

	int16 alphaBlend;
	int16 modeBlend;
	if (archive->FindInt16("_blend", 0, &alphaBlend) == B_OK
		&& archive->FindInt16("_blend", 1, &modeBlend) == B_OK)
		SetBlendingMode( (source_alpha)alphaBlend, (alpha_function)modeBlend);

	uint32 drawingMode;
	if (archive->FindInt32("_dmod", (int32*)&drawingMode) == B_OK)
		SetDrawingMode((drawing_mode)drawingMode);

	fLayoutData->PopulateFromArchive(archive);

	if (archive->FindInt16("_show", &fShowLevel) != B_OK)
		fShowLevel = 0;

	if (BUnarchiver::IsArchiveManaged(archive)) {
		int32 i = 0;
		while (unarchiver.EnsureUnarchived("_views", i++) == B_OK)
				;
		unarchiver.EnsureUnarchived(kLayoutField);

	} else {
		BMessage msg;
		for (int32 i = 0; archive->FindMessage("_views", i, &msg) == B_OK;
			i++) {
			BArchivable* object = instantiate_object(&msg);
			if (BView* child = dynamic_cast<BView*>(object))
				AddChild(child);
		}
	}
}


/**
 * @brief Creates a BView instance from an archive message (factory method).
 *
 * Validates the archive with validate_instantiation() before constructing.
 *
 * @param data The archive message.
 * @return A newly allocated BView, or NULL if the archive is invalid.
 */
BArchivable*
BView::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data , "BView"))
		return NULL;

	return new(std::nothrow) BView(data);
}


/**
 * @brief Archives the view's configuration into a BMessage.
 *
 * Stores the frame, resizing mode, flags, event mask, font, colours (including
 * UI-colour aliases and tints), drawing state (origin, scale, transform, pen,
 * line modes, fill rule, blending, drawing mode), layout hints, and visibility
 * level.  When @p deep is true, child views and the attached layout are
 * recursively archived.
 *
 * @param data  The destination message.
 * @param deep  If true, recursively archive child views and the layout.
 * @return B_OK on success, or the first error encountered.
 *
 * @see BView::Instantiate(), BView::AllUnarchived()
 */
status_t
BView::Archive(BMessage* data, bool deep) const
{
	BArchiver archiver(data);
	status_t ret = BHandler::Archive(data, deep);

	if (ret != B_OK)
		return ret;

	if ((fState->archiving_flags & B_VIEW_FRAME_BIT) != 0)
		ret = data->AddRect("_frame", Bounds().OffsetToCopy(fParentOffset));

	if (ret == B_OK)
		ret = data->AddInt32("_resize_mode", ResizingMode());

	if (ret == B_OK)
		ret = data->AddInt32("_flags", Flags());

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_EVENT_MASK_BIT) != 0) {
		ret = data->AddInt32("_evmask", fEventMask);
		if (ret == B_OK)
			ret = data->AddInt32("_evmask", fEventOptions);
	}

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_FONT_BIT) != 0) {
		BFont font;
		GetFont(&font);

		font_family family;
		font_style style;
		font.GetFamilyAndStyle(&family, &style);
		ret = data->AddString("_fname", family);
		if (ret == B_OK)
			ret = data->AddString("_fname", style);
		if (ret == B_OK)
			ret = data->AddFloat("_fflt", font.Size());
		if (ret == B_OK)
			ret = data->AddFloat("_fflt", font.Shear());
		if (ret == B_OK)
			ret = data->AddFloat("_fflt", font.Rotation());
	}

	// colors
	if (ret == B_OK)
		ret = data->AddInt32("_color", get_uint32_color(HighColor()));
	if (ret == B_OK)
		ret = data->AddInt32("_color", get_uint32_color(LowColor()));
	if (ret == B_OK)
		ret = data->AddInt32("_color", get_uint32_color(ViewColor()));

	if (ret == B_OK)
		ret = data->AddInt32("_uicolor", (int32)HighUIColor());
	if (ret == B_OK)
		ret = data->AddInt32("_uicolor", (int32)LowUIColor());
	if (ret == B_OK)
		ret = data->AddInt32("_uicolor", (int32)ViewUIColor());

	if (ret == B_OK)
		ret = data->AddFloat("_uitint", fState->which_high_color_tint);
	if (ret == B_OK)
		ret = data->AddFloat("_uitint", fState->which_low_color_tint);
	if (ret == B_OK)
		ret = data->AddFloat("_uitint", fState->which_view_color_tint);

//	NOTE: we do not use this flag any more
//	if ( 1 ){
//		ret = data->AddInt32("_dbuf", 1);
//	}

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_ORIGIN_BIT) != 0)
		ret = data->AddPoint("_origin", Origin());

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_SCALE_BIT) != 0)
		ret = data->AddFloat("_scale", Scale());

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_TRANSFORM_BIT) != 0) {
		BAffineTransform transform = Transform();
		ret = data->AddFlat("_transform", &transform);
	}

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_PEN_SIZE_BIT) != 0)
		ret = data->AddFloat("_psize", PenSize());

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_PEN_LOCATION_BIT) != 0)
		ret = data->AddPoint("_ploc", PenLocation());

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_LINE_MODES_BIT) != 0) {
		ret = data->AddInt16("_lmcapjoin", (int16)LineCapMode());
		if (ret == B_OK)
			ret = data->AddInt16("_lmcapjoin", (int16)LineJoinMode());
		if (ret == B_OK)
			ret = data->AddFloat("_lmmiter", LineMiterLimit());
	}

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_FILL_RULE_BIT) != 0)
		ret = data->AddInt16("_fillrule", (int16)FillRule());

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_BLENDING_BIT) != 0) {
		source_alpha alphaSourceMode;
		alpha_function alphaFunctionMode;
		GetBlendingMode(&alphaSourceMode, &alphaFunctionMode);

		ret = data->AddInt16("_blend", (int16)alphaSourceMode);
		if (ret == B_OK)
			ret = data->AddInt16("_blend", (int16)alphaFunctionMode);
	}

	if (ret == B_OK && (fState->archiving_flags & B_VIEW_DRAWING_MODE_BIT) != 0)
		ret = data->AddInt32("_dmod", DrawingMode());

	if (ret == B_OK)
		ret = fLayoutData->AddDataToArchive(data);

	if (ret == B_OK)
		ret = data->AddInt16("_show", fShowLevel);

	if (deep && ret == B_OK) {
		for (BView* child = fFirstChild; child != NULL && ret == B_OK;
			child = child->fNextSibling)
			ret = archiver.AddArchivable("_views", child, deep);

		if (ret == B_OK)
			ret = archiver.AddArchivable(kLayoutField, GetLayout(), deep);
	}

	return archiver.Finish(ret);
}


/**
 * @brief Completes unarchiving once all objects in the archive have been instantiated.
 *
 * Called by the BUnarchiver framework after all archived objects have been
 * created.  Locates and adds child views by token, then installs the archived
 * BLayout (if any) and sets B_SUPPORTS_LAYOUT.
 *
 * @param from The original archive message.
 * @return B_OK on success, or an error if a required object is missing.
 */
status_t
BView::AllUnarchived(const BMessage* from)
{
	BUnarchiver unarchiver(from);
	status_t err = B_OK;

	int32 count;
	from->GetInfo("_views", NULL, &count);

	for (int32 i = 0; err == B_OK && i < count; i++) {
		BView* child;
		err = unarchiver.FindObject<BView>("_views", i, child);
		if (err == B_OK)
			err = _AddChild(child, NULL) ? B_OK : B_ERROR;
	}

	if (err == B_OK) {
		BLayout*& layout = fLayoutData->fLayout;
		err = unarchiver.FindObject(kLayoutField, layout);
		if (err == B_OK && layout) {
			fFlags |= B_SUPPORTS_LAYOUT;
			fLayoutData->fLayout->SetOwner(this);
		}
	}

	return err;
}


/**
 * @brief Finalises archiving after all objects have been archived (hook).
 *
 * Delegates to BHandler::AllArchived().  Subclasses may override to store
 * additional cross-object references that are only valid once all objects in
 * the archive have been written.
 *
 * @param into The archive message being finalised.
 * @return B_OK on success.
 */
status_t
BView::AllArchived(BMessage* into) const
{
	return BHandler::AllArchived(into);
}


/**
 * @brief Destroys the view, its children, and all associated resources.
 *
 * Recursively deletes all child views, removes the attached layout, releases
 * the tool-tip reference, disconnects scroll bars, and frees the internal
 * ViewState and LayoutData objects.
 *
 * @note The view must not belong to a window when it is deleted; call
 *       RemoveSelf() first, otherwise this destructor calls debugger().
 */
BView::~BView()
{
	STRACE(("BView(%s)::~BView()\n", this->Name()));

	if (fOwner != NULL) {
		debugger("Trying to delete a view that belongs to a window. "
			"Call RemoveSelf first.");
	}

	// we also delete all our children

	BView* child = fFirstChild;
	while (child) {
		BView* nextChild = child->fNextSibling;

		delete child;
		child = nextChild;
	}

	SetLayout(NULL);
	_RemoveLayoutItemsFromLayout(true);

	delete fLayoutData;

	_RemoveSelf();

	if (fToolTip != NULL)
		fToolTip->ReleaseReference();

	if (fVerScroller != NULL)
		fVerScroller->SetTarget((BView*)NULL);
	if (fHorScroller != NULL)
		fHorScroller->SetTarget((BView*)NULL);

	SetName(NULL);

	_RemoveCommArray();
	delete fState;
}


/**
 * @brief Returns the view's bounds rectangle in its own local coordinate system.
 *
 * During printing the print rectangle is returned instead of the on-screen
 * bounds so that drawing code can adapt its layout to the page size.
 *
 * @return The bounds rectangle in view-local coordinates.
 */
BRect
BView::Bounds() const
{
	_CheckLock();

	if (fIsPrinting)
		return fState->print_rect;

	return fBounds;
}


/**
 * @brief Converts @p point from view-local to parent coordinates in place.
 *
 * Accounts for the view's scroll offset (fBounds.left/top) and its position
 * within the parent (fParentOffset).  Does nothing if the view has no parent.
 *
 * @param point     The point to convert; modified in place.
 * @param checkLock If true, asserts that the owning window is locked.
 */
void
BView::_ConvertToParent(BPoint* point, bool checkLock) const
{
	if (!fParent)
		return;

	if (checkLock)
		_CheckLock();

	// - our scrolling offset
	// + our bounds location within the parent
	point->x += -fBounds.left + fParentOffset.x;
	point->y += -fBounds.top + fParentOffset.y;
}


/**
 * @brief Converts @p point from view-local to parent coordinates in place.
 *
 * @param point The point to convert; modified in place.
 */
void
BView::ConvertToParent(BPoint* point) const
{
	_ConvertToParent(point, true);
}


/**
 * @brief Returns @p point converted from view-local to parent coordinates.
 *
 * @param point The point to convert.
 * @return The converted point in parent-view coordinates.
 */
BPoint
BView::ConvertToParent(BPoint point) const
{
	ConvertToParent(&point);

	return point;
}


/**
 * @brief Converts @p point from parent coordinates to view-local in place.
 *
 * Reverses _ConvertToParent(): subtracts the view's parent offset and adds
 * back the scroll origin.  Does nothing if the view has no parent.
 *
 * @param point     The point to convert; modified in place.
 * @param checkLock If true, asserts that the owning window is locked.
 */
void
BView::_ConvertFromParent(BPoint* point, bool checkLock) const
{
	if (!fParent)
		return;

	if (checkLock)
		_CheckLock();

	// - our bounds location within the parent
	// + our scrolling offset
	point->x += -fParentOffset.x + fBounds.left;
	point->y += -fParentOffset.y + fBounds.top;
}


/**
 * @brief Converts @p point from parent coordinates to view-local in place.
 *
 * @param point The point to convert; modified in place.
 */
void
BView::ConvertFromParent(BPoint* point) const
{
	_ConvertFromParent(point, true);
}


/**
 * @brief Returns @p point converted from parent coordinates to view-local.
 *
 * @param point The point to convert.
 * @return The converted point in view-local coordinates.
 */
BPoint
BView::ConvertFromParent(BPoint point) const
{
	ConvertFromParent(&point);

	return point;
}


/**
 * @brief Converts @p rect from view-local to parent coordinates in place.
 *
 * @param rect The rectangle to convert; modified in place.
 */
void
BView::ConvertToParent(BRect* rect) const
{
	if (!fParent)
		return;

	_CheckLock();

	// - our scrolling offset
	// + our bounds location within the parent
	rect->OffsetBy(-fBounds.left + fParentOffset.x,
		-fBounds.top + fParentOffset.y);
}


/**
 * @brief Returns @p rect converted from view-local to parent coordinates.
 *
 * @param rect The rectangle to convert.
 * @return The converted rectangle in parent-view coordinates.
 */
BRect
BView::ConvertToParent(BRect rect) const
{
	ConvertToParent(&rect);

	return rect;
}


/**
 * @brief Converts @p rect from parent coordinates to view-local in place.
 *
 * @param rect The rectangle to convert; modified in place.
 */
void
BView::ConvertFromParent(BRect* rect) const
{
	if (!fParent)
		return;

	_CheckLock();

	// - our bounds location within the parent
	// + our scrolling offset
	rect->OffsetBy(-fParentOffset.x + fBounds.left,
		-fParentOffset.y + fBounds.top);
}


/**
 * @brief Returns @p rect converted from parent coordinates to view-local.
 *
 * @param rect The rectangle to convert.
 * @return The converted rectangle in view-local coordinates.
 */
BRect
BView::ConvertFromParent(BRect rect) const
{
	ConvertFromParent(&rect);

	return rect;
}


/**
 * @brief Converts @p point from view-local to screen coordinates in place.
 *
 * Walks up the view hierarchy, applying each parent's conversion, and finally
 * delegates to BWindow::ConvertToScreen() at the top level.
 *
 * @param point     The point to convert; modified in place.
 * @param checkLock If true, asserts that the owning window is locked.
 */
void
BView::_ConvertToScreen(BPoint* point, bool checkLock) const
{
	if (!fParent) {
		if (fOwner)
			fOwner->ConvertToScreen(point);

		return;
	}

	if (checkLock)
		_CheckOwnerLock();

	_ConvertToParent(point, false);
	fParent->_ConvertToScreen(point, false);
}


/**
 * @brief Converts @p point from view-local to screen coordinates in place.
 *
 * @param point The point to convert; modified in place.
 */
void
BView::ConvertToScreen(BPoint* point) const
{
	_ConvertToScreen(point, true);
}


/**
 * @brief Returns @p point converted from view-local to screen coordinates.
 *
 * @param point The point to convert.
 * @return The converted point in screen coordinates.
 */
BPoint
BView::ConvertToScreen(BPoint point) const
{
	ConvertToScreen(&point);

	return point;
}


/**
 * @brief Converts @p point from screen coordinates to view-local in place.
 *
 * Walks up the view hierarchy in reverse, unapplying each parent's conversion,
 * starting from BWindow::ConvertFromScreen() at the top level.
 *
 * @param point     The point to convert; modified in place.
 * @param checkLock If true, asserts that the owning window is locked.
 */
void
BView::_ConvertFromScreen(BPoint* point, bool checkLock) const
{
	if (!fParent) {
		if (fOwner)
			fOwner->ConvertFromScreen(point);

		return;
	}

	if (checkLock)
		_CheckOwnerLock();

	_ConvertFromParent(point, false);
	fParent->_ConvertFromScreen(point, false);
}


/**
 * @brief Converts @p point from screen coordinates to view-local in place.
 *
 * @param point The point to convert; modified in place.
 */
void
BView::ConvertFromScreen(BPoint* point) const
{
	_ConvertFromScreen(point, true);
}


/**
 * @brief Returns @p point converted from screen coordinates to view-local.
 *
 * @param point The point to convert.
 * @return The converted point in view-local coordinates.
 */
BPoint
BView::ConvertFromScreen(BPoint point) const
{
	ConvertFromScreen(&point);

	return point;
}


/**
 * @brief Converts @p rect from view-local to screen coordinates in place.
 *
 * @param rect The rectangle to convert; modified in place.
 */
void
BView::ConvertToScreen(BRect* rect) const
{
	BPoint offset(0.0, 0.0);
	ConvertToScreen(&offset);
	rect->OffsetBy(offset);
}


/**
 * @brief Returns @p rect converted from view-local to screen coordinates.
 *
 * @param rect The rectangle to convert.
 * @return The converted rectangle in screen coordinates.
 */
BRect
BView::ConvertToScreen(BRect rect) const
{
	ConvertToScreen(&rect);

	return rect;
}


/**
 * @brief Converts @p rect from screen coordinates to view-local in place.
 *
 * @param rect The rectangle to convert; modified in place.
 */
void
BView::ConvertFromScreen(BRect* rect) const
{
	BPoint offset(0.0, 0.0);
	ConvertFromScreen(&offset);
	rect->OffsetBy(offset);
}


/**
 * @brief Returns @p rect converted from screen coordinates to view-local.
 *
 * @param rect The rectangle to convert.
 * @return The converted rectangle in view-local coordinates.
 */
BRect
BView::ConvertFromScreen(BRect rect) const
{
	ConvertFromScreen(&rect);

	return rect;
}


/**
 * @brief Returns the view's flag bits, excluding the internal resizing mask.
 *
 * @return The current view flags (B_WILL_DRAW, B_FRAME_EVENTS, etc.).
 */
uint32
BView::Flags() const
{
	_CheckLock();
	return fFlags & ~_RESIZE_MASK_;
}


/**
 * @brief Replaces the view's flag bits.
 *
 * If the view is attached to a window, relevant flag changes (B_WILL_DRAW,
 * B_FULL_UPDATE_ON_RESIZE, B_FRAME_EVENTS, etc.) are forwarded to the
 * app_server immediately.  Enabling B_PULSE_NEEDED also ensures the window's
 * pulse runner is started.
 *
 * @param flags The new flag set; the resizing mask bits are preserved from the
 *              existing value and must not be included in @p flags.
 */
void
BView::SetFlags(uint32 flags)
{
	if (Flags() == flags)
		return;

	if (fOwner) {
		if (flags & B_PULSE_NEEDED) {
			_CheckLock();
			if (fOwner->fPulseRunner == NULL)
				fOwner->SetPulseRate(fOwner->PulseRate());
		}

		uint32 changesFlags = flags ^ fFlags;
		if (changesFlags & (B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE
				| B_FRAME_EVENTS | B_SUBPIXEL_PRECISE
				| B_TRANSPARENT_BACKGROUND)) {
			_CheckLockAndSwitchCurrent();

			fOwner->fLink->StartMessage(AS_VIEW_SET_FLAGS);
			fOwner->fLink->Attach<uint32>(flags);
			fOwner->fLink->Flush();
		}
	}

	/* Some useful info:
		fFlags is a unsigned long (32 bits)
		* bits 1-16 are used for BView's flags
		* bits 17-32 are used for BView' resize mask
		* _RESIZE_MASK_ is used for that. Look into View.h to see how
			it's defined
	*/
	fFlags = (flags & ~_RESIZE_MASK_) | (fFlags & _RESIZE_MASK_);

	fState->archiving_flags |= B_VIEW_FLAGS_BIT;
}


/**
 * @brief Returns the view's frame rectangle in its parent's coordinate system.
 *
 * The frame is the bounds rectangle offset to the view's position within
 * its parent (fParentOffset).
 *
 * @return The frame rectangle in parent-view coordinates.
 */
BRect
BView::Frame() const
{
	return Bounds().OffsetToCopy(fParentOffset.x, fParentOffset.y);
}


/**
 * @brief Hides the view by incrementing the show-level counter.
 *
 * When the show level transitions from 0 to 1 the view becomes hidden:
 * the app_server is notified (AS_VIEW_HIDE) and the parent layout is
 * invalidated so it can reclaim the space.
 *
 * @note Calls to Hide() must be balanced by equal calls to Show().
 * @see BView::Show(), BView::IsHidden()
 */
void
BView::Hide()
{
	if (fOwner && fShowLevel == 0) {
		_CheckLockAndSwitchCurrent();
		fOwner->fLink->StartMessage(AS_VIEW_HIDE);
		fOwner->fLink->Flush();
	}
	fShowLevel++;

	if (fShowLevel == 1)
		_InvalidateParentLayout();
}


/**
 * @brief Shows the view by decrementing the show-level counter.
 *
 * When the show level transitions from 1 to 0 the view becomes visible:
 * the app_server is notified (AS_VIEW_SHOW) and the parent layout is
 * invalidated so it can allocate space for the view again.
 *
 * @note Calls to Show() must balance prior calls to Hide().
 * @see BView::Hide(), BView::IsHidden()
 */
void
BView::Show()
{
	fShowLevel--;
	if (fOwner && fShowLevel == 0) {
		_CheckLockAndSwitchCurrent();
		fOwner->fLink->StartMessage(AS_VIEW_SHOW);
		fOwner->fLink->Flush();
	}

	if (fShowLevel == 0)
		_InvalidateParentLayout();
}


/**
 * @brief Returns true if this view is the current keyboard focus.
 *
 * @return True when the owning window's current focus view is this view.
 */
bool
BView::IsFocus() const
{
	if (fOwner) {
		_CheckLock();
		return fOwner->CurrentFocus() == this;
	} else
		return false;
}


/**
 * @brief Returns whether this view is hidden from a given observer's perspective.
 *
 * A view is hidden if its show level is greater than zero, or if any ancestor
 * view (up to @p lookingFrom) is hidden.  When @p lookingFrom is NULL the
 * window's own visibility is also considered.
 *
 * @param lookingFrom The view acting as the observer, or NULL for global visibility.
 * @return True if the view is hidden from the observer's perspective.
 */
bool
BView::IsHidden(const BView* lookingFrom) const
{
	if (fShowLevel > 0)
		return true;

	// may we be egocentric?
	if (lookingFrom == this)
		return false;

	// we have the same visibility state as our
	// parent, if there is one
	if (fParent)
		return fParent->IsHidden(lookingFrom);

	// if we're the top view, and we're interested
	// in the "global" view, we're inheriting the
	// state of the window's visibility
	if (fOwner && lookingFrom == NULL)
		return fOwner->IsHidden();

	return false;
}


/**
 * @brief Returns whether this view is globally hidden.
 *
 * Equivalent to IsHidden(NULL): considers the view's own show level, all
 * ancestor views, and the owning window's visibility.
 *
 * @return True if the view is not visible on screen.
 */
bool
BView::IsHidden() const
{
	return IsHidden(NULL);
}


/**
 * @brief Returns true while the view is in the middle of a print job.
 *
 * @return True when the view's drawing calls are being recorded for printing.
 */
bool
BView::IsPrinting() const
{
	return fIsPrinting;
}


/**
 * @brief Returns the top-left corner of the view's bounds rectangle.
 *
 * @return The origin of the view in view-local coordinates (the scroll offset).
 */
BPoint
BView::LeftTop() const
{
	return Bounds().LeftTop();
}


/**
 * @brief Sets the view's resizing mode (how it follows its parent when resized).
 *
 * Stores the new mode in the lower bits of fFlags and notifies the app_server
 * if the view is currently attached to a window.
 *
 * @param mode A combination of B_FOLLOW_* constants.
 * @see BView::ResizingMode()
 */
void
BView::SetResizingMode(uint32 mode)
{
	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_RESIZE_MODE);
		fOwner->fLink->Attach<uint32>(mode);
	}

	// look at SetFlags() for more info on the below line
	fFlags = (fFlags & ~_RESIZE_MASK_) | (mode & _RESIZE_MASK_);
}


/**
 * @brief Returns the current resizing mode of the view.
 *
 * @return The resizing mode bits (B_FOLLOW_* constants) stored in fFlags.
 */
uint32
BView::ResizingMode() const
{
	return fFlags & _RESIZE_MASK_;
}


/**
 * @brief Sets the mouse cursor to display when the pointer is over this view.
 *
 * Sends AS_SET_VIEW_CURSOR to the app_server with the cursor's server token.
 * If @p sync is true, blocks until the server has processed the request to
 * guarantee the cursor change takes effect before the function returns.
 *
 * @param cursor The cursor to use; ignored if NULL.
 * @param sync   If true, flush and wait for the server to acknowledge.
 */
void
BView::SetViewCursor(const BCursor* cursor, bool sync)
{
	if (cursor == NULL || fOwner == NULL)
		return;

	_CheckLock();

	ViewSetViewCursorInfo info;
	info.cursorToken = cursor->fServerToken;
	info.viewToken = _get_object_token_(this);
	info.sync = sync;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_VIEW_CURSOR);
	link.Attach<ViewSetViewCursorInfo>(info);

	if (sync) {
		// Make sure the server has processed the message.
		int32 code;
		link.FlushWithReply(code);
	}
}


/**
 * @brief Flushes the owning window's command buffer to the app_server.
 *
 * Delegates to BWindow::Flush().  Has no effect if the view is not attached
 * to a window.
 */
void
BView::Flush() const
{
	if (fOwner)
		fOwner->Flush();
}


/**
 * @brief Flushes the command buffer and waits for the app_server to finish.
 *
 * Delegates to BWindow::Sync().  Blocks until all previously issued drawing
 * commands have been processed by the server.
 */
void
BView::Sync() const
{
	_CheckOwnerLock();
	if (fOwner)
		fOwner->Sync();
}


/**
 * @brief Returns the BWindow this view is attached to, or NULL.
 *
 * @return The owning BWindow, or NULL if the view is not currently attached.
 */
BWindow*
BView::Window() const
{
	return fOwner;
}


//	#pragma mark - Hook Functions


/**
 * @brief Hook called when the view is added to a window's view hierarchy.
 *
 * Called after the view has been associated with a BWindow and the server-side
 * view object has been created.  Override to perform one-time setup that
 * requires a valid Window() (e.g. adopting parent colours, computing preferred
 * sizes, starting timers).
 *
 * @note The default implementation does nothing.
 * @see BView::AllAttached(), BView::DetachedFromWindow()
 */
void
BView::AttachedToWindow()
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::AttachedToWindow()\n", Name()));
}


/**
 * @brief Hook called after the view and all its children have been attached.
 *
 * Called once AttachedToWindow() has been called for this view and every
 * descendant.  Override to perform setup that depends on child views already
 * being fully attached.
 *
 * @note The default implementation does nothing.
 * @see BView::AttachedToWindow()
 */
void
BView::AllAttached()
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::AllAttached()\n", Name()));
}


/**
 * @brief Hook called when the view is about to be removed from its window.
 *
 * Override to release resources that were acquired in AttachedToWindow() or
 * that require a valid Window() to clean up (e.g. stopping timers, releasing
 * server-side objects).
 *
 * @note The default implementation does nothing.
 * @see BView::AllDetached(), BView::AttachedToWindow()
 */
void
BView::DetachedFromWindow()
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::DetachedFromWindow()\n", Name()));
}


/**
 * @brief Hook called after the view and all its children have been detached.
 *
 * Override to perform cleanup that depends on all descendant views having
 * already received their DetachedFromWindow() call.
 *
 * @note The default implementation does nothing.
 * @see BView::DetachedFromWindow()
 */
void
BView::AllDetached()
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::AllDetached()\n", Name()));
}


/**
 * @brief Hook called to draw the view's content.
 *
 * Override to perform all custom drawing for this view.  The drawing state
 * is pushed before this call and popped after, so any state changes are
 * automatically reverted.  @p updateRect is the invalid area expressed in
 * the view's local coordinate system.
 *
 * @param updateRect The rectangle that needs to be redrawn.
 *
 * @note The default implementation does nothing.
 * @see BView::DrawAfterChildren(), BView::Invalidate()
 */
void
BView::Draw(BRect updateRect)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::Draw()\n", Name()));
}


/**
 * @brief Hook called to draw over the view's children.
 *
 * Override when B_DRAW_ON_CHILDREN is set in the view flags.  This hook is
 * called after all child views have been drawn, allowing a parent to paint
 * decorations on top of its children.
 *
 * @param updateRect The rectangle that needs to be redrawn, in view coordinates.
 *
 * @note The default implementation does nothing.
 * @see BView::Draw()
 */
void
BView::DrawAfterChildren(BRect updateRect)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::DrawAfterChildren()\n", Name()));
}


/**
 * @brief Hook called when the view's frame position changes.
 *
 * Invoked after the view has been moved within its parent.  Override to
 * react to position changes (e.g. updating cached screen coordinates).
 *
 * @param newPosition The new top-left corner in the parent's coordinate system.
 *
 * @note Requires B_FRAME_EVENTS in the view flags to be delivered.
 */
void
BView::FrameMoved(BPoint newPosition)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::FrameMoved()\n", Name()));
}


/**
 * @brief Hook called when the view's frame size changes.
 *
 * Invoked after the view has been resized.  Override to adapt the view's
 * content to the new dimensions (e.g. repositioning child views that do not
 * use automatic layout).
 *
 * @param newWidth  The new width of the view's bounds rectangle.
 * @param newHeight The new height of the view's bounds rectangle.
 *
 * @note Requires B_FRAME_EVENTS in the view flags to be delivered.
 */
void
BView::FrameResized(float newWidth, float newHeight)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::FrameResized()\n", Name()));
}


/**
 * @brief Returns the view's preferred size based on its current bounds.
 *
 * The default implementation returns the current bounds dimensions.  Override
 * to provide a meaningful preferred size for layout negotiation.
 *
 * @param _width  Receives the preferred width; may be NULL.
 * @param _height Receives the preferred height; may be NULL.
 *
 * @see BView::ResizeToPreferred(), BView::MinSize(), BView::MaxSize()
 */
void
BView::GetPreferredSize(float* _width, float* _height)
{
	STRACE(("\tHOOK: BView(%s)::GetPreferredSize()\n", Name()));

	if (_width != NULL)
		*_width = fBounds.Width();
	if (_height != NULL)
		*_height = fBounds.Height();
}


/**
 * @brief Resizes the view to its preferred size.
 *
 * Calls GetPreferredSize() and then ResizeTo() with the returned dimensions.
 *
 * @see BView::GetPreferredSize()
 */
void
BView::ResizeToPreferred()
{
	STRACE(("\tHOOK: BView(%s)::ResizeToPreferred()\n", Name()));

	float width;
	float height;
	GetPreferredSize(&width, &height);

	ResizeTo(width, height);
}


/**
 * @brief Hook called when a key is pressed while this view has the focus.
 *
 * The default implementation forwards navigation keys to the window's keyboard
 * navigation system.  Override to handle key input; call the base class for
 * keys you do not handle to preserve tab/arrow navigation.
 *
 * @param bytes    UTF-8 encoded bytes for the key, not NUL-terminated.
 * @param numBytes Number of bytes in @p bytes.
 *
 * @see BView::KeyUp(), BView::MakeFocus()
 */
void
BView::KeyDown(const char* bytes, int32 numBytes)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::KeyDown()\n", Name()));

	if (Window())
		Window()->_KeyboardNavigation();
}


/**
 * @brief Hook called when a key is released while this view has the focus.
 *
 * @param bytes    UTF-8 encoded bytes for the key, not NUL-terminated.
 * @param numBytes Number of bytes in @p bytes.
 *
 * @note The default implementation does nothing.
 * @see BView::KeyDown()
 */
void
BView::KeyUp(const char* bytes, int32 numBytes)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::KeyUp()\n", Name()));
}


/**
 * @brief Hook called when a mouse button is pressed over this view.
 *
 * @param where The location of the click in view-local coordinates.
 *
 * @note The default implementation does nothing.
 * @see BView::MouseUp(), BView::MouseMoved(), BView::SetMouseEventMask()
 */
void
BView::MouseDown(BPoint where)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::MouseDown()\n", Name()));
}


/**
 * @brief Hook called when a mouse button is released.
 *
 * @param where The location of the release in view-local coordinates.
 *
 * @note The default implementation does nothing.
 * @see BView::MouseDown()
 */
void
BView::MouseUp(BPoint where)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::MouseUp()\n", Name()));
}


/**
 * @brief Hook called when the mouse moves over this view.
 *
 * @param where       Current mouse position in view-local coordinates.
 * @param code        Transit code: B_ENTERED_VIEW, B_INSIDE_VIEW,
 *                    B_EXITED_VIEW, or B_OUTSIDE_VIEW.
 * @param dragMessage The drag-and-drop message if a drag is in progress,
 *                    otherwise NULL.
 *
 * @note The default implementation does nothing.
 * @see BView::MouseDown(), BView::DragMessage()
 */
void
BView::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::MouseMoved()\n", Name()));
}


/**
 * @brief Hook called at the window's pulse rate when B_PULSE_NEEDED is set.
 *
 * Override to perform periodic animations or state updates.
 *
 * @note The default implementation does nothing.
 * @see BWindow::SetPulseRate()
 */
void
BView::Pulse()
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::Pulse()\n", Name()));
}


/**
 * @brief Hook called when this view becomes the target of a BScrollView.
 *
 * Override to react to being enclosed in a scroll view, for example to adjust
 * the document range or initial scroll position.
 *
 * @param scroll_view The BScrollView that is now targeting this view.
 *
 * @note The default implementation does nothing.
 * @see BScrollView
 */
void
BView::TargetedByScrollView(BScrollView* scroll_view)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::TargetedByScrollView()\n", Name()));
}


/**
 * @brief Hook called when the owning window gains or loses focus.
 *
 * Override to change the view's appearance when the window becomes active or
 * inactive (e.g. dimming a selection highlight).
 *
 * @param active True if the window has just become active.
 *
 * @note The default implementation does nothing.
 */
void
BView::WindowActivated(bool active)
{
	// Hook function
	STRACE(("\tHOOK: BView(%s)::WindowActivated()\n", Name()));
}


//	#pragma mark - Input Functions


/**
 * @brief Begins server-side rubber-band rectangle tracking.
 *
 * Sends AS_VIEW_BEGIN_RECT_TRACK to the app_server, which draws and animates
 * the tracking rectangle on screen without further client-side involvement.
 *
 * @param startRect The initial tracking rectangle in view-local coordinates.
 * @param style     Tracking style: B_TRACK_WHOLE_RECT or B_TRACK_RECT_CORNER.
 *
 * @see BView::EndRectTracking()
 */
void
BView::BeginRectTracking(BRect startRect, uint32 style)
{
	if (_CheckOwnerLockAndSwitchCurrent()) {
		fOwner->fLink->StartMessage(AS_VIEW_BEGIN_RECT_TRACK);
		fOwner->fLink->Attach<BRect>(startRect);
		fOwner->fLink->Attach<uint32>(style);
		fOwner->fLink->Flush();
	}
}


/**
 * @brief Ends server-side rubber-band rectangle tracking.
 *
 * Sends AS_VIEW_END_RECT_TRACK to the app_server to stop drawing the tracking
 * rectangle started by BeginRectTracking().
 *
 * @see BView::BeginRectTracking()
 */
void
BView::EndRectTracking()
{
	if (_CheckOwnerLockAndSwitchCurrent()) {
		fOwner->fLink->StartMessage(AS_VIEW_END_RECT_TRACK);
		fOwner->fLink->Flush();
	}
}


/**
 * @brief Initiates a drag-and-drop operation using a dashed-border rectangle.
 *
 * Constructs a RGBA32 bitmap with a dashed-border appearance matching
 * @p dragRect and calls the bitmap-based DragMessage() overload.  The drag
 * offset is computed from the current mouse position relative to the
 * rectangle's top-left corner.
 *
 * @param message  The drag payload; must not be NULL.
 * @param dragRect The rectangle whose dashed outline is used as the drag image.
 * @param replyTo  The handler to receive the B_COPY/B_MOVE reply, or NULL to
 *                 use this view.
 *
 * @see BView::DragMessage(BMessage*, BBitmap*, BPoint, BHandler*)
 */
void
BView::DragMessage(BMessage* message, BRect dragRect, BHandler* replyTo)
{
	if (!message)
		return;

	_CheckOwnerLock();

	// calculate the offset
	BPoint offset;
	uint32 buttons;
	BMessage* current = fOwner->CurrentMessage();
	if (!current || current->FindPoint("be:view_where", &offset) != B_OK)
		GetMouse(&offset, &buttons, false);
	offset -= dragRect.LeftTop();

	if (!dragRect.IsValid()) {
		DragMessage(message, NULL, B_OP_BLEND, offset, replyTo);
		return;
	}

	// TODO: that's not really what should happen - the app_server should take
	// the chance *NOT* to need to drag a whole bitmap around but just a frame.

	// create a drag bitmap for the rect
	BBitmap* bitmap = new(std::nothrow) BBitmap(dragRect, B_RGBA32);
	if (bitmap == NULL)
		return;

	uint32* bits = (uint32*)bitmap->Bits();
	uint32 bytesPerRow = bitmap->BytesPerRow();
	uint32 width = dragRect.IntegerWidth() + 1;
	uint32 height = dragRect.IntegerHeight() + 1;
	uint32 lastRow = (height - 1) * width;

	memset(bits, 0x00, height * bytesPerRow);

	// top
	for (uint32 i = 0; i < width; i += 2)
		bits[i] = 0xff000000;

	// bottom
	for (uint32 i = (height % 2 == 0 ? 1 : 0); i < width; i += 2)
		bits[lastRow + i] = 0xff000000;

	// left
	for (uint32 i = 0; i < lastRow; i += width * 2)
		bits[i] = 0xff000000;

	// right
	for (uint32 i = (width % 2 == 0 ? width : 0); i < lastRow; i += width * 2)
		bits[width - 1 + i] = 0xff000000;

	DragMessage(message, bitmap, B_OP_BLEND, offset, replyTo);
}


/**
 * @brief Initiates a drag-and-drop operation using a bitmap image (B_OP_COPY).
 *
 * Convenience overload that uses B_OP_COPY as the drawing mode.
 *
 * @param message The drag payload; must not be NULL.
 * @param image   The drag image; ownership is transferred to the server.
 * @param offset  The offset from the drag image origin to the pointer hotspot.
 * @param replyTo The handler to receive the reply, or NULL to use this view.
 */
void
BView::DragMessage(BMessage* message, BBitmap* image, BPoint offset,
	BHandler* replyTo)
{
	DragMessage(message, image, B_OP_COPY, offset, replyTo);
}


/**
 * @brief Initiates a drag-and-drop operation with full control over the drag image.
 *
 * Flattens @p message and sends AS_VIEW_DRAG_IMAGE to the app_server together
 * with the bitmap's server token, drawing mode, and pointer offset.  The call
 * blocks until the server has processed the request so the bitmap can be safely
 * deleted afterwards.  If @p image is NULL a 1x1 transparent bitmap is created
 * automatically.
 *
 * @param message  The drag payload; must not be NULL.
 * @param image    The bitmap to render under the pointer; ownership is transferred.
 * @param dragMode The drawing mode used to composite the drag image.
 * @param offset   The offset from the bitmap's top-left to the pointer hotspot.
 * @param replyTo  The handler for the drop reply, or NULL to use this view.
 */
void
BView::DragMessage(BMessage* message, BBitmap* image,
	drawing_mode dragMode, BPoint offset, BHandler* replyTo)
{
	if (message == NULL)
		return;

	if (image == NULL) {
		// TODO: workaround for drags without a bitmap - should not be necessary if
		//	we move the rectangle dragging into the app_server
		image = new(std::nothrow) BBitmap(BRect(0, 0, 0, 0), B_RGBA32);
		if (image == NULL)
			return;
	}

	if (replyTo == NULL)
		replyTo = this;

	if (replyTo->Looper() == NULL)
		debugger("DragMessage: warning - the Handler needs a looper");

	_CheckOwnerLock();

	if (!message->HasInt32("buttons")) {
		BMessage* msg = fOwner->CurrentMessage();
		uint32 buttons;

		if (msg == NULL
			|| msg->FindInt32("buttons", (int32*)&buttons) != B_OK) {
			BPoint point;
			GetMouse(&point, &buttons, false);
		}

		message->AddInt32("buttons", buttons);
	}

	BMessage::Private privateMessage(message);
	privateMessage.SetReply(BMessenger(replyTo, replyTo->Looper()));

	int32 bufferSize = message->FlattenedSize();
	char* buffer = new(std::nothrow) char[bufferSize];
	if (buffer != NULL) {
		message->Flatten(buffer, bufferSize);

		fOwner->fLink->StartMessage(AS_VIEW_DRAG_IMAGE);
		fOwner->fLink->Attach<int32>(image->_ServerToken());
		fOwner->fLink->Attach<int32>((int32)dragMode);
		fOwner->fLink->Attach<BPoint>(offset);
		fOwner->fLink->Attach<int32>(bufferSize);
		fOwner->fLink->Attach(buffer, bufferSize);

		// we need to wait for the server
		// to actually process this message
		// before we can delete the bitmap
		int32 code;
		fOwner->fLink->FlushWithReply(code);

		delete [] buffer;
	} else {
		fprintf(stderr, "BView::DragMessage() - no memory to flatten drag "
			"message\n");
	}

	delete image;
}


/**
 * @brief Retrieves the current mouse position and button state.
 *
 * When @p checkMessageQueue is true (the default) the window's message queue
 * is scanned for a recent B_MOUSE_MOVED, B_MOUSE_UP, or B_MOUSE_DOWN message.
 * If one is found (and not stale beyond 10 ms for B_MOUSE_MOVED) its data is
 * returned without a round-trip to the server.  Otherwise, or when
 * B_NO_POINTER_HISTORY is set, the server is queried directly.
 *
 * @param _location         Receives the pointer position in view-local
 *                          coordinates; may be NULL.
 * @param _buttons          Receives the current button bitmask; may be NULL.
 * @param checkMessageQueue If true, search the message queue before querying
 *                          the server.
 */
void
BView::GetMouse(BPoint* _location, uint32* _buttons, bool checkMessageQueue)
{
	if (_location == NULL && _buttons == NULL)
		return;

	_CheckOwnerLockAndSwitchCurrent();

	uint32 eventOptions = fEventOptions | fMouseEventOptions;
	bool noHistory = eventOptions & B_NO_POINTER_HISTORY;
	bool fullHistory = eventOptions & B_FULL_POINTER_HISTORY;

	if (checkMessageQueue && !noHistory) {
		Window()->UpdateIfNeeded();
		BMessageQueue* queue = Window()->MessageQueue();
		queue->Lock();

		// Look out for mouse update messages

		BMessage* message;
		for (int32 i = 0; (message = queue->FindMessage(i)) != NULL; i++) {
			switch (message->what) {
				case B_MOUSE_MOVED:
				case B_MOUSE_UP:
				case B_MOUSE_DOWN:
					bool deleteMessage;
					if (!Window()->_StealMouseMessage(message, deleteMessage))
						continue;

					if (!fullHistory && message->what == B_MOUSE_MOVED) {
						// Check if the message is too old. Some applications
						// check the message queue in such a way that mouse
						// messages *must* pile up. This check makes them work
						// as intended, although these applications could simply
						// use the version of BView::GetMouse() that does not
						// check the history. Also note that it isn't a problem
						// to delete the message in case there is not a newer
						// one. If we don't find a message in the queue, we will
						// just fall back to asking the app_sever directly. So
						// the imposed delay will not be a problem on slower
						// computers. This check also prevents another problem,
						// when the message that we use is *not* removed from
						// the queue. Subsequent calls to GetMouse() would find
						// this message over and over!
						bigtime_t eventTime;
						if (message->FindInt64("when", &eventTime) == B_OK
							&& system_time() - eventTime > 10000) {
							// just discard the message
							if (deleteMessage)
								delete message;
							continue;
						}
					}
					if (_location != NULL)
						message->FindPoint("screen_where", _location);
					if (_buttons != NULL)
						message->FindInt32("buttons", (int32*)_buttons);
					queue->Unlock();
						// we need to hold the queue lock until here, because
						// the message might still be used for something else

					if (_location != NULL)
						ConvertFromScreen(_location);

					if (deleteMessage)
						delete message;

					return;
			}
		}
		queue->Unlock();
	}

	// If no mouse update message has been found in the message queue,
	// we get the current mouse location and buttons from the app_server

	fOwner->fLink->StartMessage(AS_GET_MOUSE);

	int32 code;
	if (fOwner->fLink->FlushWithReply(code) == B_OK
		&& code == B_OK) {
		BPoint location;
		uint32 buttons;
		fOwner->fLink->Read<BPoint>(&location);
		fOwner->fLink->Read<uint32>(&buttons);
			// TODO: ServerWindow replies with an int32 here

		ConvertFromScreen(&location);
			// TODO: in beos R5, location is already converted to the view
			// local coordinate system, so if an app checks the window message
			// queue by itself, it might not find what it expects.
			// NOTE: the fact that we have mouse coords in screen space in our
			// queue avoids the problem that messages already in the queue will
			// be outdated as soon as a window or even the view moves. The
			// second situation being quite common actually, also with regards
			// to scrolling. An app reading these messages would have to know
			// the locations of the window and view for each message...
			// otherwise it is potentially broken anyways.
		if (_location != NULL)
			*_location = location;
		if (_buttons != NULL)
			*_buttons = buttons;
	} else {
		if (_location != NULL)
			_location->Set(0, 0);
		if (_buttons != NULL)
			*_buttons = 0;
	}
}


/**
 * @brief Gives or removes keyboard focus for this view.
 *
 * When @p focus is true any previously focused view in the same window is
 * defocused first.  When @p focus is false this view is defocused only if it
 * currently holds focus.
 *
 * @param focus True to acquire focus, false to release it.
 *
 * @see BView::IsFocus(), BWindow::CurrentFocus()
 */
void
BView::MakeFocus(bool focus)
{
	if (fOwner == NULL)
		return;

	// TODO: If this view has focus and focus == false,
	// will there really be no other view with focus? No
	// cycling to the next one?
	BView* focusView = fOwner->CurrentFocus();
	if (focus) {
		// Unfocus a previous focus view
		if (focusView != NULL && focusView != this)
			focusView->MakeFocus(false);

		// if we want to make this view the current focus view
		fOwner->_SetFocus(this, true);
	} else {
		// we want to unfocus this view, but only if it actually has focus
		if (focusView == this)
			fOwner->_SetFocus(NULL, true);
	}
}


/**
 * @brief Returns the scroll bar attached to this view for the given orientation.
 *
 * @param direction B_VERTICAL or B_HORIZONTAL.
 * @return The associated BScrollBar, or NULL if none is attached.
 */
BScrollBar*
BView::ScrollBar(orientation direction) const
{
	switch (direction) {
		case B_VERTICAL:
			return fVerScroller;

		case B_HORIZONTAL:
			return fHorScroller;

		default:
			return NULL;
	}
}


/**
 * @brief Scrolls the view's contents by the given delta.
 *
 * Delegates to ScrollTo() after adding the delta to the current scroll origin.
 *
 * @param deltaX Horizontal scroll delta in points.
 * @param deltaY Vertical scroll delta in points.
 *
 * @see BView::ScrollTo()
 */
void
BView::ScrollBy(float deltaX, float deltaY)
{
	ScrollTo(BPoint(fBounds.left + deltaX, fBounds.top + deltaY));
}


/**
 * @brief Scrolls the view so that the given point is at the top-left.
 *
 * The target point is rounded to integer pixel coordinates and clamped to
 * the range of any attached scroll bars.  Both the local bounds rectangle
 * and the scroll bars are updated, and the app_server is notified via
 * AS_VIEW_SCROLL.
 *
 * @param where The desired new scroll origin (bounds top-left) in view coordinates.
 *
 * @see BView::ScrollBy()
 */
void
BView::ScrollTo(BPoint where)
{
	// scrolling by fractional values is not supported
	where.x = roundf(where.x);
	where.y = roundf(where.y);

	// no reason to process this further if no scroll is intended.
	if (where.x == fBounds.left && where.y == fBounds.top)
		return;

	// make sure scrolling is within valid bounds
	if (fHorScroller) {
		float min, max;
		fHorScroller->GetRange(&min, &max);

		if (where.x < min)
			where.x = min;
		else if (where.x > max)
			where.x = max;
	}
	if (fVerScroller) {
		float min, max;
		fVerScroller->GetRange(&min, &max);

		if (where.y < min)
			where.y = min;
		else if (where.y > max)
			where.y = max;
	}

	_CheckLockAndSwitchCurrent();

	float xDiff = where.x - fBounds.left;
	float yDiff = where.y - fBounds.top;

	// if we're attached to a window tell app_server about this change
	if (fOwner) {
		fOwner->fLink->StartMessage(AS_VIEW_SCROLL);
		fOwner->fLink->Attach<float>(xDiff);
		fOwner->fLink->Attach<float>(yDiff);

		fOwner->fLink->Flush();

//		fState->valid_flags &= ~B_VIEW_FRAME_BIT;
	}

	// we modify our bounds rectangle by deltaX/deltaY coord units hor/ver.
	fBounds.OffsetTo(where.x, where.y);

	// then set the new values of the scrollbars
	if (fHorScroller && xDiff != 0.0)
		fHorScroller->SetValue(fBounds.left);
	if (fVerScroller && yDiff != 0.0)
		fVerScroller->SetValue(fBounds.top);

}


/**
 * @brief Sets the view's persistent event mask.
 *
 * Determines which events the view receives even when the pointer is outside
 * its bounds or it does not have keyboard focus.  Changes are sent to the
 * app_server immediately when the view is attached to a window.
 *
 * @param mask    A combination of B_POINTER_EVENTS, B_KEYBOARD_EVENTS, etc.
 * @param options Delivery options (B_NO_POINTER_HISTORY, B_FULL_POINTER_HISTORY).
 * @return B_OK always.
 *
 * @see BView::SetMouseEventMask(), BView::EventMask()
 */
status_t
BView::SetEventMask(uint32 mask, uint32 options)
{
	if (fEventMask == mask && fEventOptions == options)
		return B_OK;

	// don't change the mask if it's zero and we've got options
	if (mask != 0 || options == 0)
		fEventMask = mask | (fEventMask & 0xffff0000);
	fEventOptions = options;

	fState->archiving_flags |= B_VIEW_EVENT_MASK_BIT;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_EVENT_MASK);
		fOwner->fLink->Attach<uint32>(mask);
		fOwner->fLink->Attach<uint32>(options);
		fOwner->fLink->Flush();
	}

	return B_OK;
}


/**
 * @brief Returns the view's current persistent event mask.
 *
 * @return The event mask set by SetEventMask().
 */
uint32
BView::EventMask()
{
	return fEventMask;
}


/**
 * @brief Temporarily extends the event mask for the duration of a mouse drag.
 *
 * Must be called from within a MouseDown() handler.  The extended mask and
 * options apply only until the mouse button is released; after that, the
 * view reverts to its persistent event mask.
 *
 * @param mask    Additional event types to receive during the drag.
 * @param options Delivery options (B_NO_POINTER_HISTORY, B_LOCK_WINDOW_FOCUS).
 * @return B_OK if called from a valid MouseDown() context, B_ERROR otherwise.
 *
 * @see BView::SetEventMask()
 */
status_t
BView::SetMouseEventMask(uint32 mask, uint32 options)
{
	// Just don't do anything if the view is not yet attached
	// or we were called outside of BView::MouseDown()
	if (fOwner != NULL
		&& fOwner->CurrentMessage() != NULL
		&& fOwner->CurrentMessage()->what == B_MOUSE_DOWN) {
		_CheckLockAndSwitchCurrent();
		fMouseEventOptions = options;

		fOwner->fLink->StartMessage(AS_VIEW_SET_MOUSE_EVENT_MASK);
		fOwner->fLink->Attach<uint32>(mask);
		fOwner->fLink->Attach<uint32>(options);
		fOwner->fLink->Flush();
		return B_OK;
	}

	return B_ERROR;
}


//	#pragma mark - Graphic State Functions


/**
 * @brief Saves the current drawing state onto the server-side state stack.
 *
 * Sends AS_VIEW_PUSH_STATE to the app_server.  The new state starts with
 * origin (0,0), scale 1.0, and identity transform; all other attributes
 * are inherited from the parent state.  Must be balanced by a call to
 * PopState().
 *
 * @see BView::PopState()
 */
void
BView::PushState()
{
	_CheckOwnerLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_VIEW_PUSH_STATE);

	fState->valid_flags &= ~B_VIEW_PARENT_COMPOSITE_BIT;

	// initialize origin, scale and transform, new states start "clean".
	fState->valid_flags |= B_VIEW_SCALE_BIT | B_VIEW_ORIGIN_BIT
		| B_VIEW_TRANSFORM_BIT;
	fState->scale = 1.0f;
	fState->origin.Set(0, 0);
	fState->transform.Reset();
}


/**
 * @brief Restores the drawing state previously saved by PushState().
 *
 * Sends AS_VIEW_POP_STATE to the app_server and invalidates all local state
 * caches except B_VIEW_VIEW_COLOR_BIT, forcing them to be re-fetched from
 * the server on next use.
 *
 * @see BView::PushState()
 */
void
BView::PopState()
{
	_CheckOwnerLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_VIEW_POP_STATE);
	_FlushIfNotInTransaction();

	// invalidate all flags (except those that are not part of pop/push)
	fState->valid_flags = B_VIEW_VIEW_COLOR_BIT;
}


/**
 * @brief Sets the local coordinate system origin to @p where.
 *
 * @param where The new origin in current view coordinates.
 * @see BView::SetOrigin(float, float)
 */
void
BView::SetOrigin(BPoint where)
{
	SetOrigin(where.x, where.y);
}


/**
 * @brief Sets the local coordinate system origin.
 *
 * Moves the drawing origin so that subsequent drawing at (0,0) maps to
 * (@p x, @p y) in the previous coordinate system.  No-ops if the origin
 * is already at the requested position.
 *
 * @param x New origin x coordinate.
 * @param y New origin y coordinate.
 *
 * @see BView::Origin(), BView::PushState()
 */
void
BView::SetOrigin(float x, float y)
{
	if (fState->IsValid(B_VIEW_ORIGIN_BIT)
		&& x == fState->origin.x && y == fState->origin.y)
		return;

	fState->origin.x = x;
	fState->origin.y = y;

	if (_CheckOwnerLockAndSwitchCurrent()) {
		fOwner->fLink->StartMessage(AS_VIEW_SET_ORIGIN);
		fOwner->fLink->Attach<float>(x);
		fOwner->fLink->Attach<float>(y);

		fState->valid_flags |= B_VIEW_ORIGIN_BIT;
	}

	// our local coord system origin has changed, so when archiving we'll add
	// this too
	fState->archiving_flags |= B_VIEW_ORIGIN_BIT;
}


/**
 * @brief Returns the current local coordinate system origin.
 *
 * If the cached origin is invalid (e.g. after PopState()) it is re-fetched
 * from the app_server via AS_VIEW_GET_ORIGIN.
 *
 * @return The current origin point.
 */
BPoint
BView::Origin() const
{
	if (!fState->IsValid(B_VIEW_ORIGIN_BIT)) {
		// we don't keep graphics state information, therefor
		// we need to ask the server for the origin after PopState()
		_CheckOwnerLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_ORIGIN);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK && code == B_OK)
			fOwner->fLink->Read<BPoint>(&fState->origin);

		fState->valid_flags |= B_VIEW_ORIGIN_BIT;
	}

	return fState->origin;
}


/**
 * @brief Sets the uniform scale factor for the current drawing state.
 *
 * Scales all subsequent drawing operations by @p scale relative to the
 * current origin.  No-ops if the scale is already at the requested value.
 *
 * @param scale The new scale factor (1.0 = no scaling).
 *
 * @see BView::Scale(), BView::PushState()
 */
void
BView::SetScale(float scale) const
{
	if (fState->IsValid(B_VIEW_SCALE_BIT) && scale == fState->scale)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_SCALE);
		fOwner->fLink->Attach<float>(scale);

		fState->valid_flags |= B_VIEW_SCALE_BIT;
	}

	fState->scale = scale;
	fState->archiving_flags |= B_VIEW_SCALE_BIT;
}


/**
 * @brief Returns the current drawing scale factor.
 *
 * Fetches the scale from the app_server if the local cache is invalid.
 *
 * @return The current scale factor.
 */
float
BView::Scale() const
{
	if (!fState->IsValid(B_VIEW_SCALE_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_SCALE);

 		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK && code == B_OK)
			fOwner->fLink->Read<float>(&fState->scale);

		fState->valid_flags |= B_VIEW_SCALE_BIT;
	}

	return fState->scale;
}


/**
 * @brief Sets the affine transform for the current drawing state.
 *
 * Replaces the current transform with @p transform and forwards it to the
 * app_server.  No-ops if the transform is already equal to the cached value.
 *
 * @param transform The new affine transform to apply to subsequent drawing.
 *
 * @see BView::Transform(), BView::TranslateBy(), BView::ScaleBy(), BView::RotateBy()
 */
void
BView::SetTransform(BAffineTransform transform)
{
	if (fState->IsValid(B_VIEW_TRANSFORM_BIT) && transform == fState->transform)
		return;

	if (fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_TRANSFORM);
		fOwner->fLink->AttachAffineTransform(transform);

		fState->valid_flags |= B_VIEW_TRANSFORM_BIT;
	}

	fState->transform = transform;
	fState->archiving_flags |= B_VIEW_TRANSFORM_BIT;
}


/**
 * @brief Returns the current affine transform of the drawing state.
 *
 * Fetches the transform from the app_server if the local cache is invalid.
 *
 * @return The current BAffineTransform.
 */
BAffineTransform
BView::Transform() const
{
	if (!fState->IsValid(B_VIEW_TRANSFORM_BIT) && fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_TRANSFORM);

 		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK && code == B_OK)
			fOwner->fLink->ReadAffineTransform(&fState->transform);

		fState->valid_flags |= B_VIEW_TRANSFORM_BIT;
	}

	return fState->transform;
}


/**
 * @brief Returns the composite transform from view coordinates to the given basis.
 *
 * Computes the cumulative transform (including scale and origin) that maps
 * points expressed in the current drawing state to the requested coordinate
 * space.  The parent composite is fetched from the server if needed.
 *
 * @param basis The target coordinate space:
 *              B_CURRENT_STATE_COORDINATES, B_PREVIOUS_STATE_COORDINATES,
 *              B_VIEW_COORDINATES, B_PARENT_VIEW_COORDINATES,
 *              B_PARENT_VIEW_DRAW_COORDINATES, B_WINDOW_COORDINATES, or
 *              B_SCREEN_COORDINATES.
 * @return The BAffineTransform mapping from current-state coordinates to @p basis.
 */
BAffineTransform
BView::TransformTo(coordinate_space basis) const
{
	if (basis == B_CURRENT_STATE_COORDINATES)
		return B_AFFINE_IDENTITY_TRANSFORM;

	if (!fState->IsValid(B_VIEW_PARENT_COMPOSITE_BIT) && fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_PARENT_COMPOSITE);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK && code == B_OK) {
			fOwner->fLink->ReadAffineTransform(&fState->parent_composite_transform);
			fOwner->fLink->Read<float>(&fState->parent_composite_scale);
			fOwner->fLink->Read<BPoint>(&fState->parent_composite_origin);
		}

		fState->valid_flags |= B_VIEW_PARENT_COMPOSITE_BIT;
	}

	BAffineTransform transform = fState->parent_composite_transform * Transform();
	float scale = fState->parent_composite_scale * Scale();
	transform.PreScaleBy(scale, scale);
	BPoint origin = Origin();
	origin.x *= fState->parent_composite_scale;
	origin.y *= fState->parent_composite_scale;
	origin += fState->parent_composite_origin;
	transform.TranslateBy(origin);

	if (basis == B_PREVIOUS_STATE_COORDINATES) {
		transform.TranslateBy(-fState->parent_composite_origin);
		transform.PreMultiplyInverse(fState->parent_composite_transform);
		transform.ScaleBy(1.0f / fState->parent_composite_scale);
		return transform;
	}

	if (basis == B_VIEW_COORDINATES)
		return transform;

	origin = B_ORIGIN;

	if (basis == B_PARENT_VIEW_COORDINATES || basis == B_PARENT_VIEW_DRAW_COORDINATES) {
		BView* parent = Parent();
		if (parent != NULL) {
			ConvertToParent(&origin);
			transform.TranslateBy(origin);
			if (basis == B_PARENT_VIEW_DRAW_COORDINATES)
				transform = transform.PreMultiplyInverse(parent->TransformTo(B_VIEW_COORDINATES));
			return transform;
		}
		basis = B_WINDOW_COORDINATES;
	}

	ConvertToScreen(&origin);
	if (basis == B_WINDOW_COORDINATES) {
		BWindow* window = Window();
		if (window != NULL)
			origin -= window->Frame().LeftTop();
	}
	transform.TranslateBy(origin);
	return transform;
}


/**
 * @brief Applies an incremental translation to the current affine transform.
 *
 * Sends AS_VIEW_AFFINE_TRANSLATE to the server; the local transform cache is
 * invalidated and must be re-fetched on next access.
 *
 * @param x Horizontal translation in current coordinate-space units.
 * @param y Vertical translation in current coordinate-space units.
 */
void
BView::TranslateBy(double x, double y)
{
	if (fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_AFFINE_TRANSLATE);
		fOwner->fLink->Attach<double>(x);
		fOwner->fLink->Attach<double>(y);

		fState->valid_flags &= ~B_VIEW_TRANSFORM_BIT;
	}

	fState->archiving_flags |= B_VIEW_TRANSFORM_BIT;
}


/**
 * @brief Applies an incremental non-uniform scale to the current affine transform.
 *
 * Sends AS_VIEW_AFFINE_SCALE to the server; the local transform cache is
 * invalidated.
 *
 * @param x Horizontal scale factor.
 * @param y Vertical scale factor.
 */
void
BView::ScaleBy(double x, double y)
{
	if (fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_AFFINE_SCALE);
		fOwner->fLink->Attach<double>(x);
		fOwner->fLink->Attach<double>(y);

		fState->valid_flags &= ~B_VIEW_TRANSFORM_BIT;
	}

	fState->archiving_flags |= B_VIEW_TRANSFORM_BIT;
}


/**
 * @brief Applies an incremental rotation to the current affine transform.
 *
 * Sends AS_VIEW_AFFINE_ROTATE to the server; the local transform cache is
 * invalidated.
 *
 * @param angleRadians The clockwise rotation angle in radians.
 */
void
BView::RotateBy(double angleRadians)
{
	if (fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_AFFINE_ROTATE);
		fOwner->fLink->Attach<double>(angleRadians);

		fState->valid_flags &= ~B_VIEW_TRANSFORM_BIT;
	}

	fState->archiving_flags |= B_VIEW_TRANSFORM_BIT;
}


/**
 * @brief Sets the line cap style, join style, and miter limit.
 *
 * No-ops if all three values are already set and valid.  Changes are forwarded
 * to the app_server and the archiving flags are updated.
 *
 * @param lineCap   The cap style for line endpoints (B_BUTT_CAP, B_ROUND_CAP,
 *                  B_SQUARE_CAP).
 * @param lineJoin  The join style for connected line segments (B_MITER_JOIN,
 *                  B_ROUND_JOIN, B_BEVEL_JOIN).
 * @param miterLimit The maximum miter length/width ratio before the join
 *                   switches to a bevel.
 *
 * @see BView::LineCapMode(), BView::LineJoinMode(), BView::LineMiterLimit()
 */
void
BView::SetLineMode(cap_mode lineCap, join_mode lineJoin, float miterLimit)
{
	if (fState->IsValid(B_VIEW_LINE_MODES_BIT)
		&& lineCap == fState->line_cap && lineJoin == fState->line_join
		&& miterLimit == fState->miter_limit)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		ViewSetLineModeInfo info;
		info.lineJoin = lineJoin;
		info.lineCap = lineCap;
		info.miterLimit = miterLimit;

		fOwner->fLink->StartMessage(AS_VIEW_SET_LINE_MODE);
		fOwner->fLink->Attach<ViewSetLineModeInfo>(info);

		fState->valid_flags |= B_VIEW_LINE_MODES_BIT;
	}

	fState->line_cap = lineCap;
	fState->line_join = lineJoin;
	fState->miter_limit = miterLimit;

	fState->archiving_flags |= B_VIEW_LINE_MODES_BIT;
}


/**
 * @brief Returns the current line join mode.
 *
 * Triggers a server fetch via LineMiterLimit() if the line mode is not cached.
 *
 * @return The current join_mode value.
 */
join_mode
BView::LineJoinMode() const
{
	// This will update the current state, if necessary
	if (!fState->IsValid(B_VIEW_LINE_MODES_BIT))
		LineMiterLimit();

	return fState->line_join;
}


/**
 * @brief Returns the current line cap mode.
 *
 * Triggers a server fetch via LineMiterLimit() if the line mode is not cached.
 *
 * @return The current cap_mode value.
 */
cap_mode
BView::LineCapMode() const
{
	// This will update the current state, if necessary
	if (!fState->IsValid(B_VIEW_LINE_MODES_BIT))
		LineMiterLimit();

	return fState->line_cap;
}


/**
 * @brief Returns the current miter limit for line joins.
 *
 * If the cached line mode is invalid, fetches all three line-mode values
 * (cap, join, miter) from the app_server in one round-trip.
 *
 * @return The miter limit ratio.
 */
float
BView::LineMiterLimit() const
{
	if (!fState->IsValid(B_VIEW_LINE_MODES_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_LINE_MODE);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK && code == B_OK) {

			ViewSetLineModeInfo info;
			fOwner->fLink->Read<ViewSetLineModeInfo>(&info);

			fState->line_cap = info.lineCap;
			fState->line_join = info.lineJoin;
			fState->miter_limit = info.miterLimit;
		}

		fState->valid_flags |= B_VIEW_LINE_MODES_BIT;
	}

	return fState->miter_limit;
}


/**
 * @brief Sets the winding rule used to determine the interior of filled shapes.
 *
 * @param fillRule B_NONZERO or B_EVEN_ODD.
 *
 * @see BView::FillRule()
 */
void
BView::SetFillRule(int32 fillRule)
{
	if (fState->IsValid(B_VIEW_FILL_RULE_BIT) && fillRule == fState->fill_rule)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_FILL_RULE);
		fOwner->fLink->Attach<int32>(fillRule);

		fState->valid_flags |= B_VIEW_FILL_RULE_BIT;
	}

	fState->fill_rule = fillRule;

	fState->archiving_flags |= B_VIEW_FILL_RULE_BIT;
}


/**
 * @brief Returns the current fill rule (winding rule).
 *
 * Fetches the value from the app_server if the local cache is invalid.
 *
 * @return B_NONZERO or B_EVEN_ODD.
 */
int32
BView::FillRule() const
{
	if (!fState->IsValid(B_VIEW_FILL_RULE_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_FILL_RULE);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK && code == B_OK) {

			int32 fillRule;
			fOwner->fLink->Read<int32>(&fillRule);

			fState->fill_rule = fillRule;
		}

		fState->valid_flags |= B_VIEW_FILL_RULE_BIT;
	}

	return fState->fill_rule;
}


/**
 * @brief Sets the drawing mode that controls how pixels are composited.
 *
 * @param mode One of the drawing_mode constants (B_OP_COPY, B_OP_OVER, etc.).
 *
 * @see BView::DrawingMode()
 */
void
BView::SetDrawingMode(drawing_mode mode)
{
	if (fState->IsValid(B_VIEW_DRAWING_MODE_BIT)
		&& mode == fState->drawing_mode)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_DRAWING_MODE);
		fOwner->fLink->Attach<int8>((int8)mode);

		fState->valid_flags |= B_VIEW_DRAWING_MODE_BIT;
	}

	fState->drawing_mode = mode;
	fState->archiving_flags |= B_VIEW_DRAWING_MODE_BIT;
}


/**
 * @brief Returns the current drawing mode.
 *
 * Fetches the mode from the app_server if the local cache is invalid.
 *
 * @return The current drawing_mode.
 */
drawing_mode
BView::DrawingMode() const
{
	if (!fState->IsValid(B_VIEW_DRAWING_MODE_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_DRAWING_MODE);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK) {
			int8 drawingMode;
			fOwner->fLink->Read<int8>(&drawingMode);

			fState->drawing_mode = (drawing_mode)drawingMode;
			fState->valid_flags |= B_VIEW_DRAWING_MODE_BIT;
		}
	}

	return fState->drawing_mode;
}


/**
 * @brief Sets the alpha blending source and function.
 *
 * @param sourceAlpha   How the source alpha is determined (B_CONSTANT_ALPHA or
 *                      B_PIXEL_ALPHA).
 * @param alphaFunction How the source and destination are combined
 *                      (B_ALPHA_OVERLAY or B_ALPHA_COMPOSITE).
 *
 * @see BView::GetBlendingMode()
 */
void
BView::SetBlendingMode(source_alpha sourceAlpha, alpha_function alphaFunction)
{
	if (fState->IsValid(B_VIEW_BLENDING_BIT)
		&& sourceAlpha == fState->alpha_source_mode
		&& alphaFunction == fState->alpha_function_mode)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		ViewBlendingModeInfo info;
		info.sourceAlpha = sourceAlpha;
		info.alphaFunction = alphaFunction;

		fOwner->fLink->StartMessage(AS_VIEW_SET_BLENDING_MODE);
		fOwner->fLink->Attach<ViewBlendingModeInfo>(info);

		fState->valid_flags |= B_VIEW_BLENDING_BIT;
	}

	fState->alpha_source_mode = sourceAlpha;
	fState->alpha_function_mode = alphaFunction;

	fState->archiving_flags |= B_VIEW_BLENDING_BIT;
}


/**
 * @brief Retrieves the current alpha blending source and function.
 *
 * Fetches values from the app_server if the local cache is invalid.
 *
 * @param _sourceAlpha   Receives the source alpha mode; may be NULL.
 * @param _alphaFunction Receives the alpha function; may be NULL.
 */
void
BView::GetBlendingMode(source_alpha* _sourceAlpha,
	alpha_function* _alphaFunction) const
{
	if (!fState->IsValid(B_VIEW_BLENDING_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_BLENDING_MODE);

		int32 code;
 		if (fOwner->fLink->FlushWithReply(code) == B_OK && code == B_OK) {
 			ViewBlendingModeInfo info;
			fOwner->fLink->Read<ViewBlendingModeInfo>(&info);

			fState->alpha_source_mode = info.sourceAlpha;
			fState->alpha_function_mode = info.alphaFunction;

			fState->valid_flags |= B_VIEW_BLENDING_BIT;
		}
	}

	if (_sourceAlpha)
		*_sourceAlpha = fState->alpha_source_mode;

	if (_alphaFunction)
		*_alphaFunction = fState->alpha_function_mode;
}


/**
 * @brief Moves the drawing pen to @p point without drawing.
 *
 * @param point The new pen position in view-local coordinates.
 * @see BView::MovePenTo(float, float)
 */
void
BView::MovePenTo(BPoint point)
{
	MovePenTo(point.x, point.y);
}


/**
 * @brief Moves the drawing pen to (@p x, @p y) without drawing.
 *
 * No-ops if the pen is already at the requested position and the cached
 * location is valid.
 *
 * @param x New pen x coordinate.
 * @param y New pen y coordinate.
 *
 * @see BView::PenLocation(), BView::MovePenBy()
 */
void
BView::MovePenTo(float x, float y)
{
	if (fState->IsValid(B_VIEW_PEN_LOCATION_BIT)
		&& x == fState->pen_location.x && y == fState->pen_location.y)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_PEN_LOC);
		fOwner->fLink->Attach<BPoint>(BPoint(x, y));

		fState->valid_flags |= B_VIEW_PEN_LOCATION_BIT;
	}

	fState->pen_location.x = x;
	fState->pen_location.y = y;

	fState->archiving_flags |= B_VIEW_PEN_LOCATION_BIT;
}


/**
 * @brief Moves the drawing pen by (@p x, @p y) relative to its current position.
 *
 * Fetches the current pen location from the server if the cached value is
 * invalid, then delegates to MovePenTo().
 *
 * @param x Horizontal displacement.
 * @param y Vertical displacement.
 */
void
BView::MovePenBy(float x, float y)
{
	// this will update the pen location if necessary
	if (!fState->IsValid(B_VIEW_PEN_LOCATION_BIT))
		PenLocation();

	MovePenTo(fState->pen_location.x + x, fState->pen_location.y + y);
}


/**
 * @brief Returns the current drawing pen location.
 *
 * Fetches the value from the app_server if the local cache is invalid (e.g.
 * after a DrawString() or StrokeLine() that implicitly moves the pen).
 *
 * @return The pen position in view-local coordinates.
 */
BPoint
BView::PenLocation() const
{
	if (!fState->IsValid(B_VIEW_PEN_LOCATION_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_PEN_LOC);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK) {
			fOwner->fLink->Read<BPoint>(&fState->pen_location);

			fState->valid_flags |= B_VIEW_PEN_LOCATION_BIT;
		}
	}

	return fState->pen_location;
}


/**
 * @brief Sets the pen stroke width for subsequent drawing operations.
 *
 * @param size The new pen size in view-local coordinate units.
 *
 * @see BView::PenSize()
 */
void
BView::SetPenSize(float size)
{
	if (fState->IsValid(B_VIEW_PEN_SIZE_BIT) && size == fState->pen_size)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_PEN_SIZE);
		fOwner->fLink->Attach<float>(size);

		fState->valid_flags |= B_VIEW_PEN_SIZE_BIT;
	}

	fState->pen_size = size;
	fState->archiving_flags	|= B_VIEW_PEN_SIZE_BIT;
}


/**
 * @brief Returns the current pen stroke width.
 *
 * Fetches the value from the app_server if the local cache is invalid.
 *
 * @return The pen size in coordinate units.
 */
float
BView::PenSize() const
{
	if (!fState->IsValid(B_VIEW_PEN_SIZE_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_PEN_SIZE);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK) {
			fOwner->fLink->Read<float>(&fState->pen_size);

			fState->valid_flags |= B_VIEW_PEN_SIZE_BIT;
		}
	}

	return fState->pen_size;
}


/**
 * @brief Sets the high (foreground) colour for drawing operations.
 *
 * Clears any UI-colour alias (calls SetHighUIColor(B_NO_COLOR)) before
 * applying the literal colour.  No-ops if the colour is already set.
 *
 * @param color The new high colour.
 *
 * @see BView::HighColor(), BView::SetHighUIColor()
 */
void
BView::SetHighColor(rgb_color color)
{
	SetHighUIColor(B_NO_COLOR);

	// are we up-to-date already?
	if (fState->IsValid(B_VIEW_HIGH_COLOR_BIT)
		&& fState->high_color == color)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_HIGH_COLOR);
		fOwner->fLink->Attach<rgb_color>(color);

		fState->valid_flags |= B_VIEW_HIGH_COLOR_BIT;
	}

	fState->high_color = color;

	fState->archiving_flags |= B_VIEW_HIGH_COLOR_BIT;
}


/**
 * @brief Returns the current high (foreground) colour.
 *
 * Fetches from the app_server if the local cache is invalid.
 *
 * @return The current high colour as an rgb_color.
 */
rgb_color
BView::HighColor() const
{
	if (!fState->IsValid(B_VIEW_HIGH_COLOR_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_HIGH_COLOR);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK) {
			fOwner->fLink->Read<rgb_color>(&fState->high_color);

			fState->valid_flags |= B_VIEW_HIGH_COLOR_BIT;
		}
	}

	return fState->high_color;
}


/**
 * @brief Sets the high colour by referencing a named UI colour constant.
 *
 * The actual rgb_color is resolved from the system palette and cached locally.
 * When @p which is B_NO_COLOR the alias is cleared and the raw high colour
 * becomes authoritative.
 *
 * @param which The UI colour constant (e.g. B_PANEL_TEXT_COLOR).
 * @param tint  A tint factor applied to the resolved colour (B_NO_TINT = 1.0).
 *
 * @see BView::HighUIColor(), BView::SetHighColor()
 */
void
BView::SetHighUIColor(color_which which, float tint)
{
	if (fState->IsValid(B_VIEW_WHICH_HIGH_COLOR_BIT)
		&& fState->which_high_color == which
		&& fState->which_high_color_tint == tint)
		return;

	if (fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_HIGH_UI_COLOR);
		fOwner->fLink->Attach<color_which>(which);
		fOwner->fLink->Attach<float>(tint);

		fState->valid_flags |= B_VIEW_WHICH_HIGH_COLOR_BIT;
	}

	fState->which_high_color = which;
	fState->which_high_color_tint = tint;

	if (which != B_NO_COLOR) {
		fState->archiving_flags |= B_VIEW_WHICH_HIGH_COLOR_BIT;
		fState->archiving_flags &= ~B_VIEW_HIGH_COLOR_BIT;
		fState->valid_flags |= B_VIEW_HIGH_COLOR_BIT;

		fState->high_color = tint_color(ui_color(which), tint);
	} else {
		fState->valid_flags &= ~B_VIEW_HIGH_COLOR_BIT;
		fState->archiving_flags &= ~B_VIEW_WHICH_HIGH_COLOR_BIT;
	}
}


/**
 * @brief Returns the UI colour constant for the high colour, if one is set.
 *
 * Fetches from the app_server if the local cache is invalid.
 *
 * @param tint If non-NULL, receives the tint factor currently applied.
 * @return The color_which constant, or B_NO_COLOR if a literal colour is used.
 */
color_which
BView::HighUIColor(float* tint) const
{
	if (!fState->IsValid(B_VIEW_WHICH_HIGH_COLOR_BIT)
		&& fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_HIGH_UI_COLOR);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK) {
			fOwner->fLink->Read<color_which>(&fState->which_high_color);
			fOwner->fLink->Read<float>(&fState->which_high_color_tint);
			fOwner->fLink->Read<rgb_color>(&fState->high_color);

			fState->valid_flags |= B_VIEW_WHICH_HIGH_COLOR_BIT;
			fState->valid_flags |= B_VIEW_HIGH_COLOR_BIT;
		}
	}

	if (tint != NULL)
		*tint = fState->which_high_color_tint;

	return fState->which_high_color;
}


/**
 * @brief Sets the low (background fill) colour for drawing operations.
 *
 * Used for pattern fills and certain blend operations.  Clears any UI-colour
 * alias before applying the literal colour.
 *
 * @param color The new low colour.
 *
 * @see BView::LowColor(), BView::SetLowUIColor()
 */
void
BView::SetLowColor(rgb_color color)
{
	SetLowUIColor(B_NO_COLOR);

	if (fState->IsValid(B_VIEW_LOW_COLOR_BIT)
		&& fState->low_color == color)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_LOW_COLOR);
		fOwner->fLink->Attach<rgb_color>(color);

		fState->valid_flags |= B_VIEW_LOW_COLOR_BIT;
	}

	fState->low_color = color;

	fState->archiving_flags |= B_VIEW_LOW_COLOR_BIT;
}


/**
 * @brief Returns the current low colour.
 *
 * Fetches from the app_server if the local cache is invalid.
 *
 * @return The current low colour as an rgb_color.
 */
rgb_color
BView::LowColor() const
{
	if (!fState->IsValid(B_VIEW_LOW_COLOR_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_LOW_COLOR);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK) {
			fOwner->fLink->Read<rgb_color>(&fState->low_color);

			fState->valid_flags |= B_VIEW_LOW_COLOR_BIT;
		}
	}

	return fState->low_color;
}


/**
 * @brief Sets the low colour by referencing a named UI colour constant.
 *
 * @param which The UI colour constant.
 * @param tint  Tint factor applied to the resolved colour.
 *
 * @see BView::LowUIColor(), BView::SetLowColor()
 */
void
BView::SetLowUIColor(color_which which, float tint)
{
	if (fState->IsValid(B_VIEW_WHICH_LOW_COLOR_BIT)
		&& fState->which_low_color == which
		&& fState->which_low_color_tint == tint)
		return;

	if (fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_LOW_UI_COLOR);
		fOwner->fLink->Attach<color_which>(which);
		fOwner->fLink->Attach<float>(tint);

		fState->valid_flags |= B_VIEW_WHICH_LOW_COLOR_BIT;
	}

	fState->which_low_color = which;
	fState->which_low_color_tint = tint;

	if (which != B_NO_COLOR) {
		fState->archiving_flags |= B_VIEW_WHICH_LOW_COLOR_BIT;
		fState->archiving_flags &= ~B_VIEW_LOW_COLOR_BIT;
		fState->valid_flags |= B_VIEW_LOW_COLOR_BIT;

		fState->low_color = tint_color(ui_color(which), tint);
	} else {
		fState->valid_flags &= ~B_VIEW_LOW_COLOR_BIT;
		fState->archiving_flags &= ~B_VIEW_WHICH_LOW_COLOR_BIT;
	}
}


/**
 * @brief Returns the UI colour constant for the low colour, if one is set.
 *
 * @param tint If non-NULL, receives the applied tint factor.
 * @return The color_which constant, or B_NO_COLOR if a literal colour is used.
 */
color_which
BView::LowUIColor(float* tint) const
{
	if (!fState->IsValid(B_VIEW_WHICH_LOW_COLOR_BIT)
		&& fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_LOW_UI_COLOR);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK) {
			fOwner->fLink->Read<color_which>(&fState->which_low_color);
			fOwner->fLink->Read<float>(&fState->which_low_color_tint);
			fOwner->fLink->Read<rgb_color>(&fState->low_color);

			fState->valid_flags |= B_VIEW_WHICH_LOW_COLOR_BIT;
			fState->valid_flags |= B_VIEW_LOW_COLOR_BIT;
		}
	}

	if (tint != NULL)
		*tint = fState->which_low_color_tint;

	return fState->which_low_color;
}


/**
 * @brief Returns true if none of the view's colours have been explicitly set.
 *
 * Checks the archiving flags for any colour-related bits.  A view that has
 * never had a colour set returns true.
 *
 * @return True when all colour flags are at their initial default state.
 */
bool
BView::HasDefaultColors() const
{
	// If we don't have any of these flags, then we have default colors
	uint32 testMask = B_VIEW_VIEW_COLOR_BIT | B_VIEW_HIGH_COLOR_BIT
		| B_VIEW_LOW_COLOR_BIT | B_VIEW_WHICH_VIEW_COLOR_BIT
		| B_VIEW_WHICH_HIGH_COLOR_BIT | B_VIEW_WHICH_LOW_COLOR_BIT;

	return (fState->archiving_flags & testMask) == 0;
}


/**
 * @brief Returns true if all three colours use the standard panel colour aliases.
 *
 * A view "has system colours" when view colour = B_PANEL_BACKGROUND_COLOR,
 * high colour = B_PANEL_TEXT_COLOR, low colour = B_PANEL_BACKGROUND_COLOR,
 * all with B_NO_TINT.
 *
 * @return True when all colours match the system panel defaults.
 */
bool
BView::HasSystemColors() const
{
	return fState->which_view_color == B_PANEL_BACKGROUND_COLOR
		&& fState->which_high_color == B_PANEL_TEXT_COLOR
		&& fState->which_low_color == B_PANEL_BACKGROUND_COLOR
		&& fState->which_view_color_tint == B_NO_TINT
		&& fState->which_high_color_tint == B_NO_TINT
		&& fState->which_low_color_tint == B_NO_TINT;
}


/**
 * @brief Copies all three colour settings from the parent view.
 *
 * Equivalent to AdoptViewColors(Parent()).
 *
 * @see BView::AdoptViewColors()
 */
void
BView::AdoptParentColors()
{
	AdoptViewColors(Parent());
}


/**
 * @brief Sets this view's colours to the standard system panel colours.
 *
 * Sets view colour and low colour to B_PANEL_BACKGROUND_COLOR and high colour
 * to B_PANEL_TEXT_COLOR, all without tinting.
 */
void
BView::AdoptSystemColors()
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	SetHighUIColor(B_PANEL_TEXT_COLOR);
}


/**
 * @brief Copies all three colour settings from the given view.
 *
 * Locks @p view's looper if it belongs to a different window, reads its view
 * colour, low colour, and high colour (preferring UI-colour aliases where
 * available), then applies them to this view.  Does nothing if @p view is NULL
 * or cannot be locked.
 *
 * @param view The source view whose colours are adopted.
 */
void
BView::AdoptViewColors(BView* view)
{
	if (view == NULL || (view->Window() != NULL && !view->LockLooper()))
		return;

	float tint = B_NO_TINT;
	float viewTint = tint;
	color_which viewWhich = view->ViewUIColor(&viewTint);

	// View color
	if (viewWhich != B_NO_COLOR)
		SetViewUIColor(viewWhich, viewTint);
	else
		SetViewColor(view->ViewColor());

	// Low color
	color_which which = view->LowUIColor(&tint);
	if (which != B_NO_COLOR)
		SetLowUIColor(which, tint);
	else if (viewWhich != B_NO_COLOR)
		SetLowUIColor(viewWhich, viewTint);
	else
		SetLowColor(view->LowColor());

	// High color
	which = view->HighUIColor(&tint);
	if (which != B_NO_COLOR)
		SetHighUIColor(which, tint);
	else
		SetHighColor(view->HighColor());

	if (view->Window() != NULL)
		view->UnlockLooper();
}


/**
 * @brief Sets the view's background fill colour.
 *
 * The view colour is used to erase the view before Draw() is called.  Clears
 * any UI-colour alias before applying the literal colour, and also automatically
 * clears the low-colour alias (but not the literal low colour).
 *
 * @param color The new view background colour.
 *
 * @see BView::ViewColor(), BView::SetViewUIColor()
 */
void
BView::SetViewColor(rgb_color color)
{
	SetViewUIColor(B_NO_COLOR);

	if (fState->IsValid(B_VIEW_VIEW_COLOR_BIT)
		&& fState->view_color == color)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_VIEW_COLOR);
		fOwner->fLink->Attach<rgb_color>(color);
		fOwner->fLink->Flush();

		fState->valid_flags |= B_VIEW_VIEW_COLOR_BIT;
	}

	fState->view_color = color;

	fState->archiving_flags |= B_VIEW_VIEW_COLOR_BIT;
}


/**
 * @brief Returns the current view background colour.
 *
 * Fetches from the app_server if the local cache is invalid.
 *
 * @return The view background colour as an rgb_color.
 */
rgb_color
BView::ViewColor() const
{
	if (!fState->IsValid(B_VIEW_VIEW_COLOR_BIT) && fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_VIEW_COLOR);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK) {
			fOwner->fLink->Read<rgb_color>(&fState->view_color);

			fState->valid_flags |= B_VIEW_VIEW_COLOR_BIT;
		}
	}

	return fState->view_color;
}


/**
 * @brief Sets the view background colour by referencing a named UI colour.
 *
 * Also propagates the alias to the low colour when no explicit low-colour alias
 * has been set, keeping the two in sync for typical panel backgrounds.
 *
 * @param which The UI colour constant for the background.
 * @param tint  Tint factor applied to the resolved colour.
 *
 * @see BView::ViewUIColor(), BView::SetViewColor()
 */
void
BView::SetViewUIColor(color_which which, float tint)
{
	if (fState->IsValid(B_VIEW_WHICH_VIEW_COLOR_BIT)
		&& fState->which_view_color == which
		&& fState->which_view_color_tint == tint)
		return;

	if (fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_VIEW_UI_COLOR);
		fOwner->fLink->Attach<color_which>(which);
		fOwner->fLink->Attach<float>(tint);

		fState->valid_flags |= B_VIEW_WHICH_VIEW_COLOR_BIT;
	}

	fState->which_view_color = which;
	fState->which_view_color_tint = tint;

	if (which != B_NO_COLOR) {
		fState->archiving_flags |= B_VIEW_WHICH_VIEW_COLOR_BIT;
		fState->archiving_flags &= ~B_VIEW_VIEW_COLOR_BIT;
		fState->valid_flags |= B_VIEW_VIEW_COLOR_BIT;

		fState->view_color = tint_color(ui_color(which), tint);
	} else {
		fState->valid_flags &= ~B_VIEW_VIEW_COLOR_BIT;
		fState->archiving_flags &= ~B_VIEW_WHICH_VIEW_COLOR_BIT;
	}

	if (!fState->IsValid(B_VIEW_WHICH_LOW_COLOR_BIT))
		SetLowUIColor(which, tint);
}


/**
 * @brief Returns the UI colour constant for the view background, if set.
 *
 * @param tint If non-NULL, receives the applied tint factor.
 * @return The color_which constant, or B_NO_COLOR if a literal colour is used.
 */
color_which
BView::ViewUIColor(float* tint) const
{
	if (!fState->IsValid(B_VIEW_WHICH_VIEW_COLOR_BIT)
		&& fOwner != NULL) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_GET_VIEW_UI_COLOR);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK) {
			fOwner->fLink->Read<color_which>(&fState->which_view_color);
			fOwner->fLink->Read<float>(&fState->which_view_color_tint);
			fOwner->fLink->Read<rgb_color>(&fState->view_color);

			fState->valid_flags |= B_VIEW_WHICH_VIEW_COLOR_BIT;
			fState->valid_flags |= B_VIEW_VIEW_COLOR_BIT;
		}
	}

	if (tint != NULL)
		*tint = fState->which_view_color_tint;

	return fState->which_view_color;
}


/**
 * @brief Enables or disables font anti-aliasing for printing.
 *
 * When @p enable is true, text drawn by this view is not anti-aliased during
 * a print job, which can improve legibility at printer resolutions.
 *
 * @param enable True to force aliased (non-anti-aliased) font rendering.
 */
void
BView::ForceFontAliasing(bool enable)
{
	if (fState->IsValid(B_VIEW_FONT_ALIASING_BIT)
		&& enable == fState->font_aliasing)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_PRINT_ALIASING);
		fOwner->fLink->Attach<bool>(enable);

		fState->valid_flags |= B_VIEW_FONT_ALIASING_BIT;
	}

	fState->font_aliasing = enable;
	fState->archiving_flags |= B_VIEW_FONT_ALIASING_BIT;
}


/**
 * @brief Sets the view's font, applying only the attributes indicated by @p mask.
 *
 * When @p mask is B_FONT_ALL the entire font is replaced.  Otherwise only the
 * flagged attributes (family/style, size, shear, rotation, etc.) are updated.
 * Changes are forwarded to the app_server immediately when the view is attached.
 *
 * @param font A pointer to the source font; ignored if NULL.
 * @param mask A bitfield of B_FONT_* constants selecting which attributes to copy.
 *
 * @see BView::GetFont(), BView::SetFontSize()
 */
void
BView::SetFont(const BFont* font, uint32 mask)
{
	if (!font || mask == 0)
		return;

	if (mask == B_FONT_ALL) {
		fState->font = *font;
	} else {
		// TODO: move this into a BFont method
		if (mask & B_FONT_FAMILY_AND_STYLE)
			fState->font.SetFamilyAndStyle(font->FamilyAndStyle());

		if (mask & B_FONT_SIZE)
			fState->font.SetSize(font->Size());

		if (mask & B_FONT_SHEAR)
			fState->font.SetShear(font->Shear());

		if (mask & B_FONT_ROTATION)
			fState->font.SetRotation(font->Rotation());

		if (mask & B_FONT_FALSE_BOLD_WIDTH)
			fState->font.SetFalseBoldWidth(font->FalseBoldWidth());

		if (mask & B_FONT_SPACING)
			fState->font.SetSpacing(font->Spacing());

		if (mask & B_FONT_ENCODING)
			fState->font.SetEncoding(font->Encoding());

		if (mask & B_FONT_FACE)
			fState->font.SetFace(font->Face());

		if (mask & B_FONT_FLAGS)
			fState->font.SetFlags(font->Flags());
	}

	fState->font_flags |= mask;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fState->UpdateServerFontState(*fOwner->fLink);
		fState->valid_flags |= B_VIEW_FONT_BIT;
	}

	fState->archiving_flags |= B_VIEW_FONT_BIT;
	// TODO: InvalidateLayout() here for convenience?
}


/**
 * @brief Returns the view's current font.
 *
 * If the cached font state is invalid (e.g. after PopState()), the full state
 * is fetched from the app_server via UpdateFrom().
 *
 * @param font Receives a copy of the current BFont; must not be NULL.
 */
void
BView::GetFont(BFont* font) const
{
	if (!fState->IsValid(B_VIEW_FONT_BIT)) {
		// we don't keep graphics state information, therefor
		// we need to ask the server for the origin after PopState()
		_CheckOwnerLockAndSwitchCurrent();

		// TODO: add a font getter!
		fState->UpdateFrom(*fOwner->fLink);
	}

	*font = fState->font;
}


/**
 * @brief Returns the ascent, descent, and leading of the current font.
 *
 * @param height Receives the font_height metrics; must not be NULL.
 */
void
BView::GetFontHeight(font_height* height) const
{
	fState->font.GetHeight(height);
}


/**
 * @brief Sets the font point size without changing any other font attributes.
 *
 * @param size The new font size in points.
 *
 * @see BView::SetFont()
 */
void
BView::SetFontSize(float size)
{
	BFont font;
	font.SetSize(size);

	SetFont(&font, B_FONT_SIZE);
}


/**
 * @brief Returns the width of the NUL-terminated string in the current font.
 *
 * @param string A NUL-terminated UTF-8 string.
 * @return The rendered width in view-local coordinate units.
 */
float
BView::StringWidth(const char* string) const
{
	return fState->font.StringWidth(string);
}


/**
 * @brief Returns the width of @p length bytes of @p string in the current font.
 *
 * @param string A UTF-8 string (need not be NUL-terminated).
 * @param length Number of bytes to measure.
 * @return The rendered width in view-local coordinate units.
 */
float
BView::StringWidth(const char* string, int32 length) const
{
	return fState->font.StringWidth(string, length);
}


/**
 * @brief Returns the widths of multiple strings in the current font.
 *
 * @param stringArray  Array of string pointers.
 * @param lengthArray  Array of byte lengths corresponding to each string.
 * @param numStrings   Number of entries in both arrays.
 * @param widthArray   Receives the width of each string; must be at least
 *                     @p numStrings elements.
 */
void
BView::GetStringWidths(char* stringArray[], int32 lengthArray[],
	int32 numStrings, float widthArray[]) const
{
	fState->font.GetStringWidths(const_cast<const char**>(stringArray),
		const_cast<const int32*>(lengthArray), numStrings, widthArray);
}


/**
 * @brief Truncates @p string so it fits within @p width pixels.
 *
 * @param string The string to truncate in place.
 * @param mode   Truncation mode: B_TRUNCATE_BEGINNING, B_TRUNCATE_MIDDLE,
 *               B_TRUNCATE_END, or B_TRUNCATE_SMART.
 * @param width  Maximum allowed rendered width in view-local units.
 */
void
BView::TruncateString(BString* string, uint32 mode, float width) const
{
	fState->font.TruncateString(string, mode, width);
}


/**
 * @brief Intersects the current clipping region with the area covered by a picture.
 *
 * The picture is rendered into a mask; only pixels where the picture draws
 * remain visible.
 *
 * @param picture The picture that defines the clipping shape.
 * @param where   The position of the picture in view-local coordinates.
 * @param sync    If true, synchronise with the server before returning so the
 *                picture token stays valid.
 *
 * @see BView::ClipToInversePicture()
 */
void
BView::ClipToPicture(BPicture* picture, BPoint where, bool sync)
{
	_ClipToPicture(picture, where, false, sync);
}


/**
 * @brief Intersects the clipping region with the inverse of a picture's coverage.
 *
 * The complement of ClipToPicture(): pixels where the picture does NOT draw
 * remain visible.
 *
 * @param picture The picture defining the excluded area.
 * @param where   The position of the picture in view-local coordinates.
 * @param sync    If true, synchronise with the server before returning.
 *
 * @see BView::ClipToPicture()
 */
void
BView::ClipToInversePicture(BPicture* picture, BPoint where, bool sync)
{
	_ClipToPicture(picture, where, true, sync);
}


/**
 * @brief Retrieves the view's current clipping region from the app_server.
 *
 * The region is always fetched from the server because the client has no way
 * to know when the server-side clipping has changed.  During printing the
 * clipping region is replaced by the print rectangle.
 *
 * @param region Receives the current clipping region; must not be NULL.
 *               It is cleared before being filled.
 */
void
BView::GetClippingRegion(BRegion* region) const
{
	if (!region)
		return;

	// NOTE: the client has no idea when the clipping in the server
	// changed, so it is always read from the server
	region->MakeEmpty();


	if (fOwner) {
		if (fIsPrinting && _CheckOwnerLock()) {
			region->Set(fState->print_rect);
			return;
		}

		_CheckLockAndSwitchCurrent();
		fOwner->fLink->StartMessage(AS_VIEW_GET_CLIP_REGION);

 		int32 code;
 		if (fOwner->fLink->FlushWithReply(code) == B_OK
 			&& code == B_OK) {
			fOwner->fLink->ReadRegion(region);
			fState->valid_flags |= B_VIEW_CLIP_REGION_BIT;
		}
	}
}


/**
 * @brief Restricts drawing to the intersection of the current clip and @p region.
 *
 * Passing NULL removes any user-defined clipping constraint.  The change is
 * forwarded to the app_server and the local clip-region cache is invalidated.
 *
 * @param region The new user clip region, or NULL to clear user clipping.
 */
void
BView::ConstrainClippingRegion(BRegion* region)
{
	if (_CheckOwnerLockAndSwitchCurrent()) {
		fOwner->fLink->StartMessage(AS_VIEW_SET_CLIP_REGION);

		fOwner->fLink->Attach<bool>(region != NULL);
		if (region != NULL)
			fOwner->fLink->AttachRegion(*region);

		_FlushIfNotInTransaction();

		fState->valid_flags &= ~B_VIEW_CLIP_REGION_BIT;
		fState->archiving_flags |= B_VIEW_CLIP_REGION_BIT;
	}
}


/**
 * @brief Intersects the clipping region with a rectangle.
 *
 * @param rect The rectangle to intersect with, in view-local coordinates.
 * @see BView::ClipToInverseRect()
 */
void
BView::ClipToRect(BRect rect)
{
	_ClipToRect(rect, false);
}


/**
 * @brief Intersects the clipping region with everything outside @p rect.
 *
 * @param rect The rectangle to exclude, in view-local coordinates.
 * @see BView::ClipToRect()
 */
void
BView::ClipToInverseRect(BRect rect)
{
	_ClipToRect(rect, true);
}


/**
 * @brief Intersects the clipping region with the interior of a BShape.
 *
 * @param shape The shape defining the clipping area, in view-local coordinates.
 * @see BView::ClipToInverseShape()
 */
void
BView::ClipToShape(BShape* shape)
{
	_ClipToShape(shape, false);
}


/**
 * @brief Intersects the clipping region with everything outside @p shape.
 *
 * @param shape The shape defining the excluded area, in view-local coordinates.
 * @see BView::ClipToShape()
 */
void
BView::ClipToInverseShape(BShape* shape)
{
	_ClipToShape(shape, true);
}


//	#pragma mark - Drawing Functions


/**
 * @brief Draws a sub-region of a bitmap into a destination rectangle asynchronously.
 *
 * Sends AS_VIEW_DRAW_BITMAP without waiting for the server to complete.  The
 * bitmap is scaled/filtered as needed to fill @p viewRect.
 *
 * @param bitmap     The source bitmap.
 * @param bitmapRect The source rectangle within the bitmap's bounds.
 * @param viewRect   The destination rectangle in view-local coordinates.
 * @param options    Bitmap drawing options (e.g. B_FILTER_BITMAP_BILINEAR).
 *
 * @see BView::DrawBitmap()
 */
void
BView::DrawBitmapAsync(const BBitmap* bitmap, BRect bitmapRect, BRect viewRect,
	uint32 options)
{
	if (bitmap == NULL || fOwner == NULL
		|| !bitmapRect.IsValid() || !viewRect.IsValid())
		return;

	_CheckLockAndSwitchCurrent();

	ViewDrawBitmapInfo info;
	info.bitmapToken = bitmap->_ServerToken();
	info.options = options;
	info.viewRect = viewRect;
	info.bitmapRect = bitmapRect;

	fOwner->fLink->StartMessage(AS_VIEW_DRAW_BITMAP);
	fOwner->fLink->Attach<ViewDrawBitmapInfo>(info);

	_FlushIfNotInTransaction();
}


/**
 * @brief Draws a sub-region of a bitmap into a destination rectangle asynchronously (no options).
 *
 * @param bitmap     The source bitmap.
 * @param bitmapRect The source rectangle within the bitmap.
 * @param viewRect   The destination rectangle in view-local coordinates.
 */
void
BView::DrawBitmapAsync(const BBitmap* bitmap, BRect bitmapRect, BRect viewRect)
{
	DrawBitmapAsync(bitmap, bitmapRect, viewRect, 0);
}


/**
 * @brief Draws the entire bitmap scaled into @p viewRect asynchronously.
 *
 * @param bitmap   The source bitmap (full bounds used as the source rect).
 * @param viewRect The destination rectangle in view-local coordinates.
 */
void
BView::DrawBitmapAsync(const BBitmap* bitmap, BRect viewRect)
{
	if (bitmap && fOwner) {
		DrawBitmapAsync(bitmap, bitmap->Bounds().OffsetToCopy(B_ORIGIN),
			viewRect, 0);
	}
}


/**
 * @brief Draws a bitmap at @p where (1:1 pixel mapping) asynchronously.
 *
 * @param bitmap The source bitmap.
 * @param where  The position of the bitmap's top-left in view-local coordinates.
 */
void
BView::DrawBitmapAsync(const BBitmap* bitmap, BPoint where)
{
	if (bitmap == NULL || fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	ViewDrawBitmapInfo info;
	info.bitmapToken = bitmap->_ServerToken();
	info.options = 0;
	info.bitmapRect = bitmap->Bounds().OffsetToCopy(B_ORIGIN);
	info.viewRect = info.bitmapRect.OffsetToCopy(where);

	fOwner->fLink->StartMessage(AS_VIEW_DRAW_BITMAP);
	fOwner->fLink->Attach<ViewDrawBitmapInfo>(info);

	_FlushIfNotInTransaction();
}


/**
 * @brief Draws a bitmap at the current pen location asynchronously.
 *
 * @param bitmap The source bitmap to draw.
 */
void
BView::DrawBitmapAsync(const BBitmap* bitmap)
{
	DrawBitmapAsync(bitmap, PenLocation());
}


/**
 * @brief Draws a sub-region of a bitmap into a destination rectangle (synchronous).
 *
 * Calls DrawBitmapAsync() followed by Sync() to guarantee the bitmap is rendered
 * before this function returns.
 *
 * @param bitmap     The source bitmap.
 * @param bitmapRect The source rectangle within the bitmap.
 * @param viewRect   The destination rectangle in view-local coordinates.
 * @param options    Bitmap drawing options.
 */
void
BView::DrawBitmap(const BBitmap* bitmap, BRect bitmapRect, BRect viewRect,
	uint32 options)
{
	if (fOwner) {
		DrawBitmapAsync(bitmap, bitmapRect, viewRect, options);
		Sync();
	}
}


/**
 * @brief Draws a sub-region of a bitmap into a destination rectangle (synchronous, no options).
 *
 * @param bitmap     The source bitmap.
 * @param bitmapRect The source rectangle.
 * @param viewRect   The destination rectangle in view-local coordinates.
 */
void
BView::DrawBitmap(const BBitmap* bitmap, BRect bitmapRect, BRect viewRect)
{
	if (fOwner) {
		DrawBitmapAsync(bitmap, bitmapRect, viewRect, 0);
		Sync();
	}
}


/**
 * @brief Draws the entire bitmap scaled into @p viewRect (synchronous).
 *
 * @param bitmap   The source bitmap.
 * @param viewRect The destination rectangle in view-local coordinates.
 */
void
BView::DrawBitmap(const BBitmap* bitmap, BRect viewRect)
{
	if (bitmap && fOwner) {
		DrawBitmap(bitmap, bitmap->Bounds().OffsetToCopy(B_ORIGIN), viewRect,
			0);
	}
}


/**
 * @brief Draws a bitmap at @p where (1:1 pixel mapping, synchronous).
 *
 * @param bitmap The source bitmap.
 * @param where  The position of the top-left corner in view-local coordinates.
 */
void
BView::DrawBitmap(const BBitmap* bitmap, BPoint where)
{
	if (fOwner) {
		DrawBitmapAsync(bitmap, where);
		Sync();
	}
}


/**
 * @brief Draws a bitmap at the current pen location (synchronous).
 *
 * @param bitmap The source bitmap to draw.
 */
void
BView::DrawBitmap(const BBitmap* bitmap)
{
	DrawBitmap(bitmap, PenLocation());
}


/**
 * @brief Tiles a bitmap over @p viewRect starting at @p phase (asynchronous).
 *
 * The bitmap is repeated in both axes to fill @p viewRect.  The @p phase
 * controls the alignment offset of the tile pattern.
 *
 * @param bitmap   The bitmap tile to repeat.
 * @param viewRect The destination area in view-local coordinates.
 * @param phase    The tiling phase (offset within one tile period).
 *
 * @see BView::DrawTiledBitmap()
 */
void
BView::DrawTiledBitmapAsync(const BBitmap* bitmap, BRect viewRect,
	BPoint phase)
{
	if (bitmap == NULL || fOwner == NULL || !viewRect.IsValid())
		return;

	_CheckLockAndSwitchCurrent();

	ViewDrawBitmapInfo info;
	info.bitmapToken = bitmap->_ServerToken();
	info.options = B_TILE_BITMAP;
	info.viewRect = viewRect;
	info.bitmapRect = bitmap->Bounds().OffsetToCopy(phase);

	fOwner->fLink->StartMessage(AS_VIEW_DRAW_BITMAP);
	fOwner->fLink->Attach<ViewDrawBitmapInfo>(info);

	_FlushIfNotInTransaction();
}


/**
 * @brief Tiles a bitmap over @p viewRect (synchronous).
 *
 * @param bitmap   The bitmap tile to repeat.
 * @param viewRect The destination area in view-local coordinates.
 * @param phase    The tiling phase.
 */
void
BView::DrawTiledBitmap(const BBitmap* bitmap, BRect viewRect, BPoint phase)
{
	if (fOwner) {
		DrawTiledBitmapAsync(bitmap, viewRect, phase);
		Sync();
	}
}


/**
 * @brief Draws a single character at the current pen location.
 *
 * @param c The character to draw.
 */
void
BView::DrawChar(char c)
{
	DrawString(&c, 1, PenLocation());
}


/**
 * @brief Draws a single character at @p location.
 *
 * @param c        The character to draw.
 * @param location The baseline position in view-local coordinates.
 */
void
BView::DrawChar(char c, BPoint location)
{
	DrawString(&c, 1, location);
}


/**
 * @brief Draws a NUL-terminated string at the current pen location.
 *
 * @param string The UTF-8 string to render.
 * @param delta  Optional per-character spacing adjustments; may be NULL.
 */
void
BView::DrawString(const char* string, escapement_delta* delta)
{
	if (string == NULL)
		return;

	DrawString(string, strlen(string), PenLocation(), delta);
}


/**
 * @brief Draws a NUL-terminated string at @p location.
 *
 * @param string   The UTF-8 string to render.
 * @param location The baseline start position in view-local coordinates.
 * @param delta    Optional per-character spacing adjustments; may be NULL.
 */
void
BView::DrawString(const char* string, BPoint location, escapement_delta* delta)
{
	if (string == NULL)
		return;

	DrawString(string, strlen(string), location, delta);
}


/**
 * @brief Draws @p length bytes of @p string at the current pen location.
 *
 * @param string The UTF-8 string (need not be NUL-terminated).
 * @param length Number of bytes to draw.
 * @param delta  Optional per-character spacing adjustments; may be NULL.
 */
void
BView::DrawString(const char* string, int32 length, escapement_delta* delta)
{
	DrawString(string, length, PenLocation(), delta);
}


/**
 * @brief Draws @p length bytes of @p string at @p location.
 *
 * Sends AS_DRAW_STRING_WITH_DELTA or AS_DRAW_STRING to the app_server and
 * invalidates the pen location cache because the pen moves to the end of
 * the rendered text.
 *
 * @param string   The UTF-8 string to render (need not be NUL-terminated).
 * @param length   Number of bytes to draw.
 * @param location The baseline start position in view-local coordinates.
 * @param delta    Optional escapement adjustments; if NULL, AS_DRAW_STRING is used.
 */
void
BView::DrawString(const char* string, int32 length, BPoint location,
	escapement_delta* delta)
{
	if (fOwner == NULL || string == NULL || length < 1)
		return;

	_CheckLockAndSwitchCurrent();

	ViewDrawStringInfo info;
	info.stringLength = length;
	info.location = location;
	if (delta != NULL)
		info.delta = *delta;

	// quite often delta will be NULL
	if (delta)
		fOwner->fLink->StartMessage(AS_DRAW_STRING_WITH_DELTA);
	else
		fOwner->fLink->StartMessage(AS_DRAW_STRING);

	fOwner->fLink->Attach<ViewDrawStringInfo>(info);
	fOwner->fLink->Attach(string, length);

	_FlushIfNotInTransaction();

	// this modifies our pen location, so we invalidate the flag.
	fState->valid_flags &= ~B_VIEW_PEN_LOCATION_BIT;
}


/**
 * @brief Draws a NUL-terminated string with each glyph at an individual location.
 *
 * @param string        The UTF-8 string to render.
 * @param locations     Array of baseline positions, one per character.
 * @param locationCount Number of entries in @p locations.
 */
void
BView::DrawString(const char* string, const BPoint* locations,
	int32 locationCount)
{
	if (string == NULL)
		return;

	DrawString(string, strlen(string), locations, locationCount);
}


/**
 * @brief Draws @p length bytes of @p string with each glyph at an individual location.
 *
 * Sends AS_DRAW_STRING_WITH_OFFSETS; the pen location is invalidated after.
 *
 * @param string        The UTF-8 string to render (need not be NUL-terminated).
 * @param length        Number of bytes in @p string.
 * @param locations     Array of baseline BPoint positions, one per character.
 * @param locationCount Number of entries in @p locations.
 */
void
BView::DrawString(const char* string, int32 length, const BPoint* locations,
	int32 locationCount)
{
	if (fOwner == NULL || string == NULL || length < 1 || locations == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_DRAW_STRING_WITH_OFFSETS);

	fOwner->fLink->Attach<int32>(length);
	fOwner->fLink->Attach<int32>(locationCount);
	fOwner->fLink->Attach(string, length);
	fOwner->fLink->Attach(locations, locationCount * sizeof(BPoint));

	_FlushIfNotInTransaction();

	// this modifies our pen location, so we invalidate the flag.
	fState->valid_flags &= ~B_VIEW_PEN_LOCATION_BIT;
}


/**
 * @brief Strokes an ellipse specified by centre and radii.
 *
 * @param center  The centre point in view-local coordinates.
 * @param xRadius Horizontal radius.
 * @param yRadius Vertical radius.
 * @param pattern The fill pattern (e.g. B_SOLID_HIGH).
 */
void
BView::StrokeEllipse(BPoint center, float xRadius, float yRadius,
	::pattern pattern)
{
	StrokeEllipse(BRect(center.x - xRadius, center.y - yRadius,
		center.x + xRadius, center.y + yRadius), pattern);
}


/**
 * @brief Strokes an ellipse bounded by @p rect.
 *
 * @param rect    The bounding rectangle of the ellipse in view-local coordinates.
 * @param pattern The fill pattern.
 */
void
BView::StrokeEllipse(BRect rect, ::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_STROKE_ELLIPSE);
	fOwner->fLink->Attach<BRect>(rect);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes an ellipse with a gradient stroke, specified by centre and radii.
 *
 * @param center   The centre point.
 * @param xRadius  Horizontal radius.
 * @param yRadius  Vertical radius.
 * @param gradient The gradient to apply along the stroke.
 */
void
BView::StrokeEllipse(BPoint center, float xRadius, float yRadius,
	const BGradient& gradient)
{
	StrokeEllipse(BRect(center.x - xRadius, center.y - yRadius,
		center.x + xRadius, center.y + yRadius), gradient);
}


/**
 * @brief Strokes an ellipse bounded by @p rect using a gradient stroke.
 *
 * @param rect     The bounding rectangle.
 * @param gradient The gradient to apply along the stroke.
 */
void
BView::StrokeEllipse(BRect rect, const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_STROKE_ELLIPSE_GRADIENT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills an ellipse specified by centre and radii.
 *
 * @param center  The centre point in view-local coordinates.
 * @param xRadius Horizontal radius.
 * @param yRadius Vertical radius.
 * @param pattern The fill pattern.
 */
void
BView::FillEllipse(BPoint center, float xRadius, float yRadius,
	::pattern pattern)
{
	FillEllipse(BRect(center.x - xRadius, center.y - yRadius,
		center.x + xRadius, center.y + yRadius), pattern);
}


/**
 * @brief Fills an ellipse with a gradient, specified by centre and radii.
 *
 * @param center   The centre point.
 * @param xRadius  Horizontal radius.
 * @param yRadius  Vertical radius.
 * @param gradient The fill gradient.
 */
void
BView::FillEllipse(BPoint center, float xRadius, float yRadius,
	const BGradient& gradient)
{
	FillEllipse(BRect(center.x - xRadius, center.y - yRadius,
		center.x + xRadius, center.y + yRadius), gradient);
}


/**
 * @brief Fills an ellipse bounded by @p rect.
 *
 * @param rect    The bounding rectangle in view-local coordinates.
 * @param pattern The fill pattern.
 */
void
BView::FillEllipse(BRect rect, ::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_FILL_ELLIPSE);
	fOwner->fLink->Attach<BRect>(rect);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills an ellipse bounded by @p rect using a gradient.
 *
 * @param rect     The bounding rectangle.
 * @param gradient The fill gradient.
 */
void
BView::FillEllipse(BRect rect, const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_FILL_ELLIPSE_GRADIENT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes an arc specified by centre, radii, and angle range.
 *
 * @param center     The centre point.
 * @param xRadius    Horizontal radius.
 * @param yRadius    Vertical radius.
 * @param startAngle Starting angle in degrees (0 = right, counter-clockwise).
 * @param arcAngle   Sweep angle in degrees.
 * @param pattern    The stroke pattern.
 */
void
BView::StrokeArc(BPoint center, float xRadius, float yRadius, float startAngle,
	float arcAngle, ::pattern pattern)
{
	StrokeArc(BRect(center.x - xRadius, center.y - yRadius, center.x + xRadius,
		center.y + yRadius), startAngle, arcAngle, pattern);
}


/**
 * @brief Strokes an arc within the ellipse bounded by @p rect.
 *
 * @param rect       The bounding rectangle of the full ellipse.
 * @param startAngle Starting angle in degrees.
 * @param arcAngle   Sweep angle in degrees.
 * @param pattern    The stroke pattern.
 */
void
BView::StrokeArc(BRect rect, float startAngle, float arcAngle,
	::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_STROKE_ARC);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->Attach<float>(startAngle);
	fOwner->fLink->Attach<float>(arcAngle);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes an arc with a gradient, specified by centre, radii, and angle range.
 *
 * @param center     The centre point.
 * @param xRadius    Horizontal radius.
 * @param yRadius    Vertical radius.
 * @param startAngle Starting angle in degrees.
 * @param arcAngle   Sweep angle in degrees.
 * @param gradient   The stroke gradient.
 */
void
BView::StrokeArc(BPoint center, float xRadius, float yRadius, float startAngle,
	float arcAngle, const BGradient& gradient)
{
	StrokeArc(BRect(center.x - xRadius, center.y - yRadius, center.x + xRadius,
		center.y + yRadius), startAngle, arcAngle, gradient);
}


/**
 * @brief Strokes an arc within @p rect using a gradient stroke.
 *
 * @param rect       The bounding rectangle of the full ellipse.
 * @param startAngle Starting angle in degrees.
 * @param arcAngle   Sweep angle in degrees.
 * @param gradient   The stroke gradient.
 */
void
BView::StrokeArc(BRect rect, float startAngle, float arcAngle,
	const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_STROKE_ARC_GRADIENT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->Attach<float>(startAngle);
	fOwner->fLink->Attach<float>(arcAngle);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills an arc (pie-slice) specified by centre, radii, and angle range.
 *
 * @param center     The centre point.
 * @param xRadius    Horizontal radius.
 * @param yRadius    Vertical radius.
 * @param startAngle Starting angle in degrees.
 * @param arcAngle   Sweep angle in degrees.
 * @param pattern    The fill pattern.
 */
void
BView::FillArc(BPoint center,float xRadius, float yRadius, float startAngle,
	float arcAngle, ::pattern pattern)
{
	FillArc(BRect(center.x - xRadius, center.y - yRadius, center.x + xRadius,
		center.y + yRadius), startAngle, arcAngle, pattern);
}


/**
 * @brief Fills an arc with a gradient, specified by centre, radii, and angle range.
 *
 * @param center     The centre point.
 * @param xRadius    Horizontal radius.
 * @param yRadius    Vertical radius.
 * @param startAngle Starting angle in degrees.
 * @param arcAngle   Sweep angle in degrees.
 * @param gradient   The fill gradient.
 */
void
BView::FillArc(BPoint center,float xRadius, float yRadius, float startAngle,
	float arcAngle, const BGradient& gradient)
{
	FillArc(BRect(center.x - xRadius, center.y - yRadius, center.x + xRadius,
		center.y + yRadius), startAngle, arcAngle, gradient);
}


/**
 * @brief Fills an arc (pie-slice) within @p rect.
 *
 * @param rect       The bounding rectangle of the full ellipse.
 * @param startAngle Starting angle in degrees.
 * @param arcAngle   Sweep angle in degrees.
 * @param pattern    The fill pattern.
 */
void
BView::FillArc(BRect rect, float startAngle, float arcAngle,
	::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_FILL_ARC);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->Attach<float>(startAngle);
	fOwner->fLink->Attach<float>(arcAngle);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills an arc within @p rect using a gradient.
 *
 * @param rect       The bounding rectangle of the full ellipse.
 * @param startAngle Starting angle in degrees.
 * @param arcAngle   Sweep angle in degrees.
 * @param gradient   The fill gradient.
 */
void
BView::FillArc(BRect rect, float startAngle, float arcAngle,
	const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_FILL_ARC_GRADIENT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->Attach<float>(startAngle);
	fOwner->fLink->Attach<float>(arcAngle);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes a cubic Bezier curve defined by four control points.
 *
 * @param controlPoints Array of four BPoints: start, control 1, control 2, end.
 * @param pattern       The stroke pattern.
 */
void
BView::StrokeBezier(BPoint* controlPoints, ::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_STROKE_BEZIER);
	fOwner->fLink->Attach<BPoint>(controlPoints[0]);
	fOwner->fLink->Attach<BPoint>(controlPoints[1]);
	fOwner->fLink->Attach<BPoint>(controlPoints[2]);
	fOwner->fLink->Attach<BPoint>(controlPoints[3]);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes a cubic Bezier curve with a gradient stroke.
 *
 * @param controlPoints Array of four BPoints: start, control 1, control 2, end.
 * @param gradient      The stroke gradient.
 */
void
BView::StrokeBezier(BPoint* controlPoints, const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_STROKE_BEZIER_GRADIENT);
	fOwner->fLink->Attach<BPoint>(controlPoints[0]);
	fOwner->fLink->Attach<BPoint>(controlPoints[1]);
	fOwner->fLink->Attach<BPoint>(controlPoints[2]);
	fOwner->fLink->Attach<BPoint>(controlPoints[3]);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills the area enclosed by a cubic Bezier curve.
 *
 * @param controlPoints Array of four BPoints: start, control 1, control 2, end.
 * @param pattern       The fill pattern.
 */
void
BView::FillBezier(BPoint* controlPoints, ::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_FILL_BEZIER);
	fOwner->fLink->Attach<BPoint>(controlPoints[0]);
	fOwner->fLink->Attach<BPoint>(controlPoints[1]);
	fOwner->fLink->Attach<BPoint>(controlPoints[2]);
	fOwner->fLink->Attach<BPoint>(controlPoints[3]);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills the area enclosed by a cubic Bezier curve using a gradient.
 *
 * @param controlPoints Array of four BPoints: start, control 1, control 2, end.
 * @param gradient      The fill gradient.
 */
void
BView::FillBezier(BPoint* controlPoints, const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_FILL_BEZIER_GRADIENT);
	fOwner->fLink->Attach<BPoint>(controlPoints[0]);
	fOwner->fLink->Attach<BPoint>(controlPoints[1]);
	fOwner->fLink->Attach<BPoint>(controlPoints[2]);
	fOwner->fLink->Attach<BPoint>(controlPoints[3]);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes a polygon.
 *
 * @param polygon The polygon to stroke.
 * @param closed  If true, an extra line segment connects the last point to the first.
 * @param pattern The stroke pattern.
 */
void
BView::StrokePolygon(const BPolygon* polygon, bool closed, ::pattern pattern)
{
	if (polygon == NULL)
		return;

	StrokePolygon(polygon->fPoints, polygon->fCount, polygon->Frame(), closed,
		pattern);
}


/**
 * @brief Strokes a polygon defined by a point array.
 *
 * The bounding box is computed automatically.
 *
 * @param pointArray Array of vertex points.
 * @param numPoints  Number of points in @p pointArray.
 * @param closed     If true, close the polygon.
 * @param pattern    The stroke pattern.
 */
void
BView::StrokePolygon(const BPoint* pointArray, int32 numPoints, bool closed,
	::pattern pattern)
{
	if (pointArray == NULL
		|| numPoints <= 1
		|| fOwner == NULL)
		return;

	BRect bounds = BPolygon::_ComputeBounds(pointArray, numPoints);

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	if (fOwner->fLink->StartMessage(AS_STROKE_POLYGON,
			numPoints * sizeof(BPoint) + sizeof(BRect) + sizeof(bool)
				+ sizeof(int32)) == B_OK) {
		fOwner->fLink->Attach<BRect>(bounds);
		fOwner->fLink->Attach<bool>(closed);
		fOwner->fLink->Attach<int32>(numPoints);
		fOwner->fLink->Attach(pointArray, numPoints * sizeof(BPoint));

		_FlushIfNotInTransaction();
	} else {
		fprintf(stderr, "ERROR: Can't send polygon to app_server!\n");
	}
}


/**
 * @brief Strokes a polygon mapped into @p bounds.
 *
 * The vertices in @p pointArray are mapped from the polygon's natural frame
 * to @p bounds before drawing.
 *
 * @param pointArray Array of vertex points.
 * @param numPoints  Number of points.
 * @param bounds     Target bounding rectangle the polygon is mapped into.
 * @param closed     If true, close the polygon.
 * @param pattern    The stroke pattern.
 */
void
BView::StrokePolygon(const BPoint* pointArray, int32 numPoints, BRect bounds,
	bool closed, ::pattern pattern)
{
	if (pointArray == NULL
		|| numPoints <= 1
		|| fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	BPolygon polygon(pointArray, numPoints);
	polygon.MapTo(polygon.Frame(), bounds);

	if (fOwner->fLink->StartMessage(AS_STROKE_POLYGON,
			polygon.fCount * sizeof(BPoint) + sizeof(BRect) + sizeof(bool)
				+ sizeof(int32)) == B_OK) {
		fOwner->fLink->Attach<BRect>(polygon.Frame());
		fOwner->fLink->Attach<bool>(closed);
		fOwner->fLink->Attach<int32>(polygon.fCount);
		fOwner->fLink->Attach(polygon.fPoints, polygon.fCount * sizeof(BPoint));

		_FlushIfNotInTransaction();
	} else {
		fprintf(stderr, "ERROR: Can't send polygon to app_server!\n");
	}
}


/**
 * @brief Strokes a polygon with a gradient stroke.
 *
 * @param polygon  The polygon to stroke.
 * @param closed   If true, close the polygon.
 * @param gradient The stroke gradient.
 */
void
BView::StrokePolygon(const BPolygon* polygon, bool closed, const BGradient& gradient)
{
	if (polygon == NULL)
		return;

	StrokePolygon(polygon->fPoints, polygon->fCount, polygon->Frame(), closed,
		gradient);
}


/**
 * @brief Strokes a polygon (from a point array) with a gradient stroke.
 *
 * @param pointArray Array of vertex points.
 * @param numPoints  Number of points.
 * @param closed     If true, close the polygon.
 * @param gradient   The stroke gradient.
 */
void
BView::StrokePolygon(const BPoint* pointArray, int32 numPoints, bool closed,
	const BGradient& gradient)
{
	if (pointArray == NULL
		|| numPoints <= 1
		|| fOwner == NULL)
		return;

	BRect bounds = BPolygon::_ComputeBounds(pointArray, numPoints);

	_CheckLockAndSwitchCurrent();

	if (fOwner->fLink->StartMessage(AS_STROKE_POLYGON_GRADIENT,
			numPoints * sizeof(BPoint) + sizeof(BRect) + sizeof(bool)
				+ sizeof(int32)) == B_OK) {
		fOwner->fLink->Attach<BRect>(bounds);
		fOwner->fLink->Attach<bool>(closed);
		fOwner->fLink->Attach<int32>(numPoints);
		fOwner->fLink->Attach(pointArray, numPoints * sizeof(BPoint));
		fOwner->fLink->AttachGradient(gradient);

		_FlushIfNotInTransaction();
	} else {
		fprintf(stderr, "ERROR: Can't send polygon to app_server!\n");
	}
}


/**
 * @brief Strokes a polygon mapped into @p bounds, with a gradient stroke.
 *
 * @param pointArray Array of vertex points.
 * @param numPoints  Number of points.
 * @param bounds     Target bounding rectangle the polygon is mapped into.
 * @param closed     If true, close the polygon.
 * @param gradient   The stroke gradient.
 */
void
BView::StrokePolygon(const BPoint* pointArray, int32 numPoints, BRect bounds,
	bool closed, const BGradient& gradient)
{
	if (pointArray == NULL
		|| numPoints <= 1
		|| fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	BPolygon polygon(pointArray, numPoints);
	polygon.MapTo(polygon.Frame(), bounds);

	if (fOwner->fLink->StartMessage(AS_STROKE_POLYGON_GRADIENT,
			polygon.fCount * sizeof(BPoint) + sizeof(BRect) + sizeof(bool)
				+ sizeof(int32)) == B_OK) {
		fOwner->fLink->Attach<BRect>(polygon.Frame());
		fOwner->fLink->Attach<bool>(closed);
		fOwner->fLink->Attach<int32>(polygon.fCount);
		fOwner->fLink->Attach(polygon.fPoints, polygon.fCount * sizeof(BPoint));
		fOwner->fLink->AttachGradient(gradient);

		_FlushIfNotInTransaction();
	} else {
		fprintf(stderr, "ERROR: Can't send polygon to app_server!\n");
	}
}


/**
 * @brief Fills the interior of a polygon.
 *
 * @param polygon The polygon to fill (must have more than 2 vertices).
 * @param pattern The fill pattern.
 */
void
BView::FillPolygon(const BPolygon* polygon, ::pattern pattern)
{
	if (polygon == NULL
		|| polygon->fCount <= 2
		|| fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	if (fOwner->fLink->StartMessage(AS_FILL_POLYGON,
			polygon->fCount * sizeof(BPoint) + sizeof(BRect) + sizeof(int32))
				== B_OK) {
		fOwner->fLink->Attach<BRect>(polygon->Frame());
		fOwner->fLink->Attach<int32>(polygon->fCount);
		fOwner->fLink->Attach(polygon->fPoints,
			polygon->fCount * sizeof(BPoint));

		_FlushIfNotInTransaction();
	} else {
		fprintf(stderr, "ERROR: Can't send polygon to app_server!\n");
	}
}


/**
 * @brief Fills the interior of a polygon using a gradient.
 *
 * @param polygon  The polygon to fill.
 * @param gradient The fill gradient.
 */
void
BView::FillPolygon(const BPolygon* polygon, const BGradient& gradient)
{
	if (polygon == NULL
		|| polygon->fCount <= 2
		|| fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	if (fOwner->fLink->StartMessage(AS_FILL_POLYGON_GRADIENT,
			polygon->fCount * sizeof(BPoint) + sizeof(BRect) + sizeof(int32))
				== B_OK) {
		fOwner->fLink->Attach<BRect>(polygon->Frame());
		fOwner->fLink->Attach<int32>(polygon->fCount);
		fOwner->fLink->Attach(polygon->fPoints,
			polygon->fCount * sizeof(BPoint));
		fOwner->fLink->AttachGradient(gradient);

		_FlushIfNotInTransaction();
	} else {
		fprintf(stderr, "ERROR: Can't send polygon to app_server!\n");
	}
}


/**
 * @brief Fills the interior of a polygon defined by a point array.
 *
 * @param pointArray Array of vertex points (must have more than 2 entries).
 * @param numPoints  Number of points.
 * @param pattern    The fill pattern.
 */
void
BView::FillPolygon(const BPoint* pointArray, int32 numPoints, ::pattern pattern)
{
	if (pointArray == NULL
		|| numPoints <= 2
		|| fOwner == NULL)
		return;

	BRect bounds = BPolygon::_ComputeBounds(pointArray, numPoints);

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	if (fOwner->fLink->StartMessage(AS_FILL_POLYGON,
			numPoints * sizeof(BPoint) + sizeof(BRect) + sizeof(int32))
				== B_OK) {
		fOwner->fLink->Attach<BRect>(bounds);
		fOwner->fLink->Attach<int32>(numPoints);
		fOwner->fLink->Attach(pointArray, numPoints * sizeof(BPoint));

		_FlushIfNotInTransaction();
	} else {
		fprintf(stderr, "ERROR: Can't send polygon to app_server!\n");
	}
}


/**
 * @brief Fills a polygon (from a point array) using a gradient.
 *
 * @param pointArray Array of vertex points.
 * @param numPoints  Number of points.
 * @param gradient   The fill gradient.
 */
void
BView::FillPolygon(const BPoint* pointArray, int32 numPoints,
	const BGradient& gradient)
{
	if (pointArray == NULL
		|| numPoints <= 2
		|| fOwner == NULL)
		return;

	BRect bounds = BPolygon::_ComputeBounds(pointArray, numPoints);

	_CheckLockAndSwitchCurrent();

	if (fOwner->fLink->StartMessage(AS_FILL_POLYGON_GRADIENT,
			numPoints * sizeof(BPoint) + sizeof(BRect) + sizeof(int32))
				== B_OK) {
		fOwner->fLink->Attach<BRect>(bounds);
		fOwner->fLink->Attach<int32>(numPoints);
		fOwner->fLink->Attach(pointArray, numPoints * sizeof(BPoint));
		fOwner->fLink->AttachGradient(gradient);

		_FlushIfNotInTransaction();
	} else {
		fprintf(stderr, "ERROR: Can't send polygon to app_server!\n");
	}
}


/**
 * @brief Fills a polygon mapped into @p bounds.
 *
 * @param pointArray Array of vertex points.
 * @param numPoints  Number of points.
 * @param bounds     The target bounding rectangle.
 * @param pattern    The fill pattern.
 */
void
BView::FillPolygon(const BPoint* pointArray, int32 numPoints, BRect bounds,
	::pattern pattern)
{
	if (pointArray == NULL)
		return;

	BPolygon polygon(pointArray, numPoints);

	polygon.MapTo(polygon.Frame(), bounds);
	FillPolygon(&polygon, pattern);
}


/**
 * @brief Fills a polygon mapped into @p bounds using a gradient.
 *
 * @param pointArray Array of vertex points.
 * @param numPoints  Number of points.
 * @param bounds     The target bounding rectangle.
 * @param gradient   The fill gradient.
 */
void
BView::FillPolygon(const BPoint* pointArray, int32 numPoints, BRect bounds,
	const BGradient& gradient)
{
	if (pointArray == NULL)
		return;

	BPolygon polygon(pointArray, numPoints);

	polygon.MapTo(polygon.Frame(), bounds);
	FillPolygon(&polygon, gradient);
}


/**
 * @brief Strokes the outline of a rectangle.
 *
 * @param rect    The rectangle to stroke in view-local coordinates.
 * @param pattern The stroke pattern.
 */
void
BView::StrokeRect(BRect rect, ::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_STROKE_RECT);
	fOwner->fLink->Attach<BRect>(rect);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes the outline of a rectangle with a gradient stroke.
 *
 * @param rect     The rectangle to stroke.
 * @param gradient The stroke gradient.
 */
void
BView::StrokeRect(BRect rect, const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_STROKE_RECT_GRADIENT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills a rectangle with the given pattern.
 *
 * Invalid rectangles (where right < left or bottom < top) are silently
 * ignored for BeOS compatibility.
 *
 * @param rect    The rectangle to fill in view-local coordinates.
 * @param pattern The fill pattern.
 */
void
BView::FillRect(BRect rect, ::pattern pattern)
{
	if (fOwner == NULL)
		return;

	// NOTE: ensuring compatibility with R5,
	// invalid rects are not filled, they are stroked though!
	if (!rect.IsValid())
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_FILL_RECT);
	fOwner->fLink->Attach<BRect>(rect);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills a rectangle using a gradient.
 *
 * @param rect     The rectangle to fill.
 * @param gradient The fill gradient.
 */
void
BView::FillRect(BRect rect, const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	// NOTE: ensuring compatibility with R5,
	// invalid rects are not filled, they are stroked though!
	if (!rect.IsValid())
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_FILL_RECT_GRADIENT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes the outline of a rectangle with rounded corners.
 *
 * @param rect    The bounding rectangle.
 * @param xRadius The horizontal corner radius.
 * @param yRadius The vertical corner radius.
 * @param pattern The stroke pattern.
 */
void
BView::StrokeRoundRect(BRect rect, float xRadius, float yRadius,
	::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_STROKE_ROUNDRECT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->Attach<float>(xRadius);
	fOwner->fLink->Attach<float>(yRadius);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes a rounded rectangle with a gradient stroke.
 *
 * @param rect     The bounding rectangle.
 * @param xRadius  The horizontal corner radius.
 * @param yRadius  The vertical corner radius.
 * @param gradient The stroke gradient.
 */
void
BView::StrokeRoundRect(BRect rect, float xRadius, float yRadius,
	const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_STROKE_ROUNDRECT_GRADIENT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->Attach<float>(xRadius);
	fOwner->fLink->Attach<float>(yRadius);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills a rectangle with rounded corners.
 *
 * @param rect    The bounding rectangle.
 * @param xRadius The horizontal corner radius.
 * @param yRadius The vertical corner radius.
 * @param pattern The fill pattern.
 */
void
BView::FillRoundRect(BRect rect, float xRadius, float yRadius,
	::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_FILL_ROUNDRECT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->Attach<float>(xRadius);
	fOwner->fLink->Attach<float>(yRadius);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills a rounded rectangle using a gradient.
 *
 * @param rect     The bounding rectangle.
 * @param xRadius  The horizontal corner radius.
 * @param yRadius  The vertical corner radius.
 * @param gradient The fill gradient.
 */
void
BView::FillRoundRect(BRect rect, float xRadius, float yRadius,
	const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_FILL_ROUNDRECT_GRADIENT);
	fOwner->fLink->Attach<BRect>(rect);
	fOwner->fLink->Attach<float>(xRadius);
	fOwner->fLink->Attach<float>(yRadius);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills all rectangles in a region.
 *
 * @param region  The region to fill; must not be NULL.
 * @param pattern The fill pattern.
 */
void
BView::FillRegion(BRegion* region, ::pattern pattern)
{
	if (region == NULL || fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_FILL_REGION);
	fOwner->fLink->AttachRegion(*region);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills all rectangles in a region using a gradient.
 *
 * @param region   The region to fill; must not be NULL.
 * @param gradient The fill gradient.
 */
void
BView::FillRegion(BRegion* region, const BGradient& gradient)
{
	if (region == NULL || fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_FILL_REGION_GRADIENT);
	fOwner->fLink->AttachRegion(*region);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes a triangle with a pre-computed bounding rectangle.
 *
 * @param point1  First vertex.
 * @param point2  Second vertex.
 * @param point3  Third vertex.
 * @param bounds  Bounding rectangle of the three vertices.
 * @param pattern The stroke pattern.
 */
void
BView::StrokeTriangle(BPoint point1, BPoint point2, BPoint point3, BRect bounds,
	::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_STROKE_TRIANGLE);
	fOwner->fLink->Attach<BPoint>(point1);
	fOwner->fLink->Attach<BPoint>(point2);
	fOwner->fLink->Attach<BPoint>(point3);
	fOwner->fLink->Attach<BRect>(bounds);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes a triangle, computing the bounding rectangle automatically.
 *
 * @param point1  First vertex.
 * @param point2  Second vertex.
 * @param point3  Third vertex.
 * @param pattern The stroke pattern.
 */
void
BView::StrokeTriangle(BPoint point1, BPoint point2, BPoint point3,
	::pattern pattern)
{
	if (fOwner) {
		// we construct the smallest rectangle that contains the 3 points
		// for the 1st point
		BRect bounds(point1, point1);

		// for the 2nd point
		if (point2.x < bounds.left)
			bounds.left = point2.x;

		if (point2.y < bounds.top)
			bounds.top = point2.y;

		if (point2.x > bounds.right)
			bounds.right = point2.x;

		if (point2.y > bounds.bottom)
			bounds.bottom = point2.y;

		// for the 3rd point
		if (point3.x < bounds.left)
			bounds.left = point3.x;

		if (point3.y < bounds.top)
			bounds.top = point3.y;

		if (point3.x > bounds.right)
			bounds.right = point3.x;

		if (point3.y > bounds.bottom)
			bounds.bottom = point3.y;

		StrokeTriangle(point1, point2, point3, bounds, pattern);
	}
}


/**
 * @brief Strokes a triangle with a gradient stroke and a pre-computed bounding rectangle.
 *
 * @param point1   First vertex.
 * @param point2   Second vertex.
 * @param point3   Third vertex.
 * @param bounds   Bounding rectangle.
 * @param gradient The stroke gradient.
 */
void
BView::StrokeTriangle(BPoint point1, BPoint point2, BPoint point3, BRect bounds,
	const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_STROKE_TRIANGLE_GRADIENT);
	fOwner->fLink->Attach<BPoint>(point1);
	fOwner->fLink->Attach<BPoint>(point2);
	fOwner->fLink->Attach<BPoint>(point3);
	fOwner->fLink->Attach<BRect>(bounds);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes a triangle with a gradient stroke, computing the bounding rectangle.
 *
 * @param point1   First vertex.
 * @param point2   Second vertex.
 * @param point3   Third vertex.
 * @param gradient The stroke gradient.
 */
void
BView::StrokeTriangle(BPoint point1, BPoint point2, BPoint point3,
	const BGradient& gradient)
{
	if (fOwner) {
		// we construct the smallest rectangle that contains the 3 points
		// for the 1st point
		BRect bounds(point1, point1);

		// for the 2nd point
		if (point2.x < bounds.left)
			bounds.left = point2.x;

		if (point2.y < bounds.top)
			bounds.top = point2.y;

		if (point2.x > bounds.right)
			bounds.right = point2.x;

		if (point2.y > bounds.bottom)
			bounds.bottom = point2.y;

		// for the 3rd point
		if (point3.x < bounds.left)
			bounds.left = point3.x;

		if (point3.y < bounds.top)
			bounds.top = point3.y;

		if (point3.x > bounds.right)
			bounds.right = point3.x;

		if (point3.y > bounds.bottom)
			bounds.bottom = point3.y;

		StrokeTriangle(point1, point2, point3, bounds, gradient);
	}
}


/**
 * @brief Fills a triangle, computing the bounding rectangle automatically.
 *
 * @param point1  First vertex.
 * @param point2  Second vertex.
 * @param point3  Third vertex.
 * @param pattern The fill pattern.
 */
void
BView::FillTriangle(BPoint point1, BPoint point2, BPoint point3,
	::pattern pattern)
{
	if (fOwner) {
		// we construct the smallest rectangle that contains the 3 points
		// for the 1st point
		BRect bounds(point1, point1);

		// for the 2nd point
		if (point2.x < bounds.left)
			bounds.left = point2.x;

		if (point2.y < bounds.top)
			bounds.top = point2.y;

		if (point2.x > bounds.right)
			bounds.right = point2.x;

		if (point2.y > bounds.bottom)
			bounds.bottom = point2.y;

		// for the 3rd point
		if (point3.x < bounds.left)
			bounds.left = point3.x;

		if (point3.y < bounds.top)
			bounds.top = point3.y;

		if (point3.x > bounds.right)
			bounds.right = point3.x;

		if (point3.y > bounds.bottom)
			bounds.bottom = point3.y;

		FillTriangle(point1, point2, point3, bounds, pattern);
	}
}


/**
 * @brief Fills a triangle using a gradient, computing the bounding rectangle.
 *
 * @param point1   First vertex.
 * @param point2   Second vertex.
 * @param point3   Third vertex.
 * @param gradient The fill gradient.
 */
void
BView::FillTriangle(BPoint point1, BPoint point2, BPoint point3,
	const BGradient& gradient)
{
	if (fOwner) {
		// we construct the smallest rectangle that contains the 3 points
		// for the 1st point
		BRect bounds(point1, point1);

		// for the 2nd point
		if (point2.x < bounds.left)
			bounds.left = point2.x;

		if (point2.y < bounds.top)
			bounds.top = point2.y;

		if (point2.x > bounds.right)
			bounds.right = point2.x;

		if (point2.y > bounds.bottom)
			bounds.bottom = point2.y;

		// for the 3rd point
		if (point3.x < bounds.left)
			bounds.left = point3.x;

		if (point3.y < bounds.top)
			bounds.top = point3.y;

		if (point3.x > bounds.right)
			bounds.right = point3.x;

		if (point3.y > bounds.bottom)
			bounds.bottom = point3.y;

		FillTriangle(point1, point2, point3, bounds, gradient);
	}
}


/**
 * @brief Fills a triangle with a pre-computed bounding rectangle.
 *
 * @param point1  First vertex.
 * @param point2  Second vertex.
 * @param point3  Third vertex.
 * @param bounds  Bounding rectangle of the three vertices.
 * @param pattern The fill pattern.
 */
void
BView::FillTriangle(BPoint point1, BPoint point2, BPoint point3,
	BRect bounds, ::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_FILL_TRIANGLE);
	fOwner->fLink->Attach<BPoint>(point1);
	fOwner->fLink->Attach<BPoint>(point2);
	fOwner->fLink->Attach<BPoint>(point3);
	fOwner->fLink->Attach<BRect>(bounds);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills a triangle with a gradient and a pre-computed bounding rectangle.
 *
 * @param point1   First vertex.
 * @param point2   Second vertex.
 * @param point3   Third vertex.
 * @param bounds   Bounding rectangle.
 * @param gradient The fill gradient.
 */
void
BView::FillTriangle(BPoint point1, BPoint point2, BPoint point3, BRect bounds,
	const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	fOwner->fLink->StartMessage(AS_FILL_TRIANGLE_GRADIENT);
	fOwner->fLink->Attach<BPoint>(point1);
	fOwner->fLink->Attach<BPoint>(point2);
	fOwner->fLink->Attach<BPoint>(point3);
	fOwner->fLink->Attach<BRect>(bounds);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes a line from the current pen location to @p toPoint.
 *
 * @param toPoint  The end point of the line in view-local coordinates.
 * @param pattern  The stroke pattern.
 */
void
BView::StrokeLine(BPoint toPoint, ::pattern pattern)
{
	StrokeLine(PenLocation(), toPoint, pattern);
}


/**
 * @brief Strokes a line between two explicit points.
 *
 * Updates the pen to @p end after drawing.  Sends AS_STROKE_LINE to the
 * app_server and invalidates the local pen location cache.
 *
 * @param start   The start point in view-local coordinates.
 * @param end     The end point; the pen moves here after drawing.
 * @param pattern The stroke pattern.
 */
void
BView::StrokeLine(BPoint start, BPoint end, ::pattern pattern)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	ViewStrokeLineInfo info;
	info.startPoint = start;
	info.endPoint = end;

	fOwner->fLink->StartMessage(AS_STROKE_LINE);
	fOwner->fLink->Attach<ViewStrokeLineInfo>(info);

	_FlushIfNotInTransaction();

	// this modifies our pen location, so we invalidate the flag.
	fState->valid_flags &= ~B_VIEW_PEN_LOCATION_BIT;
}


/**
 * @brief Strokes a line from the pen location to @p toPoint using a gradient.
 *
 * @param toPoint  The end point.
 * @param gradient The stroke gradient.
 */
void
BView::StrokeLine(BPoint toPoint, const BGradient& gradient)
{
	StrokeLine(PenLocation(), toPoint, gradient);
}


/**
 * @brief Strokes a line between two points using a gradient stroke.
 *
 * @param start    The start point.
 * @param end      The end point; the pen moves here after drawing.
 * @param gradient The stroke gradient.
 */
void
BView::StrokeLine(BPoint start, BPoint end, const BGradient& gradient)
{
	if (fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	ViewStrokeLineInfo info;
	info.startPoint = start;
	info.endPoint = end;

	fOwner->fLink->StartMessage(AS_STROKE_LINE_GRADIENT);
	fOwner->fLink->Attach<ViewStrokeLineInfo>(info);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();

	// this modifies our pen location, so we invalidate the flag.
	fState->valid_flags &= ~B_VIEW_PEN_LOCATION_BIT;
}


/**
 * @brief Strokes the outline of a BShape.
 *
 * @param shape   The shape to stroke; must not be NULL and must contain data.
 * @param pattern The stroke pattern.
 */
void
BView::StrokeShape(BShape* shape, ::pattern pattern)
{
	if (shape == NULL || fOwner == NULL)
		return;

	shape_data* sd = BShape::Private(*shape).PrivateData();
	if (sd->opCount == 0 || sd->ptCount == 0)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_STROKE_SHAPE);
	fOwner->fLink->Attach<BRect>(shape->Bounds());
	fOwner->fLink->AttachShape(*shape);

	_FlushIfNotInTransaction();
}


/**
 * @brief Strokes the outline of a BShape with a gradient stroke.
 *
 * @param shape    The shape to stroke.
 * @param gradient The stroke gradient.
 */
void
BView::StrokeShape(BShape* shape, const BGradient& gradient)
{
	if (shape == NULL || fOwner == NULL)
		return;

	shape_data* sd = BShape::Private(*shape).PrivateData();
	if (sd->opCount == 0 || sd->ptCount == 0)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_STROKE_SHAPE_GRADIENT);
	fOwner->fLink->Attach<BRect>(shape->Bounds());
	fOwner->fLink->AttachShape(*shape);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills the interior of a BShape.
 *
 * @param shape   The shape to fill; must not be NULL and must contain data.
 * @param pattern The fill pattern.
 */
void
BView::FillShape(BShape* shape, ::pattern pattern)
{
	if (shape == NULL || fOwner == NULL)
		return;

	shape_data* sd = BShape::Private(*shape).PrivateData();
	if (sd->opCount == 0 || sd->ptCount == 0)
		return;

	_CheckLockAndSwitchCurrent();
	_UpdatePattern(pattern);

	fOwner->fLink->StartMessage(AS_FILL_SHAPE);
	fOwner->fLink->Attach<BRect>(shape->Bounds());
	fOwner->fLink->AttachShape(*shape);

	_FlushIfNotInTransaction();
}


/**
 * @brief Fills the interior of a BShape using a gradient.
 *
 * @param shape    The shape to fill.
 * @param gradient The fill gradient.
 */
void
BView::FillShape(BShape* shape, const BGradient& gradient)
{
	if (shape == NULL || fOwner == NULL)
		return;

	shape_data* sd = BShape::Private(*shape).PrivateData();
	if (sd->opCount == 0 || sd->ptCount == 0)
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_FILL_SHAPE_GRADIENT);
	fOwner->fLink->Attach<BRect>(shape->Bounds());
	fOwner->fLink->AttachShape(*shape);
	fOwner->fLink->AttachGradient(gradient);

	_FlushIfNotInTransaction();
}


/**
 * @brief Begins a batch of coloured line segments for efficient rendering.
 *
 * Allocates a client-side array of up to @p count line entries.  Subsequent
 * calls to AddLine() fill the array; EndLineArray() sends them all to the
 * server in a single message.  Nesting BeginLineArray() calls is not permitted.
 *
 * @param count The maximum number of line segments to be added; must be > 0.
 *
 * @see BView::AddLine(), BView::EndLineArray()
 */
void
BView::BeginLineArray(int32 count)
{
	if (fOwner == NULL)
		return;

	if (count <= 0)
		debugger("Calling BeginLineArray with a count <= 0");

	_CheckLock();

	if (fCommArray) {
		debugger("Can't nest BeginLineArray calls");
			// not fatal, but it helps during
			// development of your app and is in
			// line with R5...
		delete[] fCommArray->array;
		delete fCommArray;
	}

	// TODO: since this method cannot return failure, and further AddLine()
	//	calls with a NULL fCommArray would drop into the debugger anyway,
	//	we allow the possible std::bad_alloc exceptions here...
	fCommArray = new _array_data_;
	fCommArray->count = 0;

	// Make sure the fCommArray is initialized to reasonable values in cases of
	// bad_alloc. At least the exception can be caught and EndLineArray won't
	// crash.
	fCommArray->array = NULL;
	fCommArray->maxCount = 0;

	fCommArray->array = new ViewLineArrayInfo[count];
	fCommArray->maxCount = count;
}


/**
 * @brief Adds a coloured line segment to the current batch.
 *
 * Must be called between BeginLineArray() and EndLineArray().  Entries beyond
 * the count passed to BeginLineArray() are silently discarded.
 *
 * @param start The start point in view-local coordinates.
 * @param end   The end point in view-local coordinates.
 * @param color The colour of this line segment.
 *
 * @see BView::BeginLineArray(), BView::EndLineArray()
 */
void
BView::AddLine(BPoint start, BPoint end, rgb_color color)
{
	if (fOwner == NULL)
		return;

	if (!fCommArray)
		debugger("BeginLineArray must be called before using AddLine");

	_CheckLock();

	const uint32 &arrayCount = fCommArray->count;
	if (arrayCount < fCommArray->maxCount) {
		fCommArray->array[arrayCount].startPoint = start;
		fCommArray->array[arrayCount].endPoint = end;
		fCommArray->array[arrayCount].color = color;

		fCommArray->count++;
	}
}


/**
 * @brief Flushes all accumulated line segments to the app_server.
 *
 * Sends a single AS_STROKE_LINEARRAY message containing every segment added
 * since BeginLineArray(), then frees the client-side buffer.
 *
 * @see BView::BeginLineArray(), BView::AddLine()
 */
void
BView::EndLineArray()
{
	if (fOwner == NULL)
		return;

	if (fCommArray == NULL)
		debugger("Can't call EndLineArray before BeginLineArray");

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_STROKE_LINEARRAY);
	fOwner->fLink->Attach<int32>(fCommArray->count);
	fOwner->fLink->Attach(fCommArray->array,
		fCommArray->count * sizeof(ViewLineArrayInfo));

	_FlushIfNotInTransaction();

	_RemoveCommArray();
}


/**
 * @brief Begins recording drawing commands to a file on disk.
 *
 * Intended to support recording a picture to a file for later replay via
 * DrawPicture().  Currently not implemented.
 *
 * @param filename Path to the target file.
 * @param offset   Byte offset within the file where recording begins.
 *
 * @note This method is a stub; it does nothing in the current implementation.
 */
void
BView::SetDiskMode(char* filename, long offset)
{
	// TODO: implement
	// One BeBook version has this to say about SetDiskMode():
	//
	// "Begins recording a picture to the file with the given filename
	// at the given offset. Subsequent drawing commands sent to the view
	// will be written to the file until EndPicture() is called. The
	// stored commands may be played from the file with DrawPicture()."
}


/**
 * @brief Starts recording drawing commands into a BPicture.
 *
 * All subsequent drawing operations sent to this view are captured by
 * @p picture instead of being rendered on screen.  Must be balanced by
 * EndPicture().
 *
 * @param picture The picture object to record into; must not be NULL and must
 *                not already be recording (fUsurped == NULL).
 *
 * @see BView::EndPicture(), BView::AppendToPicture()
 */
void
BView::BeginPicture(BPicture* picture)
{
	if (_CheckOwnerLockAndSwitchCurrent()
		&& picture && picture->fUsurped == NULL) {
		picture->Usurp(fCurrentPicture);
		fCurrentPicture = picture;

		fOwner->fLink->StartMessage(AS_VIEW_BEGIN_PICTURE);
	}
}


/**
 * @brief Continues recording drawing commands into an existing BPicture.
 *
 * If @p picture has no server token yet, behaves like BeginPicture().
 * Otherwise, informs the server to append to the existing picture data.
 *
 * @param picture The picture to append to; must not be NULL.
 *
 * @see BView::BeginPicture(), BView::EndPicture()
 */
void
BView::AppendToPicture(BPicture* picture)
{
	_CheckLockAndSwitchCurrent();

	if (picture && picture->fUsurped == NULL) {
		int32 token = picture->Token();

		if (token == -1) {
			BeginPicture(picture);
		} else {
			picture->SetToken(-1);
			picture->Usurp(fCurrentPicture);
			fCurrentPicture = picture;
			fOwner->fLink->StartMessage(AS_VIEW_APPEND_TO_PICTURE);
			fOwner->fLink->Attach<int32>(token);
		}
	}
}


/**
 * @brief Stops recording and returns the completed BPicture.
 *
 * Sends AS_VIEW_END_PICTURE, receives the server token, assigns it to the
 * picture, downloads the picture data, and restores the previous picture
 * context.
 *
 * @return The BPicture that was being recorded, or NULL on failure.
 *
 * @see BView::BeginPicture(), BView::DrawPicture()
 */
BPicture*
BView::EndPicture()
{
	if (_CheckOwnerLockAndSwitchCurrent() && fCurrentPicture) {
		int32 token;

		fOwner->fLink->StartMessage(AS_VIEW_END_PICTURE);

		int32 code;
		if (fOwner->fLink->FlushWithReply(code) == B_OK
			&& code == B_OK
			&& fOwner->fLink->Read<int32>(&token) == B_OK) {
			BPicture* picture = fCurrentPicture;
			fCurrentPicture = picture->StepDown();
			picture->SetToken(token);

			// TODO do this more efficient e.g. use a shared area and let the
			// client write into it
			picture->_Download();
			return picture;
		}
	}

	return NULL;
}


/**
 * @brief Sets a bitmap to be composited behind the view's drawing (view bitmap).
 *
 * @param bitmap      The bitmap to use as the view background; NULL clears it.
 * @param srcRect     The source rectangle within the bitmap.
 * @param dstRect     The destination rectangle in view-local coordinates.
 * @param followFlags How the bitmap tracks view resizing (B_FOLLOW_* constants).
 * @param options     Compositing options (e.g. B_TILE_BITMAP).
 *
 * @see BView::ClearViewBitmap()
 */
void
BView::SetViewBitmap(const BBitmap* bitmap, BRect srcRect, BRect dstRect,
	uint32 followFlags, uint32 options)
{
	_SetViewBitmap(bitmap, srcRect, dstRect, followFlags, options);
}


/**
 * @brief Sets a bitmap as the view background, using its full bounds as both source and destination.
 *
 * @param bitmap      The bitmap to use.
 * @param followFlags How the bitmap tracks view resizing.
 * @param options     Compositing options.
 */
void
BView::SetViewBitmap(const BBitmap* bitmap, uint32 followFlags, uint32 options)
{
	BRect rect;
 	if (bitmap)
		rect = bitmap->Bounds();

 	rect.OffsetTo(B_ORIGIN);

	_SetViewBitmap(bitmap, rect, rect, followFlags, options);
}


/**
 * @brief Removes the view's background bitmap.
 *
 * @see BView::SetViewBitmap()
 */
void
BView::ClearViewBitmap()
{
	_SetViewBitmap(NULL, BRect(), BRect(), 0, 0);
}


/**
 * @brief Sets a hardware overlay bitmap for this view.
 *
 * The bitmap must have been created with B_BITMAP_WILL_OVERLAY.  The colour
 * key (the colour rendered transparent in the overlay) is returned via
 * @p colorKey after the server has processed the request.
 *
 * @param overlay     The overlay bitmap.
 * @param srcRect     Source rectangle within the overlay.
 * @param dstRect     Destination rectangle in view-local coordinates.
 * @param colorKey    Receives the chroma-key colour on success.
 * @param followFlags How the overlay tracks view resizing.
 * @param options     Overlay options.
 * @return B_OK on success, B_BAD_VALUE if the bitmap is unsuitable.
 */
status_t
BView::SetViewOverlay(const BBitmap* overlay, BRect srcRect, BRect dstRect,
	rgb_color* colorKey, uint32 followFlags, uint32 options)
{
	if (overlay == NULL || (overlay->fFlags & B_BITMAP_WILL_OVERLAY) == 0)
		return B_BAD_VALUE;

	status_t status = _SetViewBitmap(overlay, srcRect, dstRect, followFlags,
		options | AS_REQUEST_COLOR_KEY);
	if (status == B_OK) {
		// read the color that will be treated as transparent
		fOwner->fLink->Read<rgb_color>(colorKey);
	}

	return status;
}


/**
 * @brief Sets a hardware overlay bitmap using its full bounds.
 *
 * Convenience overload that uses the overlay bitmap's own bounds for both
 * the source and destination rectangles.
 *
 * @param overlay     The overlay bitmap.
 * @param colorKey    Receives the chroma-key colour on success.
 * @param followFlags How the overlay tracks view resizing.
 * @param options     Overlay options.
 * @return B_OK on success, B_BAD_VALUE if @p overlay is NULL.
 */
status_t
BView::SetViewOverlay(const BBitmap* overlay, rgb_color* colorKey,
	uint32 followFlags, uint32 options)
{
	if (overlay == NULL)
		return B_BAD_VALUE;

	BRect rect = overlay->Bounds();
 	rect.OffsetTo(B_ORIGIN);

	return SetViewOverlay(overlay, rect, rect, colorKey, followFlags, options);
}


/**
 * @brief Removes the hardware overlay from this view.
 *
 * @see BView::SetViewOverlay()
 */
void
BView::ClearViewOverlay()
{
	_SetViewBitmap(NULL, BRect(), BRect(), 0, 0);
}


/**
 * @brief Copies pixels from one rectangle in the view to another.
 *
 * Both @p src and @p dst must be valid rectangles.  The copy is performed
 * server-side without a client round-trip for the pixel data.
 *
 * @param src The source rectangle in view-local coordinates.
 * @param dst The destination rectangle in view-local coordinates.
 */
void
BView::CopyBits(BRect src, BRect dst)
{
	if (fOwner == NULL)
		return;

	if (!src.IsValid() || !dst.IsValid())
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_VIEW_COPY_BITS);
	fOwner->fLink->Attach<BRect>(src);
	fOwner->fLink->Attach<BRect>(dst);

	_FlushIfNotInTransaction();
}


/**
 * @brief Replays a BPicture at the current pen location (synchronous).
 *
 * @param picture The picture to draw; must not be NULL.
 *
 * @see BView::DrawPictureAsync()
 */
void
BView::DrawPicture(const BPicture* picture)
{
	if (picture == NULL)
		return;

	DrawPictureAsync(picture, PenLocation());
	Sync();
}


/**
 * @brief Replays a BPicture at @p where (synchronous).
 *
 * @param picture The picture to draw.
 * @param where   The position in view-local coordinates.
 */
void
BView::DrawPicture(const BPicture* picture, BPoint where)
{
	if (picture == NULL)
		return;

	DrawPictureAsync(picture, where);
	Sync();
}


/**
 * @brief Loads and replays a picture from a file (synchronous).
 *
 * @param filename Path to the file containing the flattened picture.
 * @param offset   Byte offset within the file where the picture starts.
 * @param where    Position in view-local coordinates.
 */
void
BView::DrawPicture(const char* filename, long offset, BPoint where)
{
	if (!filename)
		return;

	DrawPictureAsync(filename, offset, where);
	Sync();
}


/**
 * @brief Replays a BPicture at the current pen location (asynchronous).
 *
 * @param picture The picture to draw; must not be NULL.
 */
void
BView::DrawPictureAsync(const BPicture* picture)
{
	if (picture == NULL)
		return;

	DrawPictureAsync(picture, PenLocation());
}


/**
 * @brief Replays a BPicture at @p where (asynchronous).
 *
 * Sends AS_VIEW_DRAW_PICTURE without waiting for the server.
 *
 * @param picture The picture to draw; must not be NULL and must have a valid token.
 * @param where   The position in view-local coordinates.
 */
void
BView::DrawPictureAsync(const BPicture* picture, BPoint where)
{
	if (picture == NULL)
		return;

	if (_CheckOwnerLockAndSwitchCurrent() && picture->Token() > 0) {
		fOwner->fLink->StartMessage(AS_VIEW_DRAW_PICTURE);
		fOwner->fLink->Attach<int32>(picture->Token());
		fOwner->fLink->Attach<BPoint>(where);

		_FlushIfNotInTransaction();
	}
}


/**
 * @brief Loads and replays a picture from a file (asynchronous).
 *
 * @param filename Path to the file containing the flattened picture.
 * @param offset   Byte offset within the file.
 * @param where    Position in view-local coordinates.
 */
void
BView::DrawPictureAsync(const char* filename, long offset, BPoint where)
{
	if (!filename)
		return;

	// TODO: Test
	BFile file(filename, B_READ_ONLY);
	if (file.InitCheck() < B_OK)
		return;

	file.Seek(offset, SEEK_SET);

	BPicture picture;
	if (picture.Unflatten(&file) < B_OK)
		return;

	DrawPictureAsync(&picture, where);
}


/**
 * @brief Begins a compositing layer with the given opacity.
 *
 * All drawing between BeginLayer() and EndLayer() is rendered into an
 * off-screen surface and then blended into the view at the specified opacity.
 *
 * @param opacity The layer opacity (0 = fully transparent, 255 = fully opaque).
 *
 * @see BView::EndLayer()
 */
void
BView::BeginLayer(uint8 opacity)
{
	if (_CheckOwnerLockAndSwitchCurrent()) {
		fOwner->fLink->StartMessage(AS_VIEW_BEGIN_LAYER);
		fOwner->fLink->Attach<uint8>(opacity);
		_FlushIfNotInTransaction();
	}
}


/**
 * @brief Composites the current layer into the view and ends layer recording.
 *
 * @see BView::BeginLayer()
 */
void
BView::EndLayer()
{
	if (_CheckOwnerLockAndSwitchCurrent()) {
		fOwner->fLink->StartMessage(AS_VIEW_END_LAYER);
		_FlushIfNotInTransaction();
	}
}


/**
 * @brief Marks a rectangle as needing to be redrawn.
 *
 * The rectangle is rounded to integer coordinates for BeOS compatibility,
 * then sent to the app_server which will schedule a Draw() call for the
 * affected area.  Rectangles outside the view bounds are silently ignored.
 *
 * @param invalRect The invalid area in view-local coordinates.
 *
 * @see BView::Invalidate(), BView::DelayedInvalidate()
 */
void
BView::Invalidate(BRect invalRect)
{
	if (fOwner == NULL)
		return;

	// NOTE: This rounding of the invalid rect is to stay compatible with BeOS.
	// On the server side, the invalid rect will be converted to a BRegion,
	// which rounds in a different manner, so that it really includes the
	// fractional coordinates of a BRect (ie ceilf(rect.right) &
	// ceilf(rect.bottom)), which is also what BeOS does. So we have to do the
	// different rounding here to stay compatible in both ways.
	invalRect.left = (int)invalRect.left;
	invalRect.top = (int)invalRect.top;
	invalRect.right = (int)invalRect.right;
	invalRect.bottom = (int)invalRect.bottom;
	if (!invalRect.IsValid())
		return;

	_CheckLockAndSwitchCurrent();

	if (!fBounds.Intersects(invalRect))
		return;

	fOwner->fLink->StartMessage(AS_VIEW_INVALIDATE_RECT);
	fOwner->fLink->Attach<BRect>(invalRect);

// TODO: determine why this check isn't working correctly.
#if 0
	if (!fOwner->fUpdateRequested) {
		fOwner->fLink->Flush();
		fOwner->fUpdateRequested = true;
	}
#else
	fOwner->fLink->Flush();
#endif
}


/**
 * @brief Marks a region as needing to be redrawn.
 *
 * @param region The invalid region; must not be NULL.  Ignored if its frame
 *               does not intersect the view's bounds.
 */
void
BView::Invalidate(const BRegion* region)
{
	if (region == NULL || fOwner == NULL)
		return;

	_CheckLockAndSwitchCurrent();

	if (!fBounds.Intersects(region->Frame()))
		return;

	fOwner->fLink->StartMessage(AS_VIEW_INVALIDATE_REGION);
	fOwner->fLink->AttachRegion(*region);

// TODO: See above.
#if 0
	if (!fOwner->fUpdateRequested) {
		fOwner->fLink->Flush();
		fOwner->fUpdateRequested = true;
	}
#else
	fOwner->fLink->Flush();
#endif
}


/**
 * @brief Marks the entire view bounds as needing to be redrawn.
 */
void
BView::Invalidate()
{
	Invalidate(Bounds());
}


/**
 * @brief Marks the entire view bounds as invalid after @p delay microseconds.
 *
 * @param delay Minimum delay before the invalidation is processed, in microseconds.
 */
void
BView::DelayedInvalidate(bigtime_t delay)
{
	DelayedInvalidate(delay, Bounds());
}


/**
 * @brief Marks a specific rectangle as invalid after @p delay microseconds.
 *
 * The rectangle coordinates are truncated to integers before being sent to the
 * server.  The absolute wake-up time is computed as system_time() + @p delay.
 *
 * @param delay     Minimum delay before the invalidation is processed, in microseconds.
 * @param invalRect The region within the view (in view coordinates) to invalidate.
 */
void
BView::DelayedInvalidate(bigtime_t delay, BRect invalRect)
{
	if (fOwner == NULL)
		return;

	invalRect.left = (int)invalRect.left;
	invalRect.top = (int)invalRect.top;
	invalRect.right = (int)invalRect.right;
	invalRect.bottom = (int)invalRect.bottom;
	if (!invalRect.IsValid())
		return;

	_CheckLockAndSwitchCurrent();

	fOwner->fLink->StartMessage(AS_VIEW_DELAYED_INVALIDATE_RECT);
	fOwner->fLink->Attach<bigtime_t>(system_time() + delay);
	fOwner->fLink->Attach<BRect>(invalRect);
	fOwner->fLink->Flush();
}


/**
 * @brief Inverts the colours of every pixel inside @p rect.
 *
 * Sends AS_VIEW_INVERT_RECT to the server.  The operation is applied in the
 * current drawing mode and is flushed immediately unless a transaction is open.
 *
 * @param rect The rectangle to invert, in view coordinates.
 */
void
BView::InvertRect(BRect rect)
{
	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_INVERT_RECT);
		fOwner->fLink->Attach<BRect>(rect);

		_FlushIfNotInTransaction();
	}
}


//	#pragma mark - View Hierarchy Functions


/**
 * @brief Adds @p child to this view's child list and to any attached layout.
 *
 * If a BLayout is installed on this view the child is also passed to the
 * layout via BLayout::AddView().  The child is inserted before @p before, or
 * appended if @p before is NULL.
 *
 * @param child  The view to add.  Must not already have a parent.
 * @param before Existing sibling before which @p child is inserted, or NULL to
 *               append.
 *
 * @see BView::_AddChild(), BView::RemoveChild()
 */
void
BView::AddChild(BView* child, BView* before)
{
	STRACE(("BView(%s)::AddChild(child '%s', before '%s')\n",
		this->Name(),
		child != NULL && child->Name() ? child->Name() : "NULL",
		before != NULL && before->Name() ? before->Name() : "NULL"));

	if (!_AddChild(child, before))
		return;

	if (fLayoutData->fLayout)
		fLayoutData->fLayout->AddView(child);
}


/**
 * @brief Adds a layout item to the view's installed layout.
 *
 * @param child The layout item to add.
 *
 * @return true if the item was added successfully; false if no layout is installed.
 */
bool
BView::AddChild(BLayoutItem* child)
{
	if (!fLayoutData->fLayout)
		return false;
	return fLayoutData->fLayout->AddItem(child);
}


/**
 * @brief Internal worker that adds @p child to the sibling list and notifies the server.
 *
 * Validates preconditions (non-null, no existing parent, not self), adds the
 * child to the doubly-linked sibling list, sets the owner, creates the
 * server-side counterpart via _CreateSelf(), calls _Attach(), and invalidates
 * the layout.
 *
 * @param child  The view to add.
 * @param before Insert before this sibling, or NULL to append.
 *
 * @return true on success; false if any precondition fails.
 */
bool
BView::_AddChild(BView* child, BView* before)
{
	if (!child)
		return false;

	if (child->fParent != NULL) {
		debugger("AddChild failed - the view already has a parent.");
		return false;
	}

	if (child == this) {
		debugger("AddChild failed - cannot add a view to itself.");
		return false;
	}

	bool lockedOwner = false;
	if (fOwner && !fOwner->IsLocked()) {
		fOwner->Lock();
		lockedOwner = true;
	}

	if (!_AddChildToList(child, before)) {
		debugger("AddChild failed!");
		if (lockedOwner)
			fOwner->Unlock();
		return false;
	}

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		child->_SetOwner(fOwner);
		child->_CreateSelf();
		child->_Attach();

		if (lockedOwner)
			fOwner->Unlock();
	}

	InvalidateLayout();

	return true;
}


/**
 * @brief Removes @p child from this view's child list.
 *
 * @param child The child view to remove.  Must be a direct child of this view.
 *
 * @return true if @p child was found and removed; false otherwise.
 */
bool
BView::RemoveChild(BView* child)
{
	STRACE(("BView(%s)::RemoveChild(%s)\n", Name(), child->Name()));

	if (!child)
		return false;

	if (child->fParent != this)
		return false;

	return child->RemoveSelf();
}


/**
 * @brief Returns the number of direct children of this view.
 *
 * @return The count of direct child views.
 */
int32
BView::CountChildren() const
{
	_CheckLock();

	uint32 count = 0;
	BView* child = fFirstChild;

	while (child != NULL) {
		count++;
		child = child->fNextSibling;
	}

	return count;
}


/**
 * @brief Returns the child view at the given zero-based @p index.
 *
 * @param index Zero-based position in the child list.
 *
 * @return The child view at @p index, or NULL if @p index is out of range.
 */
BView*
BView::ChildAt(int32 index) const
{
	_CheckLock();

	BView* child = fFirstChild;
	while (child != NULL && index-- > 0) {
		child = child->fNextSibling;
	}

	return child;
}


/**
 * @brief Returns the next sibling of this view in the parent's child list.
 *
 * @return Pointer to the next sibling, or NULL if this is the last child.
 */
BView*
BView::NextSibling() const
{
	return fNextSibling;
}


/**
 * @brief Returns the previous sibling of this view in the parent's child list.
 *
 * @return Pointer to the previous sibling, or NULL if this is the first child.
 */
BView*
BView::PreviousSibling() const
{
	return fPreviousSibling;
}


/**
 * @brief Removes this view from its parent's child list.
 *
 * Removes any layout items belonging to the parent's layout first, then
 * delegates to _RemoveSelf() which notifies the server and invalidates the
 * parent's layout.
 *
 * @return true on success; false if the view has no parent or removal fails.
 */
bool
BView::RemoveSelf()
{
	_RemoveLayoutItemsFromLayout(false);

	return _RemoveSelf();
}


/**
 * @brief Internal worker: detaches this view from the server and parent list.
 *
 * Calls _UpdateStateForRemove() and _Detach() if attached to a window, then
 * removes the view from the parent's child list and sends AS_VIEW_DELETE to the
 * server.  Finally invalidates the parent's layout.
 *
 * @return true on success; false if the view has no parent.
 */
bool
BView::_RemoveSelf()
{
	STRACE(("BView(%s)::_RemoveSelf()\n", Name()));

	// Remove this child from its parent

	BWindow* owner = fOwner;
	_CheckLock();

	if (owner != NULL) {
		_UpdateStateForRemove();
		_Detach();
	}

	BView* parent = fParent;
	if (!parent || !parent->_RemoveChildFromList(this))
		return false;

	if (owner != NULL && !fTopLevelView) {
		// the top level view is deleted by the app_server automatically
		owner->fLink->StartMessage(AS_VIEW_DELETE);
		owner->fLink->Attach<int32>(_get_object_token_(this));
	}

	parent->InvalidateLayout();

	STRACE(("DONE: BView(%s)::_RemoveSelf()\n", Name()));

	return true;
}


/**
 * @brief Removes all layout items belonging to this view from the parent's layout.
 *
 * Iterates fLayoutData->fLayoutItems in reverse and calls RemoveSelf() on each
 * item.  If @p deleteItems is true the items are also deleted.
 *
 * @param deleteItems Pass true to delete each item after removing it.
 */
void
BView::_RemoveLayoutItemsFromLayout(bool deleteItems)
{
	if (fParent == NULL || fParent->fLayoutData->fLayout == NULL)
		return;

	int32 index = fLayoutData->fLayoutItems.CountItems();
	while (index-- > 0) {
		BLayoutItem* item = fLayoutData->fLayoutItems.ItemAt(index);
		item->RemoveSelf();
			// Removes item from fLayoutItems list
		if (deleteItems)
			delete item;
	}
}


/**
 * @brief Returns the parent view, or NULL if this is a top-level view.
 *
 * Top-level views (directly attached to a window) are hidden behind an
 * internal wrapper; this method correctly returns NULL in that case so callers
 * never receive an internal implementation detail.
 *
 * @return The parent BView, or NULL if there is none or this is top-level.
 */
BView*
BView::Parent() const
{
	if (fParent && fParent->fTopLevelView)
		return NULL;

	return fParent;
}


/**
 * @brief Searches the view hierarchy for a view with the given @p name.
 *
 * Performs a depth-first search starting from this view.
 *
 * @param name The name to look for.
 *
 * @return The first BView whose Name() matches @p name, or NULL if not found.
 */
BView*
BView::FindView(const char* name) const
{
	if (name == NULL)
		return NULL;

	if (Name() != NULL && !strcmp(Name(), name))
		return const_cast<BView*>(this);

	BView* child = fFirstChild;
	while (child != NULL) {
		BView* view = child->FindView(name);
		if (view != NULL)
			return view;

		child = child->fNextSibling;
	}

	return NULL;
}


/**
 * @brief Moves this view by the given delta relative to its current position.
 *
 * Both deltas are rounded to the nearest integer before being applied.
 *
 * @param deltaX Horizontal displacement in parent coordinates.
 * @param deltaY Vertical displacement in parent coordinates.
 */
void
BView::MoveBy(float deltaX, float deltaY)
{
	MoveTo(fParentOffset.x + roundf(deltaX), fParentOffset.y + roundf(deltaY));
}


/**
 * @brief Moves this view so that its top-left corner is at @p where.
 *
 * @param where New top-left position in parent coordinates.
 */
void
BView::MoveTo(BPoint where)
{
	MoveTo(where.x, where.y);
}


/**
 * @brief Moves this view so that its top-left corner is at (@p x, @p y).
 *
 * Coordinates are rounded to integers.  If the view is attached to a window
 * the new position is communicated to the server immediately.
 *
 * @param x New left edge in parent coordinates.
 * @param y New top edge in parent coordinates.
 */
void
BView::MoveTo(float x, float y)
{
	if (x == fParentOffset.x && y == fParentOffset.y)
		return;

	// BeBook says we should do this. And it makes sense.
	x = roundf(x);
	y = roundf(y);

	if (fOwner) {
		_CheckLockAndSwitchCurrent();
		fOwner->fLink->StartMessage(AS_VIEW_MOVE_TO);
		fOwner->fLink->Attach<float>(x);
		fOwner->fLink->Attach<float>(y);

//		fState->valid_flags |= B_VIEW_FRAME_BIT;

		_FlushIfNotInTransaction();
	}

	_MoveTo((int32)x, (int32)y);
}


/**
 * @brief Changes the view's size by the given deltas.
 *
 * Both deltas are rounded to integers.  The new size is sent to the server and
 * the local bounds are updated via _ResizeBy().
 *
 * @param deltaWidth  Change in width.
 * @param deltaHeight Change in height.
 */
void
BView::ResizeBy(float deltaWidth, float deltaHeight)
{
	// BeBook says we should do this. And it makes sense.
	deltaWidth = roundf(deltaWidth);
	deltaHeight = roundf(deltaHeight);

	if (deltaWidth == 0 && deltaHeight == 0)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();
		fOwner->fLink->StartMessage(AS_VIEW_RESIZE_TO);

		fOwner->fLink->Attach<float>(fBounds.Width() + deltaWidth);
		fOwner->fLink->Attach<float>(fBounds.Height() + deltaHeight);

//		fState->valid_flags |= B_VIEW_FRAME_BIT;

		_FlushIfNotInTransaction();
	}

	_ResizeBy((int32)deltaWidth, (int32)deltaHeight);
}


/**
 * @brief Resizes this view to the given @p width and @p height.
 *
 * @param width  New width.
 * @param height New height.
 */
void
BView::ResizeTo(float width, float height)
{
	ResizeBy(width - fBounds.Width(), height - fBounds.Height());
}


/**
 * @brief Resizes this view to the given @p size.
 *
 * @param size New width and height packed in a BSize.
 */
void
BView::ResizeTo(BSize size)
{
	ResizeBy(size.width - fBounds.Width(), size.height - fBounds.Height());
}


//	#pragma mark - Inherited Methods (from BHandler)


/**
 * @brief Appends the view's scripting suite and property info to @p data.
 *
 * Adds the "suite/vnd.Be-view" suite name and the sViewPropInfo property
 * table, then chains to BHandler::GetSupportedSuites().
 *
 * @param data Message to receive the suite descriptions.
 *
 * @return B_OK on success, or an error code from BMessage::AddString/AddFlat.
 */
status_t
BView::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t status = data->AddString("suites", "suite/vnd.Be-view");
	BPropertyInfo propertyInfo(sViewPropInfo);
	if (status == B_OK)
		status = data->AddFlat("messages", &propertyInfo);
	if (status == B_OK)
		return BHandler::GetSupportedSuites(data);
	return status;
}


/**
 * @brief Resolves a scripting specifier and returns the target handler.
 *
 * Handles B_WINDOW_MOVE_BY/B_WINDOW_MOVE_TO messages directly and
 * dispatches View, Hidden, Shelf, and Children properties via the property
 * table.  Unrecognised specifiers are forwarded to BHandler::ResolveSpecifier().
 *
 * @param message   The scripting message.
 * @param index     The current specifier index.
 * @param specifier The specifier sub-message.
 * @param what      The specifier type constant.
 * @param property  The property name string.
 *
 * @return The handler that should process the message, or NULL on error.
 */
BHandler*
BView::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 what, const char* property)
{
	if (message->what == B_WINDOW_MOVE_BY
		|| message->what == B_WINDOW_MOVE_TO) {
		return this;
	}

	BPropertyInfo propertyInfo(sViewPropInfo);
	status_t err = B_BAD_SCRIPT_SYNTAX;
	BMessage replyMsg(B_REPLY);

	switch (propertyInfo.FindMatch(message, index, specifier, what, property)) {
		case 0:
		case 1:
		case 3:
			return this;

		case 2:
			if (fShelf) {
				message->PopSpecifier();
				return fShelf;
			}

			err = B_NAME_NOT_FOUND;
			replyMsg.AddString("message", "This window doesn't have a shelf");
			break;

		case 4:
		{
			if (!fFirstChild) {
				err = B_NAME_NOT_FOUND;
				replyMsg.AddString("message", "This window doesn't have "
					"children.");
				break;
			}
			BView* child = NULL;
			switch (what) {
				case B_INDEX_SPECIFIER:
				{
					int32 index;
					err = specifier->FindInt32("index", &index);
					if (err == B_OK)
						child = ChildAt(index);
					break;
				}
				case B_REVERSE_INDEX_SPECIFIER:
				{
					int32 rindex;
					err = specifier->FindInt32("index", &rindex);
					if (err == B_OK)
						child = ChildAt(CountChildren() - rindex);
					break;
				}
				case B_NAME_SPECIFIER:
				{
					const char* name;
					err = specifier->FindString("name", &name);
					if (err == B_OK)
						child = FindView(name);
					break;
				}
			}

			if (child != NULL) {
				message->PopSpecifier();
				return child;
			}

			if (err == B_OK)
				err = B_BAD_INDEX;

			replyMsg.AddString("message",
				"Cannot find view at/with specified index/name.");
			break;
		}

		default:
			return BHandler::ResolveSpecifier(message, index, specifier, what,
				property);
	}

	if (err < B_OK) {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;

		if (err == B_BAD_SCRIPT_SYNTAX)
			replyMsg.AddString("message", "Didn't understand the specifier(s)");
		else
			replyMsg.AddString("message", strerror(err));
	}

	replyMsg.AddInt32("error", err);
	message->SendReply(&replyMsg);
	return NULL;
}


/**
 * @brief Handles messages sent to this view.
 *
 * Dispatches common interface messages (B_MOUSE_DOWN/UP/MOVED,
 * B_KEY_DOWN/UP, B_INVALIDATE, B_MOUSE_WHEEL_CHANGED, B_COLORS_UPDATED,
 * B_FONTS_UPDATED, B_SCREEN_CHANGED, B_WORKSPACE_ACTIVATED,
 * B_WORKSPACES_CHANGED) and scripting GET/SET operations for the Frame,
 * Hidden, and Children properties.  Unhandled messages are forwarded to
 * BHandler::MessageReceived().
 *
 * @param message The message to handle.
 */
void
BView::MessageReceived(BMessage* message)
{
	if (!message->HasSpecifiers()) {
		switch (message->what) {
			case B_INVALIDATE:
			{
				BRect rect;
				if (message->FindRect("be:area", &rect) == B_OK)
					Invalidate(rect);
				else
					Invalidate();
				break;
			}

			case B_KEY_DOWN:
			{
				// TODO: cannot use "string" here if we support having different
				// font encoding per view (it's supposed to be converted by
				// BWindow::_HandleKeyDown() one day)
				const char* string;
				ssize_t bytes;
				if (message->FindData("bytes", B_STRING_TYPE,
						(const void**)&string, &bytes) == B_OK)
					KeyDown(string, bytes - 1);
				break;
			}

			case B_KEY_UP:
			{
				// TODO: same as above
				const char* string;
				ssize_t bytes;
				if (message->FindData("bytes", B_STRING_TYPE,
						(const void**)&string, &bytes) == B_OK)
					KeyUp(string, bytes - 1);
				break;
			}

			case B_VIEW_RESIZED:
				FrameResized(message->GetInt32("width", 0),
					message->GetInt32("height", 0));
				break;

			case B_VIEW_MOVED:
				FrameMoved(fParentOffset);
				break;

			case B_MOUSE_DOWN:
			{
				BPoint where;
				message->FindPoint("be:view_where", &where);
				MouseDown(where);
				break;
			}

			case B_MOUSE_IDLE:
			{
				BPoint where;
				if (message->FindPoint("be:view_where", &where) != B_OK)
					break;

				BToolTip* tip;
				if (GetToolTipAt(where, &tip))
					ShowToolTip(tip);
				else
					BHandler::MessageReceived(message);
				break;
			}

			case B_MOUSE_MOVED:
			{
				uint32 eventOptions = fEventOptions | fMouseEventOptions;
				bool noHistory = eventOptions & B_NO_POINTER_HISTORY;
				bool dropIfLate = !(eventOptions & B_FULL_POINTER_HISTORY);

				bigtime_t eventTime;
				if (message->FindInt64("when", (int64*)&eventTime) < B_OK)
					eventTime = system_time();

				uint32 transit;
				message->FindInt32("be:transit", (int32*)&transit);
				// don't drop late messages with these important transit values
				if (transit == B_ENTERED_VIEW || transit == B_EXITED_VIEW)
					dropIfLate = false;

				// TODO: The dropping code may have the following problem: On
				// slower computers, 20ms may just be to abitious a delay.
				// There, we might constantly check the message queue for a
				// newer message, not find any, and still use the only but later
				// than 20ms message, which of course makes the whole thing
				// later than need be. An adaptive delay would be kind of neat,
				// but would probably use additional BWindow members to count
				// the successful versus fruitless queue searches and the delay
				// value itself or something similar.
				if (noHistory
					|| (dropIfLate && (system_time() - eventTime > 20000))) {
					// filter out older mouse moved messages in the queue
					BWindow* window = Window();
					window->_DequeueAll();
					BMessageQueue* queue = window->MessageQueue();
					queue->Lock();

					BMessage* moved;
					for (int32 i = 0; (moved = queue->FindMessage(i)) != NULL;
						 i++) {
						if (moved != message && moved->what == B_MOUSE_MOVED) {
							// there is a newer mouse moved message in the
							// queue, just ignore the current one, the newer one
							// will be handled here eventually
							queue->Unlock();
							return;
						}
					}
					queue->Unlock();
				}

				BPoint where;
				uint32 buttons;
				message->FindPoint("be:view_where", &where);
				message->FindInt32("buttons", (int32*)&buttons);

				if (transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW)
					HideToolTip();

				BMessage* dragMessage = NULL;
				if (message->HasMessage("be:drag_message")) {
					dragMessage = new BMessage();
					if (message->FindMessage("be:drag_message", dragMessage)
						!= B_OK) {
						delete dragMessage;
						dragMessage = NULL;
					}
				}

				MouseMoved(where, transit, dragMessage);
				delete dragMessage;
				break;
			}

			case B_MOUSE_UP:
			{
				BPoint where;
				message->FindPoint("be:view_where", &where);
				fMouseEventOptions = 0;
				MouseUp(where);
				break;
			}

			case B_MOUSE_WHEEL_CHANGED:
			{
				BScrollBar* horizontal = ScrollBar(B_HORIZONTAL);
				BScrollBar* vertical = ScrollBar(B_VERTICAL);
				if (horizontal == NULL && vertical == NULL) {
					// Pass the message to the next handler
					BHandler::MessageReceived(message);
					break;
				}

				float deltaX = 0.0f;
				float deltaY = 0.0f;

				if (horizontal != NULL)
					message->FindFloat("be:wheel_delta_x", &deltaX);

				if (vertical != NULL)
					message->FindFloat("be:wheel_delta_y", &deltaY);

				if (deltaX == 0.0f && deltaY == 0.0f)
					break;

				if ((modifiers() & B_CONTROL_KEY) != 0)
					std::swap(horizontal, vertical);

				if (horizontal != NULL && deltaX != 0.0f)
					ScrollWithMouseWheelDelta(horizontal, deltaX);

				if (vertical != NULL && deltaY != 0.0f)
					ScrollWithMouseWheelDelta(vertical, deltaY);

				break;
			}

			// prevent message repeats
			case B_COLORS_UPDATED:
			case B_FONTS_UPDATED:
				break;

			case B_SCREEN_CHANGED:
			case B_WORKSPACE_ACTIVATED:
			case B_WORKSPACES_CHANGED:
			{
				BWindow* window = Window();
				if (window == NULL)
					break;

				// propagate message to child views
				int32 childCount = CountChildren();
				for (int32 i = 0; i < childCount; i++) {
					BView* view = ChildAt(i);
					if (view != NULL)
						window->PostMessage(message, view);
				}
				break;
			}

			default:
				BHandler::MessageReceived(message);
				break;
		}

		return;
	}

	// Scripting message

	BMessage replyMsg(B_REPLY);
	status_t err = B_BAD_SCRIPT_SYNTAX;
	int32 index;
	BMessage specifier;
	int32 what;
	const char* property;

	if (message->GetCurrentSpecifier(&index, &specifier, &what, &property)
			!= B_OK) {
		return BHandler::MessageReceived(message);
	}

	BPropertyInfo propertyInfo(sViewPropInfo);
	switch (propertyInfo.FindMatch(message, index, &specifier, what,
			property)) {
		case 0:
			if (message->what == B_GET_PROPERTY) {
				err = replyMsg.AddRect("result", Frame());
			} else if (message->what == B_SET_PROPERTY) {
				BRect newFrame;
				err = message->FindRect("data", &newFrame);
				if (err == B_OK) {
					MoveTo(newFrame.LeftTop());
					ResizeTo(newFrame.Width(), newFrame.Height());
				}
			}
			break;
		case 1:
			if (message->what == B_GET_PROPERTY) {
				err = replyMsg.AddBool("result", IsHidden());
			} else if (message->what == B_SET_PROPERTY) {
				bool newHiddenState;
				err = message->FindBool("data", &newHiddenState);
				if (err == B_OK) {
					if (newHiddenState == true)
						Hide();
					else
						Show();
				}
			}
			break;
		case 3:
			err = replyMsg.AddInt32("result", CountChildren());
			break;
		default:
			return BHandler::MessageReceived(message);
	}

	if (err != B_OK) {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;

		if (err == B_BAD_SCRIPT_SYNTAX)
			replyMsg.AddString("message", "Didn't understand the specifier(s)");
		else
			replyMsg.AddString("message", strerror(err));

		replyMsg.AddInt32("error", err);
	}

	message->SendReply(&replyMsg);
}


/**
 * @brief Executes a virtual-dispatch perform call identified by @p code.
 *
 * This mechanism allows binary-compatible override dispatch for layout-related
 * virtuals (MinSize, MaxSize, PreferredSize, LayoutAlignment,
 * HasHeightForWidth, GetHeightForWidth, SetLayout, LayoutInvalidated,
 * DoLayout, LayoutChanged, GetToolTipAt, AllUnarchived, AllArchived).
 * Unrecognised codes are forwarded to BHandler::Perform().
 *
 * @param code  A PERFORM_CODE_* constant identifying the virtual to call.
 * @param _data In/out data struct for the requested operation.
 *
 * @return B_OK on success, or the return value of BHandler::Perform().
 */
status_t
BView::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BView::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BView::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BView::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BView::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BView::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BView::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BView::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BView::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BView::DoLayout();
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_CHANGED:
		{
			BView::LayoutChanged();
			return B_OK;
		}
		case PERFORM_CODE_GET_TOOL_TIP_AT:
		{
			perform_data_get_tool_tip_at* data
				= (perform_data_get_tool_tip_at*)_data;
			data->return_value
				= BView::GetToolTipAt(data->point, data->tool_tip);
			return B_OK;
		}
		case PERFORM_CODE_ALL_UNARCHIVED:
		{
			perform_data_all_unarchived* data =
				(perform_data_all_unarchived*)_data;

			data->return_value = BView::AllUnarchived(data->archive);
			return B_OK;
		}
		case PERFORM_CODE_ALL_ARCHIVED:
		{
			perform_data_all_archived* data =
				(perform_data_all_archived*)_data;

			data->return_value = BView::AllArchived(data->archive);
			return B_OK;
		}
	}

	return BHandler::Perform(code, _data);
}


// #pragma mark - Layout Functions


/**
 * @brief Returns the minimum size of this view for layout purposes.
 *
 * Composes the explicit minimum size (if set via SetExplicitMinSize()) with the
 * minimum size reported by the installed layout, or falls back to
 * GetPreferredSize() when no layout is installed.
 *
 * @return The effective minimum BSize.
 */
BSize
BView::MinSize()
{
	// TODO: make sure this works correctly when some methods are overridden
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(fLayoutData->fMinSize,
		(fLayoutData->fLayout ? fLayoutData->fLayout->MinSize()
			: BSize(width, height)));
}


/**
 * @brief Returns the maximum size of this view for layout purposes.
 *
 * Composes the explicit maximum size with the layout's maximum, defaulting to
 * B_SIZE_UNLIMITED when no layout is installed.
 *
 * @return The effective maximum BSize.
 */
BSize
BView::MaxSize()
{
	return BLayoutUtils::ComposeSize(fLayoutData->fMaxSize,
		(fLayoutData->fLayout ? fLayoutData->fLayout->MaxSize()
			: BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED)));
}


/**
 * @brief Returns the preferred size of this view for layout purposes.
 *
 * Composes the explicit preferred size with the layout's preferred size, or
 * GetPreferredSize() when no layout is installed.
 *
 * @return The effective preferred BSize.
 */
BSize
BView::PreferredSize()
{
	// TODO: make sure this works correctly when some methods are overridden
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(fLayoutData->fPreferredSize,
		(fLayoutData->fLayout ? fLayoutData->fLayout->PreferredSize()
			: BSize(width, height)));
}


/**
 * @brief Returns the layout alignment of this view.
 *
 * Composes the explicit alignment with the installed layout's alignment,
 * defaulting to B_ALIGN_HORIZONTAL_CENTER / B_ALIGN_VERTICAL_CENTER.
 *
 * @return The effective BAlignment.
 */
BAlignment
BView::LayoutAlignment()
{
	return BLayoutUtils::ComposeAlignment(fLayoutData->fAlignment,
		(fLayoutData->fLayout ? fLayoutData->fLayout->Alignment()
			: BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_CENTER)));
}


/**
 * @brief Sets the explicit minimum size hint for layout managers.
 *
 * @param size The minimum size to advertise.
 */
void
BView::SetExplicitMinSize(BSize size)
{
	fLayoutData->fMinSize = size;
	InvalidateLayout();
}


/**
 * @brief Sets the explicit maximum size hint for layout managers.
 *
 * @param size The maximum size to advertise.
 */
void
BView::SetExplicitMaxSize(BSize size)
{
	fLayoutData->fMaxSize = size;
	InvalidateLayout();
}


/**
 * @brief Sets the explicit preferred size hint for layout managers.
 *
 * @param size The preferred size to advertise.
 */
void
BView::SetExplicitPreferredSize(BSize size)
{
	fLayoutData->fPreferredSize = size;
	InvalidateLayout();
}


/**
 * @brief Sets the explicit minimum, maximum, and preferred sizes to the same value.
 *
 * Convenience method that locks all three size hints to @p size, effectively
 * making the view fixed-size with respect to the layout system.
 *
 * @param size The fixed size to advertise for min, max, and preferred.
 */
void
BView::SetExplicitSize(BSize size)
{
	fLayoutData->fMinSize = size;
	fLayoutData->fMaxSize = size;
	fLayoutData->fPreferredSize = size;
	InvalidateLayout();
}


/**
 * @brief Sets the explicit alignment hint for layout managers.
 *
 * @param alignment The alignment to advertise.
 */
void
BView::SetExplicitAlignment(BAlignment alignment)
{
	fLayoutData->fAlignment = alignment;
	InvalidateLayout();
}


/**
 * @brief Returns the explicit minimum size set via SetExplicitMinSize().
 *
 * @return The explicit minimum BSize (may be B_SIZE_UNSET components).
 */
BSize
BView::ExplicitMinSize() const
{
	return fLayoutData->fMinSize;
}


/**
 * @brief Returns the explicit maximum size set via SetExplicitMaxSize().
 *
 * @return The explicit maximum BSize (may be B_SIZE_UNSET components).
 */
BSize
BView::ExplicitMaxSize() const
{
	return fLayoutData->fMaxSize;
}


/**
 * @brief Returns the explicit preferred size set via SetExplicitPreferredSize().
 *
 * @return The explicit preferred BSize (may be B_SIZE_UNSET components).
 */
BSize
BView::ExplicitPreferredSize() const
{
	return fLayoutData->fPreferredSize;
}


/**
 * @brief Returns the explicit alignment set via SetExplicitAlignment().
 *
 * @return The explicit BAlignment.
 */
BAlignment
BView::ExplicitAlignment() const
{
	return fLayoutData->fAlignment;
}


/**
 * @brief Returns whether this view's height depends on its width.
 *
 * Delegates to the installed layout if one exists.
 *
 * @return true if the view (or its layout) has height-for-width behaviour.
 */
bool
BView::HasHeightForWidth()
{
	return (fLayoutData->fLayout
		? fLayoutData->fLayout->HasHeightForWidth() : false);
}


/**
 * @brief Retrieves the height constraints for a given @p width.
 *
 * Delegates to the installed layout.  If no layout is installed the output
 * pointers are left unchanged.
 *
 * @param width     The available width.
 * @param min       Receives the minimum height, or unchanged if no layout.
 * @param max       Receives the maximum height, or unchanged if no layout.
 * @param preferred Receives the preferred height, or unchanged if no layout.
 */
void
BView::GetHeightForWidth(float width, float* min, float* max, float* preferred)
{
	if (fLayoutData->fLayout)
		fLayoutData->fLayout->GetHeightForWidth(width, min, max, preferred);
}


/**
 * @brief Installs @p layout as the layout manager for this view.
 *
 * The B_SUPPORTS_LAYOUT flag is set automatically.  Any previously installed
 * layout is removed, its owner is cleared, and it is deleted.  All existing
 * child views are then added to the new layout, and the layout is invalidated.
 *
 * @param layout The new layout to install, or NULL to remove the current layout.
 *
 * @note Panics (via debugger()) if @p layout already belongs to another layout.
 */
void
BView::SetLayout(BLayout* layout)
{
	if (layout == fLayoutData->fLayout)
		return;

	if (layout && layout->Layout())
		debugger("BView::SetLayout() failed, layout is already in use.");

	fFlags |= B_SUPPORTS_LAYOUT;

	// unset and delete the old layout
	if (fLayoutData->fLayout) {
		fLayoutData->fLayout->RemoveSelf();
		fLayoutData->fLayout->SetOwner(NULL);
		delete fLayoutData->fLayout;
	}

	fLayoutData->fLayout = layout;

	if (fLayoutData->fLayout) {
		fLayoutData->fLayout->SetOwner(this);

		// add all children
		int count = CountChildren();
		for (int i = 0; i < count; i++)
			fLayoutData->fLayout->AddView(ChildAt(i));
	}

	InvalidateLayout();
}


/**
 * @brief Returns the currently installed layout manager.
 *
 * @return The installed BLayout, or NULL if none is set.
 */
BLayout*
BView::GetLayout() const
{
	return fLayoutData->fLayout;
}


/**
 * @brief Marks this view's layout (and optionally all descendants) as invalid.
 *
 * Clears both fLayoutValid and fMinMaxValid, calls the LayoutInvalidated() hook,
 * propagates the invalidation upward via the layout hierarchy or the parent view,
 * and posts B_LAYOUT_WINDOW to the owning window when called on a top-level view.
 * The call is a no-op if layout invalidation is disabled or already in progress.
 *
 * @param descendants If true, recursively invalidates all child views as well.
 */
void
BView::InvalidateLayout(bool descendants)
{
	// printf("BView(%p)::InvalidateLayout(%i), valid: %i, inProgress: %i\n",
	//	this, descendants, fLayoutData->fLayoutValid,
	//	fLayoutData->fLayoutInProgress);

	if (!fLayoutData->fMinMaxValid || fLayoutData->fLayoutInProgress
 			|| fLayoutData->fLayoutInvalidationDisabled > 0) {
		return;
	}
	fLayoutData->fLayoutValid = false;
	fLayoutData->fMinMaxValid = false;
	LayoutInvalidated(descendants);

	if (descendants) {
		for (BView* child = fFirstChild;
			child; child = child->fNextSibling) {
			child->InvalidateLayout(descendants);
		}
	}

	if (fLayoutData->fLayout)
		fLayoutData->fLayout->InvalidateLayout(descendants);
	else
		_InvalidateParentLayout();

	if (fTopLevelView
		&& fOwner != NULL)
		fOwner->PostMessage(B_LAYOUT_WINDOW);
}


/**
 * @brief Re-enables layout invalidation after a matching DisableLayoutInvalidation() call.
 *
 * Decrements the suppression counter.  Calls are reference-counted: one Enable
 * is required for each Disable before invalidation resumes.
 */
void
BView::EnableLayoutInvalidation()
{
	if (fLayoutData->fLayoutInvalidationDisabled > 0)
		fLayoutData->fLayoutInvalidationDisabled--;
}


/**
 * @brief Suppresses layout invalidation until the matching Enable call.
 *
 * Increments the suppression counter.  Must be balanced with a call to
 * EnableLayoutInvalidation().
 */
void
BView::DisableLayoutInvalidation()
{
	fLayoutData->fLayoutInvalidationDisabled++;
}


/**
 * @brief Returns whether layout invalidation is currently suppressed.
 *
 * @return true if DisableLayoutInvalidation() has been called more times than
 *         EnableLayoutInvalidation() since the last reset.
 */
bool
BView::IsLayoutInvalidationDisabled()
{
	if (fLayoutData->fLayoutInvalidationDisabled > 0)
		return true;
	return false;
}


/**
 * @brief Returns whether the current layout is valid (up to date).
 *
 * @return true if the layout has been applied since the last invalidation.
 */
bool
BView::IsLayoutValid() const
{
	return fLayoutData->fLayoutValid;
}


/**
 * @brief Marks the min/max cache as valid without performing a full layout pass.
 *
 * Used by the layout engine after reading min/max values to suppress redundant
 * invalidation cycles.
 */
void
BView::ResetLayoutInvalidation()
{
	fLayoutData->fMinMaxValid = true;
}


/**
 * @brief Returns the active layout context during a layout pass.
 *
 * @return The current BLayoutContext, or NULL outside of a layout pass.
 */
BLayoutContext*
BView::LayoutContext() const
{
	return fLayoutData->fLayoutContext;
}


/**
 * @brief Performs a layout pass on this view and all of its children.
 *
 * Creates a fresh BLayoutContext and delegates to _Layout().  If @p force is
 * true the layout is applied even if the current layout is considered valid.
 *
 * @param force Pass true to force re-layout even when already valid.
 */
void
BView::Layout(bool force)
{
	BLayoutContext context;
	_Layout(force, &context);
}


/**
 * @brief Schedules a layout pass when the current layout is valid but stale.
 *
 * Marks fNeedsRelayout and triggers Layout(false) unless a parent layout pass
 * is already in progress (in which case the parent will call Layout() on
 * this view automatically).
 */
void
BView::Relayout()
{
	if (fLayoutData->fLayoutValid && !fLayoutData->fLayoutInProgress) {
		fLayoutData->fNeedsRelayout = true;
		if (fLayoutData->fLayout)
			fLayoutData->fLayout->RequireLayout();

		// Layout() is recursive, that is if the parent view is currently laid
		// out, we don't call layout() on this view, but wait for the parent's
		// Layout() to do that for us.
		if (!fParent || !fParent->fLayoutData->fLayoutInProgress)
			Layout(false);
	}
}


/**
 * @brief Hook called when the view's layout is invalidated.
 *
 * Subclasses may override this to react to layout invalidation before a new
 * layout pass is performed.  The default implementation is empty.
 *
 * @param descendants true if the invalidation propagated from a descendant.
 */
void
BView::LayoutInvalidated(bool descendants)
{
	// hook method
}


/**
 * @brief Applies the installed layout to this view's children.
 *
 * Called by the layout engine during a layout pass.  Subclasses without an
 * installed layout can override this to position children manually.  The
 * default implementation delegates to BLayout::_LayoutWithinContext().
 */
void
BView::DoLayout()
{
	if (fLayoutData->fLayout)
		fLayoutData->fLayout->_LayoutWithinContext(false, LayoutContext());
}


/**
 * @brief Sets the view's tool tip to a plain-text string.
 *
 * If @p text is NULL or empty the tool tip is cleared.  If a BTextToolTip
 * is already installed its text is updated in place; otherwise a new
 * BTextToolTip is created.
 *
 * @param text The tool tip text, or NULL/empty to remove the tip.
 */
void
BView::SetToolTip(const char* text)
{
	if (text == NULL || text[0] == '\0') {
		SetToolTip((BToolTip*)NULL);
		return;
	}

	if (BTextToolTip* tip = dynamic_cast<BTextToolTip*>(fToolTip))
		tip->SetText(text);
	else
		SetToolTip(new BTextToolTip(text));
}


/**
 * @brief Sets the view's tool tip to the given BToolTip object.
 *
 * If @p tip differs from the current tip the current one is released via
 * ReleaseReference(), any visible tip is hidden, and @p tip is stored after
 * calling AcquireReference().  Passing NULL removes the tool tip.
 *
 * @param tip The new tool tip, or NULL to remove the current tip.
 */
void
BView::SetToolTip(BToolTip* tip)
{
	if (fToolTip == tip)
		return;
	else if (tip == NULL)
		HideToolTip();

	if (fToolTip != NULL)
		fToolTip->ReleaseReference();

	fToolTip = tip;

	if (fToolTip != NULL)
		fToolTip->AcquireReference();
}


/**
 * @brief Returns the tool tip currently associated with this view.
 *
 * @return The installed BToolTip, or NULL if none is set.
 */
BToolTip*
BView::ToolTip() const
{
	return fToolTip;
}


/**
 * @brief Displays the given tool tip at the current mouse position.
 *
 * Converts the current mouse position to screen coordinates and delegates to
 * BToolTipManager::ShowTip().  Does nothing if @p tip is NULL.
 *
 * @param tip The tool tip to display.
 */
void
BView::ShowToolTip(BToolTip* tip)
{
	if (tip == NULL)
		return;

	BPoint where;
	GetMouse(&where, NULL, false);

	BToolTipManager::Manager()->ShowTip(tip, ConvertToScreen(where), this);
}


/**
 * @brief Hides the currently visible tool tip.
 *
 * Delegates to BToolTipManager::HideTip().  Does nothing if no tool tip is
 * installed on this view.
 */
void
BView::HideToolTip()
{
	if (fToolTip == NULL)
		return;

	// TODO: Only hide if ours is the tooltip that's showing!
	BToolTipManager::Manager()->HideTip();
}


/**
 * @brief Returns the tool tip for the given view-local @p point.
 *
 * The default implementation returns the view's own tool tip regardless of
 * @p point.  Subclasses can override this to provide per-region tool tips.
 *
 * @param point The view-local point being queried.
 * @param _tip  Receives the tool tip pointer.
 *
 * @return true if a tool tip was found; false otherwise.
 */
bool
BView::GetToolTipAt(BPoint point, BToolTip** _tip)
{
	if (fToolTip != NULL) {
		*_tip = fToolTip;
		return true;
	}

	*_tip = NULL;
	return false;
}


/**
 * @brief Hook called after a layout pass has completed for this view.
 *
 * Subclasses can override this to react after children have been repositioned.
 * The default implementation is empty.
 */
void
BView::LayoutChanged()
{
	// hook method
}


/**
 * @brief Internal layout worker: performs DoLayout() and recurses to children.
 *
 * Guards against re-entrancy via fLayoutInProgress, updates the layout context,
 * calls DoLayout(), recursively lays out non-hidden children, fires the
 * LayoutChanged() hook, and optionally invalidates the drawn content when
 * B_INVALIDATE_AFTER_LAYOUT is set.
 *
 * @param force   Force re-layout even when the layout is already valid.
 * @param context The BLayoutContext for this layout pass.
 */
void
BView::_Layout(bool force, BLayoutContext* context)
{
//printf("%p->BView::_Layout(%d, %p)\n", this, force, context);
//printf("  fNeedsRelayout: %d, fLayoutValid: %d, fLayoutInProgress: %d\n",
//fLayoutData->fNeedsRelayout, fLayoutData->fLayoutValid,
//fLayoutData->fLayoutInProgress);
	if (fLayoutData->fNeedsRelayout || !fLayoutData->fLayoutValid || force) {
		fLayoutData->fLayoutValid = false;

		if (fLayoutData->fLayoutInProgress)
			return;

		BLayoutContext* oldContext = fLayoutData->fLayoutContext;
		fLayoutData->fLayoutContext = context;

		fLayoutData->fLayoutInProgress = true;
		DoLayout();
		fLayoutData->fLayoutInProgress = false;

		fLayoutData->fLayoutValid = true;
		fLayoutData->fMinMaxValid = true;
		fLayoutData->fNeedsRelayout = false;

		// layout children
		for(BView* child = fFirstChild; child; child = child->fNextSibling) {
			if (!child->IsHidden(child))
				child->_Layout(force, context);
		}

		LayoutChanged();

		fLayoutData->fLayoutContext = oldContext;

		// invalidate the drawn content, if requested
		if (fFlags & B_INVALIDATE_AFTER_LAYOUT)
			Invalidate();
	}
}


/**
 * @brief Called when the owned layout is deleted by its parent layout.
 *
 * Clears fLayoutData->fLayout to prevent a double-delete when the view itself
 * is destroyed, then invalidates the layout.
 *
 * @param deleted The layout that has already been deleted.
 */
void
BView::_LayoutLeft(BLayout* deleted)
{
	// If our layout is added to another layout (via BLayout::AddItem())
	// then we share ownership of our layout. In the event that our layout gets
	// deleted by the layout it has been added to, this method is called so
	// that we don't double-delete our layout.
	if (fLayoutData->fLayout == deleted)
		fLayoutData->fLayout = NULL;
	InvalidateLayout();
}


/**
 * @brief Propagates a layout invalidation to whichever layout owns this view.
 *
 * Climbs the layout ownership chain: if this view's layout has a parent layout
 * that parent is invalidated; otherwise any registered layout items are used to
 * find the owning layout; finally the direct parent view's InvalidateLayout() is
 * called as a fallback.
 */
void
BView::_InvalidateParentLayout()
{
	if (!fParent)
		return;

	BLayout* layout = fLayoutData->fLayout;
	BLayout* layoutParent = layout ? layout->Layout() : NULL;
	if (layoutParent) {
		layoutParent->InvalidateLayout();
	} else if (fLayoutData->fLayoutItems.CountItems() > 0) {
		int32 count = fLayoutData->fLayoutItems.CountItems();
		for (int32 i = 0; i < count; i++) {
			fLayoutData->fLayoutItems.ItemAt(i)->Layout()->InvalidateLayout();
		}
	} else {
		fParent->InvalidateLayout();
	}
}


//	#pragma mark - Private Functions


/**
 * @brief Initialises all data members to their default values.
 *
 * Called by every constructor.  Rounds the frame coordinates, sets all pointer
 * members to NULL, allocates the ViewState and LayoutData aggregates, and
 * applies the default UI colours when B_SUPPORTS_LAYOUT is set.
 *
 * @param frame        Initial frame rectangle in parent coordinates.
 * @param name         Handler name passed to BHandler.
 * @param resizingMode Resizing mode flags (low 16 bits of fFlags).
 * @param flags        View flags (high 16 bits of fFlags).
 */
void
BView::_InitData(BRect frame, const char* name, uint32 resizingMode,
	uint32 flags)
{
	// Info: The name of the view is set by BHandler constructor

	STRACE(("BView::_InitData: enter\n"));

	// initialize members
	if ((resizingMode & ~_RESIZE_MASK_) || (flags & _RESIZE_MASK_))
		printf("%s BView::_InitData(): resizing mode or flags swapped\n", name);

	// There are applications that swap the resize mask and the flags in the
	// BView constructor. This does not cause problems under BeOS as it just
	// ors the two fields to one 32bit flag.
	// For now we do the same but print the above warning message.
	// TODO: this should be removed at some point and the original
	// version restored:
	// fFlags = (resizingMode & _RESIZE_MASK_) | (flags & ~_RESIZE_MASK_);
	fFlags = resizingMode | flags;

	// handle rounding
	frame.left = roundf(frame.left);
	frame.top = roundf(frame.top);
	frame.right = roundf(frame.right);
	frame.bottom = roundf(frame.bottom);

	fParentOffset.Set(frame.left, frame.top);

	fOwner = NULL;
	fParent = NULL;
	fNextSibling = NULL;
	fPreviousSibling = NULL;
	fFirstChild = NULL;

	fShowLevel = 0;
	fTopLevelView = false;

	fCurrentPicture = NULL;
	fCommArray = NULL;

	fVerScroller = NULL;
	fHorScroller = NULL;

	fIsPrinting = false;
	fAttached = false;

	// TODO: Since we cannot communicate failure, we don't use std::nothrow here
	// TODO: Maybe we could auto-delete those views on AddChild() instead?
	fState = new BPrivate::ViewState;

	fBounds = frame.OffsetToCopy(B_ORIGIN);
	fShelf = NULL;

	fEventMask = 0;
	fEventOptions = 0;
	fMouseEventOptions = 0;

	fLayoutData = new LayoutData;

	fToolTip = NULL;

	if ((flags & B_SUPPORTS_LAYOUT) != 0) {
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetLowUIColor(ViewUIColor());
		SetHighUIColor(B_PANEL_TEXT_COLOR);
	}
}


/**
 * @brief Frees the command array used for batching drawing operations.
 *
 * Deletes the fCommArray->array buffer and the fCommArray descriptor itself,
 * then sets fCommArray to NULL.
 */
void
BView::_RemoveCommArray()
{
	if (fCommArray) {
		delete [] fCommArray->array;
		delete fCommArray;
		fCommArray = NULL;
	}
}


/**
 * @brief Transfers ownership of this view (and all descendants) to @p newOwner.
 *
 * Removes this view from the old window's handler list (releasing focus if held)
 * and registers it with the new window.  Recursively propagates to all children.
 *
 * @param newOwner The new owning window, or NULL to detach.
 */
void
BView::_SetOwner(BWindow* newOwner)
{
	if (!newOwner)
		_RemoveCommArray();

	if (fOwner != newOwner && fOwner) {
		if (fOwner->fFocus == this)
			MakeFocus(false);

		if (fOwner->fLastMouseMovedView == this)
			fOwner->fLastMouseMovedView = NULL;

		fOwner->RemoveHandler(this);
		if (fShelf)
			fOwner->RemoveHandler(fShelf);
	}

	if (newOwner && newOwner != fOwner) {
		newOwner->AddHandler(this);
		if (fShelf)
			newOwner->AddHandler(fShelf);

		if (fTopLevelView)
			SetNextHandler(newOwner);
		else
			SetNextHandler(fParent);
	}

	fOwner = newOwner;

	for (BView* child = fFirstChild; child != NULL; child = child->fNextSibling)
		child->_SetOwner(newOwner);
}


/**
 * @brief Clips the view's drawing region to the shape recorded in @p picture.
 *
 * Sends AS_VIEW_CLIP_TO_PICTURE with the picture token, offset, and invert
 * flag.  If @p sync is true (the default for public callers) a round-trip
 * Sync() is performed to ensure the picture token remains valid on the server
 * before this view's commands are processed.
 *
 * @param picture The source picture, or NULL to reset picture clipping.
 * @param where   Offset applied to the picture's coordinate system.
 * @param invert  If true, the clipping is the inverse of the picture's shape.
 * @param sync    If true, flush and wait for the server before returning.
 */
void
BView::_ClipToPicture(BPicture* picture, BPoint where, bool invert, bool sync)
{
	if (!_CheckOwnerLockAndSwitchCurrent())
		return;

	if (picture == NULL) {
		fOwner->fLink->StartMessage(AS_VIEW_CLIP_TO_PICTURE);
		fOwner->fLink->Attach<int32>(-1);

		// NOTE: No need to sync here, since the -1 token cannot
		// become invalid on the server.
	} else {
		fOwner->fLink->StartMessage(AS_VIEW_CLIP_TO_PICTURE);
		fOwner->fLink->Attach<int32>(picture->Token());
		fOwner->fLink->Attach<BPoint>(where);
		fOwner->fLink->Attach<bool>(invert);

		// NOTE: "sync" defaults to true in public methods. If you know what
		// you are doing, i.e. if you know your BPicture stays valid, you
		// can avoid the performance impact of syncing. In a use-case where
		// the client creates BPictures on the stack, these BPictures may
		// have issued a AS_DELETE_PICTURE command to the ServerApp when Draw()
		// goes out of scope, and the command is processed earlier in the
		// ServerApp thread than the AS_VIEW_CLIP_TO_PICTURE command in the
		// ServerWindow thread, which will then have the result that no
		// ServerPicture is found of the token.
		if (sync)
			Sync();
	}
}


/**
 * @brief Clips the view's drawing region to (or to the inverse of) @p rect.
 *
 * @param rect    The clipping rectangle in view coordinates.
 * @param inverse If true, the region outside @p rect is clipped instead.
 */
void
BView::_ClipToRect(BRect rect, bool inverse)
{
	if (_CheckOwnerLockAndSwitchCurrent()) {
		fOwner->fLink->StartMessage(AS_VIEW_CLIP_TO_RECT);
		fOwner->fLink->Attach<bool>(inverse);
		fOwner->fLink->Attach<BRect>(rect);
		_FlushIfNotInTransaction();
	}
}


/**
 * @brief Clips the view's drawing region to (or to the inverse of) @p shape.
 *
 * Does nothing if @p shape is NULL or contains no operations/points.
 *
 * @param shape   The clipping shape in view coordinates.
 * @param inverse If true, the region outside @p shape is clipped instead.
 */
void
BView::_ClipToShape(BShape* shape, bool inverse)
{
	if (shape == NULL)
		return;

	shape_data* sd = BShape::Private(*shape).PrivateData();
	if (sd->opCount == 0 || sd->ptCount == 0)
		return;

	if (_CheckOwnerLockAndSwitchCurrent()) {
		fOwner->fLink->StartMessage(AS_VIEW_CLIP_TO_SHAPE);
		fOwner->fLink->Attach<bool>(inverse);
		fOwner->fLink->AttachShape(*shape);
		_FlushIfNotInTransaction();
	}
}


/**
 * @brief Removes @p child from this view's doubly-linked sibling list.
 *
 * Adjusts the fFirstChild pointer, the surrounding siblings' next/previous
 * pointers, and clears the child's parent and sibling pointers.
 *
 * @param child The child to unlink.
 *
 * @return true on success; false if @p child's parent is not this view.
 */
bool
BView::_RemoveChildFromList(BView* child)
{
	if (child->fParent != this)
		return false;

	if (fFirstChild == child) {
		// it's the first view in the list
		fFirstChild = child->fNextSibling;
	} else {
		// there must be a previous sibling
		child->fPreviousSibling->fNextSibling = child->fNextSibling;
	}

	if (child->fNextSibling)
		child->fNextSibling->fPreviousSibling = child->fPreviousSibling;

	child->fParent = NULL;
	child->fNextSibling = NULL;
	child->fPreviousSibling = NULL;

	return true;
}


/**
 * @brief Inserts @p child into this view's doubly-linked sibling list.
 *
 * If @p before is non-NULL, @p child is inserted immediately before it;
 * otherwise it is appended to the end of the list.  Panics via debugger() if
 * @p child already has a parent or @p before does not belong to this view.
 *
 * @param child  The view to insert.
 * @param before The sibling before which to insert, or NULL to append.
 *
 * @return true on success; false if @p child is NULL.
 */
bool
BView::_AddChildToList(BView* child, BView* before)
{
	if (!child)
		return false;
	if (child->fParent != NULL) {
		debugger("View already belongs to someone else");
		return false;
	}
	if (before != NULL && before->fParent != this) {
		debugger("Invalid before view");
		return false;
	}

	if (before != NULL) {
		// add view before this one
		child->fNextSibling = before;
		child->fPreviousSibling = before->fPreviousSibling;
		if (child->fPreviousSibling != NULL)
			child->fPreviousSibling->fNextSibling = child;

		before->fPreviousSibling = child;
		if (fFirstChild == before)
			fFirstChild = child;
	} else {
		// add view to the end of the list
		BView* last = fFirstChild;
		while (last != NULL && last->fNextSibling != NULL) {
			last = last->fNextSibling;
		}

		if (last != NULL) {
			last->fNextSibling = child;
			child->fPreviousSibling = last;
		} else {
			fFirstChild = child;
			child->fPreviousSibling = NULL;
		}

		child->fNextSibling = NULL;
	}

	child->fParent = this;
	return true;
}


/**
 * @brief Creates the server-side counterpart of this view.
 *
 * Only called when the view is part of the hierarchy (i.e. attached to a
 * window).  Sends AS_VIEW_CREATE or AS_VIEW_CREATE_ROOT, transmits all current
 * state, and recurses to create server objects for all child views.
 * RemoveSelf() issues AS_VIEW_DELETE to destroy the server object.
 *
 * @return Always true.
 */
bool
BView::_CreateSelf()
{
	// AS_VIEW_CREATE & AS_VIEW_CREATE_ROOT do not use the
	// current view mechanism via _CheckLockAndSwitchCurrent() - the token
	// of the view and its parent are both send to the server.

	if (fTopLevelView)
		fOwner->fLink->StartMessage(AS_VIEW_CREATE_ROOT);
	else
 		fOwner->fLink->StartMessage(AS_VIEW_CREATE);

	fOwner->fLink->Attach<int32>(_get_object_token_(this));
	fOwner->fLink->AttachString(Name());
	fOwner->fLink->Attach<BRect>(Frame());
	fOwner->fLink->Attach<BPoint>(LeftTop());
	fOwner->fLink->Attach<uint32>(ResizingMode());
	fOwner->fLink->Attach<uint32>(fEventMask);
	fOwner->fLink->Attach<uint32>(fEventOptions);
	fOwner->fLink->Attach<uint32>(Flags());
	fOwner->fLink->Attach<bool>(IsHidden(this));
	fOwner->fLink->Attach<rgb_color>(fState->view_color);
	if (fTopLevelView)
		fOwner->fLink->Attach<int32>(B_NULL_TOKEN);
	else
		fOwner->fLink->Attach<int32>(_get_object_token_(fParent));
	fOwner->fLink->Flush();

	_CheckOwnerLockAndSwitchCurrent();
	fState->UpdateServerState(*fOwner->fLink);

	// we create all its children, too

	for (BView* child = fFirstChild; child != NULL;
			child = child->fNextSibling) {
		child->_CreateSelf();
	}

	fOwner->fLink->Flush();
	return true;
}


/**
 * @brief Updates the local parent-offset and fires the FrameMoved() hook.
 *
 * Does not contact the server; the server-side position is already set by
 * MoveTo() or by a B_VIEW_MOVED notification from the server.
 *
 * @param x New left edge in parent coordinates (pixel-aligned).
 * @param y New top edge in parent coordinates (pixel-aligned).
 */
void
BView::_MoveTo(int32 x, int32 y)
{
	fParentOffset.Set(x, y);

	if (Window() != NULL && fFlags & B_FRAME_EVENTS) {
		BMessage moved(B_VIEW_MOVED);
		moved.AddInt64("when", system_time());
		moved.AddPoint("where", BPoint(x, y));

		BMessenger target(this);
		target.SendMessage(&moved);
	}
}


/**
 * @brief Updates local bounds and propagates the size change to children.
 *
 * Does not contact the server directly; the server-side size is already set by
 * ResizeBy() or by a B_VIEW_RESIZED notification.  If B_SUPPORTS_LAYOUT is
 * set the children are re-laid out via Relayout(); otherwise each child's
 * _ParentResizedBy() is called according to its resizing mode.
 * Fires the FrameResized() hook when done.
 *
 * @param deltaWidth  Change in width (pixel-aligned integer).
 * @param deltaHeight Change in height (pixel-aligned integer).
 */
void
BView::_ResizeBy(int32 deltaWidth, int32 deltaHeight)
{
	fBounds.right += deltaWidth;
	fBounds.bottom += deltaHeight;

	if (Window() == NULL) {
		// we're not supposed to exercise the resizing code in case
		// we haven't been attached to a window yet
		return;
	}

	// layout the children
	if ((fFlags & B_SUPPORTS_LAYOUT) != 0) {
		Relayout();
	} else {
		for (BView* child = fFirstChild; child; child = child->fNextSibling)
			child->_ParentResizedBy(deltaWidth, deltaHeight);
	}

	if (fFlags & B_FRAME_EVENTS) {
		BMessage resized(B_VIEW_RESIZED);
		resized.AddInt64("when", system_time());
		resized.AddInt32("width", fBounds.IntegerWidth());
		resized.AddInt32("height", fBounds.IntegerHeight());

		BMessenger target(this);
		target.SendMessage(&resized);
	}
}


/**
 * @brief Repositions and/or resizes this view according to its resizing mode.
 *
 * Interprets the four nibbles of the resizing mode (left, right, top, bottom
 * edges) and adjusts the frame so edges track the parent's right/bottom or
 * stay centred as appropriate, then calls _MoveTo() and _ResizeBy() as needed.
 *
 * @param x Change in the parent's width.
 * @param y Change in the parent's height.
 */
void
BView::_ParentResizedBy(int32 x, int32 y)
{
	uint32 resizingMode = fFlags & _RESIZE_MASK_;
	BRect newFrame = Frame();

	// follow with left side
	if ((resizingMode & 0x0F00U) == _VIEW_RIGHT_ << 8)
		newFrame.left += x;
	else if ((resizingMode & 0x0F00U) == _VIEW_CENTER_ << 8)
		newFrame.left += x / 2;

	// follow with right side
	if ((resizingMode & 0x000FU) == _VIEW_RIGHT_)
		newFrame.right += x;
	else if ((resizingMode & 0x000FU) == _VIEW_CENTER_)
		newFrame.right += x / 2;

	// follow with top side
	if ((resizingMode & 0xF000U) == _VIEW_BOTTOM_ << 12)
		newFrame.top += y;
	else if ((resizingMode & 0xF000U) == _VIEW_CENTER_ << 12)
		newFrame.top += y / 2;

	// follow with bottom side
	if ((resizingMode & 0x00F0U) == _VIEW_BOTTOM_ << 4)
		newFrame.bottom += y;
	else if ((resizingMode & 0x00F0U) == _VIEW_CENTER_ << 4)
		newFrame.bottom += y / 2;

	if (newFrame.LeftTop() != fParentOffset) {
		// move view
		_MoveTo((int32)roundf(newFrame.left), (int32)roundf(newFrame.top));
	}

	if (newFrame != Frame()) {
		// resize view
		int32 widthDiff = (int32)(newFrame.Width() - fBounds.Width());
		int32 heightDiff = (int32)(newFrame.Height() - fBounds.Height());
		_ResizeBy(widthDiff, heightDiff);
	}
}


/**
 * @brief Propagates a window-activation event to this view and all descendants.
 *
 * Calls WindowActivated() on this view and then recurses to all children.
 *
 * @param active true if the owning window has become active; false if deactivated.
 */
void
BView::_Activate(bool active)
{
	WindowActivated(active);

	for (BView* child = fFirstChild; child != NULL;
			child = child->fNextSibling) {
		child->_Activate(active);
	}
}


/**
 * @brief Finalises attachment of this view and all descendants to a window.
 *
 * Re-syncs any pending UI-colour aliases with the server, calls
 * AttachedToWindow() on this view, sets fAttached, starts pulse if needed,
 * invalidates the view to force an initial draw, recursively calls _Attach()
 * on children that have not yet been attached, and finally calls AllAttached().
 */
void
BView::_Attach()
{
	if (fOwner != NULL) {
		// unmask state flags to force [re]syncing with the app_server
		fState->valid_flags &= ~(B_VIEW_WHICH_VIEW_COLOR_BIT
			| B_VIEW_WHICH_LOW_COLOR_BIT | B_VIEW_WHICH_HIGH_COLOR_BIT);

		if (fState->which_view_color != B_NO_COLOR)
			SetViewUIColor(fState->which_view_color,
				fState->which_view_color_tint);

		if (fState->which_high_color != B_NO_COLOR)
			SetHighUIColor(fState->which_high_color,
				fState->which_high_color_tint);

		if (fState->which_low_color != B_NO_COLOR)
			SetLowUIColor(fState->which_low_color,
				fState->which_low_color_tint);
	}

	AttachedToWindow();

	fAttached = true;

	// after giving the view a chance to do this itself,
	// check for the B_PULSE_NEEDED flag and make sure the
	// window set's up the pulse messaging
	if (fOwner) {
		if (fFlags & B_PULSE_NEEDED) {
			_CheckLock();
			if (fOwner->fPulseRunner == NULL)
				fOwner->SetPulseRate(fOwner->PulseRate());
		}

		if (!fOwner->IsHidden())
			Invalidate();
	}

	for (BView* child = fFirstChild; child != NULL;
			child = child->fNextSibling) {
		// we need to check for fAttached as new views could have been
		// added in AttachedToWindow() - and those are already attached
		if (!child->fAttached)
			child->_Attach();
	}

	AllAttached();
}


/**
 * @brief Updates cached colour values after a B_COLORS_UPDATED notification.
 *
 * Reads the new RGB values for view colour, low colour, and high colour from
 * @p message (keyed by ui_color_name()), applies any tint, marks the
 * corresponding state bits valid, calls MessageReceived() so subclasses can
 * react, recurses to all children, and invalidates the view.
 *
 * @param message The B_COLORS_UPDATED message containing new colour values.
 */
void
BView::_ColorsUpdated(BMessage* message)
{
	if (fTopLevelView
		&& fLayoutData->fLayout != NULL
		&& !fState->IsValid(B_VIEW_WHICH_VIEW_COLOR_BIT)) {
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetHighUIColor(B_PANEL_TEXT_COLOR);
	}

	rgb_color color;

	const char* colorName = ui_color_name(fState->which_view_color);
	if (colorName != NULL && message->FindColor(colorName, &color) == B_OK) {
		fState->view_color = tint_color(color, fState->which_view_color_tint);
		fState->valid_flags |= B_VIEW_VIEW_COLOR_BIT;
	}

	colorName = ui_color_name(fState->which_low_color);
	if (colorName != NULL && message->FindColor(colorName, &color) == B_OK) {
		fState->low_color = tint_color(color, fState->which_low_color_tint);
		fState->valid_flags |= B_VIEW_LOW_COLOR_BIT;
	}

	colorName = ui_color_name(fState->which_high_color);
	if (colorName != NULL && message->FindColor(colorName, &color) == B_OK) {
		fState->high_color = tint_color(color, fState->which_high_color_tint);
		fState->valid_flags |= B_VIEW_HIGH_COLOR_BIT;
	}

	MessageReceived(message);

	for (BView* child = fFirstChild; child != NULL;
			child = child->fNextSibling)
		child->_ColorsUpdated(message);

	Invalidate();
}


/**
 * @brief Detaches this view and all descendants from their owning window.
 *
 * Calls DetachedFromWindow() on this view, sets fAttached to false, recurses
 * to children, fires AllDetached(), then cleans up window-level references
 * (focus, default button, key menu bar, last mouse-moved view, last view
 * token) and calls _SetOwner(NULL).
 */
void
BView::_Detach()
{
	DetachedFromWindow();
	fAttached = false;

	for (BView* child = fFirstChild; child != NULL;
			child = child->fNextSibling) {
		child->_Detach();
	}

	AllDetached();

	if (fOwner) {
		_CheckLock();

		if (!fOwner->IsHidden())
			Invalidate();

		// make sure our owner doesn't need us anymore

		if (fOwner->CurrentFocus() == this) {
			MakeFocus(false);
			// MakeFocus() is virtual and might not be
			// passing through to the BView version,
			// but we need to make sure at this point
			// that we are not the focus view anymore.
			if (fOwner->CurrentFocus() == this)
				fOwner->_SetFocus(NULL, true);
		}

		if (fOwner->fDefaultButton == this)
			fOwner->SetDefaultButton(NULL);

		if (fOwner->fKeyMenuBar == this)
			fOwner->fKeyMenuBar = NULL;

		if (fOwner->fLastMouseMovedView == this)
			fOwner->fLastMouseMovedView = NULL;

		if (fOwner->fLastViewToken == _get_object_token_(this))
			fOwner->fLastViewToken = B_NULL_TOKEN;

		_SetOwner(NULL);
	}
}


/**
 * @brief Dispatches a draw request from the server to the Draw() hook.
 *
 * Switches the server's current view, converts @p updateRect from screen to
 * view coordinates, pushes state, calls Draw(), pops state, and flushes.
 * Does nothing if the view is hidden or lacks B_WILL_DRAW.
 *
 * @param updateRect The region that needs redrawing, in screen coordinates.
 */
void
BView::_Draw(BRect updateRect)
{
	if (IsHidden(this) || !(Flags() & B_WILL_DRAW))
		return;

	// NOTE: if ViewColor() == B_TRANSPARENT_COLOR and no B_WILL_DRAW
	// -> View is simply not drawn at all

	_SwitchServerCurrentView();

	ConvertFromScreen(&updateRect);

	// TODO: make states robust (the hook implementation could
	// mess things up if it uses non-matching Push- and PopState(),
	// we would not be guaranteed to still have the same state on
	// the stack after having called Draw())
	PushState();
	Draw(updateRect);
	PopState();
	Flush();
}


/**
 * @brief Dispatches a post-children draw request to the DrawAfterChildren() hook.
 *
 * Similar to _Draw() but is only called when B_DRAW_ON_CHILDREN is set.
 *
 * @param updateRect The region that needs redrawing, in screen coordinates.
 */
void
BView::_DrawAfterChildren(BRect updateRect)
{
	if (IsHidden(this) || !(Flags() & B_WILL_DRAW)
		|| !(Flags() & B_DRAW_ON_CHILDREN))
		return;

	_SwitchServerCurrentView();

	ConvertFromScreen(&updateRect);

	// TODO: make states robust (see above)
	PushState();
	DrawAfterChildren(updateRect);
	PopState();
	Flush();
}


/**
 * @brief Propagates a B_FONTS_UPDATED message to this view and all descendants.
 *
 * Calls MessageReceived() so subclasses can react, then recurses to children.
 *
 * @param message The B_FONTS_UPDATED message.
 */
void
BView::_FontsUpdated(BMessage* message)
{
	MessageReceived(message);

	for (BView* child = fFirstChild; child != NULL;
			child = child->fNextSibling) {
		child->_FontsUpdated(message);
	}
}


/**
 * @brief Propagates a pulse tick to this view and all descendants.
 *
 * Calls Pulse() if B_PULSE_NEEDED is set, then recurses to all children.
 */
void
BView::_Pulse()
{
	if ((Flags() & B_PULSE_NEEDED) != 0)
		Pulse();

	for (BView* child = fFirstChild; child != NULL;
			child = child->fNextSibling) {
		child->_Pulse();
	}
}


/**
 * @brief Reads the latest drawing state from the server before detaching.
 *
 * Called by _RemoveSelf() while the view is still connected.  Ensures that
 * the local ViewState is up to date so it can be restored if the view is
 * later re-attached.  Recurses to all attached children.
 */
void
BView::_UpdateStateForRemove()
{
	// TODO: _CheckLockAndSwitchCurrent() would be good enough, no?
	if (!_CheckOwnerLockAndSwitchCurrent())
		return;

	fState->UpdateFrom(*fOwner->fLink);
//	if (!fState->IsValid(B_VIEW_FRAME_BIT)) {
//		fOwner->fLink->StartMessage(AS_VIEW_GET_COORD);
//
//		status_t code;
//		if (fOwner->fLink->FlushWithReply(code) == B_OK
//			&& code == B_OK) {
//			fOwner->fLink->Read<BPoint>(&fParentOffset);
//			fOwner->fLink->Read<BRect>(&fBounds);
//			fState->valid_flags |= B_VIEW_FRAME_BIT;
//		}
//	}

	// update children as well

	for (BView* child = fFirstChild; child != NULL;
			child = child->fNextSibling) {
		if (child->fOwner)
			child->_UpdateStateForRemove();
	}
}


/**
 * @brief Sends the drawing pattern to the server if it has changed.
 *
 * Skips the server round-trip when the cached pattern is already valid and
 * matches @p pattern.
 *
 * @param pattern The new fill/stroke pattern.
 */
inline void
BView::_UpdatePattern(::pattern pattern)
{
	if (fState->IsValid(B_VIEW_PATTERN_BIT) && pattern == fState->pattern)
		return;

	if (fOwner) {
		_CheckLockAndSwitchCurrent();

		fOwner->fLink->StartMessage(AS_VIEW_SET_PATTERN);
		fOwner->fLink->Attach< ::pattern>(pattern);

		fState->valid_flags |= B_VIEW_PATTERN_BIT;
	}

	fState->pattern = pattern;
}


/**
 * @brief Flushes the owner's command buffer unless a transaction is open.
 *
 * Avoids an unnecessary flush when drawing commands are batched inside a
 * Begin/EndPicture or similar transaction.
 */
void
BView::_FlushIfNotInTransaction()
{
	if (!fOwner->fInTransaction) {
		fOwner->Flush();
	}
}


/**
 * @brief Returns the BShelf replicant container installed on this view.
 *
 * @return The installed BShelf, or NULL if none.
 */
BShelf*
BView::_Shelf() const
{
	return fShelf;
}


/**
 * @brief Installs or replaces the BShelf replicant container on this view.
 *
 * Removes the old shelf from the owner's handler list (if attached) before
 * storing @p shelf, then adds the new shelf to the owner.
 *
 * @param shelf The new shelf, or NULL to remove the current one.
 */
void
BView::_SetShelf(BShelf* shelf)
{
	if (fShelf != NULL && fOwner != NULL)
		fOwner->RemoveHandler(fShelf);

	fShelf = shelf;

	if (fShelf != NULL && fOwner != NULL)
		fOwner->AddHandler(fShelf);
}


/**
 * @brief Sends a view bitmap to the server for use as the view background.
 *
 * Sends AS_VIEW_SET_VIEW_BITMAP with the bitmap token (or -1 for none),
 * source/destination rectangles, follow flags, and option flags.  Waits for
 * the server's status reply.
 *
 * @param bitmap      The bitmap to install, or NULL to remove the background.
 * @param srcRect     Source rectangle within @p bitmap.
 * @param dstRect     Destination rectangle in view coordinates.
 * @param followFlags Flags controlling how the bitmap follows view resizing.
 * @param options     Additional display options (e.g. B_TILE_BITMAP).
 *
 * @return B_OK on success, or B_ERROR if there is no owner or the lock fails.
 */
status_t
BView::_SetViewBitmap(const BBitmap* bitmap, BRect srcRect, BRect dstRect,
	uint32 followFlags, uint32 options)
{
	if (!_CheckOwnerLockAndSwitchCurrent())
		return B_ERROR;

	int32 serverToken = bitmap ? bitmap->_ServerToken() : -1;

	fOwner->fLink->StartMessage(AS_VIEW_SET_VIEW_BITMAP);
	fOwner->fLink->Attach<int32>(serverToken);
	fOwner->fLink->Attach<BRect>(srcRect);
	fOwner->fLink->Attach<BRect>(dstRect);
	fOwner->fLink->Attach<int32>(followFlags);
	fOwner->fLink->Attach<int32>(options);

	status_t status = B_ERROR;
	fOwner->fLink->FlushWithReply(status);

	return status;
}


/**
 * @brief Verifies that the view has an owner and that the owner's looper is locked,
 *        then makes this view the server's current view.
 *
 * Panics via debugger() if no owner is set.
 *
 * @return true if all checks passed and the server context was switched;
 *         false if the view has no owner.
 */
bool
BView::_CheckOwnerLockAndSwitchCurrent() const
{
	STRACE(("BView(%s)::_CheckOwnerLockAndSwitchCurrent()\n", Name()));

	if (fOwner == NULL) {
		debugger("View method requires owner and doesn't have one.");
		return false;
	}

	_CheckLockAndSwitchCurrent();

	return true;
}


/**
 * @brief Verifies that the view has an owner and that the owner's looper is locked.
 *
 * Panics via debugger() if no owner is set.
 *
 * @return true if the owner is set and its looper is locked; false otherwise.
 */
bool
BView::_CheckOwnerLock() const
{
	if (fOwner) {
		fOwner->check_lock();
		return true;
	} else {
		debugger("View method requires owner and doesn't have one.");
		return false;
	}
}


/**
 * @brief Verifies the owner's looper lock and makes this the server's current view.
 *
 * Does nothing if the view has no owner.  Does not panic on missing owner,
 * unlike _CheckOwnerLockAndSwitchCurrent().
 */
void
BView::_CheckLockAndSwitchCurrent() const
{
	STRACE(("BView(%s)::_CheckLockAndSwitchCurrent()\n", Name()));

	if (!fOwner)
		return;

	fOwner->check_lock();

	_SwitchServerCurrentView();
}


/**
 * @brief Verifies that the owner's looper is locked.
 *
 * Does nothing if the view has no owner.
 */
void
BView::_CheckLock() const
{
	if (fOwner)
		fOwner->check_lock();
}


/**
 * @brief Tells the server to make this view the active drawing target.
 *
 * Sends AS_SET_CURRENT_VIEW with this view's object token only when the
 * owner's cached last-view token differs, avoiding redundant round-trips.
 */
void
BView::_SwitchServerCurrentView() const
{
	int32 serverToken = _get_object_token_(this);

	if (fOwner->fLastViewToken != serverToken) {
		STRACE(("contacting app_server... sending token: %" B_PRId32 "\n",
			serverToken));
		fOwner->fLink->StartMessage(AS_SET_CURRENT_VIEW);
		fOwner->fLink->Attach<int32>(serverToken);

		fOwner->fLastViewToken = serverToken;
	}
}


/**
 * @brief Scrolls a scroll bar by a mouse-wheel delta.
 *
 * Multiplies @p delta by the scroll bar's large step when Shift is held, or
 * by three times the small step otherwise, then calls BScrollBar::SetValue().
 *
 * @param scrollBar The scroll bar to scroll.
 * @param delta     Raw mouse-wheel delta (positive scrolls forward/down).
 *
 * @return B_OK on success; B_BAD_VALUE if @p scrollBar is NULL or @p delta is 0.
 */
status_t
BView::ScrollWithMouseWheelDelta(BScrollBar* scrollBar, float delta)
{
	if (scrollBar == NULL || delta == 0.0f)
		return B_BAD_VALUE;

	float smallStep;
	float largeStep;
	scrollBar->GetSteps(&smallStep, &largeStep);

	// pressing the shift key scrolls faster (following the pseudo-standard set
	// by other desktop environments).
	if ((modifiers() & B_SHIFT_KEY) != 0)
		delta *= largeStep;
	else
		delta *= smallStep * 3;

	scrollBar->SetValue(scrollBar->Value() + delta);

	return B_OK;
}


#if __GNUC__ == 2


extern "C" void
_ReservedView1__5BView(BView* view, BRect rect)
{
	view->BView::DrawAfterChildren(rect);
}


extern "C" void
_ReservedView2__5BView(BView* view)
{
	// MinSize()
	perform_data_min_size data;
	view->Perform(PERFORM_CODE_MIN_SIZE, &data);
}


extern "C" void
_ReservedView3__5BView(BView* view)
{
	// MaxSize()
	perform_data_max_size data;
	view->Perform(PERFORM_CODE_MAX_SIZE, &data);
}


extern "C" BSize
_ReservedView4__5BView(BView* view)
{
	// PreferredSize()
	perform_data_preferred_size data;
	view->Perform(PERFORM_CODE_PREFERRED_SIZE, &data);
	return data.return_value;
}


extern "C" BAlignment
_ReservedView5__5BView(BView* view)
{
	// LayoutAlignment()
	perform_data_layout_alignment data;
	view->Perform(PERFORM_CODE_LAYOUT_ALIGNMENT, &data);
	return data.return_value;
}


extern "C" bool
_ReservedView6__5BView(BView* view)
{
	// HasHeightForWidth()
	perform_data_has_height_for_width data;
	view->Perform(PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH, &data);
	return data.return_value;
}


extern "C" void
_ReservedView7__5BView(BView* view, float width, float* min, float* max,
	float* preferred)
{
	// GetHeightForWidth()
	perform_data_get_height_for_width data;
	data.width = width;
	view->Perform(PERFORM_CODE_GET_HEIGHT_FOR_WIDTH, &data);
	if (min != NULL)
		*min = data.min;
	if (max != NULL)
		*max = data.max;
	if (preferred != NULL)
		*preferred = data.preferred;
}


extern "C" void
_ReservedView8__5BView(BView* view, BLayout* layout)
{
	// SetLayout()
	perform_data_set_layout data;
	data.layout = layout;
	view->Perform(PERFORM_CODE_SET_LAYOUT, &data);
}


extern "C" void
_ReservedView9__5BView(BView* view, bool descendants)
{
	// LayoutInvalidated()
	perform_data_layout_invalidated data;
	data.descendants = descendants;
	view->Perform(PERFORM_CODE_LAYOUT_INVALIDATED, &data);
}


extern "C" void
_ReservedView10__5BView(BView* view)
{
	// DoLayout()
	view->Perform(PERFORM_CODE_DO_LAYOUT, NULL);
}


#endif	// __GNUC__ == 2


extern "C" bool
B_IF_GCC_2(_ReservedView11__5BView, _ZN5BView15_ReservedView11Ev)(
	BView* view, BPoint point, BToolTip** _toolTip)
{
	// GetToolTipAt()
	perform_data_get_tool_tip_at data;
	data.point = point;
	data.tool_tip = _toolTip;
	view->Perform(PERFORM_CODE_GET_TOOL_TIP_AT, &data);
	return data.return_value;
}


extern "C" void
B_IF_GCC_2(_ReservedView12__5BView, _ZN5BView15_ReservedView12Ev)(
	BView* view)
{
	// LayoutChanged();
	view->Perform(PERFORM_CODE_LAYOUT_CHANGED, NULL);
}


/** @brief Reserved for future ABI use; currently a no-op. */
void BView::_ReservedView13() {}
/** @brief Reserved for future ABI use; currently a no-op. */
void BView::_ReservedView14() {}
/** @brief Reserved for future ABI use; currently a no-op. */
void BView::_ReservedView15() {}
/** @brief Reserved for future ABI use; currently a no-op. */
void BView::_ReservedView16() {}


/**
 * @brief Intentionally non-functional copy constructor.
 *
 * Declared private and exported for ABI compatibility only.  BView objects
 * cannot be copied; calling this constructor causes undefined behaviour.
 */
BView::BView(const BView& other)
	:
	BHandler()
{
	// this is private and not functional, but exported
}


/**
 * @brief Intentionally non-functional copy-assignment operator.
 *
 * Declared private and exported for ABI compatibility only.  BView objects
 * cannot be assigned; calling this operator causes undefined behaviour.
 *
 * @return *this (unchanged).
 */
BView&
BView::operator=(const BView& other)
{
	// this is private and not functional, but exported
	return *this;
}


/**
 * @brief Dumps a human-readable summary of this view's state to standard output.
 *
 * Prints hierarchy pointers (parent, first child, siblings, owner), flags,
 * bounds, pen state, colours, drawing mode, font, and other internal fields.
 * Intended for debugging only.
 */
void
BView::_PrintToStream()
{
	printf("BView::_PrintToStream()\n");
	printf("\tName: %s\n"
		"\tParent: %s\n"
		"\tFirstChild: %s\n"
		"\tNextSibling: %s\n"
		"\tPrevSibling: %s\n"
		"\tOwner(Window): %s\n"
		"\tToken: %" B_PRId32 "\n"
		"\tFlags: %" B_PRId32 "\n"
		"\tView origin: (%f,%f)\n"
		"\tView Bounds rectangle: (%f,%f,%f,%f)\n"
		"\tShow level: %d\n"
		"\tTopView?: %s\n"
		"\tBPicture: %s\n"
		"\tVertical Scrollbar %s\n"
		"\tHorizontal Scrollbar %s\n"
		"\tIs Printing?: %s\n"
		"\tShelf?: %s\n"
		"\tEventMask: %" B_PRId32 "\n"
		"\tEventOptions: %" B_PRId32 "\n",
	Name(),
	fParent ? fParent->Name() : "NULL",
	fFirstChild ? fFirstChild->Name() : "NULL",
	fNextSibling ? fNextSibling->Name() : "NULL",
	fPreviousSibling ? fPreviousSibling->Name() : "NULL",
	fOwner ? fOwner->Name() : "NULL",
	_get_object_token_(this),
	fFlags,
	fParentOffset.x, fParentOffset.y,
	fBounds.left, fBounds.top, fBounds.right, fBounds.bottom,
	fShowLevel,
	fTopLevelView ? "YES" : "NO",
	fCurrentPicture? "YES" : "NULL",
	fVerScroller? "YES" : "NULL",
	fHorScroller? "YES" : "NULL",
	fIsPrinting? "YES" : "NO",
	fShelf? "YES" : "NO",
	fEventMask,
	fEventOptions);

	printf("\tState status:\n"
		"\t\tLocalCoordianteSystem: (%f,%f)\n"
		"\t\tPenLocation: (%f,%f)\n"
		"\t\tPenSize: %f\n"
		"\t\tHighColor: [%d,%d,%d,%d]\n"
		"\t\tLowColor: [%d,%d,%d,%d]\n"
		"\t\tViewColor: [%d,%d,%d,%d]\n"
		"\t\tPattern: %" B_PRIx64 "\n"
		"\t\tDrawingMode: %d\n"
		"\t\tLineJoinMode: %d\n"
		"\t\tLineCapMode: %d\n"
		"\t\tMiterLimit: %f\n"
		"\t\tAlphaSource: %d\n"
		"\t\tAlphaFuntion: %d\n"
		"\t\tScale: %f\n"
		"\t\t(Print)FontAliasing: %s\n"
		"\t\tFont Info:\n",
	fState->origin.x, fState->origin.y,
	fState->pen_location.x, fState->pen_location.y,
	fState->pen_size,
	fState->high_color.red, fState->high_color.blue, fState->high_color.green, fState->high_color.alpha,
	fState->low_color.red, fState->low_color.blue, fState->low_color.green, fState->low_color.alpha,
	fState->view_color.red, fState->view_color.blue, fState->view_color.green, fState->view_color.alpha,
	*((uint64*)&(fState->pattern)),
	fState->drawing_mode,
	fState->line_join,
	fState->line_cap,
	fState->miter_limit,
	fState->alpha_source_mode,
	fState->alpha_function_mode,
	fState->scale,
	fState->font_aliasing? "YES" : "NO");

	fState->font.PrintToStream();

	// TODO: also print the line array.
}


/**
 * @brief Prints the view tree rooted at this view to standard output.
 *
 * Performs an iterative depth-first traversal and prints each view's name
 * indented by two spaces per level.  Intended for debugging only.
 */
void
BView::_PrintTree()
{
	int32 spaces = 2;
	BView* c = fFirstChild; //c = short for: current
	printf( "'%s'\n", Name() );
	if (c != NULL) {
		while(true) {
			// action block
			{
				for (int i = 0; i < spaces; i++)
					printf(" ");

				printf( "'%s'\n", c->Name() );
			}

			// go deep
			if (c->fFirstChild) {
				c = c->fFirstChild;
				spaces += 2;
			} else {
				// go right
				if (c->fNextSibling) {
					c = c->fNextSibling;
				} else {
					// go up
					while (!c->fParent->fNextSibling && c->fParent != this) {
						c = c->fParent;
						spaces -= 2;
					}

					// that enough! We've reached this view.
					if (c->fParent == this)
						break;

					c = c->fParent->fNextSibling;
					spaces -= 2;
				}
			}
		}
	}
}


// #pragma mark -


/**
 * @brief Returns the layout item registered at @p index for the wrapped view.
 *
 * @param index Zero-based index into the view's fLayoutItems list.
 *
 * @return The BLayoutItem at @p index, or NULL if out of range.
 */
BLayoutItem*
BView::Private::LayoutItemAt(int32 index)
{
	return fView->fLayoutData->fLayoutItems.ItemAt(index);
}


/**
 * @brief Returns the number of layout items registered for the wrapped view.
 *
 * @return The count of BLayoutItem objects in the view's fLayoutItems list.
 */
int32
BView::Private::CountLayoutItems()
{
	return fView->fLayoutData->fLayoutItems.CountItems();
}


/**
 * @brief Registers @p item as a layout item belonging to the wrapped view.
 *
 * @param item The layout item to register.
 */
void
BView::Private::RegisterLayoutItem(BLayoutItem* item)
{
	fView->fLayoutData->fLayoutItems.AddItem(item);
}


/**
 * @brief Removes @p item from the wrapped view's registered layout item list.
 *
 * @param item The layout item to deregister.
 */
void
BView::Private::DeregisterLayoutItem(BLayoutItem* item)
{
	fView->fLayoutData->fLayoutItems.RemoveItem(item);
}


/**
 * @brief Returns whether the wrapped view's cached min/max sizes are valid.
 *
 * @return true if fMinMaxValid is set; false if the cache needs to be
 *         recomputed before the next layout pass.
 */
bool
BView::Private::MinMaxValid()
{
	return fView->fLayoutData->fMinMaxValid;
}


/**
 * @brief Returns whether the wrapped view needs a layout pass.
 *
 * @return true if a layout pass is needed (relayout requested, layout invalid,
 *         or min/max cache stale) and no layout pass is currently in progress;
 *         false otherwise.
 */
bool
BView::Private::WillLayout()
{
	BView::LayoutData* data = fView->fLayoutData;
	if (data->fLayoutInProgress)
		return false;
	if (data->fNeedsRelayout || !data->fLayoutValid || !data->fMinMaxValid)
		return true;
	return false;
}
