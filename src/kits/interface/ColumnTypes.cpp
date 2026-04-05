/*******************************************************************************
/
/	File:			ColumnTypes.h
/
/   Description:    Experimental classes that implement particular column/field
/					data types for use in BColumnListView.
/
/	Copyright 2000+, Be Incorporated, All Rights Reserved
/	Copyright 2024, Haiku, Inc. All Rights Reserved
/
*******************************************************************************/


#include "ColumnTypes.h"

#include <StringFormat.h>
#include <SystemCatalog.h>
#include <View.h>

#include <stdio.h>


using BPrivate::gSystemCatalog;

#undef B_TRANSLATE_COMMENT
#define B_TRANSLATE_COMMENT(str, comment) \
	gSystemCatalog.GetString(B_TRANSLATE_MARK_COMMENT(str, comment), \
		B_TRANSLATION_CONTEXT, (comment))


/** @brief Horizontal padding (in pixels) added on each side of text within a cell. */
#define kTEXT_MARGIN	8


/**
 * @brief Construct a BTitledColumn with a display title and layout constraints.
 *
 * @param title    The column header string shown in the list view.
 * @param width    Initial column width in pixels.
 * @param minWidth Minimum allowed width in pixels.
 * @param maxWidth Maximum allowed width in pixels.
 * @param align    Horizontal alignment of cell content.
 */
BTitledColumn::BTitledColumn(const char* title, float width, float minWidth,
	float maxWidth, alignment align)
	:
	BColumn(width, minWidth, maxWidth, align),
	fTitle(title)
{
	font_height fh;

	be_plain_font->GetHeight(&fh);
	fFontHeight = fh.descent + fh.leading;
}


/**
 * @brief Draw the column header title into the given rect, truncating if necessary.
 *
 * @param rect   The bounding rectangle for the header cell.
 * @param parent The view to draw into.
 */
void
BTitledColumn::DrawTitle(BRect rect, BView* parent)
{
	float width = rect.Width() - (2 * kTEXT_MARGIN);
	BString out_string(fTitle);

	parent->TruncateString(&out_string, B_TRUNCATE_END, width + 2);
	DrawString(out_string.String(), parent, rect);
}


/**
 * @brief Copy the column name into the provided BString.
 *
 * @param into Output string that receives the column title.
 */
void
BTitledColumn::GetColumnName(BString* into) const
{
	*into = fTitle;
}


/**
 * @brief Draw a string into a cell rect, respecting the column's alignment setting.
 *
 * Vertically centres the text within \a rect and positions the pen horizontally
 * according to B_ALIGN_LEFT, B_ALIGN_CENTER, or B_ALIGN_RIGHT.
 *
 * @param string The text to draw.
 * @param parent The view to draw into.
 * @param rect   The bounding rectangle of the cell.
 */
void
BTitledColumn::DrawString(const char* string, BView* parent, BRect rect)
{
	float width = rect.Width() - (2 * kTEXT_MARGIN);
	float y;
	BFont font;
	font_height	finfo;

	parent->GetFont(&font);
	font.GetHeight(&finfo);
	y = rect.top + finfo.ascent
		+ (rect.Height() - ceilf(finfo.ascent + finfo.descent)) / 2.0f;

	switch (Alignment()) {
		default:
		case B_ALIGN_LEFT:
			parent->MovePenTo(rect.left + kTEXT_MARGIN, y);
			break;

		case B_ALIGN_CENTER:
			parent->MovePenTo(rect.left + kTEXT_MARGIN
				+ ((width - font.StringWidth(string)) / 2), y);
			break;

		case B_ALIGN_RIGHT:
			parent->MovePenTo(rect.right - kTEXT_MARGIN
				- font.StringWidth(string), y);
			break;
	}

	parent->DrawString(string);
}


/**
 * @brief Change the column's display title.
 *
 * @param title The new title string.
 */
void
BTitledColumn::SetTitle(const char* title)
{
	fTitle.SetTo(title);
}


