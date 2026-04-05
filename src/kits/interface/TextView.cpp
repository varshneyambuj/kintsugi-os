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
 *   Copyright 2001-2020 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Stefano Ceccherini, stefano.ceccherini@gmail.com
 *       Marc Flerackers, mflerackers@androme.be
 *       Hiroshi Lockheimer (BTextView is based on his STEEngine)
 *       John Scipione, jscipione@gmail.com
 *       Oliver Tappe, zooey@hirschkaefer.de
 */


/**
 * @file TextView.cpp
 * @brief Implementation of BTextView, a full-featured multi-line text editing view
 *
 * BTextView provides a multi-line rich-text editor with word wrapping,
 * undo/redo, selection, clipboard integration, and optional run-style character
 * formatting. The core engine is derived from Hiroshi Lockheimer's STEEngine.
 *
 * @see BView, BScrollView, BTextControl
 */


// TODOs:
// - Consider using BObjectList instead of BList
// 	 for disallowed characters (it would remove a lot of reinterpret_casts)
// - Check for correctness and possible optimizations the calls to _Refresh(),
// 	 to refresh only changed parts of text (currently we often redraw the whole
//   text)

// Known Bugs:
// - Double buffering doesn't work well (disabled by default)


#include <TextView.h>

#include <algorithm>
#include <new>

#include <stdio.h>
#include <stdlib.h>

#include <Alignment.h>
#include <Application.h>
#include <Beep.h>
#include <Bitmap.h>
#include <Clipboard.h>
#include <ControlLook.h>
#include <Debug.h>
#include <Entry.h>
#include <Input.h>
#include <LayoutBuilder.h>
#include <LayoutUtils.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <PropertyInfo.h>
#include <Region.h>
#include <ScrollBar.h>
#include <SystemCatalog.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>

#include "InlineInput.h"
#include "LineBuffer.h"
#include "StyleBuffer.h"
#include "TextGapBuffer.h"
#include "UndoBuffer.h"
#include "WidthBuffer.h"


using namespace std;
using BPrivate::gSystemCatalog;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TextView"


#define TRANSLATE(str) \
	gSystemCatalog.GetString(B_TRANSLATE_MARK(str), "TextView")

#undef TRACE
#undef CALLED
//#define TRACE_TEXT_VIEW
#ifdef TRACE_TEXT_VIEW
#	include <FunctionTracer.h>
	static int32 sFunctionDepth = -1;
#	define CALLED(x...)	FunctionTracer _ft(printf, NULL, __PRETTY_FUNCTION__, sFunctionDepth)
#	define TRACE(x...)	{ BString _to; \
							_to.Append(' ', (sFunctionDepth + 1) * 2); \
							printf("%s", _to.String()); printf(x); }
#else
#	define CALLED(x...)
#	define TRACE(x...)
#endif


/** @brief Enables use of the _BWidthBuffer_ glyph-width cache for faster string measurement. */
#define USE_WIDTHBUFFER 1

/** @brief Enables double-buffered offscreen rendering (currently disabled due to known bugs). */
#define USE_DOUBLEBUFFERING 0


/**
 * @brief POD record describing a single flattened style run as serialised to a BMessage.
 *
 * Used internally by FlattenRunArray() and UnflattenRunArray() to convert between
 * the live text_run_array format and a portable, big-endian byte stream.
 */
struct flattened_text_run {
	int32	offset;
	font_family	family;
	font_style style;
	float	size;
	float	shear;		// typically 90.0
	uint16	face;		// typically 0
	uint8	red;
	uint8	green;
	uint8	blue;
	uint8	alpha;		// 255 == opaque
	uint16	_reserved_;	// 0
};

/**
 * @brief Header + variable-length array of flattened_text_run records.
 *
 * The complete on-disk/in-message representation of a text_run_array,
 * prefixed with a magic cookie and version number for validation.
 */
struct flattened_text_run_array {
	uint32	magic;
	uint32	version;
	int32	count;
	flattened_text_run styles[1];
};

/** @brief Magic cookie stored in big-endian form at the start of a flattened run array. */
static const uint32 kFlattenedTextRunArrayMagic = 'Ali!';

/** @brief Version tag embedded in every flattened run array; currently always 0. */
static const uint32 kFlattenedTextRunArrayVersion = 0;


/**
 * @brief Character-class constants used by _CharClassification() for word-break logic.
 *
 * Each constant identifies a broad category of Unicode/ASCII character, allowing
 * CanEndLine(), _PreviousWordBoundary(), and _NextWordBoundary() to make
 * language-agnostic decisions about where to break lines and words.
 */
enum {
	CHAR_CLASS_DEFAULT,       /**< Ordinary word character (letters, digits, etc.). */
	CHAR_CLASS_WHITESPACE,    /**< Space, tab, or newline. */
	CHAR_CLASS_GRAPHICAL,     /**< Operator and symbol characters (e.g. @c = @ # $ %). */
	CHAR_CLASS_QUOTE,         /**< Single or double quotation marks. */
	CHAR_CLASS_PUNCTUATION,   /**< Sentence-ending and separating punctuation. */
	CHAR_CLASS_PARENS_OPEN,   /**< Opening bracket, parenthesis, or brace. */
	CHAR_CLASS_PARENS_CLOSE,  /**< Closing bracket, parenthesis, or brace. */
	CHAR_CLASS_END_OF_TEXT    /**< The null terminator / past-end-of-buffer sentinel. */
};


/**
 * @brief Transient state maintained while the user is clicking or dragging in the view.
 *
 * Created in MouseDown() and destroyed in _StopMouseTracking(). Carries enough
 * context to extend or shrink the selection as the mouse moves, and fires a
 * recurring _PING_ message so that auto-scrolling works even when no B_MOUSE_MOVED
 * events arrive (e.g. cursor is stationary outside the view).
 */
class BTextView::TextTrackState {
public:
	/**
	 * @brief Constructs the tracking state and starts the auto-scroll pulse runner.
	 *
	 * @param messenger A BMessenger targeting the BTextView that owns this state,
	 *                  used to deliver periodic _PING_ messages for auto-scrolling.
	 */
	TextTrackState(BMessenger messenger);

	/** @brief Destroys the state and stops the auto-scroll pulse runner. */
	~TextTrackState();

	/**
	 * @brief Synthesises a MouseMoved() call so auto-scrolling works when the cursor is still.
	 *
	 * @param view The BTextView to deliver the simulated movement to.
	 */
	void SimulateMouseMovement(BTextView* view);

public:
	/** @brief Text offset at which the mouse button was originally pressed. */
	int32				clickOffset;
	/** @brief Whether the Shift key was held at the time of the initial click. */
	bool				shiftDown;
	/** @brief Bounding rectangle of the current selection at the time of the click, used to detect drag initiation. */
	BRect				selectionRect;
	/** @brief Last known mouse position, updated each time _PerformMouseMoved() is called. */
	BPoint				where;

	/** @brief The fixed end of the selection that does not move as the mouse is dragged. */
	int32				anchor;
	/** @brief Proposed new selection start, updated continuously during mouse tracking. */
	int32				selStart;
	/** @brief Proposed new selection end, updated continuously during mouse tracking. */
	int32				selEnd;

private:
	/** @brief Recurring message runner that fires _PING_ at ~40 Hz for auto-scroll. */
	BMessageRunner*		fRunner;
};


/**
 * @brief Layout-related inset and cached size data for the BTextView.
 *
 * Stores the four inset distances between the view bounds and the text rect,
 * plus the minimum and preferred BSize computed by _ValidateLayoutData().
 * The @c valid flag is cleared whenever the layout is invalidated so that the
 * cache is recomputed on the next layout pass.
 */
struct BTextView::LayoutData {
	/**
	 * @brief Initialises all insets to zero and marks the cache as invalid.
	 */
	LayoutData()
		: leftInset(0),
		  topInset(0),
		  rightInset(0),
		  bottomInset(0),
		  valid(false),
		  overridden(false)
	{
	}

	/** @brief Distance in pixels from the left view edge to the left text-rect edge. */
	float				leftInset;
	/** @brief Distance in pixels from the top view edge to the top text-rect edge. */
	float				topInset;
	/** @brief Distance in pixels from the right text-rect edge to the right view edge. */
	float				rightInset;
	/** @brief Distance in pixels from the bottom text-rect edge to the bottom view edge. */
	float				bottomInset;

	/** @brief Cached minimum size (recomputed when @c valid is false). */
	BSize				min;
	/** @brief Cached preferred size (recomputed when @c valid is false). */
	BSize				preferred;
	/** @brief True when @c min and @c preferred reflect the current text content. */
	bool				valid : 1;
	/** @brief True when the application has called SetInsets() and automatic inset calculation is suppressed. */
	bool				overridden : 1;
};


/** @brief Highlight colour used to mark the active (selected) portion of an IME inline-input string. */
static const rgb_color kBlueInputColor = { 152, 203, 255, 255 };

/** @brief Highlight colour used to mark the selected-within-inline sub-range during IME composition. */
static const rgb_color kRedInputColor = { 255, 152, 152, 255 };

/** @brief Horizontal scroll bar small-step size in pixels. */
static const float kHorizontalScrollBarStep = 10.0;

/** @brief Vertical scroll bar small-step size in pixels. */
static const float kVerticalScrollBarStep = 12.0;

/** @brief Internal message code delivered by the window shortcut handler to perform arrow-key navigation. */
static const int32 kMsgNavigateArrow = '_NvA';

/** @brief Internal message code delivered by the window shortcut handler to perform page/home/end navigation. */
static const int32 kMsgNavigatePage  = '_NvP';

/** @brief Internal message code delivered by the window shortcut handler to remove a whole word. */
static const int32 kMsgRemoveWord    = '_RmW';


/**
 * @brief Scripting property table for BTextView.
 *
 * Exposes "selection" (get/set), "Text" (count/get/set), and "text_run_array"
 * (get/set) through the BeOS scripting protocol so that external tools can
 * inspect and manipulate the view programmatically.
 *
 * @see BTextView::ResolveSpecifier(), BTextView::GetSupportedSuites()
 */
static property_info sPropertyList[] = {
	{
		"selection",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the current selection.", 0,
		{ B_INT32_TYPE, 0 }
	},
	{
		"selection",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Sets the current selection.", 0,
		{ B_INT32_TYPE, 0 }
	},
	{
		"Text",
		{ B_COUNT_PROPERTIES, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the length of the text in bytes.", 0,
		{ B_INT32_TYPE, 0 }
	},
	{
		"Text",
		{ B_GET_PROPERTY, 0 },
		{ B_RANGE_SPECIFIER, B_REVERSE_RANGE_SPECIFIER, 0 },
		"Returns the text in the specified range in the BTextView.", 0,
		{ B_STRING_TYPE, 0 }
	},
	{
		"Text",
		{ B_SET_PROPERTY, 0 },
		{ B_RANGE_SPECIFIER, B_REVERSE_RANGE_SPECIFIER, 0 },
		"Removes or inserts text into the specified range in the BTextView.", 0,
		{ B_STRING_TYPE, 0 }
	},
	{
		"text_run_array",
		{ B_GET_PROPERTY, 0 },
		{ B_RANGE_SPECIFIER, B_REVERSE_RANGE_SPECIFIER, 0 },
		"Returns the style information for the text in the specified range in "
			"the BTextView.", 0,
		{ B_RAW_TYPE, 0 },
	},
	{
		"text_run_array",
		{ B_SET_PROPERTY, 0 },
		{ B_RANGE_SPECIFIER, B_REVERSE_RANGE_SPECIFIER, 0 },
		"Sets the style information for the text in the specified range in the "
			"BTextView.", 0,
		{ B_RAW_TYPE, 0 },
	},

	{ 0 }
};


/**
 * @brief Constructs a BTextView with an explicit frame and text rect, using the default font and colour.
 *
 * @param frame      Position and size of the view within its parent.
 * @param name       Identifying name passed to BView.
 * @param textRect   Initial rectangle within the view where text is rendered.
 * @param resizeMask Resize flags passed to BView (e.g. B_FOLLOW_ALL).
 * @param flags      View flags; B_FRAME_EVENTS, B_PULSE_NEEDED, and
 *                   B_INPUT_METHOD_AWARE are always added internally.
 */
BTextView::BTextView(BRect frame, const char* name, BRect textRect,
	uint32 resizeMask, uint32 flags)
	:
	BView(frame, name, resizeMask,
		flags | B_FRAME_EVENTS | B_PULSE_NEEDED | B_INPUT_METHOD_AWARE),
	fText(NULL),
	fLines(NULL),
	fStyles(NULL),
	fDisallowedChars(NULL),
	fUndo(NULL),
	fDragRunner(NULL),
	fClickRunner(NULL),
	fLayoutData(NULL)
{
	_InitObject(textRect, NULL, NULL);
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
}


/**
 * @brief Constructs a BTextView with an explicit frame, text rect, font, and text colour.
 *
 * @param frame        Position and size of the view within its parent.
 * @param name         Identifying name passed to BView.
 * @param textRect     Initial rectangle within the view where text is rendered.
 * @param initialFont  The font to use for newly entered text; may be NULL to use the inherited view font.
 * @param initialColor The foreground colour for newly entered text; may be NULL to use B_DOCUMENT_TEXT_COLOR.
 * @param resizeMask   Resize flags passed to BView.
 * @param flags        View flags; B_FRAME_EVENTS, B_PULSE_NEEDED, and
 *                     B_INPUT_METHOD_AWARE are always added internally.
 */
BTextView::BTextView(BRect frame, const char* name, BRect textRect,
	const BFont* initialFont, const rgb_color* initialColor,
	uint32 resizeMask, uint32 flags)
	:
	BView(frame, name, resizeMask,
		flags | B_FRAME_EVENTS | B_PULSE_NEEDED | B_INPUT_METHOD_AWARE),
	fText(NULL),
	fLines(NULL),
	fStyles(NULL),
	fDisallowedChars(NULL),
	fUndo(NULL),
	fDragRunner(NULL),
	fClickRunner(NULL),
	fLayoutData(NULL)
{
	_InitObject(textRect, initialFont, initialColor);
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
}


/**
 * @brief Constructs a layout-mode BTextView with the default font and colour.
 *
 * Use this constructor when the view is managed by a BLayout; the text rect is
 * initialised to the view's initial bounds and recalculated automatically when
 * the layout assigns a size.
 *
 * @param name  Identifying name passed to BView.
 * @param flags View flags; B_FRAME_EVENTS, B_PULSE_NEEDED, and
 *              B_INPUT_METHOD_AWARE are always added internally.
 */
BTextView::BTextView(const char* name, uint32 flags)
	:
	BView(name,
		flags | B_FRAME_EVENTS | B_PULSE_NEEDED | B_INPUT_METHOD_AWARE),
	fText(NULL),
	fLines(NULL),
	fStyles(NULL),
	fDisallowedChars(NULL),
	fUndo(NULL),
	fDragRunner(NULL),
	fClickRunner(NULL),
	fLayoutData(NULL)
{
	_InitObject(Bounds(), NULL, NULL);
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
}


/**
 * @brief Constructs a layout-mode BTextView with an explicit initial font and colour.
 *
 * @param name         Identifying name passed to BView.
 * @param initialFont  The font to use for newly entered text; may be NULL.
 * @param initialColor The foreground colour for newly entered text; may be NULL.
 * @param flags        View flags; B_FRAME_EVENTS, B_PULSE_NEEDED, and
 *                     B_INPUT_METHOD_AWARE are always added internally.
 */
BTextView::BTextView(const char* name, const BFont* initialFont,
	const rgb_color* initialColor, uint32 flags)
	:
	BView(name,
		flags | B_FRAME_EVENTS | B_PULSE_NEEDED | B_INPUT_METHOD_AWARE),
	fText(NULL),
	fLines(NULL),
	fStyles(NULL),
	fDisallowedChars(NULL),
	fUndo(NULL),
	fDragRunner(NULL),
	fClickRunner(NULL),
	fLayoutData(NULL)
{
	_InitObject(Bounds(), initialFont, initialColor);
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
}


/**
 * @brief Reconstructs a BTextView from a BMessage archive (used by the scripting and layout systems).
 *
 * Restores all serialised properties including the text, selection, tab width,
 * alignment, word wrap, stylable flag, password mode, disallowed characters,
 * and the full run array.
 *
 * @param archive The archive message previously created by Archive().
 * @see BTextView::Archive(), BTextView::Instantiate()
 */
BTextView::BTextView(BMessage* archive)
	:
	BView(archive),
	fText(NULL),
	fLines(NULL),
	fStyles(NULL),
	fDisallowedChars(NULL),
	fUndo(NULL),
	fDragRunner(NULL),
	fClickRunner(NULL),
	fLayoutData(NULL)
{
	CALLED();
	BRect rect;

	if (archive->FindRect("_trect", &rect) != B_OK)
		rect.Set(0, 0, 0, 0);

	_InitObject(rect, NULL, NULL);

	bool toggle;

	if (archive->FindBool("_password", &toggle) == B_OK)
		HideTyping(toggle);

	const char* text = NULL;
	if (archive->FindString("_text", &text) == B_OK)
		SetText(text);

	int32 flag, flag2;
	if (archive->FindInt32("_align", &flag) == B_OK)
		SetAlignment((alignment)flag);

	float value;

	if (archive->FindFloat("_tab", &value) == B_OK)
		SetTabWidth(value);

	if (archive->FindInt32("_col_sp", &flag) == B_OK)
		SetColorSpace((color_space)flag);

	if (archive->FindInt32("_max", &flag) == B_OK)
		SetMaxBytes(flag);

	if (archive->FindInt32("_sel", &flag) == B_OK &&
		archive->FindInt32("_sel", &flag2) == B_OK)
		Select(flag, flag2);

	if (archive->FindBool("_stylable", &toggle) == B_OK)
		SetStylable(toggle);

	if (archive->FindBool("_auto_in", &toggle) == B_OK)
		SetAutoindent(toggle);

	if (archive->FindBool("_wrap", &toggle) == B_OK)
		SetWordWrap(toggle);

	if (archive->FindBool("_nsel", &toggle) == B_OK)
		MakeSelectable(!toggle);

	if (archive->FindBool("_nedit", &toggle) == B_OK)
		MakeEditable(!toggle);

	ssize_t disallowedCount = 0;
	const int32* disallowedChars = NULL;
	if (archive->FindData("_dis_ch", B_RAW_TYPE,
		(const void**)&disallowedChars, &disallowedCount) == B_OK) {

		fDisallowedChars = new BList;
		disallowedCount /= sizeof(int32);
		for (int32 x = 0; x < disallowedCount; x++) {
			fDisallowedChars->AddItem(
				reinterpret_cast<void*>(disallowedChars[x]));
		}
	}

	ssize_t runSize = 0;
	const void* flattenedRun = NULL;

	if (archive->FindData("_runs", B_RAW_TYPE, &flattenedRun, &runSize)
			== B_OK) {
		text_run_array* runArray = UnflattenRunArray(flattenedRun,
			(int32*)&runSize);
		if (runArray) {
			SetRunArray(0, fText->Length(), runArray);
			FreeRunArray(runArray);
		}
	}
}


/**
 * @brief Destroys the BTextView and frees all associated resources.
 *
 * Cancels any active input method transaction, stops mouse tracking, deletes
 * the offscreen bitmap, and releases the text gap buffer, line buffer, style
 * buffer, disallowed-character list, undo buffer, and layout data.
 */
BTextView::~BTextView()
{
	_CancelInputMethod();
	_StopMouseTracking();
	_DeleteOffscreen();

	delete fText;
	delete fLines;
	delete fStyles;
	delete fDisallowedChars;
	delete fUndo;
	delete fDragRunner;
	delete fClickRunner;
	delete fLayoutData;
}


/**
 * @brief Creates a new BTextView from an archive message (BArchivable hook).
 *
 * @param archive The archive message produced by Archive().
 * @return A newly allocated BTextView on success, or NULL if the archive is invalid.
 * @see BTextView::Archive()
 */
BArchivable*
BTextView::Instantiate(BMessage* archive)
{
	CALLED();
	if (validate_instantiation(archive, "BTextView"))
		return new BTextView(archive);
	return NULL;
}


/**
 * @brief Serialises the BTextView's state into a BMessage for archiving or scripting.
 *
 * Stores the text content, alignment, tab width, colour space, text rect,
 * maximum byte count, selection offsets, stylable and auto-indent flags,
 * word-wrap setting, editability/selectability, password mode, disallowed
 * characters, and the full text run array.
 *
 * @param data The message to receive the archived data.
 * @param deep If true, child views are also archived (delegated to BView).
 * @return B_OK on success, or a negative error code on failure.
 */
status_t
BTextView::Archive(BMessage* data, bool deep) const
{
	CALLED();
	status_t err = BView::Archive(data, deep);
	if (err == B_OK)
		err = data->AddString("_text", Text());
	if (err == B_OK)
		err = data->AddInt32("_align", fAlignment);
	if (err == B_OK)
		err = data->AddFloat("_tab", fTabWidth);
	if (err == B_OK)
		err = data->AddInt32("_col_sp", fColorSpace);
	if (err == B_OK)
		err = data->AddRect("_trect", fTextRect);
	if (err == B_OK)
		err = data->AddInt32("_max", fMaxBytes);
	if (err == B_OK)
		err = data->AddInt32("_sel", fSelStart);
	if (err == B_OK)
		err = data->AddInt32("_sel", fSelEnd);
	if (err == B_OK)
		err = data->AddBool("_stylable", fStylable);
	if (err == B_OK)
		err = data->AddBool("_auto_in", fAutoindent);
	if (err == B_OK)
		err = data->AddBool("_wrap", fWrap);
	if (err == B_OK)
		err = data->AddBool("_nsel", !fSelectable);
	if (err == B_OK)
		err = data->AddBool("_nedit", !fEditable);
	if (err == B_OK)
		err = data->AddBool("_password", IsTypingHidden());

	if (err == B_OK && fDisallowedChars != NULL && fDisallowedChars->CountItems() > 0) {
		err = data->AddData("_dis_ch", B_RAW_TYPE, fDisallowedChars->Items(),
			fDisallowedChars->CountItems() * sizeof(int32));
	}

	if (err == B_OK) {
		int32 runSize = 0;
		text_run_array* runArray = RunArray(0, fText->Length());

		void* flattened = FlattenRunArray(runArray, &runSize);
		if (flattened != NULL) {
			data->AddData("_runs", B_RAW_TYPE, flattened, runSize);
			free(flattened);
		} else
			err = B_NO_MEMORY;

		FreeRunArray(runArray);
	}

	return err;
}


/**
 * @brief Called when the view is attached to a window; performs initial setup.
 *
 * Sets the drawing mode to B_OP_COPY, configures the window pulse rate for
 * caret blinking, validates the text rect, triggers an auto-resize pass,
 * updates the scroll bars, and resets all click/drag tracking state.
 */
void
BTextView::AttachedToWindow()
{
	BView::AttachedToWindow();

	SetDrawingMode(B_OP_COPY);

	Window()->SetPulseRate(500000);

	fCaretVisible = false;
	fCaretTime = 0;
	fClickCount = 0;
	fClickTime = 0;
	fDragOffset = -1;
	fActive = false;

	_ValidateTextRect();

	_AutoResize(true);

	_UpdateScrollbars();

	SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
}


/**
 * @brief Called when the view is detached from its window.
 *
 * Delegates to BView::DetachedFromWindow(); subclasses may override to
 * perform additional cleanup.
 */
void
BTextView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Draws the text lines that intersect the given update rectangle.
 *
 * Determines which lines overlap @a updateRect and delegates to _DrawLines()
 * to render them along with any active selection highlight or caret.
 *
 * @param updateRect The rectangle that needs to be redrawn, in view coordinates.
 */
void
BTextView::Draw(BRect updateRect)
{
	// what lines need to be drawn?
	int32 startLine = _LineAt(BPoint(0.0, updateRect.top));
	int32 endLine = _LineAt(BPoint(0.0, updateRect.bottom));

	_DrawLines(startLine, endLine, -1, true);
}


/**
 * @brief Handles a mouse-button press inside the view.
 *
 * Cancels any active IME transaction, takes focus, hides the caret, and then
 * either shows the context menu (right button), begins a drag-detection pass
 * (click inside existing selection), or positions the caret and starts tracking
 * the selection (all other cases). Initiates a click-count sequence for double
 * and triple-click word/line selection.
 *
 * @param where The position of the mouse click in view coordinates.
 */
void
BTextView::MouseDown(BPoint where)
{
	// should we even bother?
	if (!fEditable && !fSelectable)
		return;

	_CancelInputMethod();

	if (!IsFocus())
		MakeFocus();

	_HideCaret();

	_StopMouseTracking();

	int32 modifiers = 0;
	uint32 buttons = 0;
	BMessage* currentMessage = Window()->CurrentMessage();
	if (currentMessage != NULL) {
		currentMessage->FindInt32("modifiers", &modifiers);
		currentMessage->FindInt32("buttons", (int32*)&buttons);
	}

	if (buttons == B_SECONDARY_MOUSE_BUTTON) {
		_ShowContextMenu(where);
		return;
	}

	BMessenger messenger(this);
	fTrackingMouse = new (nothrow) TextTrackState(messenger);
	if (fTrackingMouse == NULL)
		return;

	fTrackingMouse->clickOffset = OffsetAt(where);
	fTrackingMouse->shiftDown = modifiers & B_SHIFT_KEY;
	fTrackingMouse->where = where;

	bigtime_t clickTime = system_time();
	bigtime_t clickSpeed = 0;
	get_click_speed(&clickSpeed);
	bool multipleClick
		= clickTime - fClickTime < clickSpeed
			&& fLastClickOffset == fTrackingMouse->clickOffset;

	fWhere = where;

	SetMouseEventMask(B_POINTER_EVENTS | B_KEYBOARD_EVENTS,
		B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);

	if (fSelStart != fSelEnd && !fTrackingMouse->shiftDown && !multipleClick) {
		BRegion region;
		GetTextRegion(fSelStart, fSelEnd, &region);
		if (region.Contains(where)) {
			// Setup things for dragging
			fTrackingMouse->selectionRect = region.Frame();
			fClickCount = 1;
			fClickTime = clickTime;
			fLastClickOffset = OffsetAt(where);
			return;
		}
	}

	if (multipleClick) {
		if (fClickCount > 3) {
			fClickCount = 0;
			fClickTime = 0;
		} else {
			fClickCount++;
			fClickTime = clickTime;
		}
	} else if (!fTrackingMouse->shiftDown) {
		// If no multiple click yet and shift is not pressed, this is an
		// independent first click somewhere into the textview - we initialize
		// the corresponding members for handling potential multiple clicks:
		fLastClickOffset = fCaretOffset = fTrackingMouse->clickOffset;
		fClickCount = 1;
		fClickTime = clickTime;

		// Deselect any previously selected text
		Select(fTrackingMouse->clickOffset, fTrackingMouse->clickOffset);
	}

	if (fClickTime == clickTime) {
		BMessage message(_PING_);
		message.AddInt64("clickTime", clickTime);
		delete fClickRunner;

		BMessenger messenger(this);
		fClickRunner = new (nothrow) BMessageRunner(messenger, &message,
			clickSpeed, 1);
	}

	if (!fSelectable) {
		_StopMouseTracking();
		return;
	}

	int32 offset = fSelStart;
	if (fTrackingMouse->clickOffset > fSelStart)
		offset = fSelEnd;

	fTrackingMouse->anchor = offset;

	MouseMoved(where, B_INSIDE_VIEW, NULL);
}


/**
 * @brief Handles a mouse-button release inside the view.
 *
 * Finalises the current selection or drag operation via _PerformMouseUp(),
 * then releases the drag runner if one is active.
 *
 * @param where The position of the mouse release in view coordinates.
 */
void
BTextView::MouseUp(BPoint where)
{
	BView::MouseUp(where);
	_PerformMouseUp(where);

	delete fDragRunner;
	fDragRunner = NULL;
}


/**
 * @brief Handles mouse-movement events to update the selection or drag caret.
 *
 * If the mouse is being tracked for a click-and-drag, _PerformMouseMoved()
 * handles selection extension or drag initiation. Otherwise the view cursor
 * and drag caret are updated based on whether a drop message is present.
 *
 * @param where       Current mouse position in view coordinates.
 * @param code        One of B_ENTERED_VIEW, B_INSIDE_VIEW, B_EXITED_VIEW, etc.
 * @param dragMessage The dragging message if a drag-and-drop is in progress, or NULL.
 */
void
BTextView::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	// check if it's a "click'n'move"
	if (_PerformMouseMoved(where, code))
		return;

	switch (code) {
		case B_ENTERED_VIEW:
		case B_INSIDE_VIEW:
			_TrackMouse(where, dragMessage, true);
			break;

		case B_EXITED_VIEW:
			_DragCaret(-1);
			if (Window()->IsActive() && dragMessage == NULL)
				SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
			break;

		default:
			BView::MouseMoved(where, code, dragMessage);
	}
}


