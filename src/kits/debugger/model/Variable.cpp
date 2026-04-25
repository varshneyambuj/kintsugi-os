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
 * @file Variable.cpp
 * @brief Implementation of Variable, a debug-info-resolved program variable.
 *
 * A Variable bundles an immutable identity (ObjectID), display name, debug
 * type, value location (register/memory expression), and an optional
 * CpuState used to evaluate the location. All non-NULL collaborators are
 * referenced-counted by the Variable for the duration of its lifetime.
 */


#include "Variable.h"

#include "CpuState.h"
#include "ObjectID.h"
#include "Type.h"
#include "ValueLocation.h"


/**
 * @brief Constructs a Variable and acquires references to its collaborators.
 *
 * @param id       Identity object distinguishing this variable; reference acquired.
 * @param name     Display name of the variable.
 * @param type     Debug type describing the variable; reference acquired.
 * @param location Resolved value location; reference acquired.
 * @param state    Optional CpuState used for register-relative locations;
 *                 reference acquired when non-NULL.
 */
Variable::Variable(ObjectID* id, const BString& name, Type* type,
	ValueLocation* location, CpuState* state)
	:
	fID(id),
	fName(name),
	fType(type),
	fLocation(location),
	fCpuState(state)
{
	fID->AcquireReference();
	fType->AcquireReference();
	fLocation->AcquireReference();
	if (fCpuState != NULL)
		fCpuState->AcquireReference();
}


/**
 * @brief Releases all references acquired in the constructor.
 */
Variable::~Variable()
{
	fID->ReleaseReference();
	fType->ReleaseReference();
	fLocation->ReleaseReference();
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();
}
