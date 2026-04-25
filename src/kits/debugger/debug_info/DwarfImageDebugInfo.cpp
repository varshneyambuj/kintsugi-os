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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2012-2018, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfImageDebugInfo.cpp
 * @brief Implementation of DwarfImageDebugInfo, the per-image DWARF
 *        backend that drives function enumeration, type lookups,
 *        statement/source resolution and stack-frame construction.
 *
 * The class iterates the DwarfFile's compilation units to materialize
 * DwarfFunctionDebugInfo objects for each declared subprogram, and
 * maintains a name-keyed type table so cross-image type lookups can
 * short-circuit. CreateFrame() unwinds a single frame using the DWARF
 * frame information, then constructs Variables for the parameters,
 * locals and return values via DwarfStackFrameDebugInfo.
 *
 * @see DwarfFile, DwarfFunctionDebugInfo, DwarfStackFrameDebugInfo,
 *      ImageDebugInfo
 */


#include "DwarfImageDebugInfo.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "Architecture.h"
#include "BasicFunctionDebugInfo.h"
#include "CLanguage.h"
#include "CompilationUnit.h"
#include "CppLanguage.h"
#include "CpuState.h"
#include "DebuggerInterface.h"
#include "DebugInfoEntries.h"
#include "Demangler.h"
#include "DisassembledCode.h"
#include "Dwarf.h"
#include "DwarfFile.h"
#include "DwarfFunctionDebugInfo.h"
#include "DwarfStackFrameDebugInfo.h"
#include "DwarfTargetInterface.h"
#include "DwarfTypeFactory.h"
#include "DwarfTypes.h"
#include "DwarfUtils.h"
#include "ElfFile.h"
#include "FileManager.h"
#include "FileSourceCode.h"
#include "FunctionID.h"
#include "FunctionInstance.h"
#include "GlobalTypeLookup.h"
#include "Image.h"
#include "ImageDebugInfo.h"
#include "InstructionInfo.h"
#include "LocatableFile.h"
#include "Register.h"
#include "RegisterMap.h"
#include "SourceFile.h"
#include "StackFrame.h"
#include "Statement.h"
#include "SymbolInfo.h"
#include "TargetAddressRangeList.h"
#include "Team.h"
#include "TeamFunctionSourceInformation.h"
#include "TeamMemory.h"
#include "Tracing.h"
#include "TypeLookupConstraints.h"
#include "UnsupportedLanguage.h"
#include "Variable.h"
#include "ValueLocation.h"


namespace {


// #pragma mark - HasTypePredicate


/**
 * @brief Predicate template selecting DIEs that supply a non-null
 *        @c DW_AT_type attribute.
 */
template<typename EntryType>
struct HasTypePredicate {
	inline bool operator()(EntryType* entry) const
	{
		return entry->GetType() != NULL;
	}
};

}


// #pragma mark - BasicTargetInterface


/**
 * @brief Read-only DwarfTargetInterface used to evaluate DWARF location
 *        expressions when no live CPU state is needed (e.g. for static
 *        memory accesses driven by the team-memory interface).
 *
 * Register reads always fail; ReadMemory() forwards to the team memory.
 * This is the base class for UnwindTargetInterface, which adds register
 * support.
 */
struct DwarfImageDebugInfo::BasicTargetInterface : DwarfTargetInterface {
	/** @brief Caches references and acquires the from-DWARF register map. */
	BasicTargetInterface(const Register* registers, int32 registerCount,
		RegisterMap* fromDwarfMap, Architecture* architecture,
		TeamMemory* teamMemory)
		:
		fRegisters(registers),
		fRegisterCount(registerCount),
		fFromDwarfMap(fromDwarfMap),
		fArchitecture(architecture),
		fTeamMemory(teamMemory)
	{
		fFromDwarfMap->AcquireReference();
	}

	~BasicTargetInterface()
	{
		fFromDwarfMap->ReleaseReference();
	}

	virtual uint32 CountRegisters() const
	{
		return fRegisterCount;
	}

	virtual uint32 RegisterValueType(uint32 index) const
	{
		const Register* reg = _RegisterAt(index);
		return reg != NULL ? reg->ValueType() : 0;
	}

	virtual bool GetRegisterValue(uint32 index, BVariant& _value) const
	{
		return false;
	}

	virtual bool SetRegisterValue(uint32 index, const BVariant& value)
	{
		return false;
	}

	virtual bool IsCalleePreservedRegister(uint32 index) const
	{
		const Register* reg = _RegisterAt(index);
		return reg != NULL && reg->IsCalleePreserved();
	}

	virtual status_t InitRegisterRules(CfaContext& context) const
	{
		return fArchitecture->InitRegisterRules(context);
	}

	virtual bool ReadMemory(target_addr_t address, void* buffer,
		size_t size) const
	{
		ssize_t bytesRead = fTeamMemory->ReadMemory(address, buffer, size);
		return bytesRead >= 0 && (size_t)bytesRead == size;
	}

	virtual bool ReadValueFromMemory(target_addr_t address,
		uint32 valueType, BVariant& _value) const
	{
		return fArchitecture->ReadValueFromMemory(address, valueType, _value)
			== B_OK;
	}

	virtual bool ReadValueFromMemory(target_addr_t addressSpace,
		target_addr_t address, uint32 valueType, BVariant& _value) const
	{
		return fArchitecture->ReadValueFromMemory(addressSpace, address,
			valueType, _value) == B_OK;
	}

protected:
	const Register* _RegisterAt(uint32 dwarfIndex) const
	{
		int32 index = fFromDwarfMap->MapRegisterIndex(dwarfIndex);
		return index >= 0 && index < fRegisterCount ? fRegisters + index : NULL;
	}

protected:
	const Register*	fRegisters;
	int32			fRegisterCount;
	RegisterMap*	fFromDwarfMap;
	Architecture*	fArchitecture;
	TeamMemory*		fTeamMemory;
};


// #pragma mark - UnwindTargetInterface


/**
 * @brief DwarfTargetInterface used during a single frame unwind: layered
 *        on top of BasicTargetInterface with register access through a
 *        captured CpuState.
 */
struct DwarfImageDebugInfo::UnwindTargetInterface : BasicTargetInterface {
	/** @brief Acquires references on the to-DWARF register map and the
	           CPU state. */
	UnwindTargetInterface(const Register* registers, int32 registerCount,
		RegisterMap* fromDwarfMap, RegisterMap* toDwarfMap, CpuState* cpuState,
		Architecture* architecture, TeamMemory* teamMemory)
		:
		BasicTargetInterface(registers, registerCount, fromDwarfMap,
			architecture, teamMemory),
		fToDwarfMap(toDwarfMap),
		fCpuState(cpuState)
	{
		fToDwarfMap->AcquireReference();
		fCpuState->AcquireReference();
	}

	~UnwindTargetInterface()
	{
		fToDwarfMap->ReleaseReference();
		fCpuState->ReleaseReference();
	}

	virtual bool GetRegisterValue(uint32 index, BVariant& _value) const
	{
		const Register* reg = _RegisterAt(index);
		if (reg == NULL)
			return false;
		return fCpuState->GetRegisterValue(reg, _value);
	}

	virtual bool SetRegisterValue(uint32 index, const BVariant& value)
	{
		const Register* reg = _RegisterAt(index);
		if (reg == NULL)
			return false;
		return fCpuState->SetRegisterValue(reg, value);
	}

private:
	RegisterMap*	fToDwarfMap;
	CpuState*		fCpuState;
};


// #pragma mark - EntryListWrapper


/**
 * @brief Wrapper around a DebugInfoEntryList so the type can be referenced
 *        in DwarfImageDebugInfo's header without including DWARF-internal
 *        headers there.
 *
 * The wrapped list is stored by reference; the caller retains ownership.
 */
