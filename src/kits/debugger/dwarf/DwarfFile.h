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
 * MIT License. Copyright 2009-2010, Ingo Weinhold; Copyright 2012-2013, Rene
 * Gollent.
 */

/** @file DwarfFile.h
    @brief Top-level reader and evaluator for the DWARF debug information of one ELF object. */

#ifndef DWARF_FILE_H
#define DWARF_FILE_H


#include <ObjectList.h>
#include <Referenceable.h>
#include <util/DoublyLinkedList.h>
#include <util/OpenHashTable.h>

#include "DebugInfoEntries.h"
#include "TypeUnit.h"


struct AbbreviationEntry;
class AbbreviationTable;
class BVariant;
class CfaContext;
class CompilationUnit;
class DataReader;
class DwarfTargetInterface;
class ElfFile;
class ElfSection;
class TargetAddressRangeList;
class ValueLocation;


/**
 * @brief Loads and queries the DWARF debug info attached to an ELF binary.
 *
 * Owns the parsed compilation and type units, the abbreviation tables,
 * the FDE lookup info for both .debug_frame and .eh_frame, and provides
 * the high-level entry points used by the debugger backend: call-frame
 * unwinding, expression evaluation, and location resolution.
 */
class DwarfFile : public BReferenceable,
	public DoublyLinkedListLinkImpl<DwarfFile> {
public:
								DwarfFile();
								~DwarfFile();

			status_t			StartLoading(const char* fileName,
									BString& _requiredExternalFile);
			status_t			Load(uint8 addressSize, bool isBigEndian,
									const BString& externalFilePath);
			status_t			FinishLoading(uint8 addressSize, bool isBigEndian);

			const char*			Name() const		{ return fName; }
			ElfFile*			GetElfFile() const	{ return fElfFile; }

			bool				HasFrameInformation() const
									{ return fDebugFrameSection != NULL
										|| fEHFrameSection != NULL; }

			int32				CountCompilationUnits() const;
			CompilationUnit*	CompilationUnitAt(int32 index) const;
			CompilationUnit*	CompilationUnitForDIE(
									const DebugInfoEntry* entry) const;

			TargetAddressRangeList* ResolveRangeList(CompilationUnit* unit,
									uint64 offset) const;

			status_t			UnwindCallFrame(CompilationUnit* unit,
									uint8 addressSize, bool isBigEndian,
									DIESubprogram* subprogramEntry,
									target_addr_t location,
									const DwarfTargetInterface* inputInterface,
									DwarfTargetInterface* outputInterface,
									target_addr_t& _framePointer);

			status_t			EvaluateExpression(CompilationUnit* unit,
									uint8 addressSize, bool isBigEndian,
									DIESubprogram* subprogramEntry,
									const void* expression,
									off_t expressionLength,
									const DwarfTargetInterface* targetInterface,
									target_addr_t instructionPointer,
									target_addr_t framePointer,
									target_addr_t valueToPush, bool pushValue,
									target_addr_t& _result);
			status_t			ResolveLocation(CompilationUnit* unit,
									uint8 addressSize, bool isBigEndian,
									DIESubprogram* subprogramEntry,
									const LocationDescription* location,
									const DwarfTargetInterface* targetInterface,
									target_addr_t instructionPointer,
									target_addr_t objectPointer,
									bool hasObjectPointer,
									target_addr_t framePointer,
									target_addr_t relocationDelta,
									ValueLocation& _result);
									// The returned location will have DWARF
									// semantics regarding register numbers and
									// bit offsets/sizes (cf. bit pieces).

			status_t			EvaluateConstantValue(CompilationUnit* unit,
									uint8 addressSize, bool isBigEndian,
									DIESubprogram* subprogramEntry,
									const ConstantAttributeValue* value,
									const DwarfTargetInterface* targetInterface,
									target_addr_t instructionPointer,
									target_addr_t framePointer,
									BVariant& _result);
			status_t			EvaluateDynamicValue(CompilationUnit* unit,
									uint8 addressSize, bool isBigEndian,
									DIESubprogram* subprogramEntry,
									const DynamicAttributeValue* value,
									const DwarfTargetInterface* targetInterface,
									target_addr_t instructionPointer,
									target_addr_t framePointer,
									BVariant& _result, DIEType** _type = NULL);

private:
			struct ExpressionEvaluationContext;
			struct FDEAugmentation;
			struct CIEAugmentation;
			struct FDELookupInfo;

			typedef DoublyLinkedList<AbbreviationTable> AbbreviationTableList;
			typedef BObjectList<CompilationUnit, true> CompilationUnitList;
			typedef BOpenHashTable<TypeUnitTableHashDefinition> TypeUnitTable;
			typedef BObjectList<FDELookupInfo, true> FDEInfoList;

private:
			status_t			_ParseDebugInfoSection(uint8 _addressSize, bool isBigEndian);
			status_t			_ParseTypesSection(uint8 _addressSize, bool isBigEndian);
			status_t			_ParseFrameSection(ElfSection* section,
									uint8 addressSize, bool isBigEndian,
									bool ehFrame, FDEInfoList& infos);
			status_t			_ParseCompilationUnit(CompilationUnit* unit);
			status_t			_ParseTypeUnit(TypeUnit* unit);
			status_t			_ParseDebugInfoEntry(DataReader& dataReader,
									BaseUnit* unit,
									AbbreviationTable* abbreviationTable,
									DebugInfoEntry*& _entry,
									bool& _endOfEntryList, int level = 0);
			status_t			_FinishUnit(BaseUnit* unit);
			status_t			_ReadStringIndirect(BaseUnit* unit,
									uint64 index, const char*& value) const;
			status_t			_ReadAddressIndirect(BaseUnit* unit,
									uint64 index, uint64& value) const;
			status_t			_ParseEntryAttributes(DataReader& dataReader,
									BaseUnit* unit,
									DebugInfoEntry* entry,
									AbbreviationEntry& abbreviationEntry);

			status_t			_ParseLineInfoFormatString(CompilationUnit* unit,
									DataReader &dataReader,
									uint64 format, const char*& value);
			status_t			_ParseLineInfoFormatUint(CompilationUnit* unit,
									DataReader &dataReader,
									uint64 format, uint64 &value);
			status_t			_ParseLineInfo(CompilationUnit* unit);

			status_t			_UnwindCallFrame(CompilationUnit* unit,
									uint8 addressSize, bool isBigEndian,
									DIESubprogram* subprogramEntry,
									target_addr_t location,
									const FDELookupInfo* info,
									const DwarfTargetInterface* inputInterface,
									DwarfTargetInterface* outputInterface,
									target_addr_t& _framePointer);

			status_t			_ParseCIEHeader(ElfSection* debugFrameSection,
									bool usingEHFrameSection,
									CompilationUnit* unit,
									uint8 addressSize, bool isBigEndian,
									CfaContext& context, off_t cieOffset,
									CIEAugmentation& cieAugmentation,
									DataReader& reader,
									off_t& _cieRemaining);
			status_t			_ParseFrameInfoInstructions(
									CompilationUnit* unit, CfaContext& context,
									DataReader& dataReader,
									CIEAugmentation& cieAugmentation);

			status_t			_ParsePublicTypesInfo(uint8 _addressSize, bool isBigEndian);
			status_t			_ParsePublicTypesInfo(DataReader& dataReader,
									bool dwarf64);

			status_t			_GetAbbreviationTable(off_t offset,
									AbbreviationTable*& _table);

			DebugInfoEntry*		_ResolveReference(BaseUnit* unit,
									uint64 offset,
									uint8 refType) const;

			status_t			_GetLocationExpression(CompilationUnit* unit,
									const LocationDescription* location,
									target_addr_t instructionPointer,
									const void*& _expression,
									off_t& _length) const;
			status_t			_FindLocationExpression(CompilationUnit* unit,
									uint64 offset, target_addr_t address,
									const void*& _expression,
									off_t& _length) const;

			status_t			_LocateDebugInfo(
									BString& _requiredExternalFileName,
									const char* locatedFilePath = NULL);

			status_t			_GetDebugInfoPath(const char* fileName,
									BString& _infoPath) const;

			TypeUnitTableEntry*	_GetTypeUnit(uint64 signature) const;
			CompilationUnit*	_GetContainingCompilationUnit(
									off_t refAddr) const;

			FDELookupInfo*		_GetContainingFDEInfo(
									target_addr_t offset) const;

			FDELookupInfo*		_GetContainingFDEInfo(
									target_addr_t offset,
									const FDEInfoList& infoList) const;

private:
			friend struct 		DwarfFile::ExpressionEvaluationContext;

private:
			char*				fName;
			char*				fAlternateName;
			ElfFile*			fElfFile;
			ElfFile*			fAlternateElfFile;
			ElfSection*			fDebugInfoSection;
			ElfSection*			fDebugAbbrevSection;
			ElfSection*			fDebugAddressSection;
			ElfSection*			fDebugStringSection;
			ElfSection*			fDebugStrOffsetsSection;
			ElfSection*			fDebugRangesSection;
			ElfSection*			fDebugLineSection;
			ElfSection*			fDebugLineStrSection;
			ElfSection*			fDebugFrameSection;
			ElfSection*			fEHFrameSection;
			ElfSection*			fDebugLocationSection;
			ElfSection*			fDebugPublicTypesSection;
			ElfSection*			fDebugTypesSection;
			AbbreviationTableList fAbbreviationTables;
			DebugInfoEntryFactory fDebugInfoFactory;
			CompilationUnitList	fCompilationUnits;
			TypeUnitTable		fTypeUnits;
			FDEInfoList			fDebugFrameInfos;
			FDEInfoList			fEHFrameInfos;
			bool				fTypesSectionRequired;
			bool				fFinished;
			bool				fItaniumEHFrameFormat;
			status_t			fFinishError;
};


#endif	// DWARF_FILE_H