/**
 * @brief Copy the column title into the provided BString.
 *
 * @param forTitle Output string that receives the title; does nothing if NULL.
 */
void
BTitledColumn::Title(BString* forTitle) const
{
	if (forTitle)
		forTitle->SetTo(fTitle.String());
}


/**
 * @brief Return the combined descent and leading of the default plain font.
 *
 * This value is cached at construction time and is useful for computing the
 * vertical position of text within a cell.
 *
 * @return Font height in pixels (descent + leading).
 */
float
BTitledColumn::FontHeight() const
{
	return fFontHeight;
}


/**
 * @brief Return the preferred column width needed to display the column title.
 *
 * @param _field Unused; the width is based solely on the title string.
 * @param parent The view used to measure the string width.
 * @return Preferred width in pixels (title string width plus margins).
 */
float
BTitledColumn::GetPreferredWidth(BField *_field, BView* parent) const
{
	return parent->StringWidth(fTitle.String()) + 2 * kTEXT_MARGIN;
}


// #pragma mark - BStringField


/**
 * @brief Construct a BStringField holding the given string value.
 *
 * @param string The initial string content for this field.
 */
BStringField::BStringField(const char* string)
	:
	fWidth(0),
	fString(string),
	fClippedString(string)
{
}


/**
 * @brief Replace the string content of this field.
 *
 * Also clears the cached clipped string and the cached column width.
 *
 * @param val The new string value.
 */
void
BStringField::SetString(const char* val)
{
	fString = val;
	fClippedString = "";
	fWidth = 0;
}


/**
 * @brief Return the full (unclipped) string stored in this field.
 *
 * @return A pointer to the internal string data; valid until the next mutation.
 */
const char*
BStringField::String() const
{
	return fString.String();
}


/**
 * @brief Cache the column width at which the current clipped string was computed.
 *
 * @param width The pixel width associated with the cached clipped string.
 */
void
BStringField::SetWidth(float width)
{
	fWidth = width;
}


/**
 * @brief Return the cached column width used for the current clipped string.
 *
 * @return The cached width in pixels.
 */
float
BStringField::Width()
{
	return fWidth;
}


/**
 * @brief Store the pre-truncated version of the field string.
 *
 * @param val The truncated string to cache; pass an empty string to clear.
 */
void
BStringField::SetClippedString(const char* val)
{
	fClippedString = val;
}


/**
 * @brief Return whether a non-empty clipped string is currently cached.
 *
 * @return true if a clipped string is cached, false if the full string fits.
 */
bool
BStringField::HasClippedString() const
{
	return !fClippedString.IsEmpty();
}


/**
 * @brief Return the cached truncated string.
 *
 * @return The clipped string, or an empty string if no clipping was needed.
 */
const char*
BStringField::ClippedString()
{
	return fClippedString.String();
}


// #pragma mark - BStringColumn


/**
 * @brief Construct a BStringColumn with display constraints and truncation mode.
 *
 * @param title    Column header text.
 * @param width    Initial column width in pixels.
 * @param minWidth Minimum column width in pixels.
 * @param maxWidth Maximum column width in pixels.
 * @param truncate B_TRUNCATE_* constant controlling where long strings are cut.
 * @param align    Horizontal alignment of cell text.
 */
BStringColumn::BStringColumn(const char* title, float width, float minWidth,
	float maxWidth, uint32 truncate, alignment align)
	:
	BTitledColumn(title, width, minWidth, maxWidth, align),
	fTruncate(truncate)
{
}


/**
 * @brief Draw a BStringField into a list-view cell, caching the truncated string.
 *
 * Re-truncates the field's string whenever the available cell width has changed
 * since the last draw. The truncated result is cached in the field to avoid
 * redundant work on subsequent paints at the same width.
 *
 * @param _field The BStringField to render.
 * @param rect   The cell bounding rectangle.
 * @param parent The view to draw into.
 */