/**
 * @brief Called when the view's window is activated or deactivated.
 *
 * Activates or deactivates the view (showing/hiding the caret and selection
 * highlight) and updates the cursor if the mouse is within the view bounds.
 *
 * @param active true if the window has become active, false if it has lost focus.
 */
void
BTextView::WindowActivated(bool active)
{
	BView::WindowActivated(active);

	if (active && IsFocus()) {
		if (!fActive)
			_Activate();
	} else {
		if (fActive)
			_Deactivate();
	}

	BPoint where;
	uint32 buttons;
	GetMouse(&where, &buttons, false);

	if (Bounds().Contains(where))
		_TrackMouse(where, NULL);
}


/**
 * @brief Handles a key-down event, routing it to the appropriate handler.
 *
 * Dispatches navigation keys (arrows, page, home/end) to non-editable views
 * for scrolling. For editable views, dispatches backspace, delete, navigation,
 * and printable characters to their dedicated handlers, filtering disallowed
 * characters and showing/hiding the caret appropriately.
 *
 * @param bytes    The UTF-8 byte sequence of the key that was pressed.
 * @param numBytes The number of bytes in @a bytes.
 */
void
BTextView::KeyDown(const char* bytes, int32 numBytes)
{
	const char keyPressed = bytes[0];

	if (!fEditable) {
		// only arrow and page keys are allowed
		// (no need to hide the cursor)
		switch (keyPressed) {
			case B_LEFT_ARROW:
			case B_RIGHT_ARROW:
			case B_UP_ARROW:
			case B_DOWN_ARROW:
				_HandleArrowKey(keyPressed);
				break;

			case B_HOME:
			case B_END:
			case B_PAGE_UP:
			case B_PAGE_DOWN:
				_HandlePageKey(keyPressed);
				break;

			default:
				BView::KeyDown(bytes, numBytes);
				break;
		}

		return;
	}

	// hide the cursor and caret
	if (IsFocus())
		be_app->ObscureCursor();
	_HideCaret();

	switch (keyPressed) {
		case B_BACKSPACE:
			_HandleBackspace();
			break;

		case B_LEFT_ARROW:
		case B_RIGHT_ARROW:
		case B_UP_ARROW:
		case B_DOWN_ARROW:
			_HandleArrowKey(keyPressed);
			break;

		case B_DELETE:
			_HandleDelete();
			break;

		case B_HOME:
		case B_END:
		case B_PAGE_UP:
		case B_PAGE_DOWN:
			_HandlePageKey(keyPressed);
			break;

		case B_ESCAPE:
		case B_INSERT:
		case B_FUNCTION_KEY:
			// ignore, pass it up to superclass
			BView::KeyDown(bytes, numBytes);
			break;

		default:
			// bail out if the character is not allowed
			if (fDisallowedChars
				&& fDisallowedChars->HasItem(
					reinterpret_cast<void*>((uint32)keyPressed))) {
				beep();
				return;
			}

			_HandleAlphaKey(bytes, numBytes);
			break;
	}

	// draw the caret
	if (fSelStart == fSelEnd)
		_ShowCaret();
}


/**
 * @brief Called at the window pulse rate (500 ms) to blink the insertion caret.
 *
 * Inverts the caret if the view is active, editable or selectable, and the
 * selection is collapsed to a single offset.
 */
void
BTextView::Pulse()
{
	if (fActive && (fEditable || fSelectable) && fSelStart == fSelEnd) {
		if (system_time() > (fCaretTime + 500000.0))
			_InvertCaret();
	}
}


/**
 * @brief Called when the view's frame rectangle changes size.
 *
 * If the view is not auto-resizable, adjusts the text rect for the new width
 * and height, recalculates line breaks for word-wrap mode, or repositions the
 * text rect and updates scroll bars for non-wrap mode.
 *
 * @param newWidth  The new width of the view in pixels.
 * @param newHeight The new height of the view in pixels.
 */
void
BTextView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);

	// frame resized in _AutoResize() instead
	if (fResizable)
		return;

	if (fWrap) {
		// recalculate line breaks
		// will update scroll bars if text rect changes
		_ResetTextRect();
	} else {
		// don't recalculate line breaks,
		// move text rect into position and redraw.

		float dataWidth = _TextWidth();
		newWidth = std::max(dataWidth, newWidth);

		// align rect
		BRect rect(fLayoutData->leftInset, fLayoutData->topInset,
			newWidth - fLayoutData->rightInset,
			newHeight - fLayoutData->bottomInset);

		rect = BLayoutUtils::AlignOnRect(rect,
			BSize(fTextRect.Width(), fTextRect.Height()),
			BAlignment(fAlignment, B_ALIGN_TOP));
		fTextRect.OffsetTo(rect.left, rect.top);

		// must invalidate whole thing because of highlighting
		Invalidate();
		_UpdateScrollbars();
	}
}


/**
 * @brief Gives or removes keyboard focus from the view.
 *
 * Activates the view (showing caret/selection) when gaining focus in an active
 * window, and deactivates it when losing focus.
 *
 * @param focus true to give focus to this view, false to remove it.
 */
void
BTextView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);

	if (focus && Window() != NULL && Window()->IsActive()) {
		if (!fActive)
			_Activate();
	} else {
		if (fActive)
			_Deactivate();
	}
}


/**
 * @brief Dispatches incoming BMessages to the appropriate handler.
 *
 * Handles clipboard operations (B_CUT, B_COPY, B_PASTE, B_UNDO, B_SELECT_ALL),
 * IME events (B_INPUT_METHOD_EVENT), scripting messages (B_SET_PROPERTY,
 * B_GET_PROPERTY, B_COUNT_PROPERTIES), and internal timing messages
 * (_PING_, _DISPOSE_DRAG_, kMsgNavigateArrow, kMsgNavigatePage, kMsgRemoveWord).
 * Dropped messages are forwarded to _MessageDropped().
 *
 * @param message The message to be processed.
 */
void
BTextView::MessageReceived(BMessage* message)
{
	// ToDo: block input if not editable (Andrew)

	// was this message dropped?
	if (message->WasDropped()) {
		BPoint dropOffset;
		BPoint dropPoint = message->DropPoint(&dropOffset);
		ConvertFromScreen(&dropPoint);
		ConvertFromScreen(&dropOffset);
		if (!_MessageDropped(message, dropPoint, dropOffset))
			BView::MessageReceived(message);

		return;
	}

	switch (message->what) {
		case B_CUT:
			if (!IsTypingHidden())
				Cut(be_clipboard);
			else
				beep();
			break;

		case B_COPY:
			if (!IsTypingHidden())
				Copy(be_clipboard);
			else
				beep();
			break;

		case B_PASTE:
			Paste(be_clipboard);
			break;

		case B_UNDO:
			Undo(be_clipboard);
			break;

		case B_SELECT_ALL:
			SelectAll();
			break;

		case B_INPUT_METHOD_EVENT:
		{
			int32 opcode;
			if (message->FindInt32("be:opcode", &opcode) == B_OK) {
				switch (opcode) {
					case B_INPUT_METHOD_STARTED:
					{
						BMessenger messenger;
						if (message->FindMessenger("be:reply_to", &messenger)
								== B_OK) {
							ASSERT(fInline == NULL);
							fInline = new InlineInput(messenger);
						}
						break;
					}

					case B_INPUT_METHOD_STOPPED:
						delete fInline;
						fInline = NULL;
						break;

					case B_INPUT_METHOD_CHANGED:
						if (fInline != NULL)
							_HandleInputMethodChanged(message);
						break;

					case B_INPUT_METHOD_LOCATION_REQUEST:
						if (fInline != NULL)
							_HandleInputMethodLocationRequest();
						break;

					default:
						break;
				}
			}
			break;
		}

		case B_SET_PROPERTY:
		case B_GET_PROPERTY:
		case B_COUNT_PROPERTIES:
		{
			BPropertyInfo propInfo(sPropertyList);
			BMessage specifier;
			const char* property;

			if (message->GetCurrentSpecifier(NULL, &specifier) < B_OK
				|| specifier.FindString("property", &property) < B_OK) {
				BView::MessageReceived(message);
				return;
			}

			if (propInfo.FindMatch(message, 0, &specifier, specifier.what,
					property) < B_OK) {
				BView::MessageReceived(message);
				break;
			}

			BMessage reply;
			bool handled = false;
			switch(message->what) {
				case B_GET_PROPERTY:
					handled = _GetProperty(message, &specifier, property,
						&reply);
					break;

				case B_SET_PROPERTY:
					handled = _SetProperty(message, &specifier, property,
						&reply);
					break;

				case B_COUNT_PROPERTIES:
					handled = _CountProperties(message, &specifier,
						property, &reply);
					break;

				default:
					break;
			}
			if (handled)
				message->SendReply(&reply);
			else
				BView::MessageReceived(message);
			break;
		}

		case _PING_:
		{
			if (message->HasInt64("clickTime")) {
				bigtime_t clickTime;
				message->FindInt64("clickTime", &clickTime);
				if (clickTime == fClickTime) {
					if (fSelStart != fSelEnd && fSelectable) {
						BRegion region;
						GetTextRegion(fSelStart, fSelEnd, &region);
						if (region.Contains(fWhere))
							_TrackMouse(fWhere, NULL);
					}
					delete fClickRunner;
					fClickRunner = NULL;
				}
			} else if (fTrackingMouse) {
				fTrackingMouse->SimulateMouseMovement(this);
				_PerformAutoScrolling();
			}
			break;
		}

		case _DISPOSE_DRAG_:
			if (fEditable)
				_TrackDrag(fWhere);
			break;

		case kMsgNavigateArrow:
		{
			int32 key = message->GetInt32("key", 0);
			int32 modifiers = message->GetInt32("modifiers", 0);
			_HandleArrowKey(key, modifiers);
			break;
		}

		case kMsgNavigatePage:
		{
			int32 key = message->GetInt32("key", 0);
			int32 modifiers = message->GetInt32("modifiers", 0);
			_HandlePageKey(key, modifiers);
			break;
		}

		case kMsgRemoveWord:
		{
			int32 key = message->GetInt32("key", 0);
			int32 modifiers = message->GetInt32("modifiers", 0);
			if (key == B_DELETE)
				_HandleDelete(modifiers);
			else if (key == B_BACKSPACE)
				_HandleBackspace(modifiers);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Resolves a scripting specifier to the BHandler that should handle the message.
 *
 * Checks the property against sPropertyList; if it matches, returns @c this,
 * otherwise delegates to BView::ResolveSpecifier().
 *
 * @param message   The scripting message being resolved.
 * @param index     The specifier index within the message.
 * @param specifier The specifier sub-message extracted from @a message.
 * @param what      The specifier constant (e.g. B_DIRECT_SPECIFIER).
 * @param property  The property name string.
 * @return The BHandler that should handle the message.
 */
BHandler*
BTextView::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 what, const char* property)
{
	BPropertyInfo propInfo(sPropertyList);
	BHandler* target = this;

	if (propInfo.FindMatch(message, index, specifier, what, property) < B_OK) {
		target = BView::ResolveSpecifier(message, index, specifier, what,
			property);
	}

	return target;
}


/**
 * @brief Reports the scripting suites and properties supported by BTextView.
 *
 * Adds the suite name "suite/vnd.Be-text-view" and the flattened property
 * info table to @a data, then delegates to BView::GetSupportedSuites().
 *
 * @param data The message to receive the suite and property information.
 * @return B_OK on success, B_BAD_VALUE if @a data is NULL, or another error code.
 */
status_t
BTextView::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t err = data->AddString("suites", "suite/vnd.Be-text-view");
	if (err != B_OK)
		return err;

	BPropertyInfo prop_info(sPropertyList);
	err = data->AddFlat("messages", &prop_info);

	if (err != B_OK)
		return err;
	return BView::GetSupportedSuites(data);
}


/**
 * @brief Dispatches binary-compatibility hook calls for layout-related overrides.
 *
 * Handles PERFORM_CODE_MIN_SIZE, PERFORM_CODE_MAX_SIZE, PERFORM_CODE_PREFERRED_SIZE,
 * PERFORM_CODE_LAYOUT_ALIGNMENT, PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH,
 * PERFORM_CODE_GET_HEIGHT_FOR_WIDTH, PERFORM_CODE_SET_LAYOUT,
 * PERFORM_CODE_LAYOUT_INVALIDATED, and PERFORM_CODE_DO_LAYOUT.
 * Unknown codes are forwarded to BView::Perform().
 *
 * @param code  The perform code identifying which virtual method to call.
 * @param _data Pointer to the perform_data_* struct corresponding to @a code.
 * @return B_OK on success, or the result from BView::Perform() for unknown codes.
 */
status_t
BTextView::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BTextView::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BTextView::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BTextView::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BTextView::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BTextView::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BTextView::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BTextView::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BTextView::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BTextView::DoLayout();
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


/**
 * @brief Replaces all text in the view with the given null-terminated string.
 *
 * Computes the length automatically via strlen() and delegates to
 * SetText(const char*, int32, const text_run_array*).
 *
 * @param text The null-terminated replacement string, or NULL to clear all text.
 * @param runs Optional run array specifying per-character font and colour; may be NULL.
 */
void
BTextView::SetText(const char* text, const text_run_array* runs)
{
	SetText(text, text ? strlen(text) : 0, runs);
}


/**
 * @brief Replaces all text in the view with the first @a length bytes of @a text.
 *
 * Hides the caret, clears the existing text, inserts the new text, recalculates
 * line breaks, and resets the caret to offset 0.
 *
 * @param text   The replacement text buffer (does not need to be null-terminated).
 * @param length The number of bytes from @a text to insert.
 * @param runs   Optional run array for rich-text styling; may be NULL.
 */
void
BTextView::SetText(const char* text, int32 length, const text_run_array* runs)
{
	_CancelInputMethod();

	// hide the caret/unhighlight the selection
	if (fActive) {
		if (fSelStart != fSelEnd) {
			if (fSelectable)
				Highlight(fSelStart, fSelEnd);
		} else
			_HideCaret();
	}

	// remove data from buffer
	if (fText->Length() > 0)
		DeleteText(0, fText->Length());

	if (text != NULL && length > 0)
		InsertText(text, length, 0, runs);

	// bounds are invalid, set them based on text
	if (!Bounds().IsValid()) {
		ResizeTo(LineWidth(0) - 1, LineHeight(0));
		fTextRect = Bounds();
		_ValidateTextRect();
		_UpdateInsets(fTextRect);
	}

	// recalculate line breaks and draw the text
	_Refresh(0, length);
	fCaretOffset = fSelStart = fSelEnd = 0;

	// draw the caret
	_ShowCaret();
}


/**
 * @brief Replaces all text in the view with bytes read from a BFile.
 *
 * Reads @a length bytes starting at @a offset from @a file into the text
 * buffer, applies the optional run array, recalculates line breaks, and
 * scrolls to the beginning.
 *
 * @param file   The open BFile to read from; must not be NULL.
 * @param offset The byte offset within @a file at which to start reading.
 * @param length The number of bytes to read.
 * @param runs   Optional run array for rich-text styling; may be NULL.
 */
void
BTextView::SetText(BFile* file, int32 offset, int32 length,
	const text_run_array* runs)
{
	CALLED();

	_CancelInputMethod();

	if (file == NULL)
		return;

	if (fText->Length() > 0)
		DeleteText(0, fText->Length());

	if (!fText->InsertText(file, offset, length, 0))
		return;

	// update the start offsets of each line below offset
	fLines->BumpOffset(length, _LineAt(offset) + 1);

	// update the style runs
	fStyles->BumpOffset(length, fStyles->OffsetToRun(offset - 1) + 1);

	if (fStylable && runs != NULL)
		SetRunArray(offset, offset + length, runs);
	else {
		// apply null-style to inserted text
		_ApplyStyleRange(offset, offset + length);
	}

	// recalculate line breaks and draw the text
	_Refresh(0, length);
	fCaretOffset = fSelStart = fSelEnd = 0;
	ScrollToOffset(fSelStart);

	// draw the caret
	_ShowCaret();
}


/**
 * @brief Inserts a null-terminated string at the current selection start.
 *
 * @param text The null-terminated text to insert; ignored if NULL.
 * @param runs Optional run array for rich-text styling; may be NULL.
 */
void
BTextView::Insert(const char* text, const text_run_array* runs)
{
	if (text != NULL)
		_DoInsertText(text, strlen(text), fSelStart, runs);
}


/**
 * @brief Inserts the first @a length bytes of @a text at the current selection start.
 *
 * @param text   The text buffer to insert; ignored if NULL.
 * @param length The maximum number of bytes to read from @a text.
 * @param runs   Optional run array for rich-text styling; may be NULL.
 */
void
BTextView::Insert(const char* text, int32 length, const text_run_array* runs)
{
	if (text != NULL && length > 0)
		_DoInsertText(text, strnlen(text, length), fSelStart, runs);
}


/**
 * @brief Inserts text at an explicit byte offset within the buffer.
 *
 * @param offset The byte offset at which to insert; clamped to [0, TextLength()].
 * @param text   The text buffer to insert; ignored if NULL.
 * @param length The maximum number of bytes to read from @a text.
 * @param runs   Optional run array for rich-text styling; may be NULL.
 */
void
BTextView::Insert(int32 offset, const char* text, int32 length,
	const text_run_array* runs)
{
	// pin offset at reasonable values
	if (offset < 0)
		offset = 0;
	else if (offset > fText->Length())
		offset = fText->Length();

	if (text != NULL && length > 0)
		_DoInsertText(text, strnlen(text, length), offset, runs);
}


/**
 * @brief Deletes the currently selected text (from fSelStart to fSelEnd).
 *
 * Equivalent to Delete(fSelStart, fSelEnd).
 */
void
BTextView::Delete()
{
	Delete(fSelStart, fSelEnd);
}


/**
 * @brief Deletes the text in the range [@a startOffset, @a endOffset).
 *
 * Both offsets are clamped to [0, TextLength()]. If the range is empty,
 * nothing happens. Adjusts the caret and selection after deletion and
 * redraws the affected lines.
 *
 * @param startOffset The byte offset of the first character to delete.
 * @param endOffset   The byte offset one past the last character to delete.
 */
void
BTextView::Delete(int32 startOffset, int32 endOffset)
{
	CALLED();

	// pin offsets at reasonable values
	if (startOffset < 0)
		startOffset = 0;
	else if (startOffset > fText->Length())
		startOffset = fText->Length();

	if (endOffset < 0)
		endOffset = 0;
	else if (endOffset > fText->Length())
		endOffset = fText->Length();

	// anything to delete?
	if (startOffset == endOffset)
		return;

	// hide the caret/unhighlight the selection
	if (fActive) {
		if (fSelStart != fSelEnd) {
			if (fSelectable)
				Highlight(fSelStart, fSelEnd);
		} else
			_HideCaret();
	}
	// remove data from buffer
	DeleteText(startOffset, endOffset);

	// check if the caret needs to be moved
	if (fCaretOffset >= endOffset)
		fCaretOffset -= (endOffset - startOffset);
	else if (fCaretOffset >= startOffset && fCaretOffset < endOffset)
		fCaretOffset = startOffset;

	fSelEnd = fSelStart = fCaretOffset;

	// recalculate line breaks and draw what's left
	_Refresh(startOffset, endOffset, fCaretOffset);

	// draw the caret
	_ShowCaret();
}


/**
 * @brief Returns a pointer to the raw text buffer (gap buffer flattened to contiguous memory).
 *
 * The pointer is valid until the next modification of the text.
 *
 * @return A null-terminated C string containing all text in the view.
 */
const char*
BTextView::Text() const
{
	return fText->RealText();
}


/**
 * @brief Returns the total number of bytes of text currently stored in the view.
 *
 * @return The byte length of the text, not including any null terminator.
 */
int32
BTextView::TextLength() const
{
	return fText->Length();
}


/**
 * @brief Copies @a length bytes of text starting at @a offset into @a buffer.
 *
 * The caller is responsible for ensuring that @a buffer is large enough to
 * hold @a length bytes plus a null terminator.
 *
 * @param offset The byte offset of the first character to copy.
 * @param length The number of bytes to copy.
 * @param buffer Destination buffer; ignored if NULL.
 */
void
BTextView::GetText(int32 offset, int32 length, char* buffer) const
{
	if (buffer != NULL)
		fText->GetString(offset, length, buffer);
}


/**
 * @brief Returns the byte value of the character at the given byte @a offset.
 *
 * @param offset The zero-based byte offset of the desired character.
 * @return The byte at @a offset, or '\0' if @a offset is out of range.
 */
uchar
BTextView::ByteAt(int32 offset) const
{
	if (offset < 0 || offset >= fText->Length())
		return '\0';

	return fText->RealCharAt(offset);
}


/**
 * @brief Returns the total number of lines of text in the view.
 *
 * @return The line count, always at least 1 even when the buffer is empty.
 */
int32
BTextView::CountLines() const
{
	return fLines->NumLines();
}


/**
 * @brief Returns the line number that contains the start of the current selection.
 *
 * @return The zero-based line index of the caret or selection start.
 */
int32
BTextView::CurrentLine() const
{
	return LineAt(fSelStart);
}


/**
 * @brief Moves the caret to the start of the specified line.
 *
 * Cancels any IME transaction, hides the current caret, collapses the
 * selection to the first byte of line @a index, and shows the caret there.
 *
 * @param index The zero-based line number to navigate to.
 */
void
BTextView::GoToLine(int32 index)
{
	_CancelInputMethod();
	_HideCaret();
	fSelStart = fSelEnd = fCaretOffset = OffsetAt(index);
	_ShowCaret();
}


/**
 * @brief Cuts the selected text to the clipboard.
 *
 * Records a CutUndoBuffer, copies the selection to @a clipboard, then
 * deletes it. Does nothing if the view is not editable.
 *
 * @param clipboard The clipboard to receive the cut text.
 */
void
BTextView::Cut(BClipboard* clipboard)
{
	_CancelInputMethod();
	if (!fEditable)
		return;
	if (fUndo) {
		delete fUndo;
		fUndo = new CutUndoBuffer(this);
	}
	Copy(clipboard);
	Delete();
}


/**
 * @brief Copies the selected text (and, if stylable, its run array) to the clipboard.
 *
 * Places "text/plain" MIME data in @a clipboard and, when the view is stylable,
 * also adds "application/x-vnd.Be-text_run_array" data.
 *
 * @param clipboard The clipboard to receive the copied data.
 */
void
BTextView::Copy(BClipboard* clipboard)
{
	_CancelInputMethod();

	if (clipboard->Lock()) {
		clipboard->Clear();

		BMessage* clip = clipboard->Data();
		if (clip != NULL) {
			int32 numBytes = fSelEnd - fSelStart;
			const char* text = fText->GetString(fSelStart, &numBytes);
			clip->AddData("text/plain", B_MIME_TYPE, text, numBytes);

			int32 size;
			if (fStylable) {
				text_run_array* runArray = RunArray(fSelStart, fSelEnd, &size);
				clip->AddData("application/x-vnd.Be-text_run_array",
					B_MIME_TYPE, runArray, size);
				FreeRunArray(runArray);
			}
			clipboard->Commit();
		}
		clipboard->Unlock();
	}
}


/**
 * @brief Pastes plain text (and optionally run-array style data) from the clipboard.
 *
 * Filters disallowed characters, records a PasteUndoBuffer, replaces any
 * current selection, inserts the clipboard text, and scrolls to the new
 * selection end. Does nothing if the view is not editable or the clipboard
 * cannot be locked.
 *
 * @param clipboard The clipboard to paste from.
 */
void
BTextView::Paste(BClipboard* clipboard)
{
	CALLED();
	_CancelInputMethod();

	if (!fEditable || !clipboard->Lock())
		return;

	BMessage* clip = clipboard->Data();
	if (clip != NULL) {
		const char* text = NULL;
		ssize_t length = 0;

		if (clip->FindData("text/plain", B_MIME_TYPE,
				(const void**)&text, &length) == B_OK) {
			text_run_array* runArray = NULL;
			ssize_t runLength = 0;

			if (fStylable) {
				clip->FindData("application/x-vnd.Be-text_run_array",
					B_MIME_TYPE, (const void**)&runArray, &runLength);
			}

			_FilterDisallowedChars((char*)text, length, runArray);

			if (length < 1) {
				beep();
				clipboard->Unlock();
				return;
			}

			if (fUndo) {
				delete fUndo;
				fUndo = new PasteUndoBuffer(this, text, length, runArray,
					runLength);
			}

			if (fSelStart != fSelEnd)
				Delete();

			Insert(text, length, runArray);
			ScrollToOffset(fSelEnd);
		}
	}

	clipboard->Unlock();
}


/**
 * @brief Deletes the currently selected text, recording a ClearUndoBuffer if undo is enabled.
 *
 * Unlike Delete(), Clear() always creates an undo record. Equivalent to
 * recording a ClearUndoBuffer and calling Delete().
 */
void
BTextView::Clear()
{
	// We always check for fUndo != NULL (not only here),
	// because when fUndo is NULL, undo is deactivated.
	if (fUndo) {
		delete fUndo;
		fUndo = new ClearUndoBuffer(this);
	}

	Delete();
}


/**
 * @brief Returns whether the view would accept a paste from the given clipboard.
 *
 * @param clipboard The clipboard to test.
 * @return true if the view is editable and the clipboard contains "text/plain" data.
 */
bool
BTextView::AcceptsPaste(BClipboard* clipboard)
{
	bool result = false;

	if (fEditable && clipboard && clipboard->Lock()) {
		BMessage* data = clipboard->Data();
		result = data && data->HasData("text/plain", B_MIME_TYPE);
		clipboard->Unlock();
	}

	return result;
}


/**
 * @brief Returns whether the view would accept a drag-and-drop of the given message.
 *
 * @param message The drag message to evaluate.
 * @return true if the view is editable and @a message carries "text/plain" data.
 */
bool
BTextView::AcceptsDrop(const BMessage* message)
{
	return fEditable && message
		&& message->HasData("text/plain", B_MIME_TYPE);
}


/**
 * @brief Sets the selection to the range [@a startOffset, @a endOffset).
 *
 * Both offsets are clamped to [0, TextLength()]. Only the changed portions of
 * the highlight are redrawn. If start equals end, the selection is collapsed
 * to a caret at that position.
 *
 * @param startOffset The byte offset of the first selected character.
 * @param endOffset   The byte offset one past the last selected character.
 * @note Has no effect if the view is not selectable.
 */
void
BTextView::Select(int32 startOffset, int32 endOffset)
{
	CALLED();
	if (!fSelectable)
		return;

	_CancelInputMethod();

	// pin offsets at reasonable values
	if (startOffset < 0)
		startOffset = 0;
	else if (startOffset > fText->Length())
		startOffset = fText->Length();
	if (endOffset < 0)
		endOffset = 0;
	else if (endOffset > fText->Length())
		endOffset = fText->Length();

	// a negative selection?
	if (startOffset > endOffset)
		return;

	// is the new selection any different from the current selection?
	if (startOffset == fSelStart && endOffset == fSelEnd)
		return;

	fStyles->InvalidateNullStyle();

	_HideCaret();

	if (startOffset == endOffset) {
		if (fSelStart != fSelEnd) {
			// unhilite the selection
			if (fActive)
				Highlight(fSelStart, fSelEnd);
		}
		fSelStart = fSelEnd = fCaretOffset = startOffset;
		_ShowCaret();
	} else {
		if (fActive) {
			// draw only those ranges that are different
			long start, end;
			if (startOffset != fSelStart) {
				// start of selection has changed
				if (startOffset > fSelStart) {
					start = fSelStart;
					end = startOffset;
				} else {
					start = startOffset;
					end = fSelStart;
				}
				Highlight(start, end);
			}

			if (endOffset != fSelEnd) {
				// end of selection has changed
				if (endOffset > fSelEnd) {
					start = fSelEnd;
					end = endOffset;
				} else {
					start = endOffset;
					end = fSelEnd;
				}
				Highlight(start, end);
			}
		}
		fSelStart = startOffset;
		fSelEnd = endOffset;
	}
}


/**
 * @brief Selects all text in the view.
 *
 * Places the caret at the end of the buffer and selects the entire text range
 * [0, TextLength()].
 */
void
BTextView::SelectAll()
{
	// Place the cursor at the end of the selection.
	fCaretOffset = fText->Length();
	Select(0, fCaretOffset);
}


/**
 * @brief Returns the byte offsets of the current selection endpoints.
 *
 * Both out-parameters are set to 0 if the view is not selectable.
 *
 * @param _start Receives the start offset of the selection; may be NULL.
 * @param _end   Receives the end offset of the selection; may be NULL.
 */
void
BTextView::GetSelection(int32* _start, int32* _end) const
{
	int32 start = 0;
	int32 end = 0;

	if (fSelectable) {
		start = fSelStart;
		end = fSelEnd;
	}

	if (_start)
		*_start = start;

	if (_end)
		*_end = end;
}


/**
 * @brief Applies the system document background and text colours to the view.
 *
 * When the view is not editable, a tint is applied to the background to
 * provide a visual distinction. The low colour is set to match the view colour,
 * and the high colour is set to B_DOCUMENT_TEXT_COLOR.
 */
void
BTextView::AdoptSystemColors()
{
	if (IsEditable())
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	else
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR, _UneditableTint());

	SetLowUIColor(ViewUIColor());
	SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
}


