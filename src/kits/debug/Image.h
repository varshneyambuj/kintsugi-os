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
 * MIT License. Copyright 2005-2009, Ingo Weinhold.
 */

/** @file Image.h
    @brief Abstract Image hierarchy for representing a loaded ELF image's
           metadata and symbol table during debugging. */

#ifndef IMAGE_H
#define IMAGE_H

#include <stdio.h>

#include <elf_private.h>
#include <image.h>
#include <OS.h>

#include <util/DoublyLinkedList.h>


struct image_t;
struct runtime_loader_debug_area;


namespace BPrivate {
namespace Debug {


/** @brief Abstract base for a loaded image's metadata; concrete subclasses
 *         resolve symbols against a memory-mapped ELF file or kernel image. */
class Image : public DoublyLinkedListLinkImpl<Image> {
public:
								Image();
	virtual						~Image();

	/** @brief Returns the image_info captured for this image. */
			const image_info&	Info() const		{ return fInfo; }
	/** @brief Returns the image_id assigned to this image by the loader. */
			image_id			ID() const			{ return fInfo.id; }
	/** @brief Returns the path/name of the underlying file. */
			const char*			Name() const		{ return fInfo.name; }
	/** @brief Returns the base address of the image's text segment. */
			addr_t				TextAddress() const
				{ return (addr_t)fInfo.text; }
	/** @brief Returns the size of the image's text segment in bytes. */
			size_t				TextSize() const	{ return fInfo.text_size; }

	virtual	const elf_sym*		LookupSymbol(addr_t address,
									addr_t* _baseAddress,
									const char** _symbolName,
									size_t *_symbolNameLen,
									bool *_exactMatch) const = 0;
	virtual	status_t			NextSymbol(int32& iterator,
									const char** _symbolName,
									size_t* _symbolNameLen,
									addr_t* _symbolAddress, size_t* _symbolSize,
									int32* _symbolType) const = 0;

	virtual	status_t			GetSymbol(const char* name, int32 symbolType,
									void** _symbolLocation, size_t* _symbolSize,
									int32* _symbolType) const;

protected:
			image_info			fInfo;
};


/** @brief Image specialisation that resolves symbols by linearly scanning a
 *         loaded ELF symbol/string table pair. */
class SymbolTableBasedImage : public Image {
public:
								SymbolTableBasedImage();
	virtual						~SymbolTableBasedImage();

	virtual	const elf_sym*		LookupSymbol(addr_t address,
									addr_t* _baseAddress,
									const char** _symbolName,
									size_t *_symbolNameLen,
									bool *_exactMatch) const;
	virtual	status_t			NextSymbol(int32& iterator,
									const char** _symbolName,
									size_t* _symbolNameLen,
									addr_t* _symbolAddress, size_t* _symbolSize,
									int32* _symbolType) const;

protected:
			size_t				_SymbolNameLen(const char* symbolName) const;

protected:
			addr_t				fLoadDelta;
			elf_sym*			fSymbolTable;
			char*				fStringTable;
			int32				fSymbolCount;
			size_t				fStringTableSize;
};


/** @brief Concrete Image backed by an ELF file mapped from disk; used when the
 *         original executable is still available outside the running team. */
class ImageFile : public SymbolTableBasedImage {
public:
								ImageFile();
	virtual						~ImageFile();

			status_t			Init(const image_info& info);
			status_t			Init(const char* path);

private:
			status_t			_LoadFile(const char* path,
									addr_t* _textAddress, size_t* _textSize,
									addr_t* _dataAddress, size_t* _dataSize);

			status_t			_FindTableInSection(elf_ehdr* elfHeader,
									uint16 sectionType);

private:
			int					fFD;
			off_t				fFileSize;
			uint8*				fMappedFile;
};


/** @brief Concrete Image representing the kernel itself, loaded from the
 *         running kernel's symbol tables. */
class KernelImage : public SymbolTableBasedImage {
public:
								KernelImage();
	virtual						~KernelImage();

			status_t			Init(const image_info& info);
};


/** @brief Concrete Image representing the kernel-shared "comm page" image
 *         that exposes a small set of vsyscall-style entry points. */
class CommPageImage : public SymbolTableBasedImage {
public:
								CommPageImage();
	virtual						~CommPageImage();

			status_t			Init(const image_info& info);
};

}	// namespace Debug
}	// namespace BPrivate


using BPrivate::Debug::ImageFile;


#endif	// IMAGE_H
