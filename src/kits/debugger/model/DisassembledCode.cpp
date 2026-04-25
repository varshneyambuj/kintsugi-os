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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DisassembledCode.cpp
 * @brief Implementation of DisassembledCode, a SourceCode realisation that
 *        renders a function as one line per disassembled instruction.
 *
 * DisassembledCode keeps the rendered text and a parallel array of
 * ContiguousStatements pointing back at the instruction's address range,
 * so the source view can map line numbers to code addresses (and vice
 * versa) for stepping and breakpoint placement.
 */


#include "DisassembledCode.h"

#include <stdlib.h>
#include <string.h>

#include <new>

#include <String.h>

#include "SourceLanguage.h"
#include "Statement.h"


/**
 * @brief One rendered text line, optionally tied to a ContiguousStatement.
 *
 * Comment lines have a NULL @c statement; instruction lines reference the
 * statement that gives them an address range.
 */
struct DisassembledCode::Line {
	BString					line;
	ContiguousStatement*	statement;

	/**
	 * @brief Constructs a Line.
	 *
	 * @param line      Rendered text content.
	 * @param statement Owning statement for instruction lines, or NULL.
	 */
	Line(const BString& line, ContiguousStatement* statement)
		:
		line(line),
		statement(statement)
	{
	}
};


/**
 * @brief Constructs an empty DisassembledCode tagged with @a language.
 *
 * @param language Source language to report; reference acquired.
 */
DisassembledCode::DisassembledCode(SourceLanguage* language)
	:
	fLanguage(language),
	fLines(20)
{
	fLanguage->AcquireReference();
}


/**
 * @brief Releases statement references and the language reference.
 */
DisassembledCode::~DisassembledCode()
{
	for (int32 i = 0; Statement* statement = fStatements.ItemAt(i); i++)
		statement->ReleaseReference();

	fLanguage->ReleaseReference();
}


/**
 * @brief Pseudo-lock acquisition (the object is immutable after construction).
 *
 * @return Always true.
 */
bool
DisassembledCode::Lock()
{
	// We're immutable, so no locking required.
	return true;
}


/**
 * @brief Counterpart to @c Lock(): a no-op since the object is immutable.
 */
void
DisassembledCode::Unlock()
{
}


/**
 * @brief Returns the SourceLanguage tag attached to the code.
 *
 * @return The owned SourceLanguage pointer.
 */
SourceLanguage*
DisassembledCode::GetSourceLanguage() const
{
	return fLanguage;
}


/**
 * @brief Returns the number of rendered lines.
 *
 * @return Line count, including comment lines.
 */
int32
DisassembledCode::CountLines() const
{
	return fLines.CountItems();
}


/**
 * @brief Returns the text of the @a index'th rendered line.
 *
 * @param index Zero-based line index.
 * @return     NUL-terminated text, or NULL if @a index is out of range.
 */
const char*
DisassembledCode::LineAt(int32 index) const
{
	Line* line = fLines.ItemAt(index);
	return line != NULL ? line->line.String() : NULL;
}


/**
 * @brief Returns the byte length of the @a index'th rendered line.
 *
 * @param index Zero-based line index.
 * @return     Length in bytes, or 0 if out of range.
 */
int32
DisassembledCode::LineLengthAt(int32 index) const
{
	Line* line = fLines.ItemAt(index);
	return line != NULL ? line->line.Length() : 0;
}


/**
 * @brief Computes the source-location range covering the statement at @a location.
 *
 * For DisassembledCode each instruction occupies a single line, so the
 * range is always one line wide.
 *
 * @param location Query location (line index).
 * @param _start   Receives the statement start.
 * @param _end     Receives the statement end (exclusive).
 * @return        True if the line carries a statement, false otherwise.
 *
 * @todo Multi-line instructions for variable-length representations.
 */