/**
 * @brief Returns whether the view is currently using the system document colours.
 *
 * Checks that the view, low, and high UI colours are exactly the system
 * document background and text colours (with the appropriate editability tint).
 *
 * @return true if all three colours match the expected system values.
 */
bool
BTextView::HasSystemColors() const
{
	float tint = B_NO_TINT;
	float uneditableTint = _UneditableTint();

	return ViewUIColor(&tint) == B_DOCUMENT_BACKGROUND_COLOR
		&& (tint == B_NO_TINT || tint == uneditableTint)
		&& LowUIColor(&tint) == B_DOCUMENT_BACKGROUND_COLOR
		&& (tint == B_NO_TINT || tint == uneditableTint)
		&& HighUIColor(&tint) == B_DOCUMENT_TEXT_COLOR && tint == B_NO_TINT;
}


/**
 * @brief Applies a font and/or colour change to the current selection.
 *
 * Delegates to SetFontAndColor(int32, int32, ...) with the current selection
 * offsets as the range.
 *
 * @param font  The font to apply, or NULL to leave the font unchanged.
 * @param mode  A bitmask of B_FONT_* constants controlling which font attributes to change.
 * @param color The foreground colour to apply, or NULL to leave the colour unchanged.
 */
void
BTextView::SetFontAndColor(const BFont* font, uint32 mode, const rgb_color* color)
{
	SetFontAndColor(fSelStart, fSelEnd, font, mode, color);
}


/**
 * @brief Applies a font and/or colour change to the text in the given byte range.
 *
 * For non-stylable views the range is ignored and the whole buffer is updated.
 * If the change involves font family or size, line breaks are recalculated;
 * otherwise only the affected lines are redrawn.
 *
 * @param startOffset First byte of the range to style.
 * @param endOffset   One past the last byte of the range to style.
 * @param font        The font to apply, or NULL.
 * @param mode        Bitmask of B_FONT_* constants; use B_FONT_ALL to apply everything.
 * @param color       The foreground colour to apply, or NULL.
 */
void
BTextView::SetFontAndColor(int32 startOffset, int32 endOffset,
	const BFont* font, uint32 mode, const rgb_color* color)
{
	CALLED();

	_HideCaret();

	const int32 textLength = fText->Length();

	if (!fStylable) {
		// When the text view is not stylable, we always set the whole text's
		// style and ignore the offsets
		startOffset = 0;
		endOffset = textLength;
	} else {
		// pin offsets at reasonable values
		if (startOffset < 0)
			startOffset = 0;
		else if (startOffset > textLength)
			startOffset = textLength;

		if (endOffset < 0)
			endOffset = 0;
		else if (endOffset > textLength)
			endOffset = textLength;
	}

	// apply the style to the style buffer
	fStyles->InvalidateNullStyle();
	_ApplyStyleRange(startOffset, endOffset, mode, font, color);

	if ((mode & (B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE)) != 0) {
		// ToDo: maybe only invalidate the layout (depending on
		// B_SUPPORTS_LAYOUT) and have it _Refresh() automatically?
		InvalidateLayout();
		// recalc the line breaks and redraw with new style
		_Refresh(startOffset, endOffset);
	} else {
		// the line breaks wont change, simply redraw
		_RequestDrawLines(_LineAt(startOffset), _LineAt(endOffset));
	}

	_ShowCaret();
}


/**
 * @brief Retrieves the font and colour in effect at the given byte offset.
 *
 * @param offset The byte offset to query.
 * @param _font  Receives the font at @a offset; may be NULL.
 * @param _color Receives the text colour at @a offset; may be NULL.
 */
void
BTextView::GetFontAndColor(int32 offset, BFont* _font,
	rgb_color* _color) const
{
	fStyles->GetStyle(offset, _font, _color);
}


/**
 * @brief Retrieves the font, mode mask, and colour for the current selection.
 *
 * Inspects all style runs in the current selection and fills in the common
 * attributes. If the colour varies across the selection, @a _sameColor is set
 * to false.
 *
 * @param _font      Receives the font attributes shared by the whole selection; may be NULL.
 * @param _mode      Receives a bitmask indicating which font attributes are uniform; may be NULL.
 * @param _color     Receives the text colour if uniform across the selection; may be NULL.
 * @param _sameColor Receives true if every run in the selection has the same colour; may be NULL.
 */
void
BTextView::GetFontAndColor(BFont* _font, uint32* _mode,
	rgb_color* _color, bool* _sameColor) const
{
	fStyles->ContinuousGetStyle(_font, _mode, _color, _sameColor,
		fSelStart, fSelEnd);
}


/**
 * @brief Applies a text_run_array to the specified byte range.
 *
 * For non-stylable views, the offsets are ignored and the first run is applied
 * to the whole buffer. For stylable views, each run's font and colour are
 * applied to its sub-range, line breaks are recalculated, and the view is
 * redrawn.
 *
 * @param startOffset First byte of the range to style.
 * @param endOffset   One past the last byte of the range to style.
 * @param runs        The run array describing the styles to apply.
 */
void
BTextView::SetRunArray(int32 startOffset, int32 endOffset,
	const text_run_array* runs)
{
	CALLED();

	_CancelInputMethod();

	text_run_array oneRun;

	if (!fStylable) {
		// when the text view is not stylable, we always set the whole text's
		// style with the first run and ignore the offsets
		if (runs->count == 0)
			return;

		startOffset = 0;
		endOffset = fText->Length();
		oneRun.count = 1;
		oneRun.runs[0] = runs->runs[0];
		oneRun.runs[0].offset = 0;
		runs = &oneRun;
	} else {
		// pin offsets at reasonable values
		if (startOffset < 0)
			startOffset = 0;
		else if (startOffset > fText->Length())
			startOffset = fText->Length();

		if (endOffset < 0)
			endOffset = 0;
		else if (endOffset > fText->Length())
			endOffset = fText->Length();
	}

	_SetRunArray(startOffset, endOffset, runs);

	_Refresh(startOffset, endOffset);
}


/**
 * @brief Returns a newly allocated text_run_array covering the given byte range.
 *
 * The returned array must be freed with FreeRunArray(). Both offsets are
 * clamped to [0, TextLength()].
 *
 * @param startOffset First byte of the range to query.
 * @param endOffset   One past the last byte of the range to query.
 * @param _size       If non-NULL, receives the byte size of the returned array.
 * @return A heap-allocated text_run_array, or NULL on allocation failure.
 */
text_run_array*
BTextView::RunArray(int32 startOffset, int32 endOffset, int32* _size) const
{
	// pin offsets at reasonable values
	if (startOffset < 0)
		startOffset = 0;
	else if (startOffset > fText->Length())
		startOffset = fText->Length();

	if (endOffset < 0)
		endOffset = 0;
	else if (endOffset > fText->Length())
		endOffset = fText->Length();

	STEStyleRange* styleRange
		= fStyles->GetStyleRange(startOffset, endOffset - 1);
	if (styleRange == NULL)
		return NULL;

	text_run_array* runArray = AllocRunArray(styleRange->count, _size);
	if (runArray != NULL) {
		for (int32 i = 0; i < runArray->count; i++) {
			runArray->runs[i].offset = styleRange->runs[i].offset;
			runArray->runs[i].font = styleRange->runs[i].style.font;
			runArray->runs[i].color = styleRange->runs[i].style.color;
		}
	}

	free(styleRange);

	return runArray;
}


/**
 * @brief Returns the line number of the character at the given byte offset.
 *
 * @param offset The byte offset of the character to query; clamped to [0, TextLength()].
 * @return The zero-based line number, accounting for the empty last line after a trailing newline.
 */
int32
BTextView::LineAt(int32 offset) const
{
	// pin offset at reasonable values
	if (offset < 0)
		offset = 0;
	else if (offset > fText->Length())
		offset = fText->Length();

	int32 lineNum = _LineAt(offset);
	if (_IsOnEmptyLastLine(offset))
		lineNum++;
	return lineNum;
}


/**
 * @brief Returns the line number that corresponds to the given point in view coordinates.
 *
 * @param point A point in the view's coordinate system.
 * @return The zero-based line number under @a point.
 */
int32
BTextView::LineAt(BPoint point) const
{
	int32 lineNum = _LineAt(point);
	if ((*fLines)[lineNum + 1]->origin <= point.y - fTextRect.top)
		lineNum++;

	return lineNum;
}


/**
 * @brief Returns the view-coordinate point corresponding to the given byte offset.
 *
 * The returned point is the top-left corner of the character at @a offset.
 * For the empty last line (after a trailing newline), the point is on the next
 * logical line.
 *
 * @param offset  The byte offset to convert; clamped to [0, TextLength()].
 * @param _height If non-NULL, receives the pixel height of the line at @a offset.
 * @return The position of the character at @a offset in view coordinates.
 */
BPoint
BTextView::PointAt(int32 offset, float* _height) const
{
	// pin offset at reasonable values
	if (offset < 0)
		offset = 0;
	else if (offset > fText->Length())
		offset = fText->Length();

	// ToDo: Cleanup.
	int32 lineNum = _LineAt(offset);
	STELine* line = (*fLines)[lineNum];
	float height = 0;

	BPoint result;
	result.x = 0.0;
	result.y = line->origin + fTextRect.top;

	bool onEmptyLastLine = _IsOnEmptyLastLine(offset);

	if (fStyles->NumRuns() == 0) {
		// Handle the case where there is only one line (no text inserted)
		fStyles->SyncNullStyle(0);
		height = _NullStyleHeight();
	} else {
		height = (line + 1)->origin - line->origin;

		if (onEmptyLastLine) {
			// special case: go down one line if offset is at the newline
			// at the end of the buffer ...
			result.y += height;
			// ... and return the height of that (empty) line
			fStyles->SyncNullStyle(offset);
			height = _NullStyleHeight();
		} else {
			int32 length = offset - line->offset;
			result.x += _TabExpandedStyledWidth(line->offset, length);
		}
	}

	if (fAlignment != B_ALIGN_LEFT) {
		float lineWidth = onEmptyLastLine ? 0.0 : LineWidth(lineNum);
		float alignmentOffset = fTextRect.Width() + 1 - lineWidth;
		if (fAlignment == B_ALIGN_CENTER)
			alignmentOffset = floorf(alignmentOffset / 2);
		result.x += alignmentOffset;
	}

	// convert from text rect coordinates
	result.x += fTextRect.left;

	// round up
	result.x = lroundf(result.x);
	result.y = lroundf(result.y);
	if (_height != NULL)
		*_height = height;

	return result;
}


/**
 * @brief Returns the byte offset of the character closest to the given view-coordinate point.
 *
 * Performs a left-to-right scan of the line under @a point, expanding tabs,
 * and returns the offset of the character whose centre is nearest to the
 * horizontal position.
 *
 * @param point A point in the view's coordinate system.
 * @return The byte offset of the character nearest to @a point.
 */
int32
BTextView::OffsetAt(BPoint point) const
{
	const int32 textLength = fText->Length();

	// should we even bother?
	if (point.y >= fTextRect.bottom)
		return textLength;
	else if (point.y < fTextRect.top)
		return 0;

	int32 lineNum = _LineAt(point);
	STELine* line = (*fLines)[lineNum];

#define COMPILE_PROBABLY_BAD_CODE 1

#if COMPILE_PROBABLY_BAD_CODE
	// special case: if point is within the text rect and PixelToLine()
	// tells us that it's on the last line, but if point is actually
	// lower than the bottom of the last line, return the last offset
	// (can happen for newlines)
	if (lineNum == (fLines->NumLines() - 1)) {
		if (point.y >= ((line + 1)->origin + fTextRect.top))
			return textLength;
	}
#endif

	// convert to text rect coordinates
	if (fAlignment != B_ALIGN_LEFT) {
		float alignmentOffset = fTextRect.Width() + 1 - LineWidth(lineNum);
		if (fAlignment == B_ALIGN_CENTER)
			alignmentOffset = floorf(alignmentOffset / 2);
		point.x -= alignmentOffset;
	}

	point.x -= fTextRect.left;
	point.x = std::max(point.x, 0.0f);

	// ToDo: The following code isn't very efficient, because it always starts
	// from the left end, so when the point is near the right end it's very
	// slow.
	int32 offset = line->offset;
	const int32 limit = (line + 1)->offset;
	float location = 0;
	do {
		const int32 nextInitial = _NextInitialByte(offset);
		const int32 saveOffset = offset;
		float width = 0;
		if (ByteAt(offset) == B_TAB)
			width = _ActualTabWidth(location);
		else
			width = _StyledWidth(saveOffset, nextInitial - saveOffset);
		if (location + width > point.x) {
			if (fabs(location + width - point.x) < fabs(location - point.x))
				offset = nextInitial;
			break;
		}

		location += width;
		offset = nextInitial;
	} while (offset < limit);

	if (offset == (line + 1)->offset) {
		// special case: newlines aren't visible
		// return the offset of the character preceding the newline
		if (ByteAt(offset - 1) == B_ENTER)
			return --offset;

		// special case: return the offset preceding any spaces that
		// aren't at the end of the buffer
		if (offset != textLength && ByteAt(offset - 1) == B_SPACE)
			return --offset;
	}

	return offset;
}


/**
 * @brief Returns the byte offset of the first character on the given line.
 *
 * @param line The zero-based line number to query.
 * @return 0 if @a line is before the first line, TextLength() if @a line is
 *         beyond the last line, or the start offset of @a line otherwise.
 */
int32
BTextView::OffsetAt(int32 line) const
{
	if (line < 0)
		return 0;

	if (line > fLines->NumLines())
		return fText->Length();

	return (*fLines)[line]->offset;
}


/**
 * @brief Finds the word boundary around the character at the given offset.
 *
 * Sets @a _fromOffset to the start of the word and @a _toOffset to its end.
 * Both parameters may be NULL if the caller only needs one of the values.
 *
 * @param offset      The byte offset of any character within the target word.
 * @param _fromOffset Receives the start offset of the word; may be NULL.
 * @param _toOffset   Receives the end offset of the word; may be NULL.
 */
void
BTextView::FindWord(int32 offset, int32* _fromOffset, int32* _toOffset)
{
	if (offset < 0) {
		if (_fromOffset)
			*_fromOffset = 0;

		if (_toOffset)
			*_toOffset = 0;

		return;
	}

	if (offset > fText->Length()) {
		if (_fromOffset)
			*_fromOffset = fText->Length();

		if (_toOffset)
			*_toOffset = fText->Length();

		return;
	}

	if (_fromOffset)
		*_fromOffset = _PreviousWordBoundary(offset);

	if (_toOffset)
		*_toOffset = _NextWordBoundary(offset);
}


/**
 * @brief Returns whether a line break is permitted immediately after the character at @a offset.
 *
 * Uses character classification rules (whitespace, punctuation, quotes,
 * parentheses) to determine appropriate word-wrap break points.
 *
 * @param offset The byte offset of the candidate break character.
 * @return true if the line may wrap after the character at @a offset.
 */
bool
BTextView::CanEndLine(int32 offset)
{
	if (offset < 0 || offset > fText->Length())
		return false;

	// TODO: This should be improved using the LocaleKit.
	uint32 classification = _CharClassification(offset);

	// wrapping is always allowed at end of text and at newlines
	if (classification == CHAR_CLASS_END_OF_TEXT || ByteAt(offset) == B_ENTER)
		return true;

	uint32 nextClassification = _CharClassification(offset + 1);
	if (nextClassification == CHAR_CLASS_END_OF_TEXT)
		return true;

	// never separate a punctuation char from its preceeding word
	if (classification == CHAR_CLASS_DEFAULT
		&& nextClassification == CHAR_CLASS_PUNCTUATION) {
		return false;
	}

	if ((classification == CHAR_CLASS_WHITESPACE
			&& nextClassification != CHAR_CLASS_WHITESPACE)
		|| (classification != CHAR_CLASS_WHITESPACE
			&& nextClassification == CHAR_CLASS_WHITESPACE)) {
		return true;
	}

	// allow wrapping after whitespace, unless more whitespace (except for
	// newline) follows
	if (classification == CHAR_CLASS_WHITESPACE
		&& (nextClassification != CHAR_CLASS_WHITESPACE
			|| ByteAt(offset + 1) == B_ENTER)) {
		return true;
	}

	// allow wrapping after punctuation chars, unless more punctuation, closing
	// parens or quotes follow
	if (classification == CHAR_CLASS_PUNCTUATION
		&& nextClassification != CHAR_CLASS_PUNCTUATION
		&& nextClassification != CHAR_CLASS_PARENS_CLOSE
		&& nextClassification != CHAR_CLASS_QUOTE) {
		return true;
	}

	// allow wrapping after quotes, graphical chars and closing parens only if
	// whitespace follows (not perfect, but seems to do the right thing most
	// of the time)
	if ((classification == CHAR_CLASS_QUOTE
			|| classification == CHAR_CLASS_GRAPHICAL
			|| classification == CHAR_CLASS_PARENS_CLOSE)
		&& nextClassification == CHAR_CLASS_WHITESPACE) {
		return true;
	}

	return false;
}


/**
 * @brief Returns the pixel width of the rendered text on the given line.
 *
 * Trailing newline characters are excluded from the measurement.
 *
 * @param lineNumber The zero-based line index to measure.
 * @return The width in pixels, or 0.0 if @a lineNumber is out of range.
 */
float
BTextView::LineWidth(int32 lineNumber) const
{
	if (lineNumber < 0 || lineNumber >= fLines->NumLines())
		return 0;

	STELine* line = (*fLines)[lineNumber];
	int32 length = (line + 1)->offset - line->offset;

	// skip newline at the end of the line, if any, as it does no contribute
	// to the width
	if (ByteAt((line + 1)->offset - 1) == B_ENTER)
		length--;

	return _TabExpandedStyledWidth(line->offset, length);
}


/**
 * @brief Returns the pixel height of a single line of text.
 *
 * Falls back to the initial style's font metrics if no text has been inserted yet.
 *
 * @param lineNumber The zero-based line index to measure.
 * @return The height in pixels of the specified line.
 */
float
BTextView::LineHeight(int32 lineNumber) const
{
	float lineHeight = TextHeight(lineNumber, lineNumber);
	if (lineHeight == 0.0) {
		// We probably don't have text content yet. Take the initial
		// style's font height or fall back to the plain font.
		const BFont* font;
		fStyles->GetNullStyle(&font, NULL);
		if (font == NULL)
			font = be_plain_font;

		font_height fontHeight;
		font->GetHeight(&fontHeight);
		// This is how the height is calculated in _RecalculateLineBreaks().
		lineHeight = ceilf(fontHeight.ascent + fontHeight.descent) + 1;
	}

	return lineHeight;
}


/**
 * @brief Returns the total pixel height spanning from @a startLine to @a endLine (inclusive).
 *
 * Accounts for the extra empty line that appears after a trailing newline at
 * the very end of the buffer.
 *
 * @param startLine Zero-based index of the first line to measure.
 * @param endLine   Zero-based index of the last line to measure.
 * @return The combined height in pixels of the specified line range.
 */
float
BTextView::TextHeight(int32 startLine, int32 endLine) const
{
	const int32 numLines = fLines->NumLines();
	if (startLine < 0)
		startLine = 0;
	else if (startLine > numLines - 1)
		startLine = numLines - 1;

	if (endLine < 0)
		endLine = 0;
	else if (endLine > numLines - 1)
		endLine = numLines - 1;

	float height = (*fLines)[endLine + 1]->origin
		- (*fLines)[startLine]->origin;

	if (startLine != endLine && endLine == numLines - 1
		&& fText->RealCharAt(fText->Length() - 1) == B_ENTER) {
		height += (*fLines)[endLine + 1]->origin - (*fLines)[endLine]->origin;
	}

	return ceilf(height);
}


/**
 * @brief Computes the visual BRegion covered by the text between two byte offsets.
 *
 * The resulting region may consist of multiple rectangles when the range
 * spans more than one line. The first line extends from the start point to
 * the right edge of the view; the last line extends from the left edge to
 * the end point; intermediate lines span the full view width.
 *
 * @param startOffset First byte of the range to compute; clamped to [0, TextLength()].
 * @param endOffset   One past the last byte of the range; clamped to [0, TextLength()].
 * @param outRegion   The BRegion to fill with the selection geometry; must not be NULL.
 */
void
BTextView::GetTextRegion(int32 startOffset, int32 endOffset,
	BRegion* outRegion) const
{
	if (!outRegion)
		return;

	outRegion->MakeEmpty();

	// pin offsets at reasonable values
	if (startOffset < 0)
		startOffset = 0;
	else if (startOffset > fText->Length())
		startOffset = fText->Length();
	if (endOffset < 0)
		endOffset = 0;
	else if (endOffset > fText->Length())
		endOffset = fText->Length();

	// return an empty region if the range is invalid
	if (startOffset >= endOffset)
		return;

	float startLineHeight = 0.0;
	float endLineHeight = 0.0;
	BPoint startPt = PointAt(startOffset, &startLineHeight);
	BPoint endPt = PointAt(endOffset, &endLineHeight);

	startLineHeight = ceilf(startLineHeight);
	endLineHeight = ceilf(endLineHeight);

	BRect selRect;
	const BRect bounds(Bounds());

	if (startPt.y == endPt.y) {
		// this is a one-line region
		selRect.left = startPt.x;
		selRect.top = startPt.y;
		selRect.right = endPt.x - 1;
		selRect.bottom = endPt.y + endLineHeight - 1;
		outRegion->Include(selRect);
	} else {
		// more than one line in the specified offset range

		// include first line from start of selection to end of window
		selRect.left = startPt.x;
		selRect.top = startPt.y;
		selRect.right = std::max(fTextRect.right,
			bounds.right - fLayoutData->rightInset);
		selRect.bottom = startPt.y + startLineHeight - 1;
		outRegion->Include(selRect);

		if (startPt.y + startLineHeight < endPt.y) {
			// more than two lines in the range
			// include middle lines from start to end of window
			selRect.left = std::min(fTextRect.left,
				bounds.left + fLayoutData->leftInset);
			selRect.top = startPt.y + startLineHeight;
			selRect.right = std::max(fTextRect.right,
				bounds.right - fLayoutData->rightInset);
			selRect.bottom = endPt.y - 1;
			outRegion->Include(selRect);
		}

		// include last line start of window to end of selection
		selRect.left = std::min(fTextRect.left,
			bounds.left + fLayoutData->leftInset);
		selRect.top = endPt.y;
		selRect.right = endPt.x - 1;
		selRect.bottom = endPt.y + endLineHeight - 1;
		outRegion->Include(selRect);
	}
}


/**
 * @brief Scrolls the view so that the character at @a offset is visible.
 *
 * Computes the required horizontal and vertical scroll deltas and calls
 * ScrollBy(), clamping to avoid scrolling out of the text rect bounds.
 * Also repositions the text rect and updates scroll bars when not word-wrapping.
 *
 * @param offset The byte offset of the character to scroll into view.
 */
void
BTextView::ScrollToOffset(int32 offset)
{
	BRect bounds = Bounds();
	float lineHeight = 0.0;
	BPoint point = PointAt(offset, &lineHeight);
	BPoint scrollBy(B_ORIGIN);

	// horizontal
	if (point.x < bounds.left)
		scrollBy.x = point.x - bounds.right;
	else if (point.x > bounds.right)
		scrollBy.x = point.x - bounds.left;

	// prevent from scrolling out of view
	if (scrollBy.x != 0.0) {
		float rightMax = fTextRect.right + fLayoutData->rightInset;
		if (bounds.right + scrollBy.x > rightMax)
			scrollBy.x = rightMax - bounds.right;
		float leftMin = fTextRect.left - fLayoutData->leftInset;
		if (bounds.left + scrollBy.x < leftMin)
			scrollBy.x = leftMin - bounds.left;
	}

	// vertical
	if (CountLines() > 1) {
		// scroll in Y only if multiple lines!
		if (point.y < bounds.top - fLayoutData->topInset)
			scrollBy.y = point.y - bounds.top - fLayoutData->topInset;
		else if (point.y + lineHeight > bounds.bottom
				+ fLayoutData->bottomInset) {
			scrollBy.y = point.y + lineHeight - bounds.bottom
				+ fLayoutData->bottomInset;
		}
	}

	ScrollBy(scrollBy.x, scrollBy.y);

	// Update text rect position and scroll bars
	if (CountLines() > 1 && !fWrap)
		FrameResized(Bounds().Width(), Bounds().Height());
}


/**
 * @brief Scrolls the view so that the start of the current selection is visible.
 *
 * Equivalent to ScrollToOffset(fSelStart).
 */
void
BTextView::ScrollToSelection()
{
	ScrollToOffset(fSelStart);
}


/**
 * @brief Toggles the selection highlight for the given byte range using XOR painting.
 *
 * Uses B_OP_INVERT to invert the pixels in the region, making successive
 * calls toggle the highlight on and off. Both offsets are clamped to
 * [0, TextLength()].
 *
 * @param startOffset First byte of the range to highlight.
 * @param endOffset   One past the last byte of the range to highlight.
 */
void
BTextView::Highlight(int32 startOffset, int32 endOffset)
{
	// pin offsets at reasonable values
	if (startOffset < 0)
		startOffset = 0;
	else if (startOffset > fText->Length())
		startOffset = fText->Length();
	if (endOffset < 0)
		endOffset = 0;
	else if (endOffset > fText->Length())
		endOffset = fText->Length();

	if (startOffset >= endOffset)
		return;

	BRegion selRegion;
	GetTextRegion(startOffset, endOffset, &selRegion);

	SetDrawingMode(B_OP_INVERT);
	FillRegion(&selRegion, B_SOLID_HIGH);
	SetDrawingMode(B_OP_COPY);
}


// #pragma mark - Configuration methods


/**
 * @brief Sets the rectangle within which text is rendered and reflowed.
 *
 * In non-wrap mode, the rect's right and bottom edges are pinned to the view
 * bounds. Triggers a full line-break recalculation via _ResetTextRect().
 *
 * @param rect The new text rect in view coordinates.
 */
void
BTextView::SetTextRect(BRect rect)
{
	if (rect == fTextRect)
		return;

	if (!fWrap) {
		rect.right = Bounds().right;
		rect.bottom = Bounds().bottom;
	}

	_UpdateInsets(rect);

	fTextRect = rect;

	_ResetTextRect();
}


/**
 * @brief Returns the current text rect in view coordinates.
 *
 * @return The BRect within which text is currently rendered.
 */
BRect
BTextView::TextRect() const
{
	return fTextRect;
}


/**
 * @brief Resets the text rect to the view bounds minus current insets and recalculates line breaks.
 *
 * Called internally whenever the view is resized, word-wrap is toggled, or
 * the text rect is explicitly changed. Invalidates any area that differs
 * between the old and new text rects so it gets repainted.
 */
void
BTextView::_ResetTextRect()
{
	BRect oldTextRect(fTextRect);
	// reset text rect to bounds minus insets ...
	fTextRect = Bounds().OffsetToCopy(B_ORIGIN);
	fTextRect.left += fLayoutData->leftInset;
	fTextRect.top += fLayoutData->topInset;
	fTextRect.right -= fLayoutData->rightInset;
	fTextRect.bottom -= fLayoutData->bottomInset;

	// and rewrap (potentially adjusting the right and the bottom of the text
	// rect)
	_Refresh(0, fText->Length());

	// Make sure that the dirty area outside the text is redrawn too.
	BRegion invalid(oldTextRect | fTextRect);
	invalid.Exclude(fTextRect);
	Invalidate(&invalid);
}


/**
 * @brief Explicitly sets the inset distances between the view bounds and the text rect.
 *
 * Once this is called, automatic inset calculation is suppressed (the
 * @c overridden flag is set). Triggers a layout invalidation and a full redraw.
 *
 * @param left   Pixels of padding on the left side.
 * @param top    Pixels of padding on the top.
 * @param right  Pixels of padding on the right side.
 * @param bottom Pixels of padding on the bottom.
 */