void
BStringColumn::DrawField(BField* _field, BRect rect, BView* parent)
{
	float width = rect.Width() - (2 * kTEXT_MARGIN);
	BStringField* field = static_cast<BStringField*>(_field);
	float fieldWidth = field->Width();
	bool updateNeeded = width != fieldWidth;

	if (updateNeeded) {
		BString out_string(field->String());
		float preferredWidth = parent->StringWidth(out_string.String());
		if (width < preferredWidth) {
			parent->TruncateString(&out_string, fTruncate, width + 2);
			field->SetClippedString(out_string.String());
		} else
			field->SetClippedString("");
		field->SetWidth(width);
	}

	DrawString(field->HasClippedString()
		? field->ClippedString()
		: field->String(), parent, rect);
}


/**
 * @brief Return the minimum column width required to display the field's full string.
 *
 * @param _field The BStringField to measure.
 * @param parent The view used to measure the string width.
 * @return Preferred width in pixels.
 */
float
BStringColumn::GetPreferredWidth(BField *_field, BView* parent) const
{
	BStringField* field = static_cast<BStringField*>(_field);
	return parent->StringWidth(field->String()) + 2 * kTEXT_MARGIN;
}


/**
 * @brief Compare two BStringFields case-insensitively for sort ordering.
 *
 * @param field1 First field to compare.
 * @param field2 Second field to compare.
 * @return Negative, zero, or positive in the same convention as strcmp().
 */
int
BStringColumn::CompareFields(BField* field1, BField* field2)
{
	return ICompare(((BStringField*)field1)->String(),
		(((BStringField*)field2)->String()));
}


/**
 * @brief Return whether this column accepts a given field type.
 *
 * @param field The field to test.
 * @return true if \a field is a BStringField, false otherwise.
 */
bool
BStringColumn::AcceptsField(const BField *field) const
{
	return static_cast<bool>(dynamic_cast<const BStringField*>(field));
}


// #pragma mark - BDateField


/**
 * @brief Construct a BDateField from a POSIX time_t value.
 *
 * Converts \a time to both a broken-down struct tm (local time) and a
 * normalised time_t via mktime() for later comparison.
 *
 * @param time Pointer to the POSIX timestamp to represent.
 */
BDateField::BDateField(time_t* time)
	:
	fTime(*localtime(time)),
	fUnixTime(*time),
	fSeconds(0),
	fClippedString(""),
	fWidth(0)
{
	fSeconds = mktime(&fTime);
}


/**
 * @brief Cache the column width used for the current clipped date string.
 *
 * @param width The pixel width to cache.
 */
void
BDateField::SetWidth(float width)
{
	fWidth = width;
}


/**
 * @brief Return the cached column width for the current clipped date string.
 *
 * @return Cached width in pixels.
 */
float
BDateField::Width()
{
	return fWidth;
}


/**
 * @brief Store the pre-formatted (possibly truncated) date string.
 *
 * @param string The formatted date string to cache.
 */
void
BDateField::SetClippedString(const char* string)
{
	fClippedString = string;
}


/**
 * @brief Return the cached formatted date string.
 *
 * @return The last string set by SetClippedString(), or "" if not yet set.
 */
const char*
BDateField::ClippedString()
{
	return fClippedString.String();
}


/**
 * @brief Return the normalised timestamp (via mktime) for sort comparison.
 *
 * @return The time_t produced by normalising the local-time breakdown.
 */
time_t
BDateField::Seconds()
{
	return fSeconds;
}


/**
 * @brief Return the original POSIX timestamp stored in this field.
 *
 * @return The raw time_t value passed to the constructor.
 */
time_t
BDateField::UnixTime()
{
	return fUnixTime;
}


// #pragma mark - BDateColumn


/**
 * @brief Construct a BDateColumn with display constraints.
 *
 * @param title    Column header text.
 * @param width    Initial column width in pixels.
 * @param minWidth Minimum column width in pixels.
 * @param maxWidth Maximum column width in pixels.
 * @param align    Horizontal alignment of cell text.
 */
