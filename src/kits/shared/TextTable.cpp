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
 *   Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */

/** @file TextTable.cpp
 *  @brief Implements TextTable, a simple plain-text table formatter that prints
 *         column-aligned, optionally truncated tabular data to stdout.
 */


#include <TextTable.h>

#include <stdio.h>
#include <ctype.h>
#include <utf8_functions.h>

#include <algorithm>


namespace BPrivate {


// #pragma mark - Column


/** @brief Represents a single column in a TextTable, tracking its title,
 *         alignment, truncation policy, and computed display width.
 */
struct TextTable::Column {
	/** @brief Constructs a Column with the given properties.
	 *  @param title       The column header string.
	 *  @param align       Text alignment within the column.
	 *  @param canTruncate Whether this column may be truncated to fit maxWidth.
	 */
	Column(const BString& title, enum alignment align, bool canTruncate)
		:
		fTitle(title),
		fAlignment(align),
		fCanBeTruncated(canTruncate),
		fNeededWidth(0),
		fWidth(0)
	{
		UpdateNeededWidth(fTitle);
		fMinWidth = fNeededWidth;
	}

	/** @brief Returns the column title string. */
	const BString& Title() const
	{
		return fTitle;
	}

	/** @brief Returns the text alignment of the column. */
	enum alignment Alignment() const
	{
		return fAlignment;
	}

	/** @brief Returns whether this column may be truncated during Print(). */
	bool CanBeTruncated() const
	{
		return fCanBeTruncated;
	}

	/** @brief Returns the maximum content width seen so far (in display columns). */
	int32 NeededWidth() const
	{
		return fNeededWidth;
	}

	/** @brief Returns the minimum width (equal to the title width). */
	int32 MinWidth() const
	{
		return fMinWidth;
	}

	/** @brief Returns the currently assigned display width. */
	int32 Width() const
	{
		return fWidth;
	}

	/** @brief Sets the display width used during formatting.
	 *  @param width  The desired display width in character columns.
	 */
	void SetWidth(int32 width)
	{
		fWidth = width;
	}

	/** @brief Computes the visible display width of a UTF-8 string.
	 *
	 *  Counts printable character positions while skipping ANSI escape
	 *  sequences.  Full-width character support is not yet implemented.
	 *
	 *  @param text  The string whose display width is to be measured.
	 *  @return Number of terminal display columns occupied by the string.
	 */
	static int32 TextWidth(const BString& text)
	{
		// TODO: Full-width character support.
		int32 textWidth = 0;
		const char* string = text.String(), *stringEnd = text.String() + text.Length();
		while (string < stringEnd) {
			uint32 charLen = UTF8NextCharLen(string, stringEnd - string);
			if (charLen == 1 && string[0] == '\033') {
				// ANSI escape code.
				charLen++;
				if (string[charLen - 1] == '[') {
					// Keep going until we hit an end character.
					while (!isalpha(string[charLen - 1]) && string[charLen - 1] != '\0')
						charLen++;
				}
			} else {
				textWidth++;
			}
			string += charLen;
		}
		return textWidth;
	}

	/** @brief Updates the needed width if the given text is wider than current.
	 *  @param text  The candidate text string.
	 */
	void UpdateNeededWidth(const BString& text)
	{
		int32 textWidth = TextWidth(text);
		if (textWidth > fNeededWidth)
			fNeededWidth = textWidth;
	}