void
BTextView::SetInsets(float left, float top, float right, float bottom)
{
	if (fLayoutData->leftInset == left
		&& fLayoutData->topInset == top
		&& fLayoutData->rightInset == right
		&& fLayoutData->bottomInset == bottom)
		return;

	fLayoutData->leftInset = left;
	fLayoutData->topInset = top;
	fLayoutData->rightInset = right;
	fLayoutData->bottomInset = bottom;

	fLayoutData->overridden = true;

	InvalidateLayout();
	Invalidate();
}


/**
 * @brief Returns the current inset distances between the view bounds and the text rect.
 *
 * @param _left   Receives the left inset in pixels; may be NULL.
 * @param _top    Receives the top inset in pixels; may be NULL.
 * @param _right  Receives the right inset in pixels; may be NULL.
 * @param _bottom Receives the bottom inset in pixels; may be NULL.
 */
void
BTextView::GetInsets(float* _left, float* _top, float* _right,
	float* _bottom) const
{
	if (_left)
		*_left = fLayoutData->leftInset;
	if (_top)
		*_top = fLayoutData->topInset;
	if (_right)
		*_right = fLayoutData->rightInset;
	if (_bottom)
		*_bottom = fLayoutData->bottomInset;
}


/**
 * @brief Controls whether the view supports multiple font and colour runs.
 *
 * When @a stylable is false, all text uses a single uniform style.
 *
 * @param stylable true to enable per-character styling, false to disable it.
 */
void
BTextView::SetStylable(bool stylable)
{
	fStylable = stylable;
}


/**
 * @brief Returns whether the view supports per-character font and colour styling.
 *
 * @return true if multiple style runs are enabled.
 */
bool
BTextView::IsStylable() const
{
	return fStylable;
}


/**
 * @brief Sets the width (in pixels) of a tab stop.
 *
 * If the view is attached to a window, triggers a full line-break
 * recalculation and redraw.
 *
 * @param width The new tab width in pixels.
 */
void
BTextView::SetTabWidth(float width)
{
	if (width == fTabWidth)
		return;

	fTabWidth = width;

	if (Window() != NULL)
		_Refresh(0, fText->Length());
}


/**
 * @brief Returns the current tab-stop width in pixels.
 *
 * @return The tab width as set by SetTabWidth().
 */
float
BTextView::TabWidth() const
{
	return fTabWidth;
}


/**
 * @brief Controls whether the user can select text in the view.
 *
 * If selectability changes while there is an active selection and the view is
 * currently active, the highlight is toggled immediately.
 *
 * @param selectable true to allow selection, false to disable it.
 */
void
BTextView::MakeSelectable(bool selectable)
{
	if (selectable == fSelectable)
		return;

	fSelectable = selectable;

	if (fActive && fSelStart != fSelEnd && Window() != NULL)
		Highlight(fSelStart, fSelEnd);
}


/**
 * @brief Returns whether the user can select text in the view.
 *
 * @return true if text selection is enabled.
 */
bool
BTextView::IsSelectable() const
{
	return fSelectable;
}


/**
 * @brief Controls whether the user can modify the text content.
 *
 * Applies or removes the uneditable colour tint, invalidates the null style
 * when re-enabling editing, and hides the caret or cancels any IME session
 * when disabling editing.
 *
 * @param editable true to allow editing, false to make the view read-only.
 */
void
BTextView::MakeEditable(bool editable)
{
	if (editable == fEditable)
		return;

	fEditable = editable;

	// apply uneditable colors or unapply them
	if (HasSystemColors())
		AdoptSystemColors();

	// TextControls change the color of the text when
	// they are made editable, so we need to invalidate
	// the NULL style here
	// TODO: it works well, but it could be caused by a bug somewhere else
	if (fEditable)
		fStyles->InvalidateNullStyle();
	if (Window() != NULL && fActive) {
		if (!fEditable) {
			if (!fSelectable)
				_HideCaret();
			_CancelInputMethod();
		}
	}
}


/**
 * @brief Returns whether the view currently allows text editing.
 *
 * @return true if the view is in editable mode.
 */
bool
BTextView::IsEditable() const
{
	return fEditable;
}


/**
 * @brief Enables or disables automatic word-wrapping at the text rect boundary.
 *
 * Toggles the wrap mode, recalculates line breaks accordingly, scrolls the
 * caret into view, and updates the scroll bars if the bounds change.
 *
 * @param wrap true to enable word wrap, false to disable it.
 */
void
BTextView::SetWordWrap(bool wrap)
{
	if (wrap == fWrap)
		return;

	bool updateOnScreen = fActive && Window() != NULL;
	if (updateOnScreen) {
		// hide the caret, unhilite the selection
		if (fSelStart != fSelEnd) {
			if (fSelectable)
				Highlight(fSelStart, fSelEnd);
		} else
			_HideCaret();
	}

	BRect savedBounds = Bounds();

	fWrap = wrap;
	if (wrap)
		_ResetTextRect(); // calls _Refresh
	else
		_Refresh(0, fText->Length());

	if (fEditable || fSelectable)
		ScrollToOffset(fCaretOffset);

	// redraw text rect and update scroll bars if bounds have changed
	if (Bounds() != savedBounds)
		FrameResized(Bounds().Width(), Bounds().Height());

	if (updateOnScreen) {
		// show the caret, hilite the selection
		if (fSelStart != fSelEnd) {
			if (fSelectable)
				Highlight(fSelStart, fSelEnd);
		} else
			_ShowCaret();
	}
}


/**
 * @brief Returns whether word wrapping is currently enabled.
 *
 * @return true if word wrap is on.
 */
bool
BTextView::DoesWordWrap() const
{
	return fWrap;
}


/**
 * @brief Sets the maximum number of bytes that the view will accept.
 *
 * If the current text already exceeds @a max bytes, the excess is deleted,
 * respecting multi-byte character boundaries.
 *
 * @param max The maximum byte count; use INT32_MAX for no limit.
 */
void
BTextView::SetMaxBytes(int32 max)
{
	const int32 textLength = fText->Length();
	fMaxBytes = max;

	if (fMaxBytes < textLength) {
		int32 offset = fMaxBytes;
		// Delete the text after fMaxBytes, but
		// respect multibyte characters boundaries.
		const int32 previousInitial = _PreviousInitialByte(offset);
		if (_NextInitialByte(previousInitial) != offset)
			offset = previousInitial;

		Delete(offset, textLength);
	}
}


/**
 * @brief Returns the maximum number of bytes the view will accept.
 *
 * @return The current byte limit as set by SetMaxBytes().
 */
int32
BTextView::MaxBytes() const
{
	return fMaxBytes;
}


/**
 * @brief Prevents the given character from being typed or pasted into the view.
 *
 * The character is added to the internal disallowed-characters list if not
 * already present. Attempting to insert a disallowed character causes a beep.
 *
 * @param character The character code to disallow.
 */
void
BTextView::DisallowChar(uint32 character)
{
	if (fDisallowedChars == NULL)
		fDisallowedChars = new BList;
	if (!fDisallowedChars->HasItem(reinterpret_cast<void*>(character)))
		fDisallowedChars->AddItem(reinterpret_cast<void*>(character));
}


/**
 * @brief Removes a character from the disallowed-characters list.
 *
 * If @a character was not previously disallowed, this call has no effect.
 *
 * @param character The character code to re-allow.
 */
void
BTextView::AllowChar(uint32 character)
{
	if (fDisallowedChars != NULL)
		fDisallowedChars->RemoveItem(reinterpret_cast<void*>(character));
}


/**
 * @brief Sets the horizontal text alignment within the text rect.
 *
 * Accepts B_ALIGN_LEFT, B_ALIGN_CENTER, or B_ALIGN_RIGHT. Triggers a
 * text-rect repositioning and a full redraw if the value changes.
 *
 * @param align The new alignment value.
 */
void
BTextView::SetAlignment(alignment align)
{
	// Do a reality check
	if (fAlignment != align &&
			(align == B_ALIGN_LEFT ||
			 align == B_ALIGN_RIGHT ||
			 align == B_ALIGN_CENTER)) {
		fAlignment = align;

		// After setting new alignment, update the view/window
		if (Window() != NULL) {
			FrameResized(Bounds().Width(), Bounds().Height());
				// text rect position and scroll bars may change
			Invalidate();
		}
	}
}


/**
 * @brief Returns the current horizontal text alignment.
 *
 * @return One of B_ALIGN_LEFT, B_ALIGN_CENTER, or B_ALIGN_RIGHT.
 */
alignment
BTextView::Alignment() const
{
	return fAlignment;
}


/**
 * @brief Controls whether pressing Enter automatically indents the new line.
 *
 * When enabled, the indentation of the current line (leading spaces and tabs)
 * is reproduced on the next line when Enter is pressed.
 *
 * @param state true to enable auto-indent, false to disable it.
 */
void
BTextView::SetAutoindent(bool state)
{
	fAutoindent = state;
}


/**
 * @brief Returns whether auto-indent is enabled.
 *
 * @return true if new lines are automatically indented to match the previous line.
 */
bool
BTextView::DoesAutoindent() const
{
	return fAutoindent;
}


/**
 * @brief Sets the colour space for the offscreen drawing bitmap.
 *
 * If the colour space changes and an offscreen bitmap exists, the old one is
 * destroyed and a new one is created with the new colour space.
 *
 * @param colors The new colour space (e.g. B_RGB32, B_CMAP8).
 */
void
BTextView::SetColorSpace(color_space colors)
{
	if (colors != fColorSpace && fOffscreen) {
		fColorSpace = colors;
		_DeleteOffscreen();
		_NewOffscreen();
	}
}


/**
 * @brief Returns the colour space used for the offscreen drawing bitmap.
 *
 * @return The colour_space value as set by SetColorSpace().
 */
color_space
BTextView::ColorSpace() const
{
	return fColorSpace;
}


/**
 * @brief Enables or disables the auto-resize mode used by Tracker rename fields.
 *
 * When @a resize is true, the view resizes its container (@a resizeView) to
 * fit the text width automatically. Word-wrap is always disabled in this mode.
 * When @a resize is false, a new offscreen bitmap is created if needed.
 *
 * @param resize     true to enable auto-resizing, false to disable it.
 * @param resizeView The parent container view to resize; may be NULL.
 * @note This mode is primarily used by Tracker for inline file renaming.
 */
void
BTextView::MakeResizable(bool resize, BView* resizeView)
{
	if (resize) {
		fResizable = true;
		fContainerView = resizeView;

		// Wrapping mode and resizable mode can't live together
		if (fWrap) {
			fWrap = false;

			if (fActive && Window() != NULL) {
				if (fSelStart != fSelEnd) {
					if (fSelectable)
						Highlight(fSelStart, fSelEnd);
				} else
					_HideCaret();
			}
		}
		// We need to reset the right inset, as otherwise the auto-resize would
		// get confused about just how wide the textview needs to be.
		// This seems to be an artifact of how Tracker creates the textview
		// during a rename action.
		fLayoutData->rightInset = fLayoutData->leftInset;
	} else {
		fResizable = false;
		fContainerView = NULL;
		if (fOffscreen)
			_DeleteOffscreen();
		_NewOffscreen();
	}

	_Refresh(0, fText->Length());
}


/**
 * @brief Returns whether the view is in auto-resize mode.
 *
 * @return true if MakeResizable(true, ...) has been called.
 */
bool
BTextView::IsResizable() const
{
	return fResizable;
}


/**
 * @brief Enables or disables the undo/redo history for this view.
 *
 * When @a undo is true and no undo buffer exists, a new UndoBuffer is created.
 * When @a undo is false, any existing undo buffer is deleted.
 *
 * @param undo true to enable undo, false to disable it.
 */
void
BTextView::SetDoesUndo(bool undo)
{
	if (undo && fUndo == NULL)
		fUndo = new UndoBuffer(this, B_UNDO_UNAVAILABLE);
	else if (!undo && fUndo != NULL) {
		delete fUndo;
		fUndo = NULL;
	}
}


/**
 * @brief Returns whether the undo/redo history is active.
 *
 * @return true if undo is enabled (an UndoBuffer exists).
 */
bool
BTextView::DoesUndo() const
{
	return fUndo != NULL;
}


/**
 * @brief Enables or disables password-masking (typing-hidden) mode.
 *
 * When @a enabled is true, all existing text is deleted and new characters
 * are masked with a placeholder glyph in the underlying TextGapBuffer.
 *
 * @param enabled true to hide typed characters (password mode), false to show them.
 */
void
BTextView::HideTyping(bool enabled)
{
	if (enabled)
		Delete(0, fText->Length());

	fText->SetPasswordMode(enabled);
}


/**
 * @brief Returns whether password-masking mode is active.
 *
 * @return true if typed characters are masked (password mode is on).
 */
bool
BTextView::IsTypingHidden() const
{
	return fText->PasswordMode();
}


// #pragma mark - Size methods


/**
 * @brief Resizes the view to its preferred size.
 *
 * Delegates to BView::ResizeToPreferred(), which queries PreferredSize().
 */
void
BTextView::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


/**
 * @brief Returns the view's preferred width and height.
 *
 * The preferred size is at least the minimum computed by _ValidateLayoutData().
 * When the view supports layout (B_SUPPORTS_LAYOUT flag), the layout-computed
 * minimum is always returned regardless of the current bounds.
 *
 * @param _width  Receives the preferred width; may be NULL.
 * @param _height Receives the preferred height; may be NULL.
 */
void
BTextView::GetPreferredSize(float* _width, float* _height)
{
	CALLED();

	_ValidateLayoutData();

	if (_width) {
		float width = Bounds().Width();
		if (width < fLayoutData->min.width
			|| (Flags() & B_SUPPORTS_LAYOUT) != 0) {
			width = fLayoutData->min.width;
		}
		*_width = width;
	}

	if (_height) {
		float height = Bounds().Height();
		if (height < fLayoutData->min.height
			|| (Flags() & B_SUPPORTS_LAYOUT) != 0) {
			height = fLayoutData->min.height;
		}
		*_height = height;
	}
}


/**
 * @brief Returns the view's minimum size for layout purposes.
 *
 * Computes a minimum wide enough for roughly three line-heights and tall
 * enough for a single line, plus insets. The explicit min size (if set) is
 * composed with this value via BLayoutUtils::ComposeSize().
 *
 * @return The minimum BSize.
 */
BSize
BTextView::MinSize()
{
	CALLED();

	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), fLayoutData->min);
}


/**
 * @brief Returns the view's maximum size for layout purposes.
 *
 * BTextView can expand to fill any available space, so this returns
 * B_SIZE_UNLIMITED in both dimensions (composed with any explicit max size).
 *
 * @return The maximum BSize.
 */
BSize
BTextView::MaxSize()
{
	CALLED();

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
}


/**
 * @brief Returns the view's preferred size for layout purposes.
 *
 * In wrap mode this is the minimum width plus extra space; in non-wrap mode
 * it is the width of the widest line plus insets. Composed with any explicit
 * preferred size via BLayoutUtils::ComposeSize().
 *
 * @return The preferred BSize.
 */
BSize
BTextView::PreferredSize()
{
	CALLED();

	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		fLayoutData->preferred);
}


/**
 * @brief Returns whether the view reports a height that depends on its width.
 *
 * For non-editable views where all text should be visible, this returns true.
 * For editable views, the base class implementation is used.
 *
 * @return true if the view has a height-for-width dependency.
 */
bool
BTextView::HasHeightForWidth()
{
	if (IsEditable())
		return BView::HasHeightForWidth();

	// When not editable, we assume that all text is supposed to be visible.
	return true;
}


/**
 * @brief Computes the height required to display all text at the given width.
 *
 * For editable views, delegates to BView::GetHeightForWidth(). For non-editable
 * views, temporarily sets the text rect width to @a width, recalculates line
 * breaks, measures the resulting text height, and restores the original text rect.
 *
 * @param width     The hypothetical width to evaluate.
 * @param min       Receives the minimum height at @a width; may be NULL.
 * @param max       Receives the maximum height (B_SIZE_UNLIMITED); may be NULL.
 * @param preferred Receives the preferred height at @a width; may be NULL.
 */
void
BTextView::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	if (IsEditable()) {
		BView::GetHeightForWidth(width, min, max, preferred);
		return;
	}

	BRect saveTextRect = fTextRect;

	fTextRect.right = fTextRect.left + width;

	// If specific insets were set, reduce the width accordingly (this may result in more
	// linebreaks being inserted)
	if (fLayoutData->overridden) {
		fTextRect.left += fLayoutData->leftInset;
		fTextRect.right -= fLayoutData->rightInset;
	}

	int32 fromLine = _LineAt(0);
	int32 toLine = _LineAt(fText->Length());
	_RecalculateLineBreaks(&fromLine, &toLine);

	// If specific insets were set, add the top and bottom margins to the returned preferred height
	if (fLayoutData->overridden) {
		fTextRect.top -= fLayoutData->topInset;
		fTextRect.bottom += fLayoutData->bottomInset;
	}

	if (min != NULL)
		*min = fTextRect.Height();
	if (max != NULL)
		*max = B_SIZE_UNLIMITED;
	if (preferred != NULL)
		*preferred = fTextRect.Height();

	// Restore the text rect since we were not supposed to change it in this method.
	// Unfortunately, we did change a few other things by calling _RecalculateLineBreaks, that are
	// not so easily undone. However, we are likely to soon get resized to the new width and height
	// computed here, and that will recompute the linebreaks and do a full _Refresh if needed.
	fTextRect = saveTextRect;
}


//	#pragma mark - Layout methods


/**
 * @brief Called by the layout system when the view's layout is invalidated.
 *
 * Clears the cached layout data so that _ValidateLayoutData() recomputes
 * the min and preferred sizes on the next layout pass.
 *
 * @param descendants true if child views were also invalidated.
 */
void
BTextView::LayoutInvalidated(bool descendants)
{
	CALLED();

	fLayoutData->valid = false;
}


/**
 * @brief Performs layout of the view's contents.
 *
 * Validates the layout data, enforces the minimum size, and resets the
 * text rect to match the new bounds. If a child layout is attached, delegates
 * to BView::DoLayout() instead.
 */
void
BTextView::DoLayout()
{
	// Bail out, if we shan't do layout.
	if (!(Flags() & B_SUPPORTS_LAYOUT))
		return;

	CALLED();

	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout()) {
		BView::DoLayout();
		return;
	}

	_ValidateLayoutData();

	// validate current size
	BSize size(Bounds().Size());
	if (size.width < fLayoutData->min.width)
		size.width = fLayoutData->min.width;
	if (size.height < fLayoutData->min.height)
		size.height = fLayoutData->min.height;

	_ResetTextRect();
}


/**
 * @brief Recomputes and caches the minimum and preferred layout sizes if stale.
 *
 * The minimum size accommodates three line-heights wide and one line-height
 * tall, plus insets. The preferred height is the total text height; the
 * preferred width depends on the wrap mode. In non-wrap mode, the minimum
 * size is expanded to match the widest line.
 */
void
BTextView::_ValidateLayoutData()
{
	if (fLayoutData->valid)
		return;

	CALLED();

	float lineHeight = ceilf(LineHeight(0));
	TRACE("line height: %.2f\n", lineHeight);

	// compute our minimal size
	BSize min(lineHeight * 3, lineHeight);
	min.width += fLayoutData->leftInset + fLayoutData->rightInset;
	min.height += fLayoutData->topInset + fLayoutData->bottomInset;

	fLayoutData->min = min;

	// compute our preferred size
	fLayoutData->preferred.height = _TextHeight();

	if (fWrap)
		fLayoutData->preferred.width = min.width + 5 * lineHeight;
	else {
		float maxWidth = fLines->MaxWidth() + fLayoutData->leftInset + fLayoutData->rightInset;
		if (maxWidth < min.width)
			maxWidth = min.width;

		fLayoutData->preferred.width = maxWidth;
		fLayoutData->min = fLayoutData->preferred;
	}

	fLayoutData->valid = true;
	ResetLayoutInvalidation();

	TRACE("width: %.2f, height: %.2f\n", min.width, min.height);
}


//	#pragma mark -


/**
 * @brief Called after the view and all its children have been attached to a window.
 *
 * Delegates to BView::AllAttached(); subclasses may override for post-attach work.
 */
void
BTextView::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Called after the view and all its children have been detached from a window.
 *
 * Delegates to BView::AllDetached(); subclasses may override for cleanup.
 */
void
BTextView::AllDetached()
{
	BView::AllDetached();
}


/**
 * @brief Allocates a new text_run_array with the given number of entries.
 *
 * Uses calloc() for the backing memory and calls BFont constructors explicitly
 * for each run entry. The caller must free the result with FreeRunArray().
 *
 * @param entryCount The number of text_run entries to allocate.
 * @param outSize    If non-NULL, receives the total byte size of the allocation.
 * @return A pointer to the allocated array, or NULL on failure.
 */
/* static */
text_run_array*
BTextView::AllocRunArray(int32 entryCount, int32* outSize)
{
	int32 size = sizeof(text_run_array) + (entryCount - 1) * sizeof(text_run);

	text_run_array* runArray = (text_run_array*)calloc(size, 1);
	if (runArray == NULL) {
		if (outSize != NULL)
			*outSize = 0;
		return NULL;
	}

	runArray->count = entryCount;

	// Call constructors explicitly as the text_run_array
	// was allocated with malloc (and has to, for backwards
	// compatibility)
	for (int32 i = 0; i < runArray->count; i++)
		new (&runArray->runs[i].font) BFont;

	if (outSize != NULL)
		*outSize = size;

	return runArray;
}


/**
 * @brief Creates a deep copy of the first @a countDelta entries of a text_run_array.
 *
 * @param orig       The source run array.
 * @param countDelta The number of runs from @a orig to copy.
 * @return A newly allocated copy, or NULL on failure.
 */
/* static */
text_run_array*
BTextView::CopyRunArray(const text_run_array* orig, int32 countDelta)
{
	text_run_array* copy = AllocRunArray(countDelta, NULL);
	if (copy != NULL) {
		for (int32 i = 0; i < countDelta; i++) {
			copy->runs[i].offset = orig->runs[i].offset;
			copy->runs[i].font = orig->runs[i].font;
			copy->runs[i].color = orig->runs[i].color;
		}
	}
	return copy;
}


/**
 * @brief Frees a text_run_array previously allocated by AllocRunArray() or RunArray().
 *
 * Calls BFont destructors explicitly before releasing the memory with free().
 * Passing NULL is safe and has no effect.
 *
 * @param array The run array to free; may be NULL.
 */
/* static */
void
BTextView::FreeRunArray(text_run_array* array)
{
	if (array == NULL)
		return;

	// Call destructors explicitly
	for (int32 i = 0; i < array->count; i++)
		array->runs[i].font.~BFont();

	free(array);
}


/**
 * @brief Serialises a text_run_array to a portable big-endian byte buffer.
 *
 * The resulting buffer contains a flattened_text_run_array structure suitable
 * for storage in a BMessage or file. The caller must free the returned buffer
 * with free().
 *
 * @param runArray The run array to flatten; must not be NULL.
 * @param _size    If non-NULL, receives the byte size of the returned buffer.
 * @return A malloc'd buffer containing the serialised data, or NULL on failure.
 * @see UnflattenRunArray()
 */
/* static */
void*
BTextView::FlattenRunArray(const text_run_array* runArray, int32* _size)
{
	CALLED();
	int32 size = sizeof(flattened_text_run_array) + (runArray->count - 1)
		* sizeof(flattened_text_run);

	flattened_text_run_array* array = (flattened_text_run_array*)malloc(size);
	if (array == NULL) {
		if (_size)
			*_size = 0;
		return NULL;
	}

	array->magic = B_HOST_TO_BENDIAN_INT32(kFlattenedTextRunArrayMagic);
	array->version = B_HOST_TO_BENDIAN_INT32(kFlattenedTextRunArrayVersion);
	array->count = B_HOST_TO_BENDIAN_INT32(runArray->count);

	for (int32 i = 0; i < runArray->count; i++) {
		array->styles[i].offset = B_HOST_TO_BENDIAN_INT32(
			runArray->runs[i].offset);
		runArray->runs[i].font.GetFamilyAndStyle(&array->styles[i].family,
			&array->styles[i].style);
		array->styles[i].size = B_HOST_TO_BENDIAN_FLOAT(
			runArray->runs[i].font.Size());
		array->styles[i].shear = B_HOST_TO_BENDIAN_FLOAT(
			runArray->runs[i].font.Shear());
		array->styles[i].face = B_HOST_TO_BENDIAN_INT16(
			runArray->runs[i].font.Face());
		array->styles[i].red = runArray->runs[i].color.red;
		array->styles[i].green = runArray->runs[i].color.green;
		array->styles[i].blue = runArray->runs[i].color.blue;
		array->styles[i].alpha = 255;
		array->styles[i]._reserved_ = 0;
	}

	if (_size)
		*_size = size;

	return array;
}


/**
 * @brief Deserialises a flat byte buffer back into a text_run_array.
 *
 * Validates the magic cookie and version tag before parsing. Family and style
 * are applied independently so that the best available match is used even if
 * the original font is not installed.
 *
 * @param data  The buffer previously produced by FlattenRunArray().
 * @param _size If non-NULL, receives the byte size of the returned array.
 * @return A newly allocated text_run_array, or NULL if @a data is invalid or allocation fails.
 * @see FlattenRunArray()
 */
/* static */
text_run_array*
BTextView::UnflattenRunArray(const void* data, int32* _size)
{
	CALLED();
	flattened_text_run_array* array = (flattened_text_run_array*)data;

	if (B_BENDIAN_TO_HOST_INT32(array->magic) != kFlattenedTextRunArrayMagic
		|| B_BENDIAN_TO_HOST_INT32(array->version)
			!= kFlattenedTextRunArrayVersion) {
		if (_size)
			*_size = 0;

		return NULL;
	}

	int32 count = B_BENDIAN_TO_HOST_INT32(array->count);

	text_run_array* runArray = AllocRunArray(count, _size);
	if (runArray == NULL)
		return NULL;

	for (int32 i = 0; i < count; i++) {
		runArray->runs[i].offset = B_BENDIAN_TO_HOST_INT32(
			array->styles[i].offset);

		// Set family and style independently from each other, so that
		// even if the family doesn't exist, we try to preserve the style
		runArray->runs[i].font.SetFamilyAndStyle(array->styles[i].family, NULL);
		runArray->runs[i].font.SetFamilyAndStyle(NULL, array->styles[i].style);

		runArray->runs[i].font.SetSize(B_BENDIAN_TO_HOST_FLOAT(
			array->styles[i].size));
		runArray->runs[i].font.SetShear(B_BENDIAN_TO_HOST_FLOAT(
			array->styles[i].shear));

		uint16 face = B_BENDIAN_TO_HOST_INT16(array->styles[i].face);
		if (face != B_REGULAR_FACE) {
			// Be's version doesn't seem to set this correctly
			runArray->runs[i].font.SetFace(face);
		}

		runArray->runs[i].color.red = array->styles[i].red;
		runArray->runs[i].color.green = array->styles[i].green;
		runArray->runs[i].color.blue = array->styles[i].blue;
		runArray->runs[i].color.alpha = array->styles[i].alpha;
	}

	return runArray;
}


/**
 * @brief Protected hook: inserts raw text into the gap buffer at the given offset.
 *
 * Updates the line buffer, style buffer, and selection offsets. Applies the
 * run array if stylable, otherwise uses the current null style. Subclasses
 * may override this to intercept or modify insertions.
 *
 * @param text   The bytes to insert.
 * @param length The number of bytes in @a text.
 * @param offset The byte offset at which to insert; clamped to [0, TextLength()].
 * @param runs   Optional run array for styling; may be NULL.
 */
void
BTextView::InsertText(const char* text, int32 length, int32 offset,
	const text_run_array* runs)
{
	CALLED();

	if (length < 0)
		length = 0;

	if (offset < 0)
		offset = 0;
	else if (offset > fText->Length())
		offset = fText->Length();

	if (length > 0) {
		// add the text to the buffer
		fText->InsertText(text, length, offset);

		// update the start offsets of each line below offset
		fLines->BumpOffset(length, _LineAt(offset) + 1);

		// update the style runs
		fStyles->BumpOffset(length, fStyles->OffsetToRun(offset - 1) + 1);

		// offset the caret/selection, if the text was inserted before it
		if (offset <= fSelEnd) {
			fSelStart += length;
			fCaretOffset = fSelEnd = fSelStart;
		}
	}

	if (fStylable && runs != NULL)
		_SetRunArray(offset, offset + length, runs);
	else {
		// apply null-style to inserted text
		_ApplyStyleRange(offset, offset + length);
	}
}


/**
 * @brief Protected hook: removes text from the gap buffer in the given range.
 *
 * Synchronises the null style, removes the bytes from the text buffer,
 * removes the corresponding lines and style runs, and adjusts the selection.
 * Subclasses may override to intercept or veto deletions.
 *
 * @param fromOffset The byte offset of the first character to remove.
 * @param toOffset   The byte offset one past the last character to remove.
 */