/*!	Wraps a DebugInfoEntryList, which is a typedef and thus cannot appear in
	the header, since our policy disallows us to include DWARF headers there.
*/
struct DwarfImageDebugInfo::EntryListWrapper {
	const DebugInfoEntryList&	list;

	/** @brief Stores the list reference. */
	EntryListWrapper(const DebugInfoEntryList& list)
		:
		list(list)
	{
	}
};


// #pragma mark - DwarfImageDebugInfo::TypeNameKey


/**
 * @brief Hashable key wrapping a single type name; used as the lookup key
 *        for the per-image type-name table.
 */
struct DwarfImageDebugInfo::TypeNameKey {
	BString			typeName;

	/** @brief Stores the name verbatim. */
	TypeNameKey(const BString& typeName)
		:
		typeName(typeName)
	{
	}

	/** @brief Hashes the underlying name string. */
	uint32 HashValue() const
	{
		return typeName.HashValue();
	}

	/** @brief String-equality comparison. */
	bool operator==(const TypeNameKey& other) const
	{
		return typeName == other.typeName;
	}
};


// #pragma mark - DwarfImageDebugInfo::TypeNameEntry


/**
 * @brief Hash-table entry that maps a type name to a list of candidate
 *        DIE/CU pairs (one entry per matching type definition in the
 *        image).
 */
struct DwarfImageDebugInfo::TypeNameEntry : TypeNameKey {
	TypeNameEntry* next;
	TypeEntryList types;

	/** @brief Initializes an empty list of candidates. */
	TypeNameEntry(const BString& name)
		:
		TypeNameKey(name),
		types(10)
	{
	}

	/** @brief Destructor; nothing to release. */
	~TypeNameEntry()
	{
	}

};


// #pragma mark - DwarfImageDebugInfo::TypeNameEntryHashDefinition


/**
 * @brief Hash-table policy keying TypeNameEntry by TypeNameKey.
 */
struct DwarfImageDebugInfo::TypeNameEntryHashDefinition {
	typedef TypeNameKey		KeyType;
	typedef	TypeNameEntry	ValueType;

	/** @brief Hashes the lookup key. */
	size_t HashKey(const TypeNameKey& key) const
	{
		return key.HashValue();
	}

	/** @brief Hashes a stored entry. */
	size_t Hash(const TypeNameEntry* value) const
	{
		return value->HashValue();
	}

	/** @brief Equality test between key and stored entry. */
	bool Compare(const TypeNameKey& key,
		const TypeNameEntry* value) const
	{
		return key == *value;
	}

	/** @brief Returns the chain pointer used by the hash table. */
	TypeNameEntry*& GetLink(TypeNameEntry* value) const
	{
		return value->next;
	}
};


// #pragma mark - DwarfImageDebugInfo::TypeEntryInfo


/**
 * @brief Pair binding a DIEType to its CompilationUnit; one of the
 *        candidate definitions kept in TypeNameEntry::types.
 */
struct DwarfImageDebugInfo::TypeEntryInfo {
	DIEType* type;
	CompilationUnit* unit;

	/** @brief Stores both fields verbatim. */
	TypeEntryInfo(DIEType* type, CompilationUnit* unit)
		:
		type(type),
		unit(unit)
	{
	}
};


// #pragma mark - DwarfImageDebugInfo


/**
 * @brief Constructs a DWARF-backed per-image debug-info object.
 *
 * Most state (PLT/text section bounds, relocation delta, type name table)
 * is left zeroed and initialized lazily in Init().
 *
 * @param imageInfo     Image identity information.
 * @param interface     Debugger interface; reference acquired.
 * @param architecture  Target architecture; reference acquired.
 * @param fileManager   File manager used to resolve source files.
 * @param typeLookup    Cross-image type resolver.
 * @param typeCache     Global type cache; reference acquired.
 * @param sourceInfo    Shared source-information cache.
 * @param file          DwarfFile parsed for this image; reference
 *                      acquired.
 */
DwarfImageDebugInfo::DwarfImageDebugInfo(const ImageInfo& imageInfo,
	DebuggerInterface* interface, Architecture* architecture,
	FileManager* fileManager, GlobalTypeLookup* typeLookup,
	GlobalTypeCache* typeCache, TeamFunctionSourceInformation* sourceInfo,
	DwarfFile* file)
	:
	fLock("dwarf image debug info"),
	fImageInfo(imageInfo),
	fDebuggerInterface(interface),
	fArchitecture(architecture),
	fFileManager(fileManager),
	fTypeLookup(typeLookup),
	fTypeCache(typeCache),
	fSourceInfo(sourceInfo),
	fTypeNameTable(NULL),
	fFile(file),
	fTextSegment(NULL),
	fRelocationDelta(0),
	fTextSectionStart(0),
	fTextSectionEnd(0),
	fPLTSectionStart(0),
	fPLTSectionEnd(0)
{
	fDebuggerInterface->AcquireReference();
	fFile->AcquireReference();
	fTypeCache->AcquireReference();
}


/**
 * @brief Destroys the object and releases all held references.
 */
DwarfImageDebugInfo::~DwarfImageDebugInfo()
{
	fDebuggerInterface->ReleaseReference();
	fFile->ReleaseReference();
	fTypeCache->ReleaseReference();

	TypeNameEntry* entry = fTypeNameTable->Clear(true);
	while (entry != NULL) {
		TypeNameEntry* next = entry->next;
		delete entry;
		entry = next;
	}
	delete fTypeNameTable;
}


/**
 * @brief Performs deferred initialization.
 *
 * Validates the lock, finalizes DwarfFile loading, captures the relocation
 * delta and the .text/.plt section bounds for later address classification.
 *
 * @retval B_OK         Initialization succeeded.
 * @retval other        Errors from the lock or DwarfFile finalization.
 */
status_t
DwarfImageDebugInfo::Init()
{
	status_t error = fLock.InitCheck();
	if (error != B_OK)
		return error;

	fTextSegment = fFile->GetElfFile()->TextSegment();
	if (fTextSegment == NULL)
		return B_ENTRY_NOT_FOUND;

	fRelocationDelta = fImageInfo.TextBase() - fTextSegment->LoadAddress();

	ElfSection* section = fFile->GetElfFile()->FindSection(".text");
	if (section != NULL) {
		fTextSectionStart = section->LoadAddress() + fRelocationDelta;
		fTextSectionEnd = fTextSectionStart + section->Size();
	}

	section = fFile->GetElfFile()->FindSection(".plt");
	if (section != NULL) {
		fPLTSectionStart = section->LoadAddress() + fRelocationDelta;
		fPLTSectionEnd = fPLTSectionStart + section->Size();
	}

	return _BuildTypeNameTable();
}


/**
 * @brief Enumerates DWARF-described functions and their address ranges.
 *
 * Walks every CU and namespace, descending into namespace DIEs. For each
 * subprogram with non-empty PC ranges a DwarfFunctionDebugInfo is emitted.
 * Symbols not covered by DWARF fall back to BasicFunctionDebugInfo.
 *
 * @param symbols    Image symbol list (sorted by address).
 * @param functions  Out parameter receiving the function descriptors.
 * @retval B_OK         Enumeration completed.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Errors from helpers.
 */
