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
 *   Copyright 2012, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfStackFrameDebugInfo.cpp
 * @brief Implementation of DwarfStackFrameDebugInfo plus the per-stack-frame
 *        ObjectID subclasses used to identify parameters, locals, and
 *        return values.
 *
 * Each Variable produced by this object carries an ObjectID derived from
 * the surrounding function and the variable's source-level coordinates so
 * UI bookkeeping (watch-list selection, UI persistence) survives across
 * frames. The actual type construction is delegated to a DwarfTypeFactory
 * created during Init().
 *
 * @see StackFrameDebugInfo, DwarfTypeFactory, DwarfTypeContext
 */


#include "DwarfStackFrameDebugInfo.h"

#include <new>

#include "Architecture.h"
#include "CompilationUnit.h"
#include "CpuState.h"
#include "DebugInfoEntries.h"
#include "Dwarf.h"
#include "DwarfFile.h"
#include "DwarfTargetInterface.h"
#include "DwarfTypeFactory.h"
#include "DwarfUtils.h"
#include "DwarfTypes.h"
#include "FunctionID.h"
#include "FunctionParameterID.h"
#include "GlobalTypeLookup.h"
#include "LocalVariableID.h"
#include "Register.h"
#include "RegisterMap.h"
#include "ReturnValueID.h"
#include "Tracing.h"
#include "ValueLocation.h"
#include "Variable.h"


// #pragma mark - DwarfFunctionParameterID


/**
 * @brief Stable identifier for a function parameter, keyed on function ID
 *        and parameter name.
 */
struct DwarfStackFrameDebugInfo::DwarfFunctionParameterID
	: public FunctionParameterID {

	/** @brief Constructs the ID and acquires a reference on @a functionID. */
	DwarfFunctionParameterID(FunctionID* functionID, const BString& name)
		:
		fFunctionID(functionID),
		fName(name)
	{
		fFunctionID->AcquireReference();
	}

	/** @brief Releases the held FunctionID reference. */
	virtual ~DwarfFunctionParameterID()
	{
		fFunctionID->ReleaseReference();
	}

	/** @brief Equality across ObjectID hierarchy via dynamic_cast. */
	virtual bool operator==(const ObjectID& other) const
	{
		const DwarfFunctionParameterID* parameterID
			= dynamic_cast<const DwarfFunctionParameterID*>(&other);
		return parameterID != NULL && *fFunctionID == *parameterID->fFunctionID
			&& fName == parameterID->fName;
	}

protected:
	/** @brief Combines the function and parameter name hashes. */
	virtual uint32 ComputeHashValue() const
	{
		uint32 hash = fFunctionID->HashValue();
		return hash * 19 + fName.HashValue();
	}

private:
	FunctionID*		fFunctionID;
	const BString	fName;
};


// #pragma mark - DwarfLocalVariableID


/**
 * @brief Stable identifier for a local variable, keyed on function, name
 *        and declaration line/column.
 */
struct DwarfStackFrameDebugInfo::DwarfLocalVariableID : public LocalVariableID {

	/** @brief Constructs the ID and acquires a reference on @a functionID. */
	DwarfLocalVariableID(FunctionID* functionID, const BString& name,
		int32 line, int32 column)
		:
		fFunctionID(functionID),
		fName(name),
		fLine(line),
		fColumn(column)
	{
		fFunctionID->AcquireReference();
	}

	/** @brief Releases the held FunctionID reference. */
	virtual ~DwarfLocalVariableID()
	{
		fFunctionID->ReleaseReference();
	}

	/** @brief Equality across ObjectID hierarchy via dynamic_cast. */
	virtual bool operator==(const ObjectID& other) const
	{
		const DwarfLocalVariableID* otherID
			= dynamic_cast<const DwarfLocalVariableID*>(&other);
		return otherID != NULL && *fFunctionID == *otherID->fFunctionID
			&& fName == otherID->fName && fLine == otherID->fLine
			&& fColumn == otherID->fColumn;
	}

protected:
	/** @brief Combines the function, name, line and column hashes. */
	virtual uint32 ComputeHashValue() const
	{
		uint32 hash = fFunctionID->HashValue();
		hash = hash * 19 + fName.HashValue();
		hash = hash * 19 + fLine;
		hash = hash * 19 + fColumn;
		return hash;
	}

private:
	FunctionID*		fFunctionID;
	const BString	fName;
	int32			fLine;
	int32			fColumn;
};