	/** @brief Formats \a text to exactly fWidth display columns.
	 *
	 *  If the text is wider than fWidth it is truncated.  If it is narrower,
	 *  spaces are added according to the column's alignment setting.
	 *
	 *  @param text  The text to format.
	 *  @return A new BString padded or truncated to fWidth columns.
	 */
	BString Format(const BString& text)
	{
		int32 textWidth = TextWidth(text);
		if (textWidth == fWidth)
			return text;

		// truncate, if too long
		if (textWidth > fWidth) {
			BString result(text);
			result.TruncateChars(fWidth);
			return result;
		}

		// align, if too short
		int32 missing = fWidth - textWidth;
		switch (fAlignment) {
			case B_ALIGN_LEFT:
			default:
			{
				BString result(text);
				result.Append(' ', missing);
				return result;
			}

			case B_ALIGN_RIGHT:
			{
				BString result;
				result.Append(' ', missing);
				result.Append(text);
				return result;
			}

			case B_ALIGN_CENTER:
			{
				BString result;
				result.Append(' ', missing / 2);
				result.Append(text);
				result.Append(' ', missing - missing / 2);
				return result;
			}
		}
	}

private:
	BString			fTitle;
	enum alignment fAlignment;
	bool			fCanBeTruncated;
	int32			fNeededWidth;
	int32			fMinWidth;
	int32			fWidth;
};


// #pragma mark - TextTable


/** @brief Constructs an empty TextTable. */
TextTable::TextTable()
	:
	fColumns(10),
	fRows(100)
{
}


/** @brief Destructor. */
TextTable::~TextTable()
{
}


/** @brief Returns the number of columns in the table.
 *  @return Column count.
 */
int32
TextTable::CountColumns() const
{
	return fColumns.CountItems();
}


/** @brief Appends a new column to the table.
 *
 *  Throws std::bad_alloc if memory allocation fails.
 *
 *  @param title        The column header text.
 *  @param align        Text alignment within the column.
 *  @param canTruncate  True if the column may be shortened to fit maxWidth.
 */
void
TextTable::AddColumn(const BString& title, enum alignment align,
	bool canTruncate)
{
	Column* column = new Column(title, align, canTruncate);
	if (!fColumns.AddItem(column)) {
		delete column;
		throw std::bad_alloc();
	}
}


/** @brief Returns the number of data rows in the table.
 *  @return Row count.
 */
int32
TextTable::CountRows() const
{
	return fRows.CountItems();
}


/** @brief Returns the text stored at the given row and column.
 *  @param rowIndex     Zero-based row index.
 *  @param columnIndex  Zero-based column index.
 *  @return The stored BString, or an empty string if out of range.
 */
BString
TextTable::TextAt(int32 rowIndex, int32 columnIndex) const
{
	BStringList* row = fRows.ItemAt(rowIndex);
	if (row == NULL)
		return BString();
	return row->StringAt(columnIndex);
}


/** @brief Stores a text value at the specified cell, expanding rows as needed.
 *
 *  Rows are created on demand up to \a rowIndex.  Throws std::bad_alloc on
 *  allocation failure.
 *
 *  @param rowIndex     Zero-based row index (rows are created if missing).
 *  @param columnIndex  Zero-based column index (columns are padded if missing).
 *  @param text         The text to store.
 */
void
TextTable::SetTextAt(int32 rowIndex, int32 columnIndex, const BString& text)
{
	// If necessary append empty rows up to the specified row index.
	while (rowIndex >= fRows.CountItems()) {
		BStringList* row = new BStringList();
		if (!fRows.AddItem(row)) {
			delete row;
			throw std::bad_alloc();
		}
	}

	// If necessary append empty strings up to the specified column index.
	BStringList* row = fRows.ItemAt(rowIndex);
	while (columnIndex >= row->CountStrings()) {
		if (!row->Add(BString()))
			throw std::bad_alloc();
	}

	// set the text
	if (!row->Replace(columnIndex, text))
		throw std::bad_alloc();
}


/** @brief Prints the table to stdout, optionally constraining total width.
 *
 *  Computes the maximum needed width of each column from all row data,
 *  then proportionally truncates truncatable columns if the total exceeds
 *  \a maxWidth.  Outputs a header row, a separator line, and then all data
 *  rows.
 *
 *  @param maxWidth  Maximum total output width in display columns.  Set to
 *                   INT32_MAX to allow unlimited width.
 */
void
TextTable::Print(int32 maxWidth)
{
	int32 columnCount = fColumns.CountItems();
	if (columnCount == 0)
		return;

	// determine the column widths
	int32 rowCount = fRows.CountItems();
	for (int32 rowIndex = 0; rowIndex < rowCount; rowIndex++) {
		BStringList* row = fRows.ItemAt(rowIndex);
		int32 rowColumnCount = std::min(row->CountStrings(), columnCount);
		for (int32 columnIndex = 0; columnIndex < rowColumnCount;
			columnIndex++) {
			fColumns.ItemAt(columnIndex)->UpdateNeededWidth(
				row->StringAt(columnIndex));
		}
	}

	int32 neededWidth = (columnCount - 1) * 2;
		// spacing
	for (int32 i = 0; i < columnCount; i++)
		neededWidth += fColumns.ItemAt(i)->NeededWidth();

	int32 width = neededWidth;
	int32 missingWidth = neededWidth - std::min(maxWidth, neededWidth);

	for (int32 i = 0; i < columnCount; i++) {
		Column* column = fColumns.ItemAt(i);
		if (missingWidth > 0 && column->CanBeTruncated()) {
			int32 truncateBy = std::min(missingWidth,
				column->NeededWidth() - column->MinWidth());
			column->SetWidth(column->NeededWidth() - truncateBy);
			missingWidth -= truncateBy;
			width -= truncateBy;
		} else
			column->SetWidth(column->NeededWidth());
	}

	// print the header
	BString line;
	for (int32 i = 0; i < columnCount; i++) {
		if (i > 0)
			line << "  ";

		Column* column = fColumns.ItemAt(i);
		line << column->Format(column->Title());
	}
	line << '\n';
	fputs(line.String(), stdout);

	line.SetTo('-', width);
	line << '\n';
	fputs(line.String(), stdout);

	// print the rows
	for (int32 rowIndex = 0; rowIndex < rowCount; rowIndex++) {
		line.Truncate(0);
		BStringList* row = fRows.ItemAt(rowIndex);
		for (int32 columnIndex = 0; columnIndex < columnCount; columnIndex++) {
			if (columnIndex > 0)
				line << "  ";

			line << fColumns.ItemAt(columnIndex)->Format(
				row->StringAt(columnIndex));
		}

		line << '\n';
		fputs(line.String(), stdout);
	}
}


} // namespace BPrivate
