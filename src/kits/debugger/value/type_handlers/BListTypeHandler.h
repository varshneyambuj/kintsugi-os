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
 * MIT License. Copyright 2012-2018, Haiku.
 * Original authors: Rene Gollent.
 */

/** @file BListTypeHandler.h
    @brief Specialised TypeHandler that recognises BList and BObjectList compound types. */

#ifndef BLIST_TYPE_HANDLER_H
#define BLIST_TYPE_HANDLER_H


#include "TypeHandler.h"


/**
 * @brief Routes BList/BObjectList DWARF types to BListValueNode for inspection.
 */
class BListTypeHandler : public TypeHandler {
public:
	virtual					~BListTypeHandler();

	virtual	const char*		Name() const;
	virtual float			SupportsType(Type* type) const;
	virtual status_t		CreateValueNode(ValueNodeChild* nodeChild,
								Type* type, ValueNode*& _node);
};

#endif // BLIST_TYPE_HANDLER_H
