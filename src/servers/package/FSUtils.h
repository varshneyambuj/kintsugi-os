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
 *
 * Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
  * Distributed under the terms of the MIT License.
 */

/** @file FSUtils.h
 *  @brief Utility helpers for filesystem operations including path manipulation and content comparison */

#ifndef FS_UTILS_H
#define FS_UTILS_H


#include <new>

#include <String.h>

#include <EntryOperationEngineBase.h>


class BDirectory;
class BFile;
class BPositionIO;
class BSymLink;


/** @brief Static utility class providing filesystem helpers for the package daemon */
class FSUtils {
public:
	typedef ::BPrivate::BEntryOperationEngineBase::Entry Entry;

public:
	/** @brief Lightweight path composed of up to three directory components */
	struct RelativePath {
		RelativePath(const char* component1 = NULL,
			const char* component2 = NULL, const char* component3 = NULL)
			:
			fComponentCount(kMaxComponentCount)
		{
			fComponents[0] = component1;
			fComponents[1] = component2;
			fComponents[2] = component3;

			for (size_t i = 0; i < kMaxComponentCount; i++) {
				if (fComponents[i] == NULL) {
					fComponentCount = i;
					break;
				}
			}
		}

		bool IsEmpty() const
		{
			return fComponentCount == 0;
		}

		RelativePath HeadPath(size_t componentsToDropCount = 1)
		{
			RelativePath result;
			if (componentsToDropCount < fComponentCount) {
				result.fComponentCount
					= fComponentCount - componentsToDropCount;
				for (size_t i = 0; i < result.fComponentCount; i++)
					result.fComponents[i] = fComponents[i];
			}

			return result;
		}

		const char* LastComponent() const
		{
			return fComponentCount > 0
				? fComponents[fComponentCount - 1] : NULL;
		}

		BString ToString() const
		{
			if (fComponentCount == 0)
				return BString();

			size_t length = fComponentCount - 1;
			for (size_t i = 0; i < fComponentCount; i++)
				length += strlen(fComponents[i]);

			BString result;
			char* buffer = result.LockBuffer(length + 1);
			if (buffer == NULL)
				return BString();

			for (size_t i = 0; i < fComponentCount; i++) {
				if (i > 0) {
					*buffer = '/';
					buffer++;
				}
				strcpy(buffer, fComponents[i]);
				buffer += strlen(buffer);
			}

			return result.UnlockBuffer();
		}

	private:
		static const size_t	kMaxComponentCount = 3;

		const char*	fComponents[kMaxComponentCount];
		size_t		fComponentCount;
	};

	/** @brief Normalized absolute path that throws std::bad_alloc on allocation failure */
	class Path {
	public:
		Path(const char* path)
			:
			fPath(path)
		{
			if (fPath.IsEmpty()) {
				if (path[0] != '\0')
					throw std::bad_alloc();
			} else {
				// remove duplicate '/'s
				char* buffer = fPath.LockBuffer(fPath.Length());
				int32 k = 0;
				for (int32 i = 0; buffer[i] != '\0'; i++) {
					if (buffer[i] == '/' && k > 0 && buffer[k - 1] == '/')
						continue;
					buffer[k++] = buffer[i];
				}

				// remove trailing '/'
				if (k > 1 && buffer[k - 1] == '/')
					k--;

				fPath.LockBuffer(k);
			}
		}

		Path& AppendComponent(const char* component)
		{
			if (fPath.IsEmpty()) {
				fPath = component;
				if (fPath.IsEmpty() && component[0] != '\0')
					throw std::bad_alloc();
			} else {
				int32 length = fPath.Length();
				if (fPath[length - 1] != '/') {
					fPath += '/';
					if (++length != fPath.Length())
						throw std::bad_alloc();
				}

				fPath += component;
				if (fPath.Length() <= length)
					throw std::bad_alloc();
			}

			return *this;
		}

		Path& RemoveLastComponent()
		{
			int32 index = fPath.FindLast('/');
			if (index < 0 || (index == 0 && fPath.Length() == 1))
				fPath.Truncate(0);
			else if (index == 0)
				fPath.Truncate(1);
			else
				fPath.Truncate(index);
			return *this;
		}

		const char* Leaf() const
		{
			int32 index = fPath.FindLast('/');
			if (index < 0 || (index == 0 && fPath.Length() == 1))
				return fPath.String();
			return fPath.String() + index + 1;
		}

		const BString ToString() const
		{
			return fPath;
		}

		const char* ToCString() const
		{
			return fPath.String();
		}

		operator const BString&() const
		{
			return fPath;
		}

		operator const char*() const
		{
			return fPath;
		}

	private:
		BString	fPath;
	};

public:
	/** @brief Escape shell-special characters in a string; throws std::bad_alloc */
	static	BString				ShellEscapeString(const BString& string);
									// throw std::bad_alloc

	/** @brief Open (and optionally create) a sub-directory relative to a base directory */
	static	status_t			OpenSubDirectory(
									const BDirectory& baseDirectory,
									const RelativePath& path, bool create,
									BDirectory& _directory);

	/** @brief Compare the contents of two files identified by entry */
	static	status_t			CompareFileContent(const Entry& entry1,
									const Entry& entry2, bool& _equal);
	/** @brief Compare the contents of two seekable I/O streams */
	static	status_t			CompareFileContent(BPositionIO& content1,
									BPositionIO& content2, bool& _equal);

	/** @brief Compare two symbolic link targets identified by entry */
	static	status_t			CompareSymLinks(const Entry& entry1,
									const Entry& entry2, bool& _equal);
	/** @brief Compare two already-opened symbolic link targets */
	static	status_t			CompareSymLinks(BSymLink& symLink1,
									BSymLink& symLink2, bool& _equal);

	/** @brief Extract content from a package file entry into a target directory */
	static	status_t			ExtractPackageContent(const Entry& packageEntry,
									const char* contentPath,
									const Entry& targetDirectoryEntry);
	/** @brief Extract content from a package given explicit paths */
	static	status_t			ExtractPackageContent(const char* packagePath,
									const char* contentPath,
									const char* targetDirectoryPath);

private:
	static	status_t			_OpenFile(const Entry& entry, BFile& file);
	static	status_t			_OpenSymLink(const Entry& entry,
									BSymLink& symLink);
};



#endif	// FS_UTILS_H