bool
DisassembledCode::GetStatementLocationRange(const SourceLocation& location,
	SourceLocation& _start, SourceLocation& _end) const
{
	Line* line = fLines.ItemAt(location.Line());
	if (line == NULL || line->statement == NULL)
		return false;

	_start = line->statement->StartSourceLocation();
	_end = SourceLocation(_start.Line() + 1);
		// TODO: Multi-line instructions!
	return true;
}


/**
 * @brief Returns NULL: disassembled code has no on-disk source backing.
 *
 * @return Always NULL.
 */
LocatableFile*
DisassembledCode::GetSourceFile() const
{
	return NULL;
}


/**
 * @brief Returns the statement attached to @a location.
 *
 * @param location Query line index.
 * @return        The statement, or NULL for comment-only lines.
 */
Statement*
DisassembledCode::StatementAtLocation(const SourceLocation& location) const
{
	Line* line = fLines.ItemAt(location.Line());
	return line != NULL ? line->statement : NULL;
}


/**
 * @brief Looks up the statement covering @a address via binary search.
 *
 * @param address Target-space address.
 * @return       The statement covering @a address, or NULL if none.
 */
Statement*
DisassembledCode::StatementAtAddress(target_addr_t address) const
{
	return fStatements.BinarySearchByKey(address, &_CompareAddressStatement);
}


/**
 * @brief Returns the address range spanned by all instruction statements.
 *
 * @return Range from the first to the last statement, or empty if none.
 */
TargetAddressRange
DisassembledCode::StatementAddressRange() const
{
	if (fStatements.IsEmpty())
		return TargetAddressRange();

	ContiguousStatement* first = fStatements.ItemAt(0);
	ContiguousStatement* last
		= fStatements.ItemAt(fStatements.CountItems() - 1);
	return TargetAddressRange(first->AddressRange().Start(),
		last->AddressRange().End());
}


/**
 * @brief Appends a comment line with no statement attached.
 *
 * @param line Text to render.
 * @return    True on success, false on allocation failure.
 */
bool
DisassembledCode::AddCommentLine(const BString& line)
{
	return _AddLine(line, NULL);
}


/**
 * @brief Appends an instruction line and creates its ContiguousStatement.
 *
 * @param line    Rendered instruction text.
 * @param address Instruction's target-space address.
 * @param size    Size of the instruction in bytes.
 * @return       True on success, false on allocation failure.
 */
bool
DisassembledCode::AddInstructionLine(const BString& line, target_addr_t address,
	target_size_t size)
{
	int32 lineIndex = fLines.CountItems();

	ContiguousStatement* statement = new(std::nothrow) ContiguousStatement(
		SourceLocation(lineIndex), TargetAddressRange(address, size));
	if (statement == NULL)
		return false;

	if (!fStatements.AddItem(statement)) {
		delete statement;
		return false;
	}

	if (!_AddLine(line, statement))
		return false;

	return true;
}


/**
 * @brief Internal helper that appends a Line tying text to an optional statement.
 *
 * @param _line     Rendered text.
 * @param statement Statement reference, or NULL for comment lines.
 * @return         True on success, false on allocation failure.
 */
bool
DisassembledCode::_AddLine(const BString& _line, ContiguousStatement* statement)
{
	Line* line = new(std::nothrow) Line(_line, statement);
	if (line == NULL)
		return false;

	if (!fLines.AddItem(line)) {
		delete line;
		return false;
	}

	return true;
}


/**
 * @brief Comparator locating a statement by address via binary search.
 *
 * @param address   Target address being searched for.
 * @param statement Candidate statement.
 * @return         -1 if @a address is below the statement's range, 0 if
 *                  inside, 1 if above.
 */
/*static*/ int
DisassembledCode::_CompareAddressStatement(const target_addr_t* address,
	const ContiguousStatement* statement)
{
	const TargetAddressRange& range = statement->AddressRange();

	if (*address < range.Start())
		return -1;
	return *address < range.End() ? 0 : 1;
}