BDateColumn::BDateColumn(const char* title, float width, float minWidth,
	float maxWidth, alignment align)
	:
	BTitledColumn(title, width, minWidth, maxWidth, align),
	fTitle(title)
{
}


/**
 * @brief Draw a BDateField into a cell, choosing the most detailed format that fits.
 *
 * Iterates through progressively shorter date/time format combinations until
 * the formatted string fits within the available cell width. The chosen string
 * is cached in the field to avoid reformatting on every paint.
 *
 * @param _field The BDateField to render.
 * @param rect   The cell bounding rectangle.
 * @param parent The view to draw into.
 */
void
BDateColumn::DrawField(BField* _field, BRect rect, BView* parent)
{
	float width = rect.Width() - (2 * kTEXT_MARGIN);
	BDateField* field = (BDateField*)_field;

	if (field->Width() != rect.Width()) {
		char dateString[256];
		time_t currentTime = field->UnixTime();
		tm time_data;
		BFont font;

		parent->GetFont(&font);
		localtime_r(&currentTime, &time_data);

		// dateStyles[] and timeStyles[] must be the same length
		const BDateFormatStyle dateStyles[] = {
			B_FULL_DATE_FORMAT, B_FULL_DATE_FORMAT, B_LONG_DATE_FORMAT, B_LONG_DATE_FORMAT,
			B_MEDIUM_DATE_FORMAT, B_SHORT_DATE_FORMAT,
		};

		const BTimeFormatStyle timeStyles[] = {
			B_MEDIUM_TIME_FORMAT, B_SHORT_TIME_FORMAT, B_MEDIUM_TIME_FORMAT, B_SHORT_TIME_FORMAT,
			B_SHORT_TIME_FORMAT, B_SHORT_TIME_FORMAT,
		};

		size_t index;
		for (index = 0; index < B_COUNT_OF(dateStyles); index++) {
			ssize_t output = fDateTimeFormat.Format(dateString, sizeof(dateString), currentTime,
				dateStyles[index], timeStyles[index]);
			if (output >= 0 && font.StringWidth(dateString) <= width)
				break;
		}

		if (index == B_COUNT_OF(dateStyles))
			fDateFormat.Format(dateString, sizeof(dateString), currentTime, B_SHORT_DATE_FORMAT);

		if (font.StringWidth(dateString) > width) {
			BString out_string(dateString);

			parent->TruncateString(&out_string, B_TRUNCATE_MIDDLE, width + 2);
			strcpy(dateString, out_string.String());
		}
		field->SetClippedString(dateString);
		field->SetWidth(width);
	}

	DrawString(field->ClippedString(), parent, rect);
}


/**
 * @brief Compare two BDateFields by their normalised timestamps for sort ordering.
 *
 * @param field1 First field to compare.
 * @param field2 Second field to compare.
 * @return The difference in seconds between the two timestamps (field1 - field2).
 */
int
BDateColumn::CompareFields(BField* field1, BField* field2)
{
	return((BDateField*)field1)->Seconds() - ((BDateField*)field2)->Seconds();
}


// #pragma mark - BSizeField


/**
 * @brief Construct a BSizeField holding a byte-count value.
 *
 * @param size The file or object size in bytes.
 */
BSizeField::BSizeField(off_t size)
	:
	fSize(size)
{
}


/**
 * @brief Update the byte-count value stored in this field.
 *
 * @param size The new size in bytes.
 */
void
BSizeField::SetSize(off_t size)
{
	fSize = size;
}


/**
 * @brief Return the byte-count value stored in this field.
 *
 * @return The size in bytes.
 */
off_t
BSizeField::Size()
{
	return fSize;
}


// #pragma mark - BSizeColumn


/**
 * @brief Construct a BSizeColumn with display constraints.
 *
 * @param title    Column header text.
 * @param width    Initial column width in pixels.
 * @param minWidth Minimum column width in pixels.
 * @param maxWidth Maximum column width in pixels.
 * @param align    Horizontal alignment of cell text.
 */