// #pragma mark - DwarfReturnValueID


/**
 * @brief Stable identifier for a function's synthetic return-value entry.
 */
struct DwarfStackFrameDebugInfo::DwarfReturnValueID
	: public ReturnValueID {

	/** @brief Constructs the ID with a fixed sentinel name "(returned)". */
	DwarfReturnValueID(FunctionID* functionID)
		:
		fFunctionID(functionID),
		fName("(returned)")
	{
		fFunctionID->AcquireReference();
	}

	/** @brief Releases the held FunctionID reference. */
	virtual ~DwarfReturnValueID()
	{
		fFunctionID->ReleaseReference();
	}

	/** @brief Equality across ObjectID hierarchy via dynamic_cast. */
	virtual bool operator==(const ObjectID& other) const
	{
		const DwarfReturnValueID* returnValueID
			= dynamic_cast<const DwarfReturnValueID*>(&other);
		return returnValueID != NULL
			&& *fFunctionID == *returnValueID->fFunctionID
			&& fName == returnValueID->fName;
	}

protected:
	/** @brief Combines the function and sentinel name hashes. */
	virtual uint32 ComputeHashValue() const
	{
		uint32 hash = fFunctionID->HashValue();
		return hash * 25 + fName.HashValue();
	}

private:
	FunctionID*		fFunctionID;
	const BString	fName;
};


// #pragma mark - DwarfStackFrameDebugInfo


/**
 * @brief Constructs the DWARF-driven stack frame debug info.
 *
 * Builds a DwarfTypeContext that holds the live PC/FP for the frame; the
 * type factory is created later in Init() with a stripped-down context so
 * type construction does not depend on the frame.
 *
 * @param architecture          Target architecture.
 * @param imageID               Image identifier of the function's image.
 * @param file                  DWARF file containing the data.
 * @param compilationUnit       Compilation unit for @a subprogramEntry.
 * @param subprogramEntry       Subprogram DIE for the active function.
 * @param typeLookup            Cross-image type resolver.
 * @param typeCache             Global type cache; reference acquired.
 * @param instructionPointer    Live PC, used by location expressions.
 * @param framePointer          Live FP, used by location expressions.
 * @param relocationDelta       Image relocation offset.
 * @param targetInterface       Target memory/register access interface.
 * @param fromDwarfRegisterMap  Mapping from DWARF register numbers to the
 *                              architecture's register numbering.
 */
DwarfStackFrameDebugInfo::DwarfStackFrameDebugInfo(Architecture* architecture,
	image_id imageID, DwarfFile* file, CompilationUnit* compilationUnit,
	DIESubprogram* subprogramEntry, GlobalTypeLookup* typeLookup,
	GlobalTypeCache* typeCache, target_addr_t instructionPointer,
	target_addr_t framePointer, target_addr_t relocationDelta,
	DwarfTargetInterface* targetInterface, RegisterMap* fromDwarfRegisterMap)
	:
	StackFrameDebugInfo(),
	fTypeContext(new(std::nothrow) DwarfTypeContext(architecture, imageID, file,
		compilationUnit, subprogramEntry, instructionPointer, framePointer,
		relocationDelta, targetInterface, fromDwarfRegisterMap)),
	fTypeLookup(typeLookup),
	fTypeCache(typeCache)
{
	fTypeCache->AcquireReference();
}


/**
 * @brief Destroys the object, releasing the type cache, type context and
 *        deleting the type factory.
 */
DwarfStackFrameDebugInfo::~DwarfStackFrameDebugInfo()
{
	fTypeCache->ReleaseReference();

	if (fTypeContext != NULL)
		fTypeContext->ReleaseReference();

	delete fTypeFactory;
}


