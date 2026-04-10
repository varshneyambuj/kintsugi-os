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
 *   Copyright 2002, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file media_server.h
 *  @brief Global declarations for the media server subsystem manager singletons. */

#ifndef MEDIA_SERVER_H
#define MEDIA_SERVER_H


class AppManager;
class NodeManager;
class BufferManager;
class NotificationManager;
class MediaFilesManager;
class AddOnManager;
class FormatManager;

extern AppManager* gAppManager;
extern NodeManager* gNodeManager;
extern BufferManager* gBufferManager;
extern NotificationManager* gNotificationManager;
extern MediaFilesManager* gMediaFilesManager;
extern AddOnManager* gAddOnManager;
extern FormatManager* gFormatManager;

#endif	// MEDIA_SERVER_H