BSizeColumn::BSizeColumn(const char* title, float width, float minWidth,
	float maxWidth, alignment align)
	:
	BTitledColumn(title, width, minWidth, maxWidth, align)
{
}


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "StringForSize"


/**
 * @brief Draw a BSizeField as a human-readable size string (e.g., "1.5 MiB").
 *
 * Selects the most appropriate SI binary unit (bytes, KiB, MiB, GiB, TiB) and
 * the highest precision that still fits within the available cell width. The
 * string is truncated in the middle as a last resort.
 *
 * @param _field The BSizeField to render.
 * @param rect   The cell bounding rectangle.
 * @param parent The view to draw into.
 */
void
BSizeColumn::DrawField(BField* _field, BRect rect, BView* parent)
{
	BFont font;
	BString printedSize;
	BString string;

	float width = rect.Width() - (2 * kTEXT_MARGIN);

	double value = ((BSizeField*)_field)->Size();
	parent->GetFont(&font);

	// we cannot use string_for_size due to the precision/cell width logic
	const char* kFormats[] = {
		B_TRANSLATE_MARK_COMMENT("{0, plural, one{%s byte} other{%s bytes}}", "size unit"),
		B_TRANSLATE_MARK_COMMENT("%s KiB", "size unit"),
		B_TRANSLATE_MARK_COMMENT("%s MiB", "size unit"),
		B_TRANSLATE_MARK_COMMENT("%s GiB", "size unit"),
		B_TRANSLATE_MARK_COMMENT("%s TiB", "size unit")
	};

	size_t index = 0;
	while (index < B_COUNT_OF(kFormats) - 1 && value >= 1024.0) {
		value /= 1024.0;
		index++;
	}

	BString format;
	BStringFormat formatter(
		gSystemCatalog.GetString(kFormats[index], B_TRANSLATION_CONTEXT, "size unit"));
	formatter.Format(format, value);

	if (index == 0) {
		fNumberFormat.SetPrecision(0);
		fNumberFormat.Format(printedSize, value);
		string.SetToFormat(format.String(), printedSize.String());

		if (font.StringWidth(string) > width) {
			BStringFormat formatter(B_TRANSLATE_COMMENT("%s B", "size unit, narrow space"));
			format.Truncate(0);
			formatter.Format(format, value);
			string.SetToFormat(format.String(), printedSize.String());
		}
	} else {
		int precision = 2;
		while (precision >= 0) {
			fNumberFormat.SetPrecision(precision);
			fNumberFormat.Format(printedSize, value);
			string.SetToFormat(format.String(), printedSize.String());
			if (font.StringWidth(string) <= width)
				break;

			precision--;
		}
	}

	parent->TruncateString(&string, B_TRUNCATE_MIDDLE, width + 2);
	DrawString(string.String(), parent, rect);
}

#undef B_TRANSLATION_CONTEXT


/**
 * @brief Compare two BSizeFields by their byte-count values for sort ordering.
 *
 * @param field1 First field to compare.
 * @param field2 Second field to compare.
 * @return -1 if field1 < field2, 0 if equal, 1 if field1 > field2.
 */
int
BSizeColumn::CompareFields(BField* field1, BField* field2)
{
	off_t diff = ((BSizeField*)field1)->Size() - ((BSizeField*)field2)->Size();
	if (diff > 0)
		return 1;
	if (diff < 0)
		return -1;
	return 0;
}


// #pragma mark - BIntegerField


/**
 * @brief Construct a BIntegerField holding a 32-bit integer value.
 *
 * @param number The initial integer value.
 */
BIntegerField::BIntegerField(int32 number)
	:
	fInteger(number)
{
}


/**
 * @brief Update the integer value stored in this field.
 *
 * @param value The new integer value.
 */
void
BIntegerField::SetValue(int32 value)
{
	fInteger = value;
}


/**
 * @brief Return the integer value stored in this field.
 *
 * @return The current integer value.
 */
int32
BIntegerField::Value()
{
	return fInteger;
}


// #pragma mark - BIntegerColumn