/**
 * @brief Allocates the per-frame type factory.
 *
 * Builds a sibling DwarfTypeContext without PC/FP/subprogram so type
 * resolution uses the right architecture and register map without
 * depending on transient frame state.
 *
 * @retval B_OK         Initialization succeeded.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfStackFrameDebugInfo::Init()
{
	if (fTypeContext == NULL)
		return B_NO_MEMORY;

	// create a type context without dependency to the stack frame
	DwarfTypeContext* typeContext = new(std::nothrow) DwarfTypeContext(
		fTypeContext->GetArchitecture(), fTypeContext->ImageID(),
		fTypeContext->File(), fTypeContext->GetCompilationUnit(), NULL, 0, 0,
		fTypeContext->RelocationDelta(), fTypeContext->TargetInterface(),
		fTypeContext->FromDwarfRegisterMap());
	if (typeContext == NULL)
		return B_NO_MEMORY;
	BReference<DwarfTypeContext> typeContextReference(typeContext, true);

	// create the type factory
	fTypeFactory = new(std::nothrow) DwarfTypeFactory(typeContext, fTypeLookup,
		fTypeCache);
	if (fTypeFactory == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Constructs a Variable describing one function parameter.
 *
 * Resolves the parameter's name and type, then creates a
 * DwarfFunctionParameterID and delegates to _CreateVariable() to compute
 * its location.
 *
 * @param functionID      Identifier of the enclosing function.
 * @param parameterEntry  DWARF formal parameter DIE.
 * @param _parameter      Out parameter receiving the new Variable;
 *                        reference transferred to caller.
 * @retval B_OK              Variable created successfully.
 * @retval B_NO_MEMORY       Allocation failure.
 * @retval B_BAD_VALUE       The parameter has no associated DIEType.
 * @retval other             Errors from the type factory or location
 *                           resolution.
 */
status_t
DwarfStackFrameDebugInfo::CreateParameter(FunctionID* functionID,
	DIEFormalParameter* parameterEntry, Variable*& _parameter)
{
	// get the name
	BString name;
	DwarfUtils::GetDIEName(parameterEntry, name);

	TRACE_LOCALS("DwarfStackFrameDebugInfo::CreateParameter(DIE: %p): name: "
		"\"%s\"\n", parameterEntry, name.String());

	// create the ID
	DwarfFunctionParameterID* id = new(std::nothrow) DwarfFunctionParameterID(
		functionID, name);
	if (id == NULL)
		return B_NO_MEMORY;
	BReference<DwarfFunctionParameterID> idReference(id, true);

	// create the variable
	return _CreateVariable(id, name, _GetDIEType(parameterEntry),
		parameterEntry->GetLocationDescription(), _parameter);
}


/**
 * @brief Constructs a Variable describing one local variable.
 *
 * Resolves declaration source location for ID stability, builds a
 * DwarfLocalVariableID, then delegates to _CreateVariable() for type and
 * location resolution.
 *
 * @param functionID     Identifier of the enclosing function.
 * @param variableEntry  DWARF variable DIE.
 * @param _variable      Out parameter receiving the new Variable;
 *                       reference transferred to caller.
 * @return Status; same conventions as CreateParameter().
 */
status_t
DwarfStackFrameDebugInfo::CreateLocalVariable(FunctionID* functionID,
	DIEVariable* variableEntry, Variable*& _variable)
{
	// get the name
	BString name;
	DwarfUtils::GetDIEName(variableEntry, name);

	TRACE_LOCALS("DwarfStackFrameDebugInfo::CreateLocalVariable(DIE: %p): "
		"name: \"%s\"\n", variableEntry, name.String());

	// get the declaration location
	int32 line = -1;
	int32 column = -1;
	const char* file;
	const char* directory;
	DwarfUtils::GetDeclarationLocation(fTypeContext->File(), variableEntry,
		directory, file, line, column);
		// TODO: If the declaration location is unavailable, we should probably
		// add a component to the ID to make it unique nonetheless (the name
		// might not suffice).

	// create the ID
	DwarfLocalVariableID* id = new(std::nothrow) DwarfLocalVariableID(
		functionID, name, line, column);
	if (id == NULL)
		return B_NO_MEMORY;
	BReference<DwarfLocalVariableID> idReference(id, true);

	// create the variable
	return _CreateVariable(id, name, _GetDIEType(variableEntry),
		variableEntry->GetLocationDescription(), _variable);
}


/**
 * @brief Constructs a Variable describing the function's return value.
 *
 * Builds a DwarfType for @a returnType and a DwarfReturnValueID, then
 * wraps them together with the supplied location and CPU state in a new
 * Variable.
 *
 * @param functionID   Identifier of the function whose return is described.
 * @param returnType   DIEType describing the return type.
 * @param location     ValueLocation describing where the return value
 *                     resides; ownership convention follows Variable.
 * @param state        CPU state captured at the return point.
 * @param _variable    Out parameter receiving the new Variable; reference
 *                     transferred to caller.
 * @retval B_OK         Variable created.
 * @retval B_BAD_VALUE  @a returnType is @c NULL.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Errors from the type factory.
 */