status_t
DwarfImageDebugInfo::GetFunctions(const BObjectList<SymbolInfo, true>& symbols,
	BObjectList<FunctionDebugInfo>& functions)
{
	TRACE_IMAGES("DwarfImageDebugInfo::GetFunctions()\n");
	TRACE_IMAGES("  %" B_PRId32 " compilation units\n",
		fFile->CountCompilationUnits());

	status_t error = B_OK;
	for (int32 i = 0; CompilationUnit* unit = fFile->CompilationUnitAt(i);
			i++) {
		DIECompileUnitBase* unitEntry = unit->UnitEntry();
//		printf("  %s:\n", unitEntry->Name());
//		printf("    address ranges:\n");
//		TargetAddressRangeList* rangeList = unitEntry->AddressRanges();
//		if (rangeList != NULL) {
//			int32 count = rangeList->CountRanges();
//			for (int32 i = 0; i < count; i++) {
//				TargetAddressRange range = rangeList->RangeAt(i);
//				printf("      %#llx - %#llx\n", range.Start(), range.End());
//			}
//		} else {
//			printf("      %#llx - %#llx\n", (target_addr_t)unitEntry->LowPC(),
//				(target_addr_t)unitEntry->HighPC());
//		}

//		printf("    functions:\n");
		for (DebugInfoEntryList::ConstIterator it
					= unitEntry->OtherChildren().GetIterator();
				DebugInfoEntry* entry = it.Next();) {
			if (entry->Tag() == DW_TAG_subprogram) {
				DIESubprogram* subprogramEntry
					= static_cast<DIESubprogram*>(entry);
				error = _AddFunction(subprogramEntry, unit, functions);
				if (error != B_OK)
					return error;
			}

			DIENamespace* nsEntry = dynamic_cast<DIENamespace*>(entry);
			if (nsEntry != NULL) {
				error = _RecursiveTraverseNamespaceForFunctions(nsEntry, unit,
					functions);
				if (error != B_OK)
					return error;
			}
		}
	}

	if (fFile->CountCompilationUnits() != 0)
		return B_OK;

	// if we had no compilation units, fall back to providing basic
	// debug infos with DWARF-supported call frame unwinding,
	// if available.
	if (fFile->HasFrameInformation()) {
		return SpecificImageDebugInfo::GetFunctionsFromSymbols(symbols,
			functions, fDebuggerInterface, fImageInfo, this);
	}

	return B_OK;
}


/**
 * @brief Resolves a type by name within this image's DWARF data.
 *
 * Lazily populates the per-image name table on first call; the table maps
 * names to DIE/CU pairs. Each candidate is fed through DwarfTypeFactory
 * and matched against @a constraints. The first matching type is returned.
 *
 * @param cache       Type cache to consult and populate.
 * @param name        Type name.
 * @param constraints Optional kind/subkind constraints.
 * @param _type       Out parameter receiving the resolved Type.
 * @retval B_OK              A matching type was returned.
 * @retval B_ENTRY_NOT_FOUND No DWARF entry matches.
 * @retval B_NO_MEMORY       Allocation failure.
 * @retval other             Errors from DwarfTypeFactory.
 */
