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
 * Incorporates work from the Be Incorporated media kit headers, originally
 * Copyright 1992-97, Be Incorporated.
 */

/** @file OldMediaKit.h
    @brief Legacy R5 media-kit umbrella header pulling in the deprecated audio
           stream, buffer stream, subscriber, and message-protocol headers in
           one include. Retained for binary compatibility. */


#include <OldAudioStream.h>
#include <OldBufferMsgs.h>
#include <OldBufferStream.h>
#include <OldBufferStreamManager.h>
#include <OldMediaDefs.h>
#include <OldSubscriber.h>

