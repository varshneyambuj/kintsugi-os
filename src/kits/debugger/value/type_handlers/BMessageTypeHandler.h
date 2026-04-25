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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2011-2018, Haiku.
 * Original authors: Rene Gollent.
 */

/** @file BMessageTypeHandler.h
    @brief Specialised TypeHandler that recognises BMessage compound types. */

#ifndef BMESSAGE_TYPE_HANDLER_H
#define BMESSAGE_TYPE_HANDLER_H


#include "TypeHandler.h"


/**
 * @brief Routes BMessage DWARF types to BMessageValueNode for field-aware inspection.
 */
class BMessageTypeHandler : public TypeHandler {
public:
	virtual					~BMessageTypeHandler();

	virtual	const char*		Name() const;
	virtual float			SupportsType(Type* type) const;
	virtual status_t		CreateValueNode(ValueNodeChild* nodeChild,
								Type* type, ValueNode*& _node);
};

#endif // BMESSAGE_TYPE_HANDLER_H
