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
   Copyright 2007-2015, Haiku, Inc. All rights reserved.
   Distributed under the terms of the MIT License.
 */
/** @file AutoMounter.h
 *  @brief Server application that auto-mounts disk partitions. */
#ifndef	AUTO_MOUNTER_H
#define AUTO_MOUNTER_H


#include <DiskDeviceDefs.h>
#include <File.h>
#include <Message.h>
#include <Server.h>


class BPartition;
class BPath;


enum mount_mode {
	kNoVolumes,
	kOnlyBFSVolumes,
	kAllVolumes,
	kRestorePreviousVolumes
};


/** @brief Server that automatically mounts disk partitions on events. */
class AutoMounter : public BServer {
public:
								/** @brief Construct the auto-mounter and read settings. */
								AutoMounter();
	/** @brief Unregister device watching and events. */
	virtual						~AutoMounter();

	/** @brief Perform the initial volume scan and mount. */
	virtual	void				ReadyToRun();
	/** @brief Handle mount, unmount, settings, and device messages. */
	virtual	void				MessageReceived(BMessage* message);
	/** @brief Write settings and allow quit. */
	virtual	bool				QuitRequested();

private:
			void				_GetSettings(BMessage* message);

			void				_MountVolumes(mount_mode normal,
									mount_mode removable,
									bool initialRescan = false,
									partition_id deviceID = -1);
			void				_MountVolume(const BMessage* message);
			bool				_SuggestForceUnmount(const char* name,
									status_t error);
			void				_ReportUnmountError(const char* name,
									status_t error);
			void				_UnmountAndEjectVolume(BPartition* partition,
									BPath& mountPoint, const char* name);
			void				_UnmountAndEjectVolume(BMessage* message);

			void				_FromMode(mount_mode mode, bool& all,
									bool& bfs, bool& restore);
			mount_mode			_ToMode(bool all, bool bfs,
									bool restore = false);

			void				_UpdateSettingsFromMessage(BMessage* message);
			void				_ReadSettings();
			void				_WriteSettings();

	static	bool				_SuggestMountFlags(const BPartition* partition,
									uint32* _flags);

		friend class MountVisitor;

private:
			mount_mode			fNormalMode; /**< Mount mode for non-removable devices */
			mount_mode			fRemovableMode; /**< Mount mode for removable devices */
			bool				fEjectWhenUnmounting; /**< Eject media after unmounting if true */

			BFile				fPrefsFile; /**< Persistent preferences file */
			BMessage			fSettings; /**< Cached settings message */
};


#endif // AUTO_MOUNTER_H