void
BTextView::DeleteText(int32 fromOffset, int32 toOffset)
{
	CALLED();

	if (fromOffset < 0)
		fromOffset = 0;
	else if (fromOffset > fText->Length())
		fromOffset = fText->Length();

	if (toOffset < 0)
		toOffset = 0;
	else if (toOffset > fText->Length())
		toOffset = fText->Length();

	if (fromOffset >= toOffset)
		return;

	// set nullStyle to style at beginning of range
	fStyles->InvalidateNullStyle();
	fStyles->SyncNullStyle(fromOffset);

	// remove from the text buffer
	fText->RemoveRange(fromOffset, toOffset);

	// remove any lines that have been obliterated
	fLines->RemoveLineRange(fromOffset, toOffset);

	// remove any style runs that have been obliterated
	fStyles->RemoveStyleRange(fromOffset, toOffset);

	// adjust the selection accordingly, assumes fSelEnd >= fSelStart!
	int32 range = toOffset - fromOffset;
	if (fSelStart >= toOffset) {
		// selection is behind the range that was removed
		fSelStart -= range;
		fSelEnd -= range;
	} else if (fSelStart >= fromOffset && fSelEnd <= toOffset) {
		// the selection is within the range that was removed
		fSelStart = fSelEnd = fromOffset;
	} else if (fSelStart >= fromOffset && fSelEnd > toOffset) {
		// the selection starts within and ends after the range
		// the remaining part is the part that was after the range
		fSelStart = fromOffset;
		fSelEnd = fromOffset + fSelEnd - toOffset;
	} else if (fSelStart < fromOffset && fSelEnd < toOffset) {
		// the selection starts before, but ends within the range
		fSelEnd = fromOffset;
	} else if (fSelStart < fromOffset && fSelEnd >= toOffset) {
		// the selection starts before and ends after the range
		fSelEnd -= range;
	}
}


/**
 * @brief Undoes the last editing change.
 *
 * @param clipboard A clipboard to use for the undo operation.
 */
void
BTextView::Undo(BClipboard* clipboard)
{
	if (fUndo)
		fUndo->Undo(clipboard);
}


/**
 * @brief Returns the current state of the undo/redo buffer.
 *
 * @param isRedo If non-NULL, set to true when the next Undo() call would redo.
 * @return B_UNDO_UNAVAILABLE if undo is disabled, otherwise the current undo_state.
 */
undo_state
BTextView::UndoState(bool* isRedo) const
{
	return fUndo == NULL ? B_UNDO_UNAVAILABLE : fUndo->State(isRedo);
}


//	#pragma mark - GetDragParameters() is protected


/**
 * @brief Protected hook: fills a drag message with the selected text and style data.
 *
 * Subclasses may override to customise what is placed in the drag message or
 * to provide a drag bitmap. The default implementation adds the originator
 * pointer, a B_TRASH_TARGET action, "text/plain" MIME data, and (when stylable)
 * a "application/x-vnd.Be-text_run_array" chunk.
 *
 * @param drag    The message to populate for the drag operation; must not be NULL.
 * @param bitmap  Receives a drag bitmap to display, or NULL for a rect-based drag.
 * @param point   Receives the hot-spot within @a bitmap, if used.
 * @param handler Receives the handler to respond to the drag completion; may be NULL.
 */
void
BTextView::GetDragParameters(BMessage* drag, BBitmap** bitmap, BPoint* point,
	BHandler** handler)
{
	CALLED();
	if (drag == NULL)
		return;

	// Add originator and action
	drag->AddPointer("be:originator", this);
	drag->AddInt32("be_actions", B_TRASH_TARGET);

	// add the text
	int32 numBytes = fSelEnd - fSelStart;
	const char* text = fText->GetString(fSelStart, &numBytes);
	drag->AddData("text/plain", B_MIME_TYPE, text, numBytes);

	// add the corresponding styles
	int32 size = 0;
	text_run_array* styles = RunArray(fSelStart, fSelEnd, &size);

	if (styles != NULL) {
		drag->AddData("application/x-vnd.Be-text_run_array", B_MIME_TYPE,
			styles, size);

		FreeRunArray(styles);
	}

	if (bitmap != NULL)
		*bitmap = NULL;

	if (handler != NULL)
		*handler = NULL;
}


//	#pragma mark - FBC padding and forbidden methods


/** @brief FBC padding slot 3; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView3() {}
/** @brief FBC padding slot 4; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView4() {}
/** @brief FBC padding slot 5; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView5() {}
/** @brief FBC padding slot 6; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView6() {}
/** @brief FBC padding slot 7; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView7() {}
/** @brief FBC padding slot 8; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView8() {}
/** @brief FBC padding slot 9; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView9() {}
/** @brief FBC padding slot 10; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView10() {}
/** @brief FBC padding slot 11; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView11() {}
/** @brief FBC padding slot 12; reserved for future binary-compatible additions. */
void BTextView::_ReservedTextView12() {}


// #pragma mark - Private methods


/**
 * @brief Initialises all BTextView member variables and allocates core sub-objects.
 *
 * @param textRect     The initial text rect; used as the rendering area until the view is resized.
 * @param initialFont  The font to use for newly entered text; if NULL the view font is used.
 * @param initialColor The foreground colour for newly entered text; if NULL B_DOCUMENT_TEXT_COLOR is used.
 */
void
BTextView::_InitObject(BRect textRect, const BFont* initialFont,
	const rgb_color* initialColor)
{
	BFont font;
	if (initialFont == NULL)
		GetFont(&font);
	else
		font = *initialFont;

	_NormalizeFont(&font);

	rgb_color documentTextColor = ui_color(B_DOCUMENT_TEXT_COLOR);

	if (initialColor == NULL)
		initialColor = &documentTextColor;

	fText = new BPrivate::TextGapBuffer;
	fLines = new LineBuffer;
	fStyles = new StyleBuffer(&font, initialColor);

	fInstalledNavigateCommandWordwiseShortcuts = false;
	fInstalledNavigateOptionWordwiseShortcuts = false;
	fInstalledNavigateOptionLinewiseShortcuts = false;
	fInstalledNavigateHomeEndDocwiseShortcuts = false;

	fInstalledSelectCommandWordwiseShortcuts = false;
	fInstalledSelectOptionWordwiseShortcuts = false;
	fInstalledSelectOptionLinewiseShortcuts = false;
	fInstalledSelectHomeEndDocwiseShortcuts = false;

	fInstalledRemoveCommandWordwiseShortcuts = false;
	fInstalledRemoveOptionWordwiseShortcuts = false;

	// We put these here instead of in the constructor initializer list
	// to have less code duplication, and a single place where to do changes
	// if needed.
	fTextRect = textRect;
		// NOTE: The only places where text rect is changed:
		// * width and height are adjusted in _RecalculateLineBreaks(),
		// text rect maintains constant insets, use SetInsets() to change.
	fMinTextRectWidth = fTextRect.Width();
		// see SetTextRect()
	fSelStart = fSelEnd = 0;
	fCaretVisible = false;
	fCaretTime = 0;
	fCaretOffset = 0;
	fClickCount = 0;
	fClickTime = 0;
	fDragOffset = -1;
	fCursor = 0;
	fActive = false;
	fStylable = false;
	fTabWidth = 28.0;
	fSelectable = true;
	fEditable = true;
	fWrap = true;
	fMaxBytes = INT32_MAX;
	fDisallowedChars = NULL;
	fAlignment = B_ALIGN_LEFT;
	fAutoindent = false;
	fOffscreen = NULL;
	fColorSpace = B_CMAP8;
	fResizable = false;
	fContainerView = NULL;
	fUndo = NULL;
	fInline = NULL;
	fDragRunner = NULL;
	fClickRunner = NULL;
	fTrackingMouse = NULL;

	fLayoutData = new LayoutData;
	_UpdateInsets(textRect);

	fLastClickOffset = -1;

	SetDoesUndo(true);
}


/**
 * @brief Handles the Backspace key press, deleting one character or one word to the left.
 *
 * When Command or Option is held (without Control), the selection is extended
 * to the previous word start before deletion. Records a TypingUndoBuffer entry.
 *
 * @param modifiers The current modifier key mask; pass -1 to read from the current message.
 */
void
BTextView::_HandleBackspace(int32 modifiers)
{
	if (!fEditable)
		return;

	if (modifiers < 0) {
		BMessage* currentMessage = Window()->CurrentMessage();
		if (currentMessage == NULL
			|| currentMessage->FindInt32("modifiers", &modifiers) != B_OK) {
			modifiers = 0;
		}
	}

	bool controlKeyDown = (modifiers & B_CONTROL_KEY) != 0;
	bool optionKeyDown  = (modifiers & B_OPTION_KEY)  != 0;
	bool commandKeyDown = (modifiers & B_COMMAND_KEY) != 0;

	if ((commandKeyDown || optionKeyDown) && !controlKeyDown) {
		fSelStart = _PreviousWordStart(fCaretOffset - 1);
		fSelEnd = fCaretOffset;
	}

	if (fUndo) {
		TypingUndoBuffer* undoBuffer = dynamic_cast<TypingUndoBuffer*>(fUndo);
		if (!undoBuffer) {
			delete fUndo;
			fUndo = undoBuffer = new TypingUndoBuffer(this);
		}
		undoBuffer->BackwardErase();
	}

	// we may draw twice, so turn updates off for now
	if (Window() != NULL)
		Window()->DisableUpdates();

	if (fSelStart == fSelEnd) {
		if (fSelStart != 0)
			fSelStart = _PreviousInitialByte(fSelStart);
	} else
		Highlight(fSelStart, fSelEnd);

	DeleteText(fSelStart, fSelEnd);
	fCaretOffset = fSelEnd = fSelStart;

	_Refresh(fSelStart, fSelEnd, fCaretOffset);

	// turn updates back on
	if (Window() != NULL)
		Window()->EnableUpdates();
}


/**
 * @brief Handles an arrow-key press, moving the caret or scrolling the view.
 *
 * For non-editable, non-selectable views the arrow keys scroll by the
 * configured step amount. For editable/selectable views, left/right move by
 * character or word (with Command/Option), and up/down move by line or to the
 * document start/end (with Command/Option). Shift extends the selection.
 *
 * @param arrowKey The key code (B_LEFT_ARROW, B_RIGHT_ARROW, B_UP_ARROW, or B_DOWN_ARROW).
 * @param modifiers The current modifier key mask; pass -1 to read from the current message.
 */
void
BTextView::_HandleArrowKey(uint32 arrowKey, int32 modifiers)
{
	// return if there's nowhere to go
	if (fText->Length() == 0)
		return;

	int32 selStart = fSelStart;
	int32 selEnd = fSelEnd;

	if (modifiers < 0) {
		BMessage* currentMessage = Window()->CurrentMessage();
		if (currentMessage == NULL
			|| currentMessage->FindInt32("modifiers", &modifiers) != B_OK) {
			modifiers = 0;
		}
	}

	bool shiftKeyDown   = (modifiers & B_SHIFT_KEY)   != 0;
	bool controlKeyDown = (modifiers & B_CONTROL_KEY) != 0;
	bool optionKeyDown  = (modifiers & B_OPTION_KEY)  != 0;
	bool commandKeyDown = (modifiers & B_COMMAND_KEY) != 0;

	int32 lastClickOffset = fCaretOffset;

	switch (arrowKey) {
		case B_LEFT_ARROW:
			if (!fEditable && !fSelectable)
				_ScrollBy(-kHorizontalScrollBarStep, 0);
			else if (fSelStart != fSelEnd && !shiftKeyDown)
				fCaretOffset = fSelStart;
			else {
				if ((commandKeyDown || optionKeyDown) && !controlKeyDown)
					fCaretOffset = _PreviousWordStart(fCaretOffset - 1);
				else
					fCaretOffset = _PreviousInitialByte(fCaretOffset);

				if (shiftKeyDown && fCaretOffset != lastClickOffset) {
					if (fCaretOffset < fSelStart) {
						// extend selection to the left
						selStart = fCaretOffset;
						if (lastClickOffset > fSelStart) {
							// caret has jumped across "anchor"
							selEnd = fSelStart;
						}
					} else {
						// shrink selection from the right
						selEnd = fCaretOffset;
					}
				}
			}
			break;

		case B_RIGHT_ARROW:
			if (!fEditable && !fSelectable)
				_ScrollBy(kHorizontalScrollBarStep, 0);
			else if (fSelStart != fSelEnd && !shiftKeyDown)
				fCaretOffset = fSelEnd;
			else {
				if ((commandKeyDown || optionKeyDown) && !controlKeyDown)
					fCaretOffset = _NextWordEnd(fCaretOffset);
				else
					fCaretOffset = _NextInitialByte(fCaretOffset);

				if (shiftKeyDown && fCaretOffset != lastClickOffset) {
					if (fCaretOffset > fSelEnd) {
						// extend selection to the right
						selEnd = fCaretOffset;
						if (lastClickOffset < fSelEnd) {
							// caret has jumped across "anchor"
							selStart = fSelEnd;
						}
					} else {
						// shrink selection from the left
						selStart = fCaretOffset;
					}
				}
			}
			break;

		case B_UP_ARROW:
		{
			if (!fEditable && !fSelectable)
				_ScrollBy(0, -kVerticalScrollBarStep);
			else if (fSelStart != fSelEnd && !shiftKeyDown)
				fCaretOffset = fSelStart;
			else {
				if (optionKeyDown && !commandKeyDown && !controlKeyDown)
					fCaretOffset = _PreviousLineStart(fCaretOffset);
				else if (commandKeyDown && !optionKeyDown && !controlKeyDown) {
					_ScrollTo(0, 0);
					fCaretOffset = 0;
				} else {
					float height;
					BPoint point = PointAt(fCaretOffset, &height);
					// find the caret position on the previous
					// line by gently stepping onto this line
					for (int i = 1; i <= height; i++) {
						point.y--;
						int32 offset = OffsetAt(point);
						if (offset < fCaretOffset || i == height) {
							fCaretOffset = offset;
							break;
						}
					}
				}

				if (shiftKeyDown && fCaretOffset != lastClickOffset) {
					if (fCaretOffset < fSelStart) {
						// extend selection to the top
						selStart = fCaretOffset;
						if (lastClickOffset > fSelStart) {
							// caret has jumped across "anchor"
							selEnd = fSelStart;
						}
					} else {
						// shrink selection from the bottom
						selEnd = fCaretOffset;
					}
				}
			}
			break;
		}

		case B_DOWN_ARROW:
		{
			if (!fEditable && !fSelectable)
				_ScrollBy(0, kVerticalScrollBarStep);
			else if (fSelStart != fSelEnd && !shiftKeyDown)
				fCaretOffset = fSelEnd;
			else {
				if (optionKeyDown && !commandKeyDown && !controlKeyDown)
					fCaretOffset = _NextLineEnd(fCaretOffset);
				else if (commandKeyDown && !optionKeyDown && !controlKeyDown) {
					_ScrollTo(0, fTextRect.bottom + fLayoutData->bottomInset);
					fCaretOffset = fText->Length();
				} else {
					float height;
					BPoint point = PointAt(fCaretOffset, &height);
					point.y += height;
					fCaretOffset = OffsetAt(point);
				}

				if (shiftKeyDown && fCaretOffset != lastClickOffset) {
					if (fCaretOffset > fSelEnd) {
						// extend selection to the bottom
						selEnd = fCaretOffset;
						if (lastClickOffset < fSelEnd) {
							// caret has jumped across "anchor"
							selStart = fSelEnd;
						}
					} else {
						// shrink selection from the top
						selStart = fCaretOffset;
					}
				}
			}
			break;
		}
	}

	fStyles->InvalidateNullStyle();

	if (fEditable || fSelectable) {
		if (shiftKeyDown)
			Select(selStart, selEnd);
		else
			Select(fCaretOffset, fCaretOffset);

		// scroll if needed
		ScrollToOffset(fCaretOffset);
	}
}


/**
 * @brief Handles the Delete (forward-delete) key press.
 *
 * When Command or Option is held, the selection is extended to the next word
 * end before deletion. Records a TypingUndoBuffer entry.
 *
 * @param modifiers The current modifier key mask; pass -1 to read from the current message.
 */
void
BTextView::_HandleDelete(int32 modifiers)
{
	if (!fEditable)
		return;

	if (modifiers < 0) {
		BMessage* currentMessage = Window()->CurrentMessage();
		if (currentMessage == NULL
			|| currentMessage->FindInt32("modifiers", &modifiers) != B_OK) {
			modifiers = 0;
		}
	}

	bool controlKeyDown = (modifiers & B_CONTROL_KEY) != 0;
	bool optionKeyDown  = (modifiers & B_OPTION_KEY)  != 0;
	bool commandKeyDown = (modifiers & B_COMMAND_KEY) != 0;

	if ((commandKeyDown || optionKeyDown) && !controlKeyDown) {
		fSelStart = fCaretOffset;
		fSelEnd = _NextWordEnd(fCaretOffset) + 1;
	}

	if (fUndo) {
		TypingUndoBuffer* undoBuffer = dynamic_cast<TypingUndoBuffer*>(fUndo);
		if (!undoBuffer) {
			delete fUndo;
			fUndo = undoBuffer = new TypingUndoBuffer(this);
		}
		undoBuffer->ForwardErase();
	}

	// we may draw twice, so turn updates off for now
	if (Window() != NULL)
		Window()->DisableUpdates();

	if (fSelStart == fSelEnd) {
		if (fSelEnd != fText->Length())
			fSelEnd = _NextInitialByte(fSelEnd);
	} else
		Highlight(fSelStart, fSelEnd);

	DeleteText(fSelStart, fSelEnd);
	fCaretOffset = fSelEnd = fSelStart;

	_Refresh(fSelStart, fSelEnd, fCaretOffset);

	// turn updates back on
	if (Window() != NULL)
		Window()->EnableUpdates();
}


/**
 * @brief Handles Home, End, Page Up, and Page Down key presses.
 *
 * In non-editable, non-selectable views, these keys scroll the view.
 * In editable/selectable views they move the caret and optionally extend
 * the selection (with Shift). Command+Home/End jump to the document start/end.
 *
 * @param pageKey   The key code (B_HOME, B_END, B_PAGE_UP, or B_PAGE_DOWN).
 * @param modifiers The current modifier key mask; pass -1 to read from the current message.
 */
void
BTextView::_HandlePageKey(uint32 pageKey, int32 modifiers)
{
	if (modifiers < 0) {
		BMessage* currentMessage = Window()->CurrentMessage();
		if (currentMessage == NULL
			|| currentMessage->FindInt32("modifiers", &modifiers) != B_OK) {
			modifiers = 0;
		}
	}

	bool shiftKeyDown   = (modifiers & B_SHIFT_KEY)   != 0;
	bool controlKeyDown = (modifiers & B_CONTROL_KEY) != 0;
	bool optionKeyDown  = (modifiers & B_OPTION_KEY)  != 0;
	bool commandKeyDown = (modifiers & B_COMMAND_KEY) != 0;

	STELine* line = NULL;
	int32 selStart = fSelStart;
	int32 selEnd = fSelEnd;

	int32 lastClickOffset = fCaretOffset;
	switch (pageKey) {
		case B_HOME:
			if (!fEditable && !fSelectable) {
				fCaretOffset = 0;
				_ScrollTo(0, 0);
				break;
			} else {
				if (commandKeyDown && !optionKeyDown && !controlKeyDown) {
					_ScrollTo(0, 0);
					fCaretOffset = 0;
				} else {
					// get the start of the last line if caret is on it
					line = (*fLines)[_LineAt(lastClickOffset)];
					fCaretOffset = line->offset;
				}

				if (!shiftKeyDown)
					selStart = selEnd = fCaretOffset;
				else if (fCaretOffset != lastClickOffset) {
					if (fCaretOffset < fSelStart) {
						// extend selection to the left
						selStart = fCaretOffset;
						if (lastClickOffset > fSelStart) {
							// caret has jumped across "anchor"
							selEnd = fSelStart;
						}
					} else {
						// shrink selection from the right
						selEnd = fCaretOffset;
					}
				}
			}
			break;

		case B_END:
			if (!fEditable && !fSelectable) {
				fCaretOffset = fText->Length();
				_ScrollTo(0, fTextRect.bottom + fLayoutData->bottomInset);
				break;
			} else {
				if (commandKeyDown && !optionKeyDown && !controlKeyDown) {
					_ScrollTo(0, fTextRect.bottom + fLayoutData->bottomInset);
					fCaretOffset = fText->Length();
				} else {
					// If we are on the last line, just go to the last
					// character in the buffer, otherwise get the starting
					// offset of the next line, and go to the previous character
					int32 currentLine = _LineAt(lastClickOffset);
					if (currentLine + 1 < fLines->NumLines()) {
						line = (*fLines)[currentLine + 1];
						fCaretOffset = _PreviousInitialByte(line->offset);
					} else {
						// This check is needed to avoid moving the cursor
						// when the cursor is on the last line, and that line
						// is empty
						if (fCaretOffset != fText->Length()) {
							fCaretOffset = fText->Length();
							if (ByteAt(fCaretOffset - 1) == B_ENTER)
								fCaretOffset--;
						}
					}
				}

				if (!shiftKeyDown)
					selStart = selEnd = fCaretOffset;
				else if (fCaretOffset != lastClickOffset) {
					if (fCaretOffset > fSelEnd) {
						// extend selection to the right
						selEnd = fCaretOffset;
						if (lastClickOffset < fSelEnd) {
							// caret has jumped across "anchor"
							selStart = fSelEnd;
						}
					} else {
						// shrink selection from the left
						selStart = fCaretOffset;
					}
				}
			}
			break;

		case B_PAGE_UP:
		{
			float lineHeight;
			BPoint currentPos = PointAt(fCaretOffset, &lineHeight);
			BPoint nextPos(currentPos.x,
				currentPos.y + lineHeight - Bounds().Height());
			fCaretOffset = OffsetAt(nextPos);
			nextPos = PointAt(fCaretOffset);
			_ScrollBy(0, nextPos.y - currentPos.y);

			if (!fEditable && !fSelectable)
				break;

			if (!shiftKeyDown)
				selStart = selEnd = fCaretOffset;
			else if (fCaretOffset != lastClickOffset) {
				if (fCaretOffset < fSelStart) {
					// extend selection to the top
					selStart = fCaretOffset;
					if (lastClickOffset > fSelStart) {
						// caret has jumped across "anchor"
						selEnd = fSelStart;
					}
				} else {
					// shrink selection from the bottom
					selEnd = fCaretOffset;
				}
			}

			break;
		}

		case B_PAGE_DOWN:
		{
			BPoint currentPos = PointAt(fCaretOffset);
			BPoint nextPos(currentPos.x, currentPos.y + Bounds().Height());
			fCaretOffset = OffsetAt(nextPos);
			nextPos = PointAt(fCaretOffset);
			_ScrollBy(0, nextPos.y - currentPos.y);

			if (!fEditable && !fSelectable)
				break;

			if (!shiftKeyDown)
				selStart = selEnd = fCaretOffset;
			else if (fCaretOffset != lastClickOffset) {
				if (fCaretOffset > fSelEnd) {
					// extend selection to the bottom
					selEnd = fCaretOffset;
					if (lastClickOffset < fSelEnd) {
						// caret has jumped across "anchor"
						selStart = fSelEnd;
					}
				} else {
					// shrink selection from the top
					selStart = fCaretOffset;
				}
			}

			break;
		}
	}

	if (fEditable || fSelectable) {
		if (shiftKeyDown)
			Select(selStart, selEnd);
		else
			Select(fCaretOffset, fCaretOffset);

		ScrollToOffset(fCaretOffset);
	}
}


/**
 * @brief Inserts one or more printable characters typed by the user.
 *
 * Records a TypingUndoBuffer entry, deletes any active selection, handles
 * auto-indent when the character is a newline, then inserts the character(s)
 * and scrolls the caret into view.
 *
 * @param bytes    The UTF-8 byte sequence of the character(s) to insert.
 * @param numBytes The number of bytes in @a bytes.
 */
void
BTextView::_HandleAlphaKey(const char* bytes, int32 numBytes)
{
	if (!fEditable)
		return;

	if (fUndo) {
		TypingUndoBuffer* undoBuffer = dynamic_cast<TypingUndoBuffer*>(fUndo);
		if (!undoBuffer) {
			delete fUndo;
			fUndo = undoBuffer = new TypingUndoBuffer(this);
		}
		undoBuffer->InputCharacter(numBytes);
	}

	if (fSelStart != fSelEnd) {
		Highlight(fSelStart, fSelEnd);
		DeleteText(fSelStart, fSelEnd);
	}

	// we may draw twice, so turn updates off for now
	if (Window() != NULL)
		Window()->DisableUpdates();

	if (fAutoindent && numBytes == 1 && *bytes == B_ENTER) {
		int32 start, offset;
		start = offset = OffsetAt(_LineAt(fSelStart));

		while (ByteAt(offset) != '\0' &&
				(ByteAt(offset) == B_TAB || ByteAt(offset) == B_SPACE)
				&& offset < fSelStart)
			offset++;

		_DoInsertText(bytes, numBytes, fSelStart, NULL);
		if (start != offset)
			_DoInsertText(Text() + start, offset - start, fSelStart, NULL);
	} else
		_DoInsertText(bytes, numBytes, fSelStart, NULL);

	fCaretOffset = fSelEnd;
	ScrollToOffset(fCaretOffset);

	// turn updates back on
	if (Window() != NULL)
		Window()->EnableUpdates();
}


/**
 * @brief Redraws the text between the two given offsets, recalculating line breaks if needed.
 *
 * Recalculates line breaks for the affected range, erases any area below the
 * text that shrank, updates the scroll bars if the text height changed, and
 * optionally scrolls to @a scrollTo.
 *
 * @param fromOffset The byte offset from which to begin the refresh.
 * @param toOffset   The byte offset at which to stop the refresh.
 * @param scrollTo   If not INT32_MIN, scrolls the view to this byte offset after redrawing.
 */
void
BTextView::_Refresh(int32 fromOffset, int32 toOffset, int32 scrollTo)
{
	// TODO: Cleanup
	float saveHeight = fTextRect.Height();
	float saveWidth = fTextRect.Width();
	int32 fromLine = _LineAt(fromOffset);
	int32 toLine = _LineAt(toOffset);
	int32 saveFromLine = fromLine;
	int32 saveToLine = toLine;

	_RecalculateLineBreaks(&fromLine, &toLine);

	// TODO: Maybe there is still something we can do without a window...
	if (!Window())
		return;

	BRect bounds = Bounds();
	float newHeight = fTextRect.Height();

	// if the line breaks have changed, force an erase
	if (fromLine != saveFromLine || toLine != saveToLine
			|| newHeight != saveHeight) {
		fromOffset = -1;
	}

	if (newHeight != saveHeight) {
		// the text area has changed
		if (newHeight < saveHeight)
			toLine = _LineAt(BPoint(0.0f, saveHeight + fTextRect.top));
		else
			toLine = _LineAt(BPoint(0.0f, newHeight + fTextRect.top));
	}

	// draw only those lines that are visible
	int32 fromVisible = _LineAt(BPoint(0.0f, bounds.top));
	int32 toVisible = _LineAt(BPoint(0.0f, bounds.bottom));
	fromLine = std::max(fromVisible, fromLine);
	toLine = std::min(toLine, toVisible);

	_AutoResize(false);

	_RequestDrawLines(fromLine, toLine);

	// erase the area below the text
	BRect eraseRect = bounds;
	eraseRect.top = fTextRect.top + (*fLines)[fLines->NumLines()]->origin;
	eraseRect.bottom = fTextRect.top + saveHeight;
	if (eraseRect.bottom > eraseRect.top && eraseRect.Intersects(bounds)) {
		SetLowColor(ViewColor());
		FillRect(eraseRect, B_SOLID_LOW);
	}

	// update the scroll bars if the text area has changed
	if (newHeight != saveHeight || fMinTextRectWidth != saveWidth)
		_UpdateScrollbars();

	if (scrollTo != INT32_MIN)
		ScrollToOffset(scrollTo);

	Flush();
}


/**
 * @brief Recalculates line breaks for the range from @a startLine to @a endLine.
 *
 * Iterates from @a startLine, calling _FindLineBreak() for each line and
 * inserting, updating, or removing STELine entries as needed. Stops early
 * when the old and new line layouts converge past the original @a endLine.
 * Also updates the text rect's bottom and, in non-wrap mode, adjusts the
 * left/right edges for the widest line.
 *
 * @param startLine In/out: first line to recalculate; may be adjusted downward on exit.
 * @param endLine   In/out: last line to recalculate; set to the last modified line on exit.
 */