/**
 * @brief Construct a BIntegerColumn with display constraints.
 *
 * @param title    Column header text.
 * @param width    Initial column width in pixels.
 * @param minWidth Minimum column width in pixels.
 * @param maxWidth Maximum column width in pixels.
 * @param align    Horizontal alignment of cell text.
 */
BIntegerColumn::BIntegerColumn(const char* title, float width, float minWidth,
	float maxWidth, alignment align)
	:
	BTitledColumn(title, width, minWidth, maxWidth, align)
{
}


/**
 * @brief Draw a BIntegerField as a localised integer string.
 *
 * Formats the integer using the column's number formatter and truncates in the
 * middle if the result does not fit within the cell.
 *
 * @param field  The BIntegerField to render.
 * @param rect   The cell bounding rectangle.
 * @param parent The view to draw into.
 */
void
BIntegerColumn::DrawField(BField *field, BRect rect, BView* parent)
{
	BString string;

	fNumberFormat.Format(string, (int32)((BIntegerField*)field)->Value());
	float width = rect.Width() - (2 * kTEXT_MARGIN);
	parent->TruncateString(&string, B_TRUNCATE_MIDDLE, width + 2);
	DrawString(string.String(), parent, rect);
}


/**
 * @brief Compare two BIntegerFields by their integer values for sort ordering.
 *
 * @param field1 First field to compare.
 * @param field2 Second field to compare.
 * @return Difference (field1 - field2); negative, zero, or positive.
 */
int
BIntegerColumn::CompareFields(BField *field1, BField *field2)
{
	return (((BIntegerField*)field1)->Value() - ((BIntegerField*)field2)->Value());
}


// #pragma mark - GraphColumn


/**
 * @brief Construct a GraphColumn that renders integer values as progress bars.
 *
 * @param name     Column header text.
 * @param width    Initial column width in pixels.
 * @param minWidth Minimum column width in pixels.
 * @param maxWidth Maximum column width in pixels.
 * @param align    Horizontal alignment of cell content.
 */
GraphColumn::GraphColumn(const char* name, float width, float minWidth,
	float maxWidth, alignment align)
	:
	BIntegerColumn(name, width, minWidth, maxWidth, align)
{
}


/**
 * @brief Draw a BIntegerField as a percentage bar with a centred label.
 *
 * Clamps the field value to [0, 100], draws a rounded-rect outline, fills
 * the proportional bar in the navigation colour, then overlays the percentage
 * string in invert mode so it remains readable over the fill.
 *
 * @param field  The BIntegerField whose value is treated as a percentage.
 * @param rect   The cell bounding rectangle.
 * @param parent The view to draw into.
 */
void
GraphColumn::DrawField(BField* field, BRect rect, BView* parent)
{
	double fieldValue = ((BIntegerField*)field)->Value();
	double percentValue = fieldValue / 100.0;

	if (percentValue > 1.0)
		percentValue = 1.0;
	else if (percentValue < 0.0)
		percentValue = 0.0;

	BRect graphRect(rect);
	graphRect.InsetBy(5, 3);
	parent->StrokeRoundRect(graphRect, 2.5, 2.5);

	if (percentValue > 0.0) {
		graphRect.InsetBy(1, 1);
		double value = graphRect.Width() * percentValue;
		graphRect.right = graphRect.left + value;
		parent->SetHighUIColor(B_NAVIGATION_BASE_COLOR);
		parent->FillRect(graphRect);
	}

	parent->SetDrawingMode(B_OP_INVERT);
	parent->SetHighColor(128, 128, 128);

	BString percentString;
	fNumberFormat.FormatPercent(percentString, percentValue);
	float width = be_plain_font->StringWidth(percentString);

	parent->MovePenTo(rect.left + rect.Width() / 2 - width / 2, rect.bottom - FontHeight());
	parent->DrawString(percentString.String());
}


// #pragma mark - BBitmapField