status_t
DwarfImageDebugInfo::GetType(GlobalTypeCache* cache, const BString& name,
	const TypeLookupConstraints& constraints, Type*& _type)
{
	TypeNameEntry* entry = fTypeNameTable->Lookup(name);
	if (entry == NULL)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; TypeEntryInfo* info = entry->types.ItemAt(i); i++) {
		DIEType* typeEntry = info->type;
		if (constraints.HasTypeKind()) {
			if (dwarf_tag_to_type_kind(typeEntry->Tag())
				!= constraints.TypeKind()) {
				continue;
			}

			if (!_EvaluateBaseTypeConstraints(typeEntry, constraints))
				continue;
		}

		if (constraints.HasSubtypeKind()
			&& dwarf_tag_to_subtype_kind(typeEntry->Tag())
				!= constraints.SubtypeKind()) {
			continue;
		}

		int32 registerCount = fArchitecture->CountRegisters();
		const Register* registers = fArchitecture->Registers();

		// get the DWARF <-> architecture register maps
		RegisterMap* toDwarfMap;
		RegisterMap* fromDwarfMap;
		status_t error = fArchitecture->GetDwarfRegisterMaps(&toDwarfMap,
			&fromDwarfMap);
		if (error != B_OK)
			return error;

		BReference<RegisterMap> toDwarfMapReference(toDwarfMap, true);
		BReference<RegisterMap> fromDwarfMapReference(fromDwarfMap, true);

		// create the target interface
		BasicTargetInterface* targetInterface
			= new(std::nothrow) BasicTargetInterface(registers, registerCount,
				fromDwarfMap, fArchitecture, fDebuggerInterface);
		if (targetInterface == NULL)
			return B_NO_MEMORY;

		BReference<BasicTargetInterface> targetInterfaceReference(
			targetInterface, true);

		DwarfTypeContext* typeContext = new(std::nothrow)
			DwarfTypeContext(fArchitecture, fImageInfo.ImageID(), fFile,
				info->unit, NULL, 0, 0, fRelocationDelta, targetInterface, NULL);
		if (typeContext == NULL)
			return B_NO_MEMORY;
		BReference<DwarfTypeContext> typeContextReference(typeContext, true);

		// create the type
		DwarfType* type;
		DwarfTypeFactory typeFactory(typeContext, fTypeLookup, cache);
		error = typeFactory.CreateType(typeEntry, type);
		if (error != B_OK)
			continue;

		_type = type;
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Reports whether a type with the given name exists in this image
 *        and matches optional constraints.
 *
 * Cheaper than GetType() because the type need not be constructed.
 *
 * @param name        Type name.
 * @param constraints Optional kind/subkind constraints.
 * @return @c true if a matching DIE exists in this image.
 */
bool
DwarfImageDebugInfo::HasType(const BString& name,
	const TypeLookupConstraints& constraints) const
{
	TypeNameEntry* entry = fTypeNameTable->Lookup(name);
	if (entry == NULL)
		return false;

	for (int32 i = 0; TypeEntryInfo* info = entry->types.ItemAt(i); i++) {
		DIEType* typeEntry = info->type;
		if (constraints.HasTypeKind()) {
			if (dwarf_tag_to_type_kind(typeEntry->Tag())
				!= constraints.TypeKind()) {
				continue;
			}

			if (!_EvaluateBaseTypeConstraints(typeEntry, constraints))
				continue;
		}

		if (constraints.HasSubtypeKind()
			&& dwarf_tag_to_subtype_kind(typeEntry->Tag())
				!= constraints.SubtypeKind()) {
			continue;
		}

		return true;
	}

	return false;
}


/**
 * @brief Classifies an image-relative address into a section type
 *        (function code, PLT entry, ...).
 *
 * @param address  Image-relative address.
 * @return AddressSectionType describing the section, or
 *         @c ADDRESS_SECTION_TYPE_UNKNOWN.
 */
AddressSectionType
DwarfImageDebugInfo::GetAddressSectionType(target_addr_t address)
{
	if (address >= fTextSectionStart && address < fTextSectionEnd)
		return ADDRESS_SECTION_TYPE_FUNCTION;

 	if (address >= fPLTSectionStart && address < fPLTSectionEnd)
		return ADDRESS_SECTION_TYPE_PLT;

	return ADDRESS_SECTION_TYPE_UNKNOWN;
}


/**
 * @brief Unwinds one stack frame and constructs the StackFrame object
 *        plus the predecessor's CpuState.
 *
 * Uses the DwarfFile's CFI data to compute the previous frame's PC, FP,
 * and register state. When @a getFullFrameInfo is set, also materializes
 * Variable objects for the function's parameters, locals and return
 * value via DwarfStackFrameDebugInfo.
 *
 * @param image                 Image owning the function.
 * @param functionInstance      Function instance whose frame is being
 *                              produced.
 * @param cpuState              Current frame's CpuState; reference
 *                              acquired during evaluation.
 * @param getFullFrameInfo      Whether to populate locals and return
 *                              values.
 * @param returnValueInfos      Optional list of return-value descriptions
 *                              from previous calls.
 * @param _previousFrame        Out parameter receiving the new
 *                              StackFrame; reference transferred to
 *                              caller.
 * @param _previousCpuState     Out parameter receiving the unwound
 *                              CpuState; reference transferred to caller.
 * @retval B_OK              Frame produced.
 * @retval B_UNSUPPORTED     CFI data is missing for the address.
 * @retval B_NO_MEMORY       Allocation failure.
 * @retval other             Errors from CFI evaluation.
 */
status_t
DwarfImageDebugInfo::CreateFrame(Image* image,
	FunctionInstance* functionInstance, CpuState* cpuState,
	bool getFullFrameInfo, ReturnValueInfoList* returnValueInfos,
	StackFrame*& _frame, CpuState*& _previousCpuState)
{
	DwarfFunctionDebugInfo* function = dynamic_cast<DwarfFunctionDebugInfo*>(
		functionInstance->GetFunctionDebugInfo());

	FunctionID* functionID = functionInstance->GetFunctionID();
	BReference<FunctionID> functionIDReference;
	if (functionID != NULL)
		functionIDReference.SetTo(functionID, true);

	DIESubprogram* entry = function != NULL
		? function->SubprogramEntry() : NULL;

	TRACE_CFI("DwarfImageDebugInfo::CreateFrame(): subprogram DIE: %p, "
		"function: %s\n", entry,
		functionID->FunctionName().String());

	int32 registerCount = fArchitecture->CountRegisters();
	const Register* registers = fArchitecture->Registers();

	// get the DWARF <-> architecture register maps
	RegisterMap* toDwarfMap;
	RegisterMap* fromDwarfMap;
	status_t error = fArchitecture->GetDwarfRegisterMaps(&toDwarfMap,
		&fromDwarfMap);
	if (error != B_OK)
		return error;
	BReference<RegisterMap> toDwarfMapReference(toDwarfMap, true);
	BReference<RegisterMap> fromDwarfMapReference(fromDwarfMap, true);

	// create a clean CPU state for the previous frame
	CpuState* previousCpuState;
	error = fArchitecture->CreateCpuState(previousCpuState);
	if (error != B_OK)
		return error;
	BReference<CpuState> previousCpuStateReference(previousCpuState, true);

	// create the target interfaces
	UnwindTargetInterface* inputInterface
		= new(std::nothrow) UnwindTargetInterface(registers, registerCount,
			fromDwarfMap, toDwarfMap, cpuState, fArchitecture,
			fDebuggerInterface);
	if (inputInterface == NULL)
		return B_NO_MEMORY;
	BReference<UnwindTargetInterface> inputInterfaceReference(inputInterface,
		true);

	UnwindTargetInterface* outputInterface
		= new(std::nothrow) UnwindTargetInterface(registers, registerCount,
			fromDwarfMap, toDwarfMap, previousCpuState, fArchitecture,
			fDebuggerInterface);
	if (outputInterface == NULL)
		return B_NO_MEMORY;
	BReference<UnwindTargetInterface> outputInterfaceReference(outputInterface,
		true);

	// do the unwinding
	target_addr_t instructionPointer
		= cpuState->InstructionPointer() - fRelocationDelta;
	target_addr_t framePointer;
	CompilationUnit* unit = function != NULL ? function->GetCompilationUnit()
			: NULL;
	error = fFile->UnwindCallFrame(unit,
		fArchitecture->AddressSize(), fArchitecture->IsBigEndian(),
		entry, instructionPointer, inputInterface, outputInterface,
		framePointer);

	if (error != B_OK) {
		TRACE_CFI("Failed to unwind call frame: %s\n", strerror(error));
		return B_UNSUPPORTED;
	}

	TRACE_CFI_ONLY(
		TRACE_CFI("unwound registers:\n");
		for (int32 i = 0; i < registerCount; i++) {
			const Register* reg = registers + i;
			BVariant value;
			if (previousCpuState->GetRegisterValue(reg, value)) {
				TRACE_CFI("  %3s: %#" B_PRIx64 "\n", reg->Name(),
					value.ToUInt64());
			} else
				TRACE_CFI("  %3s: undefined\n", reg->Name());
		}
	)

	// create the stack frame debug info
	DIESubprogram* subprogramEntry = function != NULL ?
		function->SubprogramEntry() : NULL;
	DwarfStackFrameDebugInfo* stackFrameDebugInfo
		= new(std::nothrow) DwarfStackFrameDebugInfo(fArchitecture,
			fImageInfo.ImageID(), fFile, unit, subprogramEntry, fTypeLookup,
			fTypeCache, instructionPointer, framePointer, fRelocationDelta,
			inputInterface, fromDwarfMap);
	if (stackFrameDebugInfo == NULL)
		return B_NO_MEMORY;
	BReference<DwarfStackFrameDebugInfo> stackFrameDebugInfoReference(
		stackFrameDebugInfo, true);

	error = stackFrameDebugInfo->Init();
	if (error != B_OK)
		return error;

	// create the stack frame
	StackFrame* frame = new(std::nothrow) StackFrame(STACK_FRAME_TYPE_STANDARD,
		cpuState, framePointer, cpuState->InstructionPointer(),
		stackFrameDebugInfo);
	if (frame == NULL)
		return B_NO_MEMORY;
	BReference<StackFrame> frameReference(frame, true);

	error = frame->Init();
	if (error != B_OK)
		return error;

	frame->SetReturnAddress(previousCpuState->InstructionPointer());
		// Note, this is correct, since we actually retrieved the return
		// address. Our caller will fix the IP for us.

	// The subprogram entry may not be available since this may be a case
	// where .eh_frame was used to unwind the stack without other DWARF
	// info being available.
	if (subprogramEntry != NULL && getFullFrameInfo) {
		// create function parameter objects
		for (DebugInfoEntryList::ConstIterator it
			= subprogramEntry->Parameters().GetIterator();
			DebugInfoEntry* entry = it.Next();) {
			if (entry->Tag() != DW_TAG_formal_parameter)
				continue;

			BString parameterName;
			DwarfUtils::GetDIEName(entry, parameterName);
			if (parameterName.Length() == 0)
				continue;

			DIEFormalParameter* parameterEntry
				= dynamic_cast<DIEFormalParameter*>(entry);
			Variable* parameter;
			if (stackFrameDebugInfo->CreateParameter(functionID,
				parameterEntry, parameter) != B_OK) {
				continue;
			}
			BReference<Variable> parameterReference(parameter, true);

			if (!frame->AddParameter(parameter))
				return B_NO_MEMORY;
		}

		// create objects for the local variables
		_CreateLocalVariables(unit, frame, functionID, *stackFrameDebugInfo,
			instructionPointer, functionInstance->Address() - fRelocationDelta,
			subprogramEntry->Variables(), subprogramEntry->Blocks());

		if (returnValueInfos != NULL && !returnValueInfos->IsEmpty()) {
			_CreateReturnValues(returnValueInfos, image, frame,
				*stackFrameDebugInfo);
		}
	}

	_frame = frameReference.Detach();
	_previousCpuState = previousCpuStateReference.Detach();

	frame->SetPreviousCpuState(_previousCpuState);

	return B_OK;
}


/**
 * @brief Returns the source statement covering a runtime address.
 *
 * Walks the DwarfFile's line program for the function's CU until the
 * statement that contains @a address is found. Falls back to the
 * architecture's instruction-level decoding when no DWARF statement
 * applies.
 *
 * @param _function  Function (must be a DwarfFunctionDebugInfo).
 * @param address    Runtime address inside the function.
 * @param _statement Out parameter receiving the new Statement.
 * @retval B_OK              Statement returned.
 * @retval B_BAD_VALUE       @a _function is not a DwarfFunctionDebugInfo.
 * @retval other             Errors from line-table evaluation or the
 *                           architecture's fallback.
 */
status_t
DwarfImageDebugInfo::GetStatement(FunctionDebugInfo* _function,
	target_addr_t address, Statement*& _statement)
{
	TRACE_CODE("DwarfImageDebugInfo::GetStatement(function: %p, address: %#"
		B_PRIx64 ")\n", _function, address);

	DwarfFunctionDebugInfo* function
		= dynamic_cast<DwarfFunctionDebugInfo*>(_function);
	if (function == NULL) {
		TRACE_LINES("  -> no dwarf function\n");
		// fall back to assembly
		return fArchitecture->GetStatement(function, address, _statement);
	}

	AutoLocker<BLocker> locker(fLock);

	// check whether we have the source code
	CompilationUnit* unit = function->GetCompilationUnit();
	LocatableFile* file = function->SourceFile();
	if (file == NULL) {
		TRACE_CODE("  -> no source file\n");

		// no source code -- rather return the assembly statement
		return fArchitecture->GetStatement(function, address, _statement);
	}

	SourceCode* sourceCode = NULL;
	status_t error = fSourceInfo->GetActiveSourceCode(_function, sourceCode);
	BReference<SourceCode> sourceReference(sourceCode, true);
	if (error != B_OK || dynamic_cast<DisassembledCode*>(sourceCode) != NULL) {
		// either no source code or disassembly is currently active (i.e.
		// due to failing to locate the source file on disk or the user
		// deliberately switching to disassembly view).
		// return the assembly statement.
		return fArchitecture->GetStatement(function, address, _statement);
	}

	// get the index of the source file in the compilation unit for cheaper
	// comparison below
	int32 fileIndex = _GetSourceFileIndex(unit, file);

	// Get the statement by executing the line number program for the
	// compilation unit.
	LineNumberProgram& program = unit->GetLineNumberProgram();
	if (!program.IsValid()) {
		TRACE_CODE("  -> no line number program\n");
		return B_BAD_DATA;
	}

	// adjust address
	address -= fRelocationDelta;

	LineNumberProgram::State state;
	program.GetInitialState(state);

	target_addr_t statementAddress = 0;
	int32 statementLine = -1;
	int32 statementColumn = -1;
	while (program.GetNextRow(state)) {
		// skip statements of other files
		if (state.file != fileIndex)
			continue;

		if (statementAddress != 0
			&& (state.isStatement || state.isSequenceEnd)) {
			target_addr_t endAddress = state.address;
			if (address >= statementAddress && address < endAddress) {
				ContiguousStatement* statement = new(std::nothrow)
					ContiguousStatement(
						SourceLocation(statementLine, statementColumn),
						TargetAddressRange(fRelocationDelta + statementAddress,
							endAddress - statementAddress));
				if (statement == NULL)
					return B_NO_MEMORY;

				_statement = statement;
				return B_OK;
			}

			statementAddress = 0;
		}

		if (state.isStatement) {
			statementAddress = state.address;
			statementLine = state.line - 1;
			// discard column info until proper support is implemented
			// statementColumn = std::max(state.column - 1, (int32)0);
			statementColumn = 0;
		}
	}

	TRACE_CODE("  -> no line number program match\n");
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Returns the first source statement at or after a given source
 *        location within a function.
 *
 * @param _function       Function (must be a DwarfFunctionDebugInfo).
 * @param sourceLocation  Source line/column of interest.
 * @param _statement      Out parameter receiving the matching Statement.
 * @retval B_OK              Statement found.
 * @retval B_BAD_VALUE       @a _function is not a DwarfFunctionDebugInfo.
 * @retval B_ENTRY_NOT_FOUND No statement matches.
 */
status_t
DwarfImageDebugInfo::GetStatementAtSourceLocation(FunctionDebugInfo* _function,
	const SourceLocation& sourceLocation, Statement*& _statement)
{
	DwarfFunctionDebugInfo* function
		= dynamic_cast<DwarfFunctionDebugInfo*>(_function);
	if (function == NULL)
		return B_BAD_VALUE;

	target_addr_t functionStartAddress = function->Address() - fRelocationDelta;
	target_addr_t functionEndAddress = functionStartAddress + function->Size();

	TRACE_LINES2("DwarfImageDebugInfo::GetStatementAtSourceLocation(%p, "
		"(%" B_PRId32 ", %" B_PRId32 ")): function range: %#" B_PRIx64 " - %#"
		B_PRIx64 "\n", function, sourceLocation.Line(), sourceLocation.Column(),
		functionStartAddress, functionEndAddress);

	AutoLocker<BLocker> locker(fLock);

	// get the source file
	LocatableFile* file = function->SourceFile();
	if (file == NULL)
		return B_ENTRY_NOT_FOUND;

	CompilationUnit* unit = function->GetCompilationUnit();

	// get the index of the source file in the compilation unit for cheaper
	// comparison below
	int32 fileIndex = _GetSourceFileIndex(unit, file);

	// Get the statement by executing the line number program for the
	// compilation unit.
	LineNumberProgram& program = unit->GetLineNumberProgram();
	if (!program.IsValid())
		return B_BAD_DATA;

	LineNumberProgram::State state;
	program.GetInitialState(state);

	target_addr_t statementAddress = 0;
	int32 statementLine = -1;
	int32 statementColumn = -1;
	while (program.GetNextRow(state)) {
		bool isOurFile = state.file == fileIndex;

		if (statementAddress != 0
			&& (!isOurFile || state.isStatement || state.isSequenceEnd)) {
			target_addr_t endAddress = state.address;

			if (statementAddress < endAddress) {
				TRACE_LINES2("  statement: %#" B_PRIx64 " - %#" B_PRIx64
					", location: (%" B_PRId32 ", %" B_PRId32 ")\n",
					statementAddress, endAddress, statementLine,
				 	statementColumn);
			}

			if (statementAddress < endAddress
				&& statementAddress >= functionStartAddress
				&& statementAddress < functionEndAddress
				&& statementLine == (int32)sourceLocation.Line()
				&& statementColumn == (int32)sourceLocation.Column()) {
				TRACE_LINES2("  -> found statement!\n");

				ContiguousStatement* statement = new(std::nothrow)
					ContiguousStatement(
						SourceLocation(statementLine, statementColumn),
						TargetAddressRange(fRelocationDelta + statementAddress,
							endAddress - statementAddress));
				if (statement == NULL)
					return B_NO_MEMORY;

				_statement = statement;
				return B_OK;
			}

			statementAddress = 0;
		}

		// skip statements of other files
		if (!isOurFile)
			continue;

		if (state.isStatement) {
			statementAddress = state.address;
			statementLine = state.line - 1;
			// discard column info until proper support is implemented
			// statementColumn = std::max(state.column - 1, (int32)0);
			statementColumn = 0;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Identifies the source language of a function based on the
 *        DW_AT_language attribute on its compilation unit.
 *
 * Returns CLanguage, CppLanguage or UnsupportedLanguage as appropriate.
 *
 * @param _function  Function (must be a DwarfFunctionDebugInfo).
 * @param _language  Out parameter receiving the language object;
 *                   reference transferred to caller.
 * @retval B_OK         Language identified.
 * @retval B_BAD_VALUE  @a _function is not a DwarfFunctionDebugInfo.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfImageDebugInfo::GetSourceLanguage(FunctionDebugInfo* _function,
	SourceLanguage*& _language)
{
	DwarfFunctionDebugInfo* function
		= dynamic_cast<DwarfFunctionDebugInfo*>(_function);
	if (function == NULL)
		return B_BAD_VALUE;

	SourceLanguage* language;
	CompilationUnit* unit = function->GetCompilationUnit();
	switch (unit->UnitEntry()->Language()) {
		case DW_LANG_C89:
		case DW_LANG_C:
		case DW_LANG_C99:
			language = new(std::nothrow) CLanguage;
			break;
		case DW_LANG_C_plus_plus:
			language = new(std::nothrow) CppLanguage;
			break;
		case 0:
		default:
			language = new(std::nothrow) UnsupportedLanguage;
			break;
	}

	if (language == NULL)
		return B_NO_MEMORY;

	_language = language;
	return B_OK;
}


/**
 * @brief Reads instruction bytes from the live target at @a address.
 *
 * @param address  Runtime address to read from.
 * @param buffer   Destination buffer.
 * @param size     Number of bytes requested.
 * @return Number of bytes read, or a negative error code.
 */
ssize_t
DwarfImageDebugInfo::ReadCode(target_addr_t address, void* buffer, size_t size)
{
	target_addr_t offset = address - fRelocationDelta
		- fTextSegment->LoadAddress() + fTextSegment->FileOffset();
	ssize_t bytesRead = pread(fFile->GetElfFile()->FD(), buffer, size, offset);
	return bytesRead >= 0 ? bytesRead : errno;
}


/**
 * @brief Annotates a FileSourceCode with the line/statement records for a
 *        given source file present in this image's DWARF data.
 *
 * Iterates every CU and dispatches to _AddSourceCodeInfo() for each CU
 * that lists @a file. Returns @c B_ENTRY_NOT_FOUND when no CU references
 * the file.
 *
 * @param file        Source file to annotate.
 * @param sourceCode  FileSourceCode to populate.
 * @return Status as described above.
 */
status_t
DwarfImageDebugInfo::AddSourceCodeInfo(LocatableFile* file,
	FileSourceCode* sourceCode)
{
	bool addedAny = false;
	for (int32 i = 0; CompilationUnit* unit = fFile->CompilationUnitAt(i);
			i++) {
		int32 fileIndex = _GetSourceFileIndex(unit, file);
		if (fileIndex < 0)
			continue;

		status_t error = _AddSourceCodeInfo(unit, sourceCode, fileIndex);
		if (error == B_NO_MEMORY)
			return error;
		addedAny |= error == B_OK;
	}

	return addedAny ? B_OK : B_ENTRY_NOT_FOUND;
}


/**
 * @brief Annotates a FileSourceCode using one CU's line program.
 *
 * @param unit        Compilation unit whose line program is consulted.
 * @param sourceCode  FileSourceCode to populate.
 * @param fileIndex   Index of the file in @a unit's file list.
 * @retval B_OK         Annotations added.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfImageDebugInfo::_AddSourceCodeInfo(CompilationUnit* unit,
	FileSourceCode* sourceCode, int32 fileIndex)
{
	// Get the statements by executing the line number program for the
	// compilation unit and filtering the rows for our source file.
	LineNumberProgram& program = unit->GetLineNumberProgram();
	if (!program.IsValid())
		return B_BAD_DATA;

	LineNumberProgram::State state;
	program.GetInitialState(state);

	target_addr_t statementAddress = 0;
	int32 statementLine = -1;
	int32 statementColumn = -1;
	while (program.GetNextRow(state)) {
		TRACE_LINES2("  %#" B_PRIx64 "  (%" B_PRId32 ", %" B_PRId32 ", %"
			B_PRId32 ")  %d\n", state.address, state.file, state.line,
			state.column, state.isStatement);

		bool isOurFile = state.file == fileIndex;

		if (statementAddress != 0
			&& (!isOurFile || state.isStatement || state.isSequenceEnd)) {
			target_addr_t endAddress = state.address;
			if (endAddress > statementAddress) {
				// add the statement
				status_t error = sourceCode->AddSourceLocation(
					SourceLocation(statementLine, statementColumn));
				if (error != B_OK)
					return error;

				TRACE_LINES2("  -> statement: %#" B_PRIx64 " - %#" B_PRIx64
					", source location: (%" B_PRId32 ", %" B_PRId32 ")\n",
					statementAddress, endAddress, statementLine,
				 	statementColumn);
			}

			statementAddress = 0;
		}

		// skip statements of other files
		if (!isOurFile)
			continue;

		if (state.isStatement) {
			statementAddress = state.address;
			statementLine = state.line - 1;
			// discard column info until proper support is implemented
			// statementColumn = std::max(state.column - 1, (int32)0);
			statementColumn = 0;
		}
	}

	return B_OK;
}


/**
 * @brief Locates a source file's index within a CU's file table.
 *
 * @param unit        Compilation unit to search.
 * @param sourceFile  File to find.
 * @return Zero-based file index, or -1 if @a sourceFile is not listed.
 */
int32
DwarfImageDebugInfo::_GetSourceFileIndex(CompilationUnit* unit,
	LocatableFile* sourceFile) const
{
	// get the index of the source file in the compilation unit for cheaper
	// comparison below
	const char* directory;
	for (int32 i = 0; const char* fileName = unit->FileAt(i, &directory); i++) {
		LocatableFile* file = fFileManager->GetSourceFile(directory, fileName);
		if (file != NULL) {
			BReference<LocatableFile> fileReference(file, true);
			if (file == sourceFile) {
				return i + 1;
					// indices are one-based
			}
		}
	}

	return -1;
}


/**
 * @brief Creates Variable objects for the formal parameters and local
 *        variables visible at @a instructionPointer.
 *
 * Iterates the supplied variable and lexical-block DIE lists, recursively
 * descending into blocks whose PC range covers the instruction pointer.
 * Each visible variable is added to @a frame.
 *
 * @param unit               CU containing the variables.
 * @param frame              StackFrame receiving the new Variables.
 * @param functionID         Identifier of the enclosing function.
 * @param factory            Stack-frame factory used to construct each
 *                           Variable.
 * @param instructionPointer Address used to filter visible blocks.
 * @param lowPC              Function start address (used to compute live
 *                           ranges).
 * @param variableEntries    Variables declared at function scope.
 * @param blockEntries       Lexical blocks declared at function scope.
 * @retval B_OK              Variables added.
 * @retval B_NO_MEMORY       Allocation failure.
 * @retval other             Errors from factory routines.
 */
status_t
DwarfImageDebugInfo::_CreateLocalVariables(CompilationUnit* unit,
	StackFrame* frame, FunctionID* functionID,
	DwarfStackFrameDebugInfo& factory, target_addr_t instructionPointer,
	target_addr_t lowPC, const EntryListWrapper& variableEntries,
	const EntryListWrapper& blockEntries)
{
	TRACE_LOCALS("DwarfImageDebugInfo::_CreateLocalVariables(): ip: %#" B_PRIx64
		", low PC: %#" B_PRIx64 "\n", instructionPointer, lowPC);

	// iterate through the variables and add the ones in scope
	for (DebugInfoEntryList::ConstIterator it
			= variableEntries.list.GetIterator();
		DIEVariable* variableEntry = dynamic_cast<DIEVariable*>(it.Next());) {

		TRACE_LOCALS("  variableEntry %p, scope start: %" B_PRIu64 "\n",
			variableEntry, variableEntry->StartScope());

		// check the variable's scope
		if (instructionPointer < lowPC + variableEntry->StartScope())
			continue;

		// add the variable
		Variable* variable;
		if (factory.CreateLocalVariable(functionID, variableEntry, variable)
				!= B_OK) {
			continue;
		}
		BReference<Variable> variableReference(variable, true);

		if (!frame->AddLocalVariable(variable))
			return B_NO_MEMORY;
	}

	// iterate through the blocks and find the one we're currently in (if any)
	for (DebugInfoEntryList::ConstIterator it = blockEntries.list.GetIterator();
		DIELexicalBlock* block = dynamic_cast<DIELexicalBlock*>(it.Next());) {

		TRACE_LOCALS("  lexical block: %p\n", block);

		// check whether the block has low/high PC attributes
		if (block->LowPC() != 0) {
			TRACE_LOCALS("    has lowPC\n");

			// yep, compare with the instruction pointer
			if (instructionPointer < block->LowPC()
				|| instructionPointer >= block->HighPC()) {
				continue;
			}
		} else {
			TRACE_LOCALS("    no lowPC\n");

			// check the address ranges instead
			TargetAddressRangeList* rangeList = fFile->ResolveRangeList(unit,
				block->AddressRangesOffset());
			if (rangeList == NULL) {
				TRACE_LOCALS("    failed to get ranges\n");
				continue;
			}
			BReference<TargetAddressRangeList> rangeListReference(rangeList,
				true);

			if (!rangeList->Contains(instructionPointer)) {
				TRACE_LOCALS("    ranges don't contain IP\n");
				continue;
			}
		}

		// found a block -- recurse
		return _CreateLocalVariables(unit, frame, functionID, factory,
			instructionPointer, lowPC, block->Variables(), block->Blocks());
	}

	return B_OK;
}


/**
 * @brief Creates Variable objects for any pending return values to display
 *        in the active stack frame.
 *
 * Each entry in @a returnValueInfos describes a captured return value;
 * this helper resolves the corresponding subprogram, looks up its return
 * type, and asks @a factory to build the Variable.
 *
 * @param returnValueInfos  List of captured return values.
 * @param image             Image owning the call site.
 * @param frame             Frame receiving the Variables.
 * @param factory           Stack-frame factory used for construction.
 * @retval B_OK              Return values added.
 * @retval B_NO_MEMORY       Allocation failure.
 * @retval other             Errors from address resolution or the factory.
 */
status_t
DwarfImageDebugInfo::_CreateReturnValues(ReturnValueInfoList* returnValueInfos,
	Image* image, StackFrame* frame, DwarfStackFrameDebugInfo& factory)
{
	for (int32 i = 0; i < returnValueInfos->CountItems(); i++) {
		Image* targetImage = image;
		ReturnValueInfo* valueInfo = returnValueInfos->ItemAt(i);
		target_addr_t subroutineAddress = valueInfo->SubroutineAddress();
		CpuState* subroutineState = valueInfo->State();
		if (!targetImage->ContainsAddress(subroutineAddress)) {
			// our current image doesn't contain the target function,
			// locate the one which does.
			targetImage = image->GetTeam()->ImageByAddress(subroutineAddress);
			if (targetImage == NULL) {
				// nothing we can do, try the next entry (if any)
				continue;
			}
		}

		status_t result = B_OK;
		ImageDebugInfo* imageInfo = targetImage->GetImageDebugInfo();
		if (imageInfo == NULL) {
			// the subroutine may have resolved to a different image
			// that doesn't have debug information available.
			continue;
		}

		FunctionInstance* targetFunction;
		if (imageInfo->GetAddressSectionType(subroutineAddress)
				== ADDRESS_SECTION_TYPE_PLT) {
			result = fArchitecture->ResolvePICFunctionAddress(
				subroutineAddress, subroutineState, subroutineAddress);
			if (result != B_OK)
				continue;
			if (!targetImage->ContainsAddress(subroutineAddress)) {
				// the PLT entry doesn't necessarily point to a function
				// in the same image; as such we may need to try to
				// resolve the target address again.
				targetImage = image->GetTeam()->ImageByAddress(
					subroutineAddress);
				if (targetImage == NULL)
					continue;
				imageInfo = targetImage->GetImageDebugInfo();
				if (imageInfo == NULL) {
					// As above, since the indirection here may have
					// landed us in an entirely different image, there is
					// no guarantee that debug info is available,
					// depending on which image it was.
					continue;
				}

			}
		}

		targetFunction = imageInfo->FunctionAtAddress(subroutineAddress);
		if (targetFunction != NULL) {
			DwarfFunctionDebugInfo* targetInfo =
				dynamic_cast<DwarfFunctionDebugInfo*>(
					targetFunction->GetFunctionDebugInfo());
			if (targetInfo != NULL) {
				DIESubprogram* subProgram = targetInfo->SubprogramEntry();
				DIEType* returnType = subProgram->ReturnType();
				if (returnType == NULL) {
					// check if we have a specification, and if so, if that has
					// a return type
					subProgram = dynamic_cast<DIESubprogram*>(
						subProgram->Specification());
					if (subProgram != NULL)
						returnType = subProgram->ReturnType();

					// function doesn't return a value, we're done.
					if (returnType == NULL)
						return B_OK;
				}

				uint32 byteSize = 0;
				if (returnType->ByteSize() == NULL) {
					if (dynamic_cast<DIEAddressingType*>(returnType) != NULL)
						byteSize = fArchitecture->AddressSize();
				} else
					byteSize = returnType->ByteSize()->constant;

				// if we were unable to determine a size for the type,
				// simply default to the architecture's register width.
				if (byteSize == 0)
					byteSize = fArchitecture->AddressSize();

				ValueLocation* location;
				result = fArchitecture->GetReturnAddressLocation(frame,
					byteSize, location);
				if (result != B_OK)
					return result;

				BReference<ValueLocation> locationReference(location, true);
				Variable* variable = NULL;
				BReference<FunctionID> idReference(
					targetFunction->GetFunctionID(), true);
				result = factory.CreateReturnValue(idReference, returnType,
					location, subroutineState, variable);
				if (result != B_OK)
					return result;

				BReference<Variable> variableReference(variable, true);
				if (!frame->AddLocalVariable(variable))
					return B_NO_MEMORY;
			}
		}
	}

	return B_OK;
}


/**
 * @brief Recursively evaluates whether a DIE-described type satisfies the
 *        given lookup constraints.
 *
 * Walks through typedef and modifier chains so a constraint stated in
 * the abstract model maps to the matching DWARF tag.
 *
 * @param type         DIE to evaluate.
 * @param constraints  Constraints to satisfy.
 * @return @c true if @a type matches; @c false otherwise.
 */
bool
DwarfImageDebugInfo::_EvaluateBaseTypeConstraints(DIEType* type,
	const TypeLookupConstraints& constraints) const
{
	if (constraints.HasBaseTypeName()) {
		BString baseEntryName;
		DIEType* baseTypeOwnerEntry = NULL;

		switch (constraints.TypeKind()) {
			case TYPE_ADDRESS:
			{
				DIEAddressingType* addressType =
					dynamic_cast<DIEAddressingType*>(type);
				if (addressType != NULL) {
					baseTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
						addressType, HasTypePredicate<DIEAddressingType>());
				}
				break;
			}
			case TYPE_ARRAY:
			{
				DIEArrayType* arrayType =
					dynamic_cast<DIEArrayType*>(type);
				if (arrayType != NULL) {
					baseTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
						arrayType, HasTypePredicate<DIEArrayType>());
				}
				break;
			}
			default:
				break;
		}

		if (baseTypeOwnerEntry != NULL) {
			DwarfUtils::GetFullyQualifiedDIEName(baseTypeOwnerEntry,
				baseEntryName);

			if (baseEntryName != constraints.BaseTypeName())
				return false;
		}
	}

	return true;
}


/**
 * @brief Recursively walks a namespace DIE and emits FunctionDebugInfo for
 *        every subprogram found.
 *
 * @param nsEntry    Namespace DIE to descend into.
 * @param unit       Owning compilation unit.
 * @param functions  Output list of function descriptors.
 * @return Status from _AddFunction() / recursive calls.
 */
status_t
DwarfImageDebugInfo::_RecursiveTraverseNamespaceForFunctions(
	DIENamespace* nsEntry, CompilationUnit* unit,
	BObjectList<FunctionDebugInfo>& functions)
{
	status_t error = B_OK;
	for (DebugInfoEntryList::ConstIterator it
				= nsEntry->Children().GetIterator();
			DebugInfoEntry* entry = it.Next();) {
		if (entry->Tag() == DW_TAG_subprogram) {
			DIESubprogram* subprogramEntry
				= static_cast<DIESubprogram*>(entry);
			error = _AddFunction(subprogramEntry, unit, functions);
			if (error != B_OK)
				return error;
		}

		DIENamespace* nsEntry = dynamic_cast<DIENamespace*>(entry);
		if (nsEntry != NULL) {
			error = _RecursiveTraverseNamespaceForFunctions(nsEntry, unit,
				functions);
			if (error != B_OK)
				return error;
			continue;
		}

		DIEClassBaseType* classEntry = dynamic_cast<DIEClassBaseType*>(entry);
		if (classEntry != NULL) {
			for (DebugInfoEntryList::ConstIterator it
						= classEntry->MemberFunctions().GetIterator();
					DebugInfoEntry* memberEntry = it.Next();) {
				error = _AddFunction(static_cast<DIESubprogram*>(memberEntry),
					unit, functions);
				if (error != B_OK)
					return error;
			}
		}
	}

	return B_OK;
}


/**
 * @brief Constructs a DwarfFunctionDebugInfo for a single subprogram and
 *        appends it to @a functions.
 *
 * Resolves the subprogram's PC ranges, name (potentially via abstract
 * origin/specification), source file and declaration location.
 *
 * @param subprogramEntry  DIE describing the subprogram.
 * @param unit             Owning compilation unit.
 * @param functions        Output list receiving the new descriptor.
 * @retval B_OK              Function added.
 * @retval B_NO_MEMORY       Allocation failure.
 * @retval B_BAD_VALUE       Subprogram has no PC ranges.
 */
status_t
DwarfImageDebugInfo::_AddFunction(DIESubprogram* subprogramEntry,
	CompilationUnit* unit, BObjectList<FunctionDebugInfo>& functions)
{
	// ignore declarations and inlined functions
	if (subprogramEntry->IsDeclaration()
		|| subprogramEntry->Inline() == DW_INL_inlined
		|| subprogramEntry->Inline() == DW_INL_declared_inlined) {
		return B_OK;
	}

	// get the name
	BString name;
	DwarfUtils::GetFullyQualifiedDIEName(subprogramEntry, name);
	if (name.Length() == 0)
		return B_OK;

	// get the address ranges
	TargetAddressRangeList* rangeList = fFile->ResolveRangeList(unit,
		subprogramEntry->AddressRangesOffset());
	if (rangeList == NULL) {
		target_addr_t lowPC = subprogramEntry->LowPC();
		target_addr_t highPC = subprogramEntry->HighPC();
		if (highPC <= lowPC)
			return B_OK;

		rangeList = new(std::nothrow) TargetAddressRangeList(
			TargetAddressRange(lowPC, highPC - lowPC));
		if (rangeList == NULL)
			return B_NO_MEMORY;
				// TODO: Clean up already added functions!
	}
	BReference<TargetAddressRangeList> rangeListReference(rangeList,
		true);

	// get the source location
	const char* directoryPath = NULL;
	const char* fileName = NULL;
	int32 line = -1;
	int32 column = -1;
	DwarfUtils::GetDeclarationLocation(fFile, subprogramEntry,
		directoryPath, fileName, line, column);

	LocatableFile* file = NULL;
	if (fileName != NULL) {
		file = fFileManager->GetSourceFile(directoryPath,
			fileName);
	}
	BReference<LocatableFile> fileReference(file, true);

	// create and add the functions
	DwarfFunctionDebugInfo* function
		= new(std::nothrow) DwarfFunctionDebugInfo(this, unit,
			subprogramEntry, rangeList, name, file,
			SourceLocation(line, std::max(column, (int32)0)));
	if (function == NULL || !functions.AddItem(function)) {
		delete function;
		return B_NO_MEMORY;
			// TODO: Clean up already added functions!
	}

	return B_OK;
}


/**
 * @brief Constructs the per-image type-name hash table on first use.
 *
 * Walks every CU and namespace, descending into namespace DIEs, and
 * inserts an entry per declared type.
 *
 * @retval B_OK         Table built.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfImageDebugInfo::_BuildTypeNameTable()
{
	fTypeNameTable = new(std::nothrow) TypeNameTable;
	if (fTypeNameTable == NULL)
		return B_NO_MEMORY;

	status_t error = fTypeNameTable->Init();
	if (error != B_OK)
		return error;

	// iterate through all compilation units
	for (int32 i = 0; CompilationUnit* unit = fFile->CompilationUnitAt(i);
		i++) {
		// iterate through all types of the compilation unit
		for (DebugInfoEntryList::ConstIterator it
				= unit->UnitEntry()->Types().GetIterator();
			DIEType* typeEntry = dynamic_cast<DIEType*>(it.Next());) {

			if (_RecursiveAddTypeNames(typeEntry, unit) != B_OK)
				return B_NO_MEMORY;
		}

		for (DebugInfoEntryList::ConstIterator it
			= unit->UnitEntry()->OtherChildren().GetIterator();
			DebugInfoEntry* child = it.Next();) {
			DIENamespace* namespaceEntry = dynamic_cast<DIENamespace*>(child);
			if (namespaceEntry == NULL)
				continue;

			if (_RecursiveTraverseNamespaceForTypes(namespaceEntry, unit)
					!= B_OK) {
				return B_NO_MEMORY;
			}
		}
	}

	return B_OK;
}


/**
 * @brief Recursively registers a DIE and any nested types into the
 *        type-name table.
 *
 * Anonymous and synthetic types are ignored.
 *
 * @param type  Type DIE to register.
 * @param unit  Owning compilation unit.
 * @retval B_OK         Insertion succeeded.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfImageDebugInfo::_RecursiveAddTypeNames(DIEType* type, CompilationUnit* unit)
{
	if (type->IsDeclaration())
		return B_OK;

	BString typeEntryName;
	DwarfUtils::GetFullyQualifiedDIEName(type, typeEntryName);

	status_t error = B_OK;
	TypeNameEntry* entry = fTypeNameTable->Lookup(typeEntryName);
	if (entry == NULL) {
		entry = new(std::nothrow) TypeNameEntry(typeEntryName);
		if (entry == NULL)
			return B_NO_MEMORY;

		error = fTypeNameTable->Insert(entry);
		if (error != B_OK)
			return error;
	}

	TypeEntryInfo* info = new(std::nothrow) TypeEntryInfo(type,	unit);
	if (info == NULL)
		return B_NO_MEMORY;

	if (!entry->types.AddItem(info)) {
		delete info;
		return B_NO_MEMORY;
	}

	DIEClassBaseType* classType = dynamic_cast<DIEClassBaseType*>(type);
	if (classType == NULL)
		return B_OK;

	for (DebugInfoEntryList::ConstIterator it
			= classType->InnerTypes().GetIterator();
		DIEType* innerType = dynamic_cast<DIEType*>(it.Next());) {
		error = _RecursiveAddTypeNames(innerType, unit);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Recursively walks a namespace DIE and registers every declared
 *        type into the type-name table.
 *
 * @param nsEntry  Namespace DIE to descend into.
 * @param unit     Owning compilation unit.
 * @retval B_OK         Traversal completed.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfImageDebugInfo::_RecursiveTraverseNamespaceForTypes(DIENamespace* nsEntry,
	CompilationUnit* unit)
{
	for (DebugInfoEntryList::ConstIterator it
				= nsEntry->Children().GetIterator();
			DebugInfoEntry* child = it.Next();) {

		if (child->IsType()) {
			DIEType* type = dynamic_cast<DIEType*>(child);
			if (_RecursiveAddTypeNames(type, unit) != B_OK)
				return B_NO_MEMORY;
		} else {
			DIENamespace* nameSpace = dynamic_cast<DIENamespace*>(child);
			if (nameSpace == NULL)
				continue;

			status_t error = _RecursiveTraverseNamespaceForTypes(nameSpace,
				unit);
			if (error != B_OK)
				return error;
			continue;
		}
	}

	return B_OK;
}