void
BTextView::_RecalculateLineBreaks(int32* startLine, int32* endLine)
{
	CALLED();

	float width = fTextRect.Width();

	// don't try to compute anything with word wrapping if the text rect is not set
	if (fWrap && (!fTextRect.IsValid() || width == 0))
		return;

	// sanity check
	*startLine = (*startLine < 0) ? 0 : *startLine;
	*endLine = (*endLine > fLines->NumLines() - 1) ? fLines->NumLines() - 1
		: *endLine;

	int32 textLength = fText->Length();
	int32 lineIndex = (*startLine > 0) ? *startLine - 1 : 0;
	int32 recalThreshold = (*fLines)[*endLine + 1]->offset;
	STELine* curLine = (*fLines)[lineIndex];
	STELine* nextLine = curLine + 1;

	do {
		float ascent, descent;
		int32 fromOffset = curLine->offset;
		int32 toOffset = _FindLineBreak(fromOffset, &ascent, &descent, &width);

		curLine->ascent = ascent;
		curLine->width = width;

		// we want to advance at least by one character
		int32 nextOffset = _NextInitialByte(fromOffset);
		if (toOffset < nextOffset && fromOffset < textLength)
			toOffset = nextOffset;

		lineIndex++;
		STELine saveLine = *nextLine;
		if (lineIndex > fLines->NumLines() || toOffset < nextLine->offset) {
			// the new line comes before the old line start, add a line
			STELine newLine;
			newLine.offset = toOffset;
			newLine.origin = ceilf(curLine->origin + ascent + descent) + 1;
			newLine.ascent = 0;
			fLines->InsertLine(&newLine, lineIndex);
		} else {
			// update the existing line
			nextLine->offset = toOffset;
			nextLine->origin = ceilf(curLine->origin + ascent + descent) + 1;

			// remove any lines that start before the current line
			while (lineIndex < fLines->NumLines()
				&& toOffset >= ((*fLines)[lineIndex] + 1)->offset) {
				fLines->RemoveLines(lineIndex + 1);
			}

			nextLine = (*fLines)[lineIndex];
			if (nextLine->offset == saveLine.offset) {
				if (nextLine->offset >= recalThreshold) {
					if (nextLine->origin != saveLine.origin)
						fLines->BumpOrigin(nextLine->origin - saveLine.origin,
							lineIndex + 1);
					break;
				}
			} else {
				if (lineIndex > 0 && lineIndex == *startLine)
					*startLine = lineIndex - 1;
			}
		}

		curLine = (*fLines)[lineIndex];
		nextLine = curLine + 1;
	} while (curLine->offset < textLength);

	// make sure that the sentinel line (which starts at the end of the buffer)
	// has always a width of 0
	(*fLines)[fLines->NumLines()]->width = 0;

	// update text rect
	fTextRect.left = Bounds().left + fLayoutData->leftInset;
	fTextRect.right = Bounds().right - fLayoutData->rightInset;

	// always set text rect bottom
	float newHeight = TextHeight(0, fLines->NumLines() - 1);
	fTextRect.bottom = fTextRect.top + newHeight;

	if (!fWrap) {
		fMinTextRectWidth = fLines->MaxWidth() - 1;

		// expand width if needed
		switch (fAlignment) {
			default:
			case B_ALIGN_LEFT:
				// move right edge
				fTextRect.right = fTextRect.left + fMinTextRectWidth;
				break;

			case B_ALIGN_RIGHT:
				// move left edge
				fTextRect.left = fTextRect.right - fMinTextRectWidth;
				break;

			case B_ALIGN_CENTER:
				// move both edges
				fTextRect.InsetBy(roundf((fTextRect.Width()
					- fMinTextRectWidth) / 2), 0);
				break;
		}

		_ValidateTextRect();
	}

	*endLine = lineIndex - 1;
	*startLine = std::min(*startLine, *endLine);
}


/**
 * @brief Ensures the text rect has positive width and height.
 *
 * If the right edge is at or left of the left edge, or the bottom is at or
 * above the top, the corresponding dimension is set to exactly 1 pixel.
 */
void
BTextView::_ValidateTextRect()
{
	// text rect right must be greater than left
	if (fTextRect.right <= fTextRect.left)
		fTextRect.right = fTextRect.left + 1;
	// text rect bottom must be greater than top
	if (fTextRect.bottom <= fTextRect.top)
		fTextRect.bottom = fTextRect.top + 1;
}


/**
 * @brief Finds the byte offset at which a line break should be inserted.
 *
 * In non-wrap mode, advances to the next newline. In wrap mode, scans forward
 * accumulating character widths (expanding tabs) until the text exceeds
 * @a inOutWidth, then backs up to the last legal break point per CanEndLine().
 * Falls back to breaking in the middle of a word if no legal break is found.
 *
 * @param fromOffset  The byte offset at which the current line begins.
 * @param _ascent     Receives the maximum ascent of the line's characters.
 * @param _descent    Receives the maximum descent of the line's characters.
 * @param inOutWidth  On entry, the available line width; on exit, the actual rendered width.
 * @return The byte offset of the first character on the next line.
 */
int32
BTextView::_FindLineBreak(int32 fromOffset, float* _ascent, float* _descent,
	float* inOutWidth)
{
	*_ascent = 0.0;
	*_descent = 0.0;

	const int32 limit = fText->Length();

	// is fromOffset at the end?
	if (fromOffset >= limit) {
		// try to return valid height info anyway
		if (fStyles->NumRuns() > 0) {
			fStyles->Iterate(fromOffset, 1, fInline, NULL, NULL, _ascent,
				_descent);
		} else {
			if (fStyles->IsValidNullStyle()) {
				const BFont* font = NULL;
				fStyles->GetNullStyle(&font, NULL);

				font_height fh;
				font->GetHeight(&fh);
				*_ascent = fh.ascent;
				*_descent = fh.descent + fh.leading;
			}
		}
		*inOutWidth = 0;

		return limit;
	}

	int32 offset = fromOffset;

	if (!fWrap) {
		// Text wrapping is turned off.
		// Just find the offset of the first \n character
		offset = limit - fromOffset;
		fText->FindChar(B_ENTER, fromOffset, &offset);
		offset += fromOffset;
		int32 toOffset = (offset < limit) ? offset : limit;

		*inOutWidth = _TabExpandedStyledWidth(fromOffset, toOffset - fromOffset,
			_ascent, _descent);

		return offset < limit ? offset + 1 : limit;
	}

	bool done = false;
	float ascent = 0.0;
	float descent = 0.0;
	int32 delta = 0;
	float deltaWidth = 0.0;
	float strWidth = 0.0;
	uchar theChar;

	// wrap the text
	while (offset < limit && !done) {
		// find the next line break candidate
		for (; (offset + delta) < limit; delta++) {
			if (CanEndLine(offset + delta)) {
				theChar = fText->RealCharAt(offset + delta);
				if (theChar != B_SPACE && theChar != B_TAB
					&& theChar != B_ENTER) {
					// we are scanning for trailing whitespace below, so we
					// have to skip non-whitespace characters, that can end
					// the line, here
					delta++;
				}
				break;
			}
		}

		int32 deltaBeforeWhitespace = delta;
		// now skip over trailing whitespace, if any
		for (; (offset + delta) < limit; delta++) {
			theChar = fText->RealCharAt(offset + delta);
			if (theChar == B_ENTER) {
				// found a newline, we're done!
				done = true;
				delta++;
				break;
			} else if (theChar != B_SPACE && theChar != B_TAB) {
				// stop at anything else than trailing whitespace
				break;
			}
		}

		delta = std::max(delta, (int32)1);

		// do not include B_ENTER-terminator into width & height calculations
		deltaWidth = _TabExpandedStyledWidth(offset,
								done ? delta - 1 : delta, &ascent, &descent);
		strWidth += deltaWidth;

		if (strWidth >= *inOutWidth) {
			// we've found where the line will wrap
			done = true;

			// we have included trailing whitespace in the width computation
			// above, but that is not being shown anyway, so we try again
			// without the trailing whitespace
			if (delta == deltaBeforeWhitespace) {
				// there is no trailing whitespace, no point in trying
				break;
			}

			// reset string width to start of current run ...
			strWidth -= deltaWidth;

			// ... and compute the resulting width (of visible characters)
			strWidth += _StyledWidth(offset, deltaBeforeWhitespace, NULL, NULL);
			if (strWidth >= *inOutWidth) {
				// width of visible characters exceeds line, we need to wrap
				// before the current "word"
				break;
			}
		}

		*_ascent = std::max(ascent, *_ascent);
		*_descent = std::max(descent, *_descent);

		offset += delta;
		delta = 0;
	}

	if (offset - fromOffset < 1) {
		// there weren't any words that fit entirely in this line
		// force a break in the middle of a word
		*_ascent = 0.0;
		*_descent = 0.0;
		strWidth = 0.0;

		int32 current = fromOffset;
		for (offset = _NextInitialByte(current); current < limit;
				current = offset, offset = _NextInitialByte(offset)) {
			strWidth += _StyledWidth(current, offset - current, &ascent,
				&descent);
			if (strWidth >= *inOutWidth) {
				offset = _PreviousInitialByte(offset);
				break;
			}

			*_ascent = std::max(ascent, *_ascent);
			*_descent = std::max(descent, *_descent);
		}
	}

	return std::min(offset, limit);
}


/**
 * @brief Returns the byte offset of the first character on the line preceding @a offset.
 *
 * Walks backwards through the text looking for a B_ENTER character, then
 * returns the offset of the character immediately after it.
 *
 * @param offset The starting offset; must be > 0.
 * @return The start offset of the previous line, or 0 if at the beginning.
 */
int32
BTextView::_PreviousLineStart(int32 offset)
{
	if (offset <= 0)
		return 0;

	while (offset > 0) {
		offset = _PreviousInitialByte(offset);
		if (_CharClassification(offset) == CHAR_CLASS_WHITESPACE
			&& ByteAt(offset) == B_ENTER) {
			return offset + 1;
		}
	}

	return offset;
}


/**
 * @brief Returns the byte offset of the newline character ending the line at @a offset.
 *
 * @param offset The starting byte offset.
 * @return The offset of the B_ENTER that ends the current line, or TextLength() if none.
 */
int32
BTextView::_NextLineEnd(int32 offset)
{
	int32 textLen = fText->Length();
	if (offset >= textLen)
		return textLen;

	while (offset < textLen) {
		if (_CharClassification(offset) == CHAR_CLASS_WHITESPACE
			&& ByteAt(offset) == B_ENTER) {
			break;
		}
		offset = _NextInitialByte(offset);
	}

	return offset;
}


/**
 * @brief Returns the offset of the boundary before the current character class run.
 *
 * Walks backward until the character class changes, returning the offset at which
 * the transition occurs.
 *
 * @param offset The starting byte offset.
 * @return The offset of the word boundary before @a offset.
 */
int32
BTextView::_PreviousWordBoundary(int32 offset)
{
	uint32 charType = _CharClassification(offset);
	int32 previous;
	while (offset > 0) {
		previous = _PreviousInitialByte(offset);
		if (_CharClassification(previous) != charType)
			break;
		offset = previous;
	}

	return offset;
}


/**
 * @brief Returns the offset of the boundary after the current character class run.
 *
 * Walks forward until the character class changes, returning the offset at which
 * the transition occurs.
 *
 * @param offset The starting byte offset.
 * @return The offset of the word boundary after @a offset.
 */
int32
BTextView::_NextWordBoundary(int32 offset)
{
	int32 textLen = fText->Length();
	uint32 charType = _CharClassification(offset);
	while (offset < textLen) {
		offset = _NextInitialByte(offset);
		if (_CharClassification(offset) != charType)
			break;
	}

	return offset;
}


/**
 * @brief Returns the byte offset of the start of the word preceding @a offset.
 *
 * Skips over non-word characters first, then walks backward through
 * word characters to find the beginning of the word.
 *
 * @param offset The starting byte offset (the search begins at offset - 1).
 * @return The offset of the first character of the preceding word, or 0.
 */
int32
BTextView::_PreviousWordStart(int32 offset)
{
	if (offset <= 1)
		return 0;

	--offset;
		// need to look at previous char
	if (_CharClassification(offset) != CHAR_CLASS_DEFAULT) {
		// skip non-word characters
		while (offset > 0) {
			offset = _PreviousInitialByte(offset);
			if (_CharClassification(offset) == CHAR_CLASS_DEFAULT)
				break;
		}
	}
	while (offset > 0) {
		// skip to start of word
		int32 previous = _PreviousInitialByte(offset);
		if (_CharClassification(previous) != CHAR_CLASS_DEFAULT)
			break;
		offset = previous;
	}

	return offset;
}


/**
 * @brief Returns the byte offset of the end of the word following @a offset.
 *
 * Skips over non-word characters first, then walks forward through word
 * characters to find the end of the word.
 *
 * @param offset The starting byte offset.
 * @return The offset one past the last character of the current or next word.
 */
int32
BTextView::_NextWordEnd(int32 offset)
{
	int32 textLen = fText->Length();
	if (_CharClassification(offset) != CHAR_CLASS_DEFAULT) {
		// skip non-word characters
		while (offset < textLen) {
			offset = _NextInitialByte(offset);
			if (_CharClassification(offset) == CHAR_CLASS_DEFAULT)
				break;
		}
	}
	while (offset < textLen) {
		// skip to end of word
		offset = _NextInitialByte(offset);
		if (_CharClassification(offset) != CHAR_CLASS_DEFAULT)
			break;
	}

	return offset;
}


/**
 * @brief Returns the pixel width of the styled characters in the given range, expanding tabs.
 *
 * Iterates through the range looking for tab characters. Between tabs, calls
 * _StyledWidth(); for each tab, computes the actual tab width via _ActualTabWidth().
 *
 * @param offset   The byte offset at which to begin measurement.
 * @param length   The number of bytes to measure.
 * @param _ascent  If non-NULL, receives the maximum ascent across all runs.
 * @param _descent If non-NULL, receives the maximum descent across all runs.
 * @return The total pixel width of the range including expanded tab stops.
 */
float
BTextView::_TabExpandedStyledWidth(int32 offset, int32 length, float* _ascent,
	float* _descent) const
{
	float ascent = 0.0;
	float descent = 0.0;
	float maxAscent = 0.0;
	float maxDescent = 0.0;

	float width = 0.0;
	int32 numBytes = length;
	bool foundTab = false;
	do {
		foundTab = fText->FindChar(B_TAB, offset, &numBytes);
		width += _StyledWidth(offset, numBytes, &ascent, &descent);

		if (maxAscent < ascent)
			maxAscent = ascent;
		if (maxDescent < descent)
			maxDescent = descent;

		if (foundTab) {
			width += _ActualTabWidth(width);
			numBytes++;
		}

		offset += numBytes;
		length -= numBytes;
		numBytes = length;
	} while (foundTab && length > 0);

	if (_ascent != NULL)
		*_ascent = maxAscent;
	if (_descent != NULL)
		*_descent = maxDescent;

	return width;
}


/**
 * @brief Returns the pixel width of the styled text in the given byte range (tabs not expanded).
 *
 * Iterates through style runs using the glyph-width cache when available.
 * If @a length is zero, the font metrics are still queried for the character
 * at @a fromOffset to allow ascent/descent to be returned.
 *
 * @param fromOffset The byte offset at which to begin measurement.
 * @param length     The number of bytes to measure.
 * @param _ascent    If non-NULL, receives the maximum ascent across all runs.
 * @param _descent   If non-NULL, receives the maximum descent across all runs.
 * @return The total pixel width of the range, or 0.0 if @a length is zero.
 */
float
BTextView::_StyledWidth(int32 fromOffset, int32 length, float* _ascent,
	float* _descent) const
{
	if (length == 0) {
		// determine height of char at given offset, but return empty width
		fStyles->Iterate(fromOffset, 1, fInline, NULL, NULL, _ascent,
			_descent);
		return 0.0;
	}

	float result = 0.0;
	float ascent = 0.0;
	float descent = 0.0;
	float maxAscent = 0.0;
	float maxDescent = 0.0;

	// iterate through the style runs
	const BFont* font = NULL;
	int32 numBytes;
	while ((numBytes = fStyles->Iterate(fromOffset, length, fInline, &font,
			NULL, &ascent, &descent)) != 0) {
		maxAscent = std::max(ascent, maxAscent);
		maxDescent = std::max(descent, maxDescent);

#if USE_WIDTHBUFFER
		// Use _BWidthBuffer_ if possible
		if (BPrivate::gWidthBuffer != NULL) {
			result += BPrivate::gWidthBuffer->StringWidth(*fText, fromOffset,
				numBytes, font);
		} else {
#endif
			const char* text = fText->GetString(fromOffset, &numBytes);
			result += font->StringWidth(text, numBytes);

#if USE_WIDTHBUFFER
		}
#endif

		fromOffset += numBytes;
		length -= numBytes;
	}

	if (_ascent != NULL)
		*_ascent = maxAscent;
	if (_descent != NULL)
		*_descent = maxDescent;

	return result;
}


/**
 * @brief Calculates the pixel width of a tab stop at the given horizontal position.
 *
 * The tab advances to the next multiple of fTabWidth beyond @a location.
 * If the result rounds to zero, the full fTabWidth is returned instead.
 *
 * @param location The current pen x-position relative to the text rect left edge.
 * @return The width in pixels that the tab character occupies at @a location.
 */
float
BTextView::_ActualTabWidth(float location) const
{
	float tabWidth = fTabWidth - fmod(location, fTabWidth);
	if (round(tabWidth) == 0)
		tabWidth = fTabWidth;

	return tabWidth;
}


/**
 * @brief Internal helper that inserts text after cancelling any IME session and enforcing the byte limit.
 *
 * Collapses any existing selection, clamps the offset, delegates to InsertText(),
 * then triggers a _Refresh() on the affected range.
 *
 * @param text   The text to insert.
 * @param length The number of bytes from @a text to insert.
 * @param offset The byte offset at which to insert.
 * @param runs   Optional run array for styling; may be NULL.
 */
void
BTextView::_DoInsertText(const char* text, int32 length, int32 offset,
	const text_run_array* runs)
{
	_CancelInputMethod();

	if (fText->Length() + length > MaxBytes())
		return;

	if (fSelStart != fSelEnd)
		Select(fSelStart, fSelStart);

	const int32 textLength = fText->Length();
	if (offset > textLength)
		offset = textLength;

	// copy data into buffer
	InsertText(text, length, offset, runs);

	// recalc line breaks and draw the text
	_Refresh(offset, offset + length);
}


/**
 * @brief Internal helper stub for delete operations (currently unused).
 *
 * @param fromOffset Start of the range to delete.
 * @param toOffset   End of the range to delete.
 */
void
BTextView::_DoDeleteText(int32 fromOffset, int32 toOffset)
{
	CALLED();
}


/**
 * @brief Renders a single line of text to @a view, expanding tabs and highlighting IME regions.
 *
 * Moves the pen to the correct position, optionally fills the erase rect,
 * then iterates through style runs drawing each segment. Tab characters are
 * expanded to the nearest tab stop. IME inline-input ranges are highlighted
 * in blue (active) and red (selected within IME).
 *
 * @param view        The BView (or offscreen buffer child) to draw into.
 * @param lineNum     The zero-based line number to draw.
 * @param startOffset If >=0, the byte offset from which drawing begins within the line;
 *                    -1 means draw the entire line.
 * @param erase       If true, the line's background is filled before drawing.
 * @param eraseRect   The bounding rect used for erasing; modified by this function.
 * @param inputRegion The BRegion covering the current IME inline-input text.
 */
void
BTextView::_DrawLine(BView* view, const int32 &lineNum,
	const int32 &startOffset, const bool &erase, BRect &eraseRect,
	BRegion &inputRegion)
{
	STELine* line = (*fLines)[lineNum];
	float startLeft = fTextRect.left;
	if (startOffset != -1) {
		if (ByteAt(startOffset) == B_ENTER) {
			// StartOffset is a newline
			startLeft = PointAt(line->offset).x;
		} else
			startLeft = PointAt(startOffset).x;
	} else if (fAlignment != B_ALIGN_LEFT) {
		float alignmentOffset = fTextRect.Width() + 1 - LineWidth(lineNum);
		if (fAlignment == B_ALIGN_CENTER)
			alignmentOffset = floorf(alignmentOffset / 2);
		startLeft += alignmentOffset;
	}

	int32 length = (line + 1)->offset;
	if (startOffset != -1)
		length -= startOffset;
	else
		length -= line->offset;

	// DrawString() chokes if you draw a newline
	if (ByteAt((line + 1)->offset - 1) == B_ENTER)
		length--;

	view->MovePenTo(startLeft,
		line->origin + line->ascent + fTextRect.top + 1);

	if (erase) {
		eraseRect.top = line->origin + fTextRect.top;
		eraseRect.bottom = (line + 1)->origin + fTextRect.top;
		view->FillRect(eraseRect, B_SOLID_LOW);
	}

	// do we have any text to draw?
	if (length <= 0)
		return;

	bool foundTab = false;
	int32 tabChars = 0;
	int32 numTabs = 0;
	int32 offset = startOffset != -1 ? startOffset : line->offset;
	const BFont* font = NULL;
	const rgb_color* color = NULL;
	int32 numBytes;
	drawing_mode defaultTextRenderingMode = DrawingMode();
	// iterate through each style on this line
	while ((numBytes = fStyles->Iterate(offset, length, fInline, &font,
			&color)) != 0) {
		view->SetFont(font);
		view->SetHighColor(*color);

		tabChars = std::min(numBytes, length);
		do {
			foundTab = fText->FindChar(B_TAB, offset, &tabChars);
			if (foundTab) {
				do {
					numTabs++;
					if (ByteAt(offset + tabChars + numTabs) != B_TAB)
						break;
				} while ((tabChars + numTabs) < numBytes);
			}

			drawing_mode textRenderingMode = defaultTextRenderingMode;

			if (inputRegion.CountRects() > 0
				&& ((offset <= fInline->Offset()
					&& fInline->Offset() < offset + tabChars)
				|| (fInline->Offset() <= offset
					&& offset < fInline->Offset() + fInline->Length()))) {

				textRenderingMode = B_OP_OVER;

				BRegion textRegion;
				GetTextRegion(offset, offset + length, &textRegion);

				textRegion.IntersectWith(&inputRegion);
				view->PushState();

				// Highlight in blue the inputted text
				view->SetHighColor(kBlueInputColor);
				view->FillRect(textRegion.Frame());

				// Highlight in red the selected part
				if (fInline->SelectionLength() > 0) {
					BRegion selectedRegion;
					GetTextRegion(fInline->Offset()
						+ fInline->SelectionOffset(), fInline->Offset()
						+ fInline->SelectionOffset()
						+ fInline->SelectionLength(), &selectedRegion);

					textRegion.IntersectWith(&selectedRegion);

					view->SetHighColor(kRedInputColor);
					view->FillRect(textRegion.Frame());
				}

				view->PopState();
			}

			int32 size = tabChars;
			const char* stringToDraw = fText->GetString(offset, &size);
			view->SetDrawingMode(textRenderingMode);
			view->DrawString(stringToDraw, size);

			if (foundTab) {
				float penPos = PenLocation().x - fTextRect.left;
				switch (fAlignment) {
					default:
					case B_ALIGN_LEFT:
						// nothing more to do
						break;

					case B_ALIGN_RIGHT:
						// subtract distance from left to line
						penPos -= fTextRect.Width() - LineWidth(lineNum);
						break;

					case B_ALIGN_CENTER:
						// subtract half distance from left to line
						penPos -= floorf((fTextRect.Width() + 1
							- LineWidth(lineNum)) / 2);
						break;
				}
				float tabWidth = _ActualTabWidth(penPos);

				// add in the rest of the tabs (if there are any)
				tabWidth += ((numTabs - 1) * fTabWidth);

				// move pen by tab(s) width
				view->MovePenBy(tabWidth, 0.0);
				tabChars += numTabs;
			}

			offset += tabChars;
			length -= tabChars;
			numBytes -= tabChars;
			tabChars = std::min(numBytes, length);
			numTabs = 0;
		} while (foundTab && tabChars > 0);
	}
}


/**
 * @brief Draws the range of lines from @a startLine to @a endLine.
 *
 * Sets up clipping, optionally uses the offscreen bitmap for double-buffered
 * drawing, iterates each line via _DrawLine(), and then paints the caret or
 * selection highlight.
 *
 * @param startLine   First line to draw (zero-based).
 * @param endLine     Last line to draw (zero-based, inclusive).
 * @param startOffset If >=0, only the portion of the first line from this byte offset
 *                    onwards is drawn; -1 draws the entire first line.
 * @param erase       If true, line backgrounds are cleared before drawing the text.
 */
void
BTextView::_DrawLines(int32 startLine, int32 endLine, int32 startOffset,
	bool erase)
{
	if (!Window())
		return;

	const BRect bounds(Bounds());

	// clip the text extending to end of selection
	BRect clipRect(fTextRect);
	clipRect.left = std::min(fTextRect.left,
		bounds.left + fLayoutData->leftInset);
	clipRect.right = std::max(fTextRect.right,
		bounds.right - fLayoutData->rightInset);
	clipRect = bounds & clipRect;

	BRegion newClip;
	newClip.Set(clipRect);
	ConstrainClippingRegion(&newClip);

	// set the low color to the view color so that
	// drawing to a non-white background will work
	SetLowColor(ViewColor());

	BView* view = NULL;
	if (fOffscreen == NULL)
		view = this;
	else {
		fOffscreen->Lock();
		view = fOffscreen->ChildAt(0);
		view->SetLowColor(ViewColor());
		view->FillRect(view->Bounds(), B_SOLID_LOW);
	}

	long maxLine = fLines->NumLines() - 1;
	if (startLine < 0)
		startLine = 0;
	if (endLine > maxLine)
		endLine = maxLine;

	// TODO: See if we can avoid this
	if (fAlignment != B_ALIGN_LEFT)
		erase = true;

	BRect eraseRect = clipRect;
	int32 startEraseLine = startLine;
	STELine* line = (*fLines)[startLine];

	if (erase && startOffset != -1 && fAlignment == B_ALIGN_LEFT) {
		// erase only to the right of startOffset
		startEraseLine++;
		int32 startErase = startOffset;

		BPoint erasePoint = PointAt(startErase);
		eraseRect.left = erasePoint.x;
		eraseRect.top = erasePoint.y;
		eraseRect.bottom = (line + 1)->origin + fTextRect.top;

		view->FillRect(eraseRect, B_SOLID_LOW);

		eraseRect = clipRect;
	}

	BRegion inputRegion;
	if (fInline != NULL && fInline->IsActive()) {
		GetTextRegion(fInline->Offset(), fInline->Offset() + fInline->Length(),
			&inputRegion);
	}

	//BPoint leftTop(startLeft, line->origin);
	for (int32 lineNum = startLine; lineNum <= endLine; lineNum++) {
		const bool eraseThisLine = erase && lineNum >= startEraseLine;
		_DrawLine(view, lineNum, startOffset, eraseThisLine, eraseRect,
			inputRegion);
		startOffset = -1;
			// Set this to -1 so the next iteration will use the line offset
	}

	// draw the caret/hilite the selection
	if (fActive) {
		if (fSelStart != fSelEnd) {
			if (fSelectable)
				Highlight(fSelStart, fSelEnd);
		} else {
			if (fCaretVisible)
				_DrawCaret(fSelStart, true);
		}
	}

	if (fOffscreen != NULL) {
		view->Sync();
		/*BPoint penLocation = view->PenLocation();
		BRect drawRect(leftTop.x, leftTop.y, penLocation.x, penLocation.y);
		DrawBitmap(fOffscreen, drawRect, drawRect);*/
		fOffscreen->Unlock();
	}

	ConstrainClippingRegion(NULL);
}


/**
 * @brief Invalidates the view rectangle covering the given line range, triggering a Draw() call.
 *
 * @param startLine First line to invalidate (zero-based).
 * @param endLine   Last line to invalidate (zero-based, inclusive).
 */
void
BTextView::_RequestDrawLines(int32 startLine, int32 endLine)
{
	if (!Window())
		return;

	long maxLine = fLines->NumLines() - 1;

	STELine* from = (*fLines)[startLine];
	STELine* to = endLine == maxLine ? NULL : (*fLines)[endLine + 1];
	BRect invalidRect(Bounds().left, from->origin + fTextRect.top,
		Bounds().right,
		to != NULL ? to->origin + fTextRect.top : fTextRect.bottom);
	Invalidate(invalidRect);
}


/**
 * @brief Draws or erases the insertion caret at the given byte offset.
 *
 * @param offset  The byte offset whose position determines the caret location.
 * @param visible If true, the caret rect is inverted (drawn); if false, it is invalidated (erased).
 */