/**
 * @brief Construct a BBitmapField wrapping a BBitmap pointer.
 *
 * The field does not take ownership of the bitmap; the caller is responsible
 * for the bitmap's lifetime.
 *
 * @param bitmap The bitmap to display; may be NULL.
 */
BBitmapField::BBitmapField(BBitmap* bitmap)
	:
	fBitmap(bitmap)
{
}


/**
 * @brief Return the bitmap stored in this field.
 *
 * @return A const pointer to the current BBitmap, or NULL if none is set.
 */
const BBitmap*
BBitmapField::Bitmap()
{
	return fBitmap;
}


/**
 * @brief Replace the bitmap stored in this field.
 *
 * The field does not free the previous bitmap; the caller manages memory.
 *
 * @param bitmap The new bitmap to store; may be NULL.
 */
void
BBitmapField::SetBitmap(BBitmap* bitmap)
{
	fBitmap = bitmap;
}


// #pragma mark - BBitmapColumn


/**
 * @brief Construct a BBitmapColumn with display constraints.
 *
 * @param title    Column header text.
 * @param width    Initial column width in pixels.
 * @param minWidth Minimum column width in pixels.
 * @param maxWidth Maximum column width in pixels.
 * @param align    Horizontal alignment of the bitmap within the cell.
 */
BBitmapColumn::BBitmapColumn(const char* title, float width, float minWidth,
	float maxWidth, alignment align)
	:
	BTitledColumn(title, width, minWidth, maxWidth, align)
{
}


/**
 * @brief Draw a BBitmapField's bitmap centred vertically in a cell.
 *
 * Positions the bitmap horizontally according to the column's alignment setting
 * and sets the appropriate drawing mode (B_OP_ALPHA for bitmaps with an alpha
 * channel, B_OP_OVER otherwise). The previous drawing mode is restored after
 * the bitmap is drawn.
 *
 * @param field  The BBitmapField to render.
 * @param rect   The cell bounding rectangle.
 * @param parent The view to draw into.
 */
void
BBitmapColumn::DrawField(BField* field, BRect rect, BView* parent)
{
	BBitmapField* bitmapField = static_cast<BBitmapField*>(field);
	const BBitmap* bitmap = bitmapField->Bitmap();

	if (bitmap != NULL) {
		float x = 0.0;
		BRect r = bitmap->Bounds();
		float y = rect.top + ((rect.Height() - r.Height()) / 2);

		switch (Alignment()) {
			default:
			case B_ALIGN_LEFT:
				x = rect.left + kTEXT_MARGIN;
				break;

			case B_ALIGN_CENTER:
				x = rect.left + ((rect.Width() - r.Width()) / 2);
				break;

			case B_ALIGN_RIGHT:
				x = rect.right - kTEXT_MARGIN - r.Width();
				break;
		}
		// setup drawing mode according to bitmap color space,
		// restore previous mode after drawing
		drawing_mode oldMode = parent->DrawingMode();
		if (bitmap->ColorSpace() == B_RGBA32
			|| bitmap->ColorSpace() == B_RGBA32_BIG) {
			parent->SetDrawingMode(B_OP_ALPHA);
			parent->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		} else {
			parent->SetDrawingMode(B_OP_OVER);
		}

		parent->DrawBitmap(bitmap, BPoint(x, y));

		parent->SetDrawingMode(oldMode);
	}
}


/**
 * @brief Compare two BBitmapFields for sort ordering.
 *
 * Bitmap fields have no meaningful ordering, so this always returns 0.
 *
 * @param field1 Unused.
 * @param field2 Unused.
 * @return Always 0.
 */
int
BBitmapColumn::CompareFields(BField* /*field1*/, BField* /*field2*/)
{
	// Comparing bitmaps doesn't really make sense...
	return 0;
}


/**
 * @brief Return whether this column accepts a given field type.
 *
 * @param field The field to test.
 * @return true if \a field is a BBitmapField, false otherwise.
 */
bool
BBitmapColumn::AcceptsField(const BField *field) const
{
	return static_cast<bool>(dynamic_cast<const BBitmapField*>(field));
}
