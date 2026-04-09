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
 *   Copyright (c) 2001-2005, Haiku, Inc.
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
 */


/**
 * @file Flattenable.cpp
 * @brief Base implementation of BFlattenable, the byte-serialisation interface.
 *
 * BFlattenable provides a protocol for converting objects to and from a
 * flat (contiguous) byte representation. Subclasses must implement
 * TypeCode(), Flatten(), Unflatten(), and FlattenedSize(). This file
 * provides the default AllowsTypeCode() implementation and the virtual
 * destructor.
 *
 * @see BFlattenable, BMessage
 */


#include <stdio.h>

#include <Flattenable.h>


/**
 * @brief Test whether this object can be unflattened from the given type code.
 *
 * The default implementation returns true only when \a code exactly matches
 * the value returned by TypeCode(). Subclasses that can decode multiple
 * compatible wire formats should override this method to broaden acceptance.
 *
 * @param code The type_code value to test.
 * @return true if \a code equals TypeCode(); false otherwise.
 * @see TypeCode(), Unflatten()
 */
bool
BFlattenable::AllowsTypeCode(type_code code) const
{
	return (TypeCode() == code);
}


/**
 * @brief Destructor.
 *
 * Virtual destructor ensuring correct cleanup in derived classes.
 * No resources are owned directly by BFlattenable itself.
 */
BFlattenable::~BFlattenable()
{
}


void BFlattenable::_ReservedFlattenable1() {}
void BFlattenable::_ReservedFlattenable2() {}
void BFlattenable::_ReservedFlattenable3() {}