void
BTextView::_DrawCaret(int32 offset, bool visible)
{
	float lineHeight;
	BPoint caretPoint = PointAt(offset, &lineHeight);
	caretPoint.x = std::min(caretPoint.x, fTextRect.right);

	BRect caretRect;
	caretRect.left = caretRect.right = caretPoint.x;
	caretRect.top = caretPoint.y;
	caretRect.bottom = caretPoint.y + lineHeight - 1;

	if (visible)
		InvertRect(caretRect);
	else
		Invalidate(caretRect);
}


/**
 * @brief Makes the insertion caret visible if it is currently hidden.
 *
 * Only acts when the view is active, editable, and the selection is collapsed.
 */
inline void
BTextView::_ShowCaret()
{
	if (fActive && !fCaretVisible && fEditable && fSelStart == fSelEnd)
		_InvertCaret();
}


/**
 * @brief Hides the insertion caret if it is currently visible.
 *
 * Only acts when the selection is collapsed (caret position).
 */
inline void
BTextView::_HideCaret()
{
	if (fCaretVisible && fSelStart == fSelEnd)
		_InvertCaret();
}


/**
 * @brief Toggles the caret visibility: hides it if shown, shows it if hidden.
 *
 * Inverts the fCaretVisible flag, redraws the caret via _DrawCaret(), and
 * records the current time so the blink timer resets correctly.
 */
void
BTextView::_InvertCaret()
{
	fCaretVisible = !fCaretVisible;
	_DrawCaret(fSelStart, fCaretVisible);
	fCaretTime = system_time();
}


/**
 * @brief Moves the drag-and-drop insertion caret to the given byte offset.
 *
 * Erases the previous drag caret position and draws a new one. If @a offset
 * falls within the active selection the drag caret is hidden and -1 is stored.
 *
 * @param offset The byte offset where the caret should appear, or -1 to hide it.
 */
void
BTextView::_DragCaret(int32 offset)
{
	// does the caret need to move?
	if (offset == fDragOffset)
		return;

	// hide the previous drag caret
	if (fDragOffset != -1)
		_DrawCaret(fDragOffset, false);

	// do we have a new location?
	if (offset != -1) {
		if (fActive) {
			// ignore if offset is within active selection
			if (offset >= fSelStart && offset <= fSelEnd) {
				fDragOffset = -1;
				return;
			}
		}

		_DrawCaret(offset, true);
	}

	fDragOffset = offset;
}


/**
 * @brief Destroys the TextTrackState, stopping mouse-tracking and the auto-scroll pulse.
 */
void
BTextView::_StopMouseTracking()
{
	delete fTrackingMouse;
	fTrackingMouse = NULL;
}


/**
 * @brief Finalises a mouse-up event: collapses a drag-detect selection or stops tracking.
 *
 * @param where The position of the mouse release in view coordinates.
 * @return true if a tracking session was active and was ended, false otherwise.
 */
bool
BTextView::_PerformMouseUp(BPoint where)
{
	if (fTrackingMouse == NULL)
		return false;

	if (fTrackingMouse->selectionRect.IsValid())
		Select(fTrackingMouse->clickOffset, fTrackingMouse->clickOffset);

	_StopMouseTracking();
	// adjust cursor if necessary
	_TrackMouse(where, NULL, true);

	return true;
}


/**
 * @brief Handles mouse movement during an active tracking session.
 *
 * If the motion exceeds the drag threshold, initiates a drag. Otherwise
 * extends or shrinks the selection by character, word (double-click), or
 * line (triple-click) according to the click count.
 *
 * @param where The current mouse position in view coordinates.
 * @param code  The movement code (B_INSIDE_VIEW, etc.).
 * @return true if the event was consumed by the tracking session.
 */
bool
BTextView::_PerformMouseMoved(BPoint where, uint32 code)
{
	fWhere = where;

	if (fTrackingMouse == NULL)
		return false;

	int32 currentOffset = OffsetAt(where);
	if (fTrackingMouse->selectionRect.IsValid()) {
		// we are tracking the mouse for drag action, if the mouse has moved
		// to another index or more than three pixels from where it was clicked,
		// we initiate a drag now:
		if (currentOffset != fTrackingMouse->clickOffset
			|| fabs(fTrackingMouse->where.x - where.x) > 3
			|| fabs(fTrackingMouse->where.y - where.y) > 3) {
			_StopMouseTracking();
			_InitiateDrag();
			return true;
		}
		return false;
	}

	switch (fClickCount) {
		case 3:
			// triple click, extend selection linewise
			if (currentOffset <= fTrackingMouse->anchor) {
				fTrackingMouse->selStart
					= (*fLines)[_LineAt(currentOffset)]->offset;
				fTrackingMouse->selEnd = fTrackingMouse->shiftDown
					? fSelEnd
					: (*fLines)[_LineAt(fTrackingMouse->anchor) + 1]->offset;
			} else {
				fTrackingMouse->selStart
					= fTrackingMouse->shiftDown
						? fSelStart
						: (*fLines)[_LineAt(fTrackingMouse->anchor)]->offset;
				fTrackingMouse->selEnd
					= (*fLines)[_LineAt(currentOffset) + 1]->offset;
			}
			break;

		case 2:
			// double click, extend selection wordwise
			if (currentOffset <= fTrackingMouse->anchor) {
				fTrackingMouse->selStart = _PreviousWordBoundary(currentOffset);
				fTrackingMouse->selEnd
					= fTrackingMouse->shiftDown
						? fSelEnd
						: _NextWordBoundary(fTrackingMouse->anchor);
			} else {
				fTrackingMouse->selStart
					= fTrackingMouse->shiftDown
						? fSelStart
						: _PreviousWordBoundary(fTrackingMouse->anchor);
				fTrackingMouse->selEnd = _NextWordBoundary(currentOffset);
			}
			break;

		default:
			// new click, extend selection char by char
			if (currentOffset <= fTrackingMouse->anchor) {
				fTrackingMouse->selStart = currentOffset;
				fTrackingMouse->selEnd
					= fTrackingMouse->shiftDown
						? fSelEnd : fTrackingMouse->anchor;
			} else {
				fTrackingMouse->selStart
					= fTrackingMouse->shiftDown
						? fSelStart : fTrackingMouse->anchor;
				fTrackingMouse->selEnd = currentOffset;
			}
			break;
	}

	// position caret to follow the direction of the selection
	if (fTrackingMouse->selEnd != fSelEnd)
		fCaretOffset = fTrackingMouse->selEnd;
	else if (fTrackingMouse->selStart != fSelStart)
		fCaretOffset = fTrackingMouse->selStart;

	Select(fTrackingMouse->selStart, fTrackingMouse->selEnd);
	_TrackMouse(where, NULL);

	return true;
}


/**
 * @brief Updates the view cursor and drag caret as the mouse moves over the view.
 *
 * Sets the cursor to I-beam when over selectable/editable text, shows the
 * drag caret when a drop is possible, and restores the default cursor otherwise.
 *
 * @param where   The current mouse position in view coordinates.
 * @param message The drag message if a drag-and-drop is in progress, or NULL.
 * @param force   Passed as the second argument to SetViewCursor() to force the update.
 */
void
BTextView::_TrackMouse(BPoint where, const BMessage* message, bool force)
{
	BRegion textRegion;
	GetTextRegion(fSelStart, fSelEnd, &textRegion);

	if (message && AcceptsDrop(message))
		_TrackDrag(where);
	else if ((fSelectable || fEditable)
		&& (fTrackingMouse != NULL || !textRegion.Contains(where))) {
		SetViewCursor(B_CURSOR_I_BEAM, force);
	} else
		SetViewCursor(B_CURSOR_SYSTEM_DEFAULT, force);
}


/**
 * @brief Updates the drag caret to show where dropped text would land.
 *
 * If @a where is within the view bounds, moves the drag caret to the character
 * offset nearest to @a where via _DragCaret().
 *
 * @param where The current drag position in view coordinates.
 */
void
BTextView::_TrackDrag(BPoint where)
{
	CALLED();
	if (Bounds().Contains(where))
		_DragCaret(OffsetAt(where));
}


/**
 * @brief Begins a drag-and-drop operation for the current selection.
 *
 * Calls GetDragParameters() to populate the drag message, then starts the
 * drag with either a bitmap or the selection's bounding rect. Also starts a
 * recurring _DISPOSE_DRAG_ message runner so the drag can be cancelled if
 * dropped onto the originating view.
 */
void
BTextView::_InitiateDrag()
{
	BMessage dragMessage(B_MIME_DATA);
	BBitmap* dragBitmap = NULL;
	BPoint bitmapPoint;
	BHandler* dragHandler = NULL;

	GetDragParameters(&dragMessage, &dragBitmap, &bitmapPoint, &dragHandler);
	SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);

	if (dragBitmap != NULL)
		DragMessage(&dragMessage, dragBitmap, bitmapPoint, dragHandler);
	else {
		BRegion region;
		GetTextRegion(fSelStart, fSelEnd, &region);
		BRect bounds = Bounds();
		BRect dragRect = region.Frame();
		if (!bounds.Contains(dragRect))
			dragRect = bounds & dragRect;

		DragMessage(&dragMessage, dragRect, dragHandler);
	}

	BMessenger messenger(this);
	BMessage message(_DISPOSE_DRAG_);
	fDragRunner = new (nothrow) BMessageRunner(messenger, &message, 100000);
}


/**
 * @brief Processes a drag-and-drop message that was dropped on the view.
 *
 * Filters disallowed characters, records a DropUndoBuffer, handles
 * internal moves (delete source then insert at new position), and
 * inserts the dropped text at the drop position. Selects the inserted
 * text if the view has focus.
 *
 * @param message The dropped BMessage containing "text/plain" data.
 * @param where   The drop position in view coordinates.
 * @param offset  The offset from the view origin to the drop point.
 * @return true if the drop was accepted and processed, false otherwise.
 */
bool
BTextView::_MessageDropped(BMessage* message, BPoint where, BPoint offset)
{
	ASSERT(message);

	void* from = NULL;
	bool internalDrop = false;
	if (message->FindPointer("be:originator", &from) == B_OK
			&& from == this && fSelEnd != fSelStart)
		internalDrop = true;

	_DragCaret(-1);

	delete fDragRunner;
	fDragRunner = NULL;

	_TrackMouse(where, NULL);

	// are we sure we like this message?
	if (!AcceptsDrop(message))
		return false;

	int32 dropOffset = OffsetAt(where);
	if (dropOffset > fText->Length())
		dropOffset = fText->Length();

	// if this view initiated the drag, move instead of copy
	if (internalDrop) {
		// dropping onto itself?
		if (dropOffset >= fSelStart && dropOffset <= fSelEnd)
			return true;
	}

	ssize_t dataLength = 0;
	const char* text = NULL;
	entry_ref ref;
	if (message->FindData("text/plain", B_MIME_TYPE, (const void**)&text,
			&dataLength) == B_OK) {
		text_run_array* runArray = NULL;
		ssize_t runLength = 0;
		if (fStylable) {
			message->FindData("application/x-vnd.Be-text_run_array",
				B_MIME_TYPE, (const void**)&runArray, &runLength);
		}

		_FilterDisallowedChars((char*)text, dataLength, runArray);

		if (dataLength < 1) {
			beep();
			return true;
		}

		if (fUndo) {
			delete fUndo;
			fUndo = new DropUndoBuffer(this, text, dataLength, runArray,
				runLength, dropOffset, internalDrop);
		}

		if (internalDrop) {
			if (dropOffset > fSelEnd)
				dropOffset -= dataLength;
			Delete();
		}

		Insert(dropOffset, text, dataLength, runArray);
		if (IsFocus())
			Select(dropOffset, dropOffset + dataLength);
	}

	return true;
}


/**
 * @brief Scrolls the view proportionally when the mouse cursor is outside the view bounds.
 *
 * Called periodically by the _PING_ message runner during mouse tracking.
 * Computes a scroll delta proportional to how far outside the view the cursor
 * is and applies it via _ScrollBy().
 */
void
BTextView::_PerformAutoScrolling()
{
	// Scroll the view a bit if mouse is outside the view bounds
	BRect bounds = Bounds();
	BPoint scrollBy(B_ORIGIN);

	// R5 does a pretty soft auto-scroll, we try to do the same by
	// simply scrolling the distance between cursor and border
	if (fWhere.x > bounds.right)
		scrollBy.x = fWhere.x - bounds.right;
	else if (fWhere.x < bounds.left)
		scrollBy.x = fWhere.x - bounds.left; // negative value

	// prevent horizontal scrolling if text rect is inside view rect
	if (fTextRect.left > bounds.left && fTextRect.right < bounds.right)
		scrollBy.x = 0;

	if (CountLines() > 1) {
		// scroll in Y only if multiple lines!
		if (fWhere.y > bounds.bottom)
			scrollBy.y = fWhere.y - bounds.bottom;
		else if (fWhere.y < bounds.top)
			scrollBy.y = fWhere.y - bounds.top; // negative value
	}

	// prevent vertical scrolling if text rect is inside view rect
	if (fTextRect.top > bounds.top && fTextRect.bottom < bounds.bottom)
		scrollBy.y = 0;

	if (scrollBy != B_ORIGIN)
		_ScrollBy(scrollBy.x, scrollBy.y);
}


/**
 * @brief Synchronises the horizontal and vertical scroll bars with the current text dimensions.
 *
 * Adjusts range, proportion, and step sizes of any scroll bars attached via
 * ScrollBar(). The proportion is set so the scroll bar thumb reflects how
 * much of the total content is currently visible.
 */
void
BTextView::_UpdateScrollbars()
{
	BRect bounds(Bounds());
	BScrollBar* horizontalScrollBar = ScrollBar(B_HORIZONTAL);
 	BScrollBar* verticalScrollBar = ScrollBar(B_VERTICAL);

	// do we have a horizontal scroll bar?
	if (horizontalScrollBar != NULL) {
		long viewWidth = bounds.IntegerWidth();
		long dataWidth = (long)ceilf(_TextWidth());

		long maxRange = dataWidth - viewWidth;
		maxRange = std::max(maxRange, 0l);

		horizontalScrollBar->SetRange(0, (float)maxRange);
		horizontalScrollBar->SetProportion((float)viewWidth
			/ (float)dataWidth);
		horizontalScrollBar->SetSteps(kHorizontalScrollBarStep,
			dataWidth / 10);
	}

	// how about a vertical scroll bar?
	if (verticalScrollBar != NULL) {
		long viewHeight = bounds.IntegerHeight();
		long dataHeight = (long)ceilf(_TextHeight());

		long maxRange = dataHeight - viewHeight;
		maxRange = std::max(maxRange, 0l);

		verticalScrollBar->SetRange(0, maxRange);
		verticalScrollBar->SetProportion((float)viewHeight
			/ (float)dataHeight);
		verticalScrollBar->SetSteps(kVerticalScrollBarStep,
			viewHeight);
	}
}


/**
 * @brief Scrolls the view by the given horizontal and vertical deltas.
 *
 * Adds the deltas to the current scroll position and delegates to _ScrollTo(),
 * which clamps the result to the valid scroll range.
 *
 * @param horizontal Pixels to scroll horizontally (positive = right).
 * @param vertical   Pixels to scroll vertically (positive = down).
 */
void
BTextView::_ScrollBy(float horizontal, float vertical)
{
	BRect bounds = Bounds();
	_ScrollTo(bounds.left + horizontal, bounds.top + vertical);
}


/**
 * @brief Scrolls the view to an absolute position, clamped to the valid scroll range.
 *
 * Calculates the scroll limits from the text rect and layout insets, then
 * calls BView::ScrollTo() with the clamped coordinates.
 *
 * @param x The target horizontal scroll position.
 * @param y The target vertical scroll position.
 */
void
BTextView::_ScrollTo(float x, float y)
{
	BRect bounds = Bounds();
	long viewWidth = bounds.IntegerWidth();
	long viewHeight = bounds.IntegerHeight();

	float minWidth = fTextRect.left - fLayoutData->leftInset;
	float maxWidth = fTextRect.right + fLayoutData->rightInset - viewWidth;
	float minHeight = fTextRect.top - fLayoutData->topInset;
	float maxHeight = fTextRect.bottom + fLayoutData->bottomInset - viewHeight;

	// set horizontal scroll limits
	if (x > maxWidth)
		x = maxWidth;
	if (x < minWidth)
		x = minWidth;

	// set vertical scroll limits
	if (y > maxHeight)
		y = maxHeight;
	if (y < minHeight)
		y = minHeight;

	ScrollTo(x, y);
}


/**
 * @brief Resizes the container view to fit the current text width (used in Tracker rename mode).
 *
 * Moves the container view to maintain the alignment, then resizes it by the
 * difference between the old and new text widths. Repositions the text rect
 * to the top-left inset and scrolls to offset 0.
 *
 * @param redraw If true, requests a redraw of the first line after resizing.
 */
void
BTextView::_AutoResize(bool redraw)
{
	if (!fResizable || fContainerView == NULL)
		return;

	// NOTE: This container view thing is only used by Tracker.
	// move container view if not left aligned
	float oldWidth = Bounds().Width();
	float newWidth = _TextWidth();
	float right = oldWidth - newWidth;

	if (fAlignment == B_ALIGN_CENTER)
		fContainerView->MoveBy(roundf(right / 2), 0);
	else if (fAlignment == B_ALIGN_RIGHT)
		fContainerView->MoveBy(right, 0);

	// resize container view
	float grow = newWidth - oldWidth;
	fContainerView->ResizeBy(grow, 0);

	// reposition text view
	fTextRect.OffsetTo(fLayoutData->leftInset, fLayoutData->topInset);

	// scroll rect to start, there is room for full text
	ScrollToOffset(0);

	if (redraw)
		_RequestDrawLines(0, 0);
}


/**
 * @brief Creates a new offscreen BBitmap with an embedded BView for double-buffered drawing.
 *
 * Deletes any existing offscreen bitmap first. The bitmap is only actually
 * created when USE_DOUBLEBUFFERING is set to 1; otherwise this is a no-op.
 *
 * @param padding Additional horizontal padding to add to the bitmap width.
 */
void
BTextView::_NewOffscreen(float padding)
{
	if (fOffscreen != NULL)
		_DeleteOffscreen();

#if USE_DOUBLEBUFFERING
	BRect bitmapRect(0, 0, fTextRect.Width() + padding, fTextRect.Height());
	fOffscreen = new BBitmap(bitmapRect, fColorSpace, true, false);
	if (fOffscreen != NULL && fOffscreen->Lock()) {
		BView* bufferView = new BView(bitmapRect, "drawing view", 0, 0);
		fOffscreen->AddChild(bufferView);
		fOffscreen->Unlock();
	}
#endif
}


/**
 * @brief Destroys the offscreen BBitmap if one exists, locking it first.
 */
void
BTextView::_DeleteOffscreen()
{
	if (fOffscreen != NULL && fOffscreen->Lock()) {
		delete fOffscreen;
		fOffscreen = NULL;
	}
}


/**
 * @brief Activates the view: shows the selection highlight, caret, and registers keyboard shortcuts.
 *
 * Creates a new offscreen bitmap, highlights any existing selection or shows
 * the caret, updates the cursor, and installs word-wise, line-wise, and
 * document-wise navigation/selection/deletion shortcuts on the window.
 */
void
BTextView::_Activate()
{
	fActive = true;

	// Create a new offscreen BBitmap
	_NewOffscreen();

	if (fSelStart != fSelEnd) {
		if (fSelectable)
			Highlight(fSelStart, fSelEnd);
	} else
		_ShowCaret();

	BPoint where;
	uint32 buttons;
	GetMouse(&where, &buttons, false);
	if (Bounds().Contains(where))
		_TrackMouse(where, NULL);

	if (Window() != NULL) {
		BMessage* message;

		if (!Window()->HasShortcut(B_LEFT_ARROW, B_COMMAND_KEY)
			&& !Window()->HasShortcut(B_RIGHT_ARROW, B_COMMAND_KEY)) {
			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_LEFT_ARROW);
			message->AddInt32("modifiers", B_COMMAND_KEY);
			Window()->AddShortcut(B_LEFT_ARROW, B_COMMAND_KEY, message, this);

			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_RIGHT_ARROW);
			message->AddInt32("modifiers", B_COMMAND_KEY);
			Window()->AddShortcut(B_RIGHT_ARROW, B_COMMAND_KEY, message, this);

			fInstalledNavigateCommandWordwiseShortcuts = true;
		}
		if (!Window()->HasShortcut(B_LEFT_ARROW, B_COMMAND_KEY | B_SHIFT_KEY)
			&& !Window()->HasShortcut(B_RIGHT_ARROW,
				B_COMMAND_KEY | B_SHIFT_KEY)) {
			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_LEFT_ARROW);
			message->AddInt32("modifiers", B_COMMAND_KEY | B_SHIFT_KEY);
			Window()->AddShortcut(B_LEFT_ARROW, B_COMMAND_KEY | B_SHIFT_KEY,
				message, this);

			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_RIGHT_ARROW);
			message->AddInt32("modifiers", B_COMMAND_KEY | B_SHIFT_KEY);
			Window()->AddShortcut(B_RIGHT_ARROW, B_COMMAND_KEY | B_SHIFT_KEY,
				message, this);

			fInstalledSelectCommandWordwiseShortcuts = true;
		}
		if (!Window()->HasShortcut(B_DELETE, B_COMMAND_KEY)
			&& !Window()->HasShortcut(B_BACKSPACE, B_COMMAND_KEY)) {
			message = new BMessage(kMsgRemoveWord);
			message->AddInt32("key", B_DELETE);
			message->AddInt32("modifiers", B_COMMAND_KEY);
			Window()->AddShortcut(B_DELETE, B_COMMAND_KEY, message, this);

			message = new BMessage(kMsgRemoveWord);
			message->AddInt32("key", B_BACKSPACE);
			message->AddInt32("modifiers", B_COMMAND_KEY);
			Window()->AddShortcut(B_BACKSPACE, B_COMMAND_KEY, message, this);

			fInstalledRemoveCommandWordwiseShortcuts = true;
		}

		if (!Window()->HasShortcut(B_LEFT_ARROW, B_OPTION_KEY)
			&& !Window()->HasShortcut(B_RIGHT_ARROW, B_OPTION_KEY)) {
			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_LEFT_ARROW);
			message->AddInt32("modifiers", B_OPTION_KEY);
			Window()->AddShortcut(B_LEFT_ARROW, B_OPTION_KEY, message, this);

			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_RIGHT_ARROW);
			message->AddInt32("modifiers", B_OPTION_KEY);
			Window()->AddShortcut(B_RIGHT_ARROW, B_OPTION_KEY, message, this);

			fInstalledNavigateOptionWordwiseShortcuts = true;
		}
		if (!Window()->HasShortcut(B_LEFT_ARROW, B_OPTION_KEY | B_SHIFT_KEY)
			&& !Window()->HasShortcut(B_RIGHT_ARROW,
				B_OPTION_KEY | B_SHIFT_KEY)) {
			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_LEFT_ARROW);
			message->AddInt32("modifiers", B_OPTION_KEY | B_SHIFT_KEY);
			Window()->AddShortcut(B_LEFT_ARROW, B_OPTION_KEY | B_SHIFT_KEY,
				message, this);

			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_RIGHT_ARROW);
			message->AddInt32("modifiers", B_OPTION_KEY | B_SHIFT_KEY);
			Window()->AddShortcut(B_RIGHT_ARROW, B_OPTION_KEY | B_SHIFT_KEY,
				message, this);

			fInstalledSelectOptionWordwiseShortcuts = true;
		}
		if (!Window()->HasShortcut(B_DELETE, B_OPTION_KEY)
			&& !Window()->HasShortcut(B_BACKSPACE, B_OPTION_KEY)) {
			message = new BMessage(kMsgRemoveWord);
			message->AddInt32("key", B_DELETE);
			message->AddInt32("modifiers", B_OPTION_KEY);
			Window()->AddShortcut(B_DELETE, B_OPTION_KEY, message, this);

			message = new BMessage(kMsgRemoveWord);
			message->AddInt32("key", B_BACKSPACE);
			message->AddInt32("modifiers", B_OPTION_KEY);
			Window()->AddShortcut(B_BACKSPACE, B_OPTION_KEY, message, this);

			fInstalledRemoveOptionWordwiseShortcuts = true;
		}

		if (!Window()->HasShortcut(B_UP_ARROW, B_OPTION_KEY)
			&& !Window()->HasShortcut(B_DOWN_ARROW, B_OPTION_KEY)) {
			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_UP_ARROW);
			message->AddInt32("modifiers", B_OPTION_KEY);
			Window()->AddShortcut(B_UP_ARROW, B_OPTION_KEY, message, this);

			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_DOWN_ARROW);
			message->AddInt32("modifiers", B_OPTION_KEY);
			Window()->AddShortcut(B_DOWN_ARROW, B_OPTION_KEY, message, this);

			fInstalledNavigateOptionLinewiseShortcuts = true;
		}
		if (!Window()->HasShortcut(B_UP_ARROW, B_OPTION_KEY | B_SHIFT_KEY)
			&& !Window()->HasShortcut(B_DOWN_ARROW,
				B_OPTION_KEY | B_SHIFT_KEY)) {
			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_UP_ARROW);
			message->AddInt32("modifiers", B_OPTION_KEY | B_SHIFT_KEY);
			Window()->AddShortcut(B_UP_ARROW, B_OPTION_KEY | B_SHIFT_KEY,
				message, this);

			message = new BMessage(kMsgNavigateArrow);
			message->AddInt32("key", B_DOWN_ARROW);
			message->AddInt32("modifiers", B_OPTION_KEY | B_SHIFT_KEY);
			Window()->AddShortcut(B_DOWN_ARROW, B_OPTION_KEY | B_SHIFT_KEY,
				message, this);

			fInstalledSelectOptionLinewiseShortcuts = true;
		}

		if (!Window()->HasShortcut(B_HOME, B_COMMAND_KEY)
			&& !Window()->HasShortcut(B_END, B_COMMAND_KEY)) {
			message = new BMessage(kMsgNavigatePage);
			message->AddInt32("key", B_HOME);
			message->AddInt32("modifiers", B_COMMAND_KEY);
			Window()->AddShortcut(B_HOME, B_COMMAND_KEY, message, this);

			message = new BMessage(kMsgNavigatePage);
			message->AddInt32("key", B_END);
			message->AddInt32("modifiers", B_COMMAND_KEY);
			Window()->AddShortcut(B_END, B_COMMAND_KEY, message, this);

			fInstalledNavigateHomeEndDocwiseShortcuts = true;
		}
		if (!Window()->HasShortcut(B_HOME, B_COMMAND_KEY | B_SHIFT_KEY)
			&& !Window()->HasShortcut(B_END, B_COMMAND_KEY | B_SHIFT_KEY)) {
			message = new BMessage(kMsgNavigatePage);
			message->AddInt32("key", B_HOME);
			message->AddInt32("modifiers", B_COMMAND_KEY | B_SHIFT_KEY);
			Window()->AddShortcut(B_HOME, B_COMMAND_KEY | B_SHIFT_KEY,
				message, this);

			message = new BMessage(kMsgNavigatePage);
			message->AddInt32("key", B_END);
			message->AddInt32("modifiers", B_COMMAND_KEY | B_SHIFT_KEY);
			Window()->AddShortcut(B_END, B_COMMAND_KEY | B_SHIFT_KEY,
				message, this);

			fInstalledSelectHomeEndDocwiseShortcuts = true;
		}
	}
}


/**
 * @brief Deactivates the view: removes the selection highlight, hides the caret, and unregisters keyboard shortcuts.
 *
 * Cancels any IME session, deletes the offscreen bitmap, and removes all
 * window shortcuts that were installed by _Activate().
 */