status_t
DwarfStackFrameDebugInfo::CreateReturnValue(FunctionID* functionID,
	DIEType* returnType, ValueLocation* location, CpuState* state,
	Variable*& _variable)
{
	if (returnType == NULL)
		return B_BAD_VALUE;

	// create the type
	DwarfType* type;
	status_t error = fTypeFactory->CreateType(returnType, type);
	if (error != B_OK)
		return error;
	BReference<DwarfType> typeReference(type, true);

	DwarfReturnValueID* id = new(std::nothrow) DwarfReturnValueID(
		functionID);
	if (id == NULL)
		return B_NO_MEMORY;

	BString name;
	name.SetToFormat("%s returned", functionID->FunctionName().String());

	Variable* variable = new(std::nothrow) Variable(id, name,
		type, location, state);
	if (variable == NULL)
		return B_NO_MEMORY;

	_variable = variable;

	return B_OK;
}


/**
 * @brief Shared back-end used by CreateParameter() and
 *        CreateLocalVariable() to build a Variable.
 *
 * Constructs a DwarfType from @a typeEntry, allocates a ValueLocation
 * matching the architecture's endianness, and invokes the type's
 * ResolveLocation() if the DWARF location description is valid. The
 * resulting Variable adopts ownership of @a id and the constructed type.
 *
 * @param id                   ObjectID identifying the variable; ownership
 *                             passed to Variable on success.
 * @param name                 Variable name for display.
 * @param typeEntry            DIE describing the variable's type.
 * @param locationDescription  DWARF location description; may be invalid.
 * @param _variable            Out parameter receiving the new Variable;
 *                             reference transferred to caller.
 * @retval B_OK         Variable created successfully.
 * @retval B_BAD_VALUE  @a typeEntry is @c NULL.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Errors from the type factory or location resolution.
 */
status_t
DwarfStackFrameDebugInfo::_CreateVariable(ObjectID* id, const BString& name,
	DIEType* typeEntry, LocationDescription* locationDescription,
	Variable*& _variable)
{
	if (typeEntry == NULL)
		return B_BAD_VALUE;

	// create the type
	DwarfType* type;
	status_t error = fTypeFactory->CreateType(typeEntry, type);
	if (error != B_OK)
		return error;
	BReference<DwarfType> typeReference(type, true);

	// get the location, if possible
	ValueLocation* location = new(std::nothrow) ValueLocation(
		fTypeContext->GetArchitecture()->IsBigEndian());
	if (location == NULL)
		return B_NO_MEMORY;
	BReference<ValueLocation> locationReference(location, true);

	if (locationDescription->IsValid()) {
		status_t error = type->ResolveLocation(fTypeContext,
			locationDescription, 0, false, *location);
		if (error != B_OK)
			return error;

		TRACE_LOCALS_ONLY(location->Dump());
	}

	// create the variable
	Variable* variable = new(std::nothrow) Variable(id, name, type, location);
	if (variable == NULL)
		return B_NO_MEMORY;

	_variable = variable;
	return B_OK;
}


/**
 * @brief Returns the DIEType associated with a DWARF entry, following
 *        @c DW_AT_abstract_origin and @c DW_AT_specification chains when
 *        the entry itself does not carry a type attribute.
 *
 * @tparam EntryType  Either DIEFormalParameter or DIEVariable; the only
 *                    instantiations used by this translation unit.
 * @param entry       Entry whose type is being resolved.
 * @return DIEType pointer or @c NULL when no type is available anywhere
 *         along the chain.
 */
template<typename EntryType>
/*static*/ DIEType*
DwarfStackFrameDebugInfo::_GetDIEType(EntryType* entry)
{
	if (DIEType* typeEntry = entry->GetType())
		return typeEntry;

	if (EntryType* abstractOrigin = dynamic_cast<EntryType*>(
			entry->AbstractOrigin())) {
		entry = abstractOrigin;
		if (DIEType* typeEntry = entry->GetType())
			return typeEntry;
	}

	if (EntryType* specification = dynamic_cast<EntryType*>(
			entry->Specification())) {
		entry = specification;
		if (DIEType* typeEntry = entry->GetType())
			return typeEntry;
	}

	return NULL;
}