void
BTextView::_Deactivate()
{
	fActive = false;

	_CancelInputMethod();
	_DeleteOffscreen();

	if (fSelStart != fSelEnd) {
		if (fSelectable)
			Highlight(fSelStart, fSelEnd);
	} else
		_HideCaret();

	if (Window() != NULL) {
		if (fInstalledNavigateCommandWordwiseShortcuts) {
			Window()->RemoveShortcut(B_LEFT_ARROW, B_COMMAND_KEY);
			Window()->RemoveShortcut(B_RIGHT_ARROW, B_COMMAND_KEY);
			fInstalledNavigateCommandWordwiseShortcuts = false;
		}
		if (fInstalledSelectCommandWordwiseShortcuts) {
			Window()->RemoveShortcut(B_LEFT_ARROW, B_COMMAND_KEY | B_SHIFT_KEY);
			Window()->RemoveShortcut(B_RIGHT_ARROW,
				B_COMMAND_KEY | B_SHIFT_KEY);
			fInstalledSelectCommandWordwiseShortcuts = false;
		}
		if (fInstalledRemoveCommandWordwiseShortcuts) {
			Window()->RemoveShortcut(B_DELETE, B_COMMAND_KEY);
			Window()->RemoveShortcut(B_BACKSPACE, B_COMMAND_KEY);
			fInstalledRemoveCommandWordwiseShortcuts = false;
		}

		if (fInstalledNavigateOptionWordwiseShortcuts) {
			Window()->RemoveShortcut(B_LEFT_ARROW, B_OPTION_KEY);
			Window()->RemoveShortcut(B_RIGHT_ARROW, B_OPTION_KEY);
			fInstalledNavigateOptionWordwiseShortcuts = false;
		}
		if (fInstalledSelectOptionWordwiseShortcuts) {
			Window()->RemoveShortcut(B_LEFT_ARROW, B_OPTION_KEY | B_SHIFT_KEY);
			Window()->RemoveShortcut(B_RIGHT_ARROW, B_OPTION_KEY | B_SHIFT_KEY);
			fInstalledSelectOptionWordwiseShortcuts = false;
		}
		if (fInstalledRemoveOptionWordwiseShortcuts) {
			Window()->RemoveShortcut(B_DELETE, B_OPTION_KEY);
			Window()->RemoveShortcut(B_BACKSPACE, B_OPTION_KEY);
			fInstalledRemoveOptionWordwiseShortcuts = false;
		}

		if (fInstalledNavigateOptionLinewiseShortcuts) {
			Window()->RemoveShortcut(B_UP_ARROW, B_OPTION_KEY);
			Window()->RemoveShortcut(B_DOWN_ARROW, B_OPTION_KEY);
			fInstalledNavigateOptionLinewiseShortcuts = false;
		}
		if (fInstalledSelectOptionLinewiseShortcuts) {
			Window()->RemoveShortcut(B_UP_ARROW, B_OPTION_KEY | B_SHIFT_KEY);
			Window()->RemoveShortcut(B_DOWN_ARROW, B_OPTION_KEY | B_SHIFT_KEY);
			fInstalledSelectOptionLinewiseShortcuts = false;
		}

		if (fInstalledNavigateHomeEndDocwiseShortcuts) {
			Window()->RemoveShortcut(B_HOME, B_COMMAND_KEY);
			Window()->RemoveShortcut(B_END, B_COMMAND_KEY);
			fInstalledNavigateHomeEndDocwiseShortcuts = false;
		}
		if (fInstalledSelectHomeEndDocwiseShortcuts) {
			Window()->RemoveShortcut(B_HOME, B_COMMAND_KEY | B_SHIFT_KEY);
			Window()->RemoveShortcut(B_END, B_COMMAND_KEY | B_SHIFT_KEY);
			fInstalledSelectHomeEndDocwiseShortcuts = false;
		}
	}
}


/**
 * @brief Normalises a font so that it can be used for display in BTextView.
 *
 * Resets rotation to 0, clears all font flags, sets spacing to
 * B_BITMAP_SPACING, and sets the encoding to B_UNICODE_UTF8.
 *
 * @param font The font to normalise in place; ignored if NULL.
 */
void
BTextView::_NormalizeFont(BFont* font)
{
	if (font) {
		font->SetRotation(0.0f);
		font->SetFlags(0);
		font->SetSpacing(B_BITMAP_SPACING);
		font->SetEncoding(B_UNICODE_UTF8);
	}
}


/**
 * @brief Applies each run in @a runs to sub-ranges within [@a startOffset, @a endOffset).
 *
 * Iterates the run array and calls _ApplyStyleRange() for each run's sub-range,
 * then invalidates the null style.
 *
 * @param startOffset First byte of the styled region.
 * @param endOffset   One past the last byte of the styled region.
 * @param runs        The run array to apply; must contain at least one entry.
 */
void
BTextView::_SetRunArray(int32 startOffset, int32 endOffset,
	const text_run_array* runs)
{
	const int32 numStyles = runs->count;
	if (numStyles > 0) {
		const text_run* theRun = &runs->runs[0];
		for (int32 index = 0; index < numStyles; index++) {
			int32 fromOffset = theRun->offset + startOffset;
			int32 toOffset = endOffset;
			if (index + 1 < numStyles) {
				toOffset = (theRun + 1)->offset + startOffset;
				toOffset = (toOffset > endOffset) ? endOffset : toOffset;
			}

			_ApplyStyleRange(fromOffset, toOffset, B_FONT_ALL, &theRun->font,
				&theRun->color, false);

			theRun++;
		}
		fStyles->InvalidateNullStyle();
	}
}


/**
 * @brief Returns the character class of the character at the given byte offset.
 *
 * Maps the character to one of the CHAR_CLASS_* constants for use in
 * word-break and line-break decisions.
 *
 * @param offset The byte offset of the character to classify.
 * @return One of the CHAR_CLASS_* enum values.
 */
uint32
BTextView::_CharClassification(int32 offset) const
{
	// TODO: Should check against a list of characters containing also
	// japanese word breakers.
	// And what about other languages ? Isn't there a better way to check
	// for separator characters ?
	// Andrew suggested to have a look at UnicodeBlockObject.h
	switch (fText->RealCharAt(offset)) {
		case '\0':
			return CHAR_CLASS_END_OF_TEXT;

		case B_SPACE:
		case B_TAB:
		case B_ENTER:
			return CHAR_CLASS_WHITESPACE;

		case '=':
		case '+':
		case '@':
		case '#':
		case '$':
		case '%':
		case '^':
		case '&':
		case '*':
		case '\\':
		case '|':
		case '<':
		case '>':
		case '/':
		case '~':
			return CHAR_CLASS_GRAPHICAL;

		case '\'':
		case '"':
			return CHAR_CLASS_QUOTE;

		case ',':
		case '.':
		case '?':
		case '!':
		case ';':
		case ':':
		case '-':
			return CHAR_CLASS_PUNCTUATION;

		case '(':
		case '[':
		case '{':
			return CHAR_CLASS_PARENS_OPEN;

		case ')':
		case ']':
		case '}':
			return CHAR_CLASS_PARENS_CLOSE;

		default:
			return CHAR_CLASS_DEFAULT;
	}
}


/**
 * @brief Returns the byte offset of the start of the next UTF-8 character.
 *
 * Skips forward past any continuation bytes (0x80–0xBF) to reach the next
 * initial byte. Returns @a offset unchanged if already at the end of the buffer.
 *
 * @param offset The byte offset to advance from.
 * @return The offset of the first byte of the next UTF-8 character.
 */
int32
BTextView::_NextInitialByte(int32 offset) const
{
	if (offset >= fText->Length())
		return offset;

	for (++offset; (ByteAt(offset) & 0xC0) == 0x80; ++offset)
		;

	return offset;
}


/**
 * @brief Returns the byte offset of the start of the previous UTF-8 character.
 *
 * Walks backward from @a offset - 1, skipping continuation bytes (0x80–0xBF)
 * until an initial byte is found. Returns 0 if @a offset is 0 or if more than
 * 6 continuation bytes are encountered (malformed input guard).
 *
 * @param offset The byte offset to retreat from.
 * @return The offset of the first byte of the preceding UTF-8 character.
 */
int32
BTextView::_PreviousInitialByte(int32 offset) const
{
	if (offset <= 0)
		return 0;

	int32 count = 6;

	for (--offset; offset > 0 && count; --offset, --count) {
		if ((ByteAt(offset) & 0xC0) != 0x80)
			break;
	}

	return count ? offset : 0;
}


/**
 * @brief Handles a B_GET_PROPERTY scripting request for supported properties.
 *
 * Responds to "selection" (returns start and end offsets), "Text" (returns
 * the text in the requested index/range, blocked in password mode), and
 * "text_run_array" (currently unsupported, returns false).
 *
 * @param message   The original scripting message.
 * @param specifier The specifier sub-message extracted from @a message.
 * @param property  The property name string.
 * @param reply     The reply message to populate with the result.
 * @return true if the property was handled and @a reply was populated.
 */
bool
BTextView::_GetProperty(BMessage* message, BMessage* specifier,
	const char* property, BMessage* reply)
{
	CALLED();
	if (strcmp(property, "selection") == 0) {
		reply->what = B_REPLY;
		reply->AddInt32("result", fSelStart);
		reply->AddInt32("result", fSelEnd);
		reply->AddInt32("error", B_OK);

		return true;
	} else if (strcmp(property, "Text") == 0) {
		if (IsTypingHidden()) {
			// Do not allow stealing passwords via scripting
			beep();
			return false;
		}

		int32 index, range;
		specifier->FindInt32("index", &index);
		specifier->FindInt32("range", &range);

		char* buffer = new char[range + 1];
		GetText(index, range, buffer);

		reply->what = B_REPLY;
		reply->AddString("result", buffer);
		reply->AddInt32("error", B_OK);

		delete[] buffer;

		return true;
	} else if (strcmp(property, "text_run_array") == 0)
		return false;

	return false;
}


/**
 * @brief Handles a B_SET_PROPERTY scripting request for supported properties.
 *
 * Responds to "selection" (calls Select()) and "Text" (inserts or deletes
 * text in the specified range). "text_run_array" is unsupported and returns false.
 *
 * @param message   The original scripting message.
 * @param specifier The specifier sub-message extracted from @a message.
 * @param property  The property name string.
 * @param reply     The reply message to populate with the result.
 * @return true if the property was handled and @a reply was populated.
 */
bool
BTextView::_SetProperty(BMessage* message, BMessage* specifier,
	const char* property, BMessage* reply)
{
	CALLED();
	if (strcmp(property, "selection") == 0) {
		int32 index, range;

		specifier->FindInt32("index", &index);
		specifier->FindInt32("range", &range);

		Select(index, index + range);

		reply->what = B_REPLY;
		reply->AddInt32("error", B_OK);

		return true;
	} else if (strcmp(property, "Text") == 0) {
		int32 index, range;
		specifier->FindInt32("index", &index);
		specifier->FindInt32("range", &range);

		const char* buffer = NULL;
		if (message->FindString("data", &buffer) == B_OK) {
			InsertText(buffer, range, index, NULL);
			_Refresh(index, index + range);
		} else {
			DeleteText(index, index + range);
			_Refresh(index, index);
		}

		reply->what = B_REPLY;
		reply->AddInt32("error", B_OK);

		return true;
	} else if (strcmp(property, "text_run_array") == 0)
		return false;

	return false;
}


/**
 * @brief Handles a B_COUNT_PROPERTIES scripting request.
 *
 * Currently handles the "Text" property, replying with the byte length of
 * the buffer (i.e. TextLength()).
 *
 * @param message   The original scripting message.
 * @param specifier The specifier sub-message.
 * @param property  The property name string.
 * @param reply     The reply message to populate with the count.
 * @return true if the property was handled and @a reply was populated.
 */
bool
BTextView::_CountProperties(BMessage* message, BMessage* specifier,
	const char* property, BMessage* reply)
{
	CALLED();
	if (strcmp(property, "Text") == 0) {
		reply->what = B_REPLY;
		reply->AddInt32("result", fText->Length());
		reply->AddInt32("error", B_OK);
		return true;
	}

	return false;
}


/**
 * @brief Handles a B_INPUT_METHOD_CHANGED message from an Input Server method add-on.
 *
 * Updates the inline-input state with the new string and clause information.
 * When the composition is confirmed, feeds each character back through KeyDown()
 * to allow special-character processing. Otherwise, inserts the transient
 * composition string and highlights the active and selected sub-ranges.
 *
 * @param message The B_INPUT_METHOD_EVENT message with opcode B_INPUT_METHOD_CHANGED.
 */
void
BTextView::_HandleInputMethodChanged(BMessage* message)
{
	// TODO: block input if not editable (Andrew)
	ASSERT(fInline != NULL);

	const char* string = NULL;
	if (message->FindString("be:string", &string) < B_OK || string == NULL)
		return;

	_HideCaret();

	if (IsFocus())
		be_app->ObscureCursor();

	// If we find the "be:confirmed" boolean (and the boolean is true),
	// it means it's over for now, so the current InlineInput object
	// should become inactive. We will probably receive a
	// B_INPUT_METHOD_STOPPED message after this one.
	bool confirmed;
	if (message->FindBool("be:confirmed", &confirmed) != B_OK)
		confirmed = false;

	// Delete the previously inserted text (if any)
	if (fInline->IsActive()) {
		const int32 oldOffset = fInline->Offset();
		DeleteText(oldOffset, oldOffset + fInline->Length());
		if (confirmed)
			fInline->SetActive(false);
		fCaretOffset = fSelStart = fSelEnd = oldOffset;
	}

	const int32 stringLen = strlen(string);

	fInline->SetOffset(fSelStart);
	fInline->SetLength(stringLen);
	fInline->ResetClauses();

	if (!confirmed && !fInline->IsActive())
		fInline->SetActive(true);

	// Get the clauses, and pass them to the InlineInput object
	// TODO: Find out if what we did it's ok, currently we don't consider
	// clauses at all, while the bebook says we should; though the visual
	// effect we obtained seems correct. Weird.
	int32 clauseCount = 0;
	int32 clauseStart;
	int32 clauseEnd;
	while (message->FindInt32("be:clause_start", clauseCount, &clauseStart)
			== B_OK
		&& message->FindInt32("be:clause_end", clauseCount, &clauseEnd)
			== B_OK) {
		if (!fInline->AddClause(clauseStart, clauseEnd))
			break;
		clauseCount++;
	}

	if (confirmed) {
		_Refresh(fSelStart, fSelEnd, fSelEnd);
		_ShowCaret();

		// now we need to feed ourselves the individual characters as if the
		// user would have pressed them now - this lets KeyDown() pick out all
		// the special characters like B_BACKSPACE, cursor keys and the like:
		const char* currPos = string;
		const char* prevPos = currPos;
		while (*currPos != '\0') {
			if ((*currPos & 0xC0) == 0xC0) {
				// found the start of an UTF-8 char, we collect while it lasts
				++currPos;
				while ((*currPos & 0xC0) == 0x80)
					++currPos;
			} else if ((*currPos & 0xC0) == 0x80) {
				// illegal: character starts with utf-8 intermediate byte,
				// skip it
				prevPos = ++currPos;
			} else {
				// single byte character/code, just feed that
				++currPos;
			}
			KeyDown(prevPos, currPos - prevPos);
			prevPos = currPos;
		}

		_Refresh(fSelStart, fSelEnd, fSelEnd);
	} else {
		// temporarily show transient state of inline input
		int32 selectionStart = 0;
		int32 selectionEnd = 0;
		message->FindInt32("be:selection", 0, &selectionStart);
		message->FindInt32("be:selection", 1, &selectionEnd);

		fInline->SetSelectionOffset(selectionStart);
		fInline->SetSelectionLength(selectionEnd - selectionStart);

		const int32 inlineOffset = fInline->Offset();
		InsertText(string, stringLen, fSelStart, NULL);

		_Refresh(inlineOffset, fSelEnd, fSelEnd);
		_ShowCaret();
	}
}


/**
 * @brief Responds to a B_INPUT_METHOD_LOCATION_REQUEST from an IME method add-on.
 *
 * Iterates over each character of the inline input string, converts its
 * view-coordinate position to screen coordinates, and sends a
 * B_INPUT_METHOD_LOCATION_REQUEST reply containing the on-screen location and
 * line height for each UTF-8 character.
 */
void
BTextView::_HandleInputMethodLocationRequest()
{
	ASSERT(fInline != NULL);

	int32 offset = fInline->Offset();
	const int32 limit = offset + fInline->Length();

	BMessage message(B_INPUT_METHOD_EVENT);
	message.AddInt32("be:opcode", B_INPUT_METHOD_LOCATION_REQUEST);

	// Add the location of the UTF8 characters
	while (offset < limit) {
		float height;
		BPoint where = PointAt(offset, &height);
		ConvertToScreen(&where);

		message.AddPoint("be:location_reply", where);
		message.AddFloat("be:height_reply", height);

		offset = _NextInitialByte(offset);
	}

	fInline->Method()->SendMessage(&message);
}


/**
 * @brief Cancels any active inline IME input session and notifies the Input Server.
 *
 * Clears the InlineInput object, refreshes the affected text range, and sends
 * a B_INPUT_METHOD_STOPPED event to the IME method messenger. Does nothing if
 * no IME session is active.
 */
void
BTextView::_CancelInputMethod()
{
	if (!fInline)
		return;

	InlineInput* inlineInput = fInline;
	fInline = NULL;

	if (inlineInput->IsActive() && Window()) {
		_Refresh(inlineInput->Offset(), fText->Length()
			- inlineInput->Offset());

		BMessage message(B_INPUT_METHOD_EVENT);
		message.AddInt32("be:opcode", B_INPUT_METHOD_STOPPED);
		inlineInput->Method()->SendMessage(&message);
	}

	delete inlineInput;
}


/**
 * @brief Returns the line number of the character at the given byte offset (internal version).
 *
 * @note This may not return the last line correctly for offsets at the end of a
 *       trailing newline; use the public LineAt() for client-facing queries.
 *
 * @param offset The byte offset of the character to locate.
 * @return The zero-based line number, never exceeding NumLines() - 1.
 */
int32
BTextView::_LineAt(int32 offset) const
{
	return fLines->OffsetToLine(offset);
}


/**
 * @brief Returns the line number that the given view-coordinate point falls on (internal version).
 *
 * @note May not return the last line correctly; use LineAt(BPoint) for public use.
 *
 * @param point The point in view coordinates to query.
 * @return The zero-based line number under @a point.
 */
int32
BTextView::_LineAt(const BPoint& point) const
{
	return fLines->PixelToLine(point.y - fTextRect.top);
}


/**
 * @brief Returns whether @a offset refers to the empty line after a trailing newline.
 *
 * This is true when @a offset equals TextLength(), the buffer is non-empty,
 * and the last character is B_ENTER. Used by PointAt() and LineAt() to
 * position the caret on the visually separate empty final line.
 *
 * @param offset The byte offset to test.
 * @return true if @a offset is on the empty last line.
 */
bool
BTextView::_IsOnEmptyLastLine(int32 offset) const
{
	return (offset == fText->Length() && offset > 0
		&& fText->RealCharAt(offset - 1) == B_ENTER);
}


/**
 * @brief Applies a font and colour to a byte range in the style buffer.
 *
 * Normalises the font before applying. For non-stylable views the range is
 * always expanded to the full buffer. Optionally synchronises the null style
 * before modifying the style runs.
 *
 * @param fromOffset    First byte of the range to style.
 * @param toOffset      One past the last byte of the range to style.
 * @param mode          Bitmask of B_FONT_* attributes to apply; defaults to B_FONT_ALL.
 * @param font          The font to apply, or NULL.
 * @param color         The foreground colour to apply, or NULL.
 * @param syncNullStyle If true, synchronises the null style to @a fromOffset first.
 */
void
BTextView::_ApplyStyleRange(int32 fromOffset, int32 toOffset, uint32 mode,
	const BFont* font, const rgb_color* color, bool syncNullStyle)
{
	BFont normalized;
		// Declared before the if so it stays allocated until the call to
		// SetStyleRange
	if (font != NULL) {
		// if a font has been given, normalize it
		normalized = *font;
		_NormalizeFont(&normalized);
		font = &normalized;
	}

	if (!fStylable) {
		// always apply font and color to full range for non-stylable textviews
		fromOffset = 0;
		toOffset = fText->Length();
	}

	if (syncNullStyle)
		fStyles->SyncNullStyle(fromOffset);

	fStyles->SetStyleRange(fromOffset, toOffset, fText->Length(), mode,
		font, color);
}


/**
 * @brief Returns the pixel height implied by the current null (insertion) style font.
 *
 * Used to determine caret height and the height of an empty last line.
 *
 * @return The ceiling of ascent + descent + 1 for the null style font.
 */
float
BTextView::_NullStyleHeight() const
{
	const BFont* font = NULL;
	fStyles->GetNullStyle(&font, NULL);

	font_height fontHeight;
	font->GetHeight(&fontHeight);
	return ceilf(fontHeight.ascent + fontHeight.descent + 1);
}


/**
 * @brief Displays the right-click context menu at the given position.
 *
 * The menu contains Undo, Redo, Cut, Copy, Paste, and Select All items,
 * each enabled or disabled based on the current edit state.
 *
 * @param where The position in view coordinates at which to show the menu.
 */
void
BTextView::_ShowContextMenu(BPoint where)
{
	bool isRedo;
	undo_state state = UndoState(&isRedo);
	bool isUndo = state != B_UNDO_UNAVAILABLE && !isRedo;

	int32 start;
	int32 finish;
	GetSelection(&start, &finish);

	bool canEdit = IsEditable();
	int32 length = fText->Length();

	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);

	BLayoutBuilder::Menu<>(menu)
		.AddItem(TRANSLATE("Undo"), B_UNDO/*, 'Z'*/)
			.SetEnabled(canEdit && isUndo)
		.AddItem(TRANSLATE("Redo"), B_UNDO/*, 'Z', B_SHIFT_KEY*/)
			.SetEnabled(canEdit && isRedo)
		.AddSeparator()
		.AddItem(TRANSLATE("Cut"), B_CUT, 'X')
			.SetEnabled(canEdit && start != finish)
		.AddItem(TRANSLATE("Copy"), B_COPY, 'C')
			.SetEnabled(start != finish)
		.AddItem(TRANSLATE("Paste"), B_PASTE, 'V')
			.SetEnabled(canEdit && be_clipboard->SystemCount() > 0)
		.AddSeparator()
		.AddItem(TRANSLATE("Select all"), B_SELECT_ALL, 'A')
			.SetEnabled(!(start == 0 && finish == length))
	;

	menu->SetTargetForItems(this);
	ConvertToScreen(&where);
	menu->Go(where, true, true,	true);
}


/**
 * @brief Removes disallowed characters from a text buffer in-place.
 *
 * Also adjusts the run-array offsets to match the compacted buffer.
 * Does nothing if no characters are disallowed or the disallowed list is empty.
 *
 * @param text     The text buffer to filter; modified in place.
 * @param length   On entry, the byte count; on exit, the filtered byte count.
 * @param runArray Optional run array whose offsets are updated to match; may be NULL.
 */
void
BTextView::_FilterDisallowedChars(char* text, ssize_t& length,
	text_run_array* runArray)
{
	if (!fDisallowedChars)
		return;

	if (fDisallowedChars->IsEmpty() || !text)
		return;

	ssize_t stringIndex = 0;
	if (runArray) {
		ssize_t remNext = 0;

		for (int i = 0; i < runArray->count; i++) {
			runArray->runs[i].offset -= remNext;
			while (stringIndex < runArray->runs[i].offset
				&& stringIndex < length) {
				if (fDisallowedChars->HasItem(
					reinterpret_cast<void*>(text[stringIndex]))) {
					memmove(text + stringIndex, text + stringIndex + 1,
						length - stringIndex - 1);
					length--;
					runArray->runs[i].offset--;
					remNext++;
				} else
					stringIndex++;
			}
		}
	}

	while (stringIndex < length) {
		if (fDisallowedChars->HasItem(
			reinterpret_cast<void*>(text[stringIndex]))) {
			memmove(text + stringIndex, text + stringIndex + 1,
				length - stringIndex - 1);
			length--;
		} else
			stringIndex++;
	}
}


/**
 * @brief Recalculates the view insets from the difference between the view bounds and @a rect.
 *
 * Does nothing if SetInsets() has been called (the @c overridden flag is set).
 * Adds default horizontal and vertical padding when the text rect equals the
 * view bounds and the view is editable or selectable.
 *
 * @param rect The text rect to derive the insets from.
 */
void
BTextView::_UpdateInsets(const BRect& rect)
{
	// do not update insets if SetInsets() was called
	if (fLayoutData->overridden)
		return;

	const BRect& bounds = Bounds();

	// we disallow negative insets, as they would cause parts of the
	// text to be hidden
	fLayoutData->leftInset = rect.left >= bounds.left
		? rect.left - bounds.left : 0;
	fLayoutData->topInset = rect.top >= bounds.top
		? rect.top - bounds.top : 0;
	fLayoutData->rightInset = bounds.right >= rect.right
		? bounds.right - rect.right : 0;
	fLayoutData->bottomInset = bounds.bottom >= rect.bottom
		? bounds.bottom - rect.bottom : 0;

	// only add default insets if text rect is set to bounds
	if (rect == bounds && (fEditable || fSelectable)) {
		float hPadding = be_control_look->DefaultLabelSpacing();
		float hInset = floorf(hPadding / 2.0f);
		float vInset = 1;
		fLayoutData->leftInset += hInset;
		fLayoutData->topInset += vInset;
		fLayoutData->rightInset += hInset;
		fLayoutData->bottomInset += vInset;
	}
}


/**
 * @brief Returns the usable width of the view interior (bounds minus horizontal insets).
 *
 * @return The available width for content in pixels.
 */
float
BTextView::_ViewWidth()
{
	return Bounds().Width()
		- fLayoutData->leftInset
		- fLayoutData->rightInset;
}


/**
 * @brief Returns the usable height of the view interior (bounds minus vertical insets).
 *
 * @return The available height for content in pixels.
 */
float
BTextView::_ViewHeight()
{
	return Bounds().Height()
		- fLayoutData->topInset
		- fLayoutData->bottomInset;
}


/**
 * @brief Returns the view's interior rect (bounds contracted by the current insets).
 *
 * @return A BRect representing the area inside the insets.
 */
BRect
BTextView::_ViewRect()
{
	BRect rect(Bounds());
	rect.left += fLayoutData->leftInset;
	rect.top += fLayoutData->topInset;
	rect.right -= fLayoutData->rightInset;
	rect.bottom -= fLayoutData->bottomInset;

	return rect;
}


/**
 * @brief Returns the total horizontal extent of the text rect including insets.
 *
 * @return The text rect width plus left and right insets in pixels.
 */
float
BTextView::_TextWidth()
{
	return fTextRect.Width()
		+ fLayoutData->leftInset
		+ fLayoutData->rightInset;
}


/**
 * @brief Returns the total vertical extent of the text rect including insets.
 *
 * @return The text rect height plus top and bottom insets in pixels.
 */
float
BTextView::_TextHeight()
{
	return fTextRect.Height()
		+ fLayoutData->topInset
		+ fLayoutData->bottomInset;
}


/**
 * @brief Returns the text rect expanded outward by the current insets.
 *
 * This is the inverse of the inset operation: it restores the full bounding
 * rect that encompasses both the text and its surrounding padding.
 *
 * @return A BRect equal to the text rect expanded by all four insets.
 */
BRect
BTextView::_TextRect()
{
	BRect rect(fTextRect);
	rect.left -= fLayoutData->leftInset;
	rect.top -= fLayoutData->topInset;
	rect.right += fLayoutData->rightInset;
	rect.bottom += fLayoutData->bottomInset;

	return rect;
}


/**
 * @brief Returns the tint factor applied to the background colour when the view is not editable.
 *
 * Uses B_DARKEN_1_TINT for light backgrounds and a fixed 0.853 value for dark ones,
 * matching the visual convention used throughout the system for disabled document views.
 *
 * @return A float tint value suitable for use with SetViewUIColor().
 */
float
BTextView::_UneditableTint() const
{
	return ui_color(B_DOCUMENT_BACKGROUND_COLOR).IsLight() ? B_DARKEN_1_TINT : 0.853;
}


// #pragma mark - BTextView::TextTrackState


BTextView::TextTrackState::TextTrackState(BMessenger messenger)
	:
	clickOffset(0),
	shiftDown(false),
	anchor(0),
	selStart(0),
	selEnd(0),
	fRunner(NULL)
{
	BMessage message(_PING_);
	const bigtime_t scrollSpeed = 25 * 1000;	// 40 scroll steps per second
	fRunner = new (nothrow) BMessageRunner(messenger, &message, scrollSpeed);
}


BTextView::TextTrackState::~TextTrackState()
{
	delete fRunner;
}


void
BTextView::TextTrackState::SimulateMouseMovement(BTextView* textView)
{
	BPoint where;
	uint32 buttons;
	// When the mouse cursor is still and outside the textview,
	// no B_MOUSE_MOVED message are sent, obviously. But scrolling
	// has to work neverthless, so we "fake" a MouseMoved() call here.
	textView->GetMouse(&where, &buttons);
	textView->_PerformMouseMoved(where, B_INSIDE_VIEW);
}


// #pragma mark - Binary ABI compat


extern "C" void
B_IF_GCC_2(InvalidateLayout__9BTextViewb,  _ZN9BTextView16InvalidateLayoutEb)(
	BTextView* view, bool descendants)
{
	perform_data_layout_invalidated data;
	data.descendants = descendants;

	view->Perform(PERFORM_CODE_LAYOUT_INVALIDATED, &data);
}
