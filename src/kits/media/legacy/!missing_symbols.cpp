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
 *   Copyright 2002, Marcus Overhagen. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file !missing_symbols.cpp
 * @brief Compatibility shims for undocumented symbols exported by BeOS R5 libmedia.so.
 *
 * Provides stub or minimal implementations for mangled C++ symbols that are
 * required by third-party add-ons (emu10k.media_addon, SoundPlay, libgame.so)
 * and old IDE tools (BeIDE) that were compiled against the BeOS R5 media kit
 * headers. All return codes are best-effort guesses. Symbols no longer
 * required (SoundPlay, rtm_create_pool_etc) are conditionally disabled.
 *
 * @see OldSubscriber.cpp, OldAudioStream.cpp
 */

#include <MediaTrack.h>
#include "MediaDebug.h"

#if 0
// SoundPlay 4.8 is evil, uses undocumented media kit API
extern "C" void *__get_decoder_manager__Fv(void);
void *__get_decoder_manager__Fv(void) { return 0; }
extern "C" void ScanDirs__Q28BPrivate13_AddonManager(void *);
void ScanDirs__Q28BPrivate13_AddonManager(void *) { }
#endif

/* According to the normal headers, these symbols should neither be
 * included in libmedia.so, nor used by anything.
 * But BeOS R5 has them, and they are required to load the BeOS R5
 * emu10k.media_addon, that might have been compiled with strange headers.
 * They should be removed once the emu10k.media_addon is no longer used.
 */
extern "C" void Connect__15BBufferProducerlRC12media_sourceRC17media_destinationRC12media_formatPc(void *);
extern "C" status_t Connected__15BBufferConsumerRC12media_sourceRC17media_destinationRC12media_formatP11media_input(void *);

/**
 * @brief Legacy GCC 2 mangled stub for BBufferProducer::Connect().
 *
 * Exported for binary compatibility with the BeOS R5 emu10k.media_addon which
 * references this mangled symbol directly. The function performs no operation.
 *
 * @param arg  Ignored; present only to match the mangled signature.
 */
void Connect__15BBufferProducerlRC12media_sourceRC17media_destinationRC12media_formatPc(void *)
{
}

/**
 * @brief Legacy GCC 2 mangled stub for BBufferConsumer::Connected().
 *
 * Exported for binary compatibility with the BeOS R5 emu10k.media_addon which
 * references this mangled symbol directly.
 *
 * @param arg  Ignored; present only to match the mangled signature.
 * @return B_OK always.
 */
status_t Connected__15BBufferConsumerRC12media_sourceRC17media_destinationRC12media_formatP11media_input(void *)
{
	return B_OK;
}

/*

used by libgame.so
BPrivate::BTrackReader::CountFrames(void)
BPrivate::BTrackReader::Format(void) const
BPrivate::BTrackReader::FrameSize(void)
BPrivate::BTrackReader::ReadFrames(void *, long)
BPrivate::BTrackReader::SeekToFrame(long long *)
BPrivate::BTrackReader::Track(void)
BPrivate::BTrackReader::~BTrackReader(void)
BPrivate::BTrackReader::BTrackReader(BMediaTrack *, media_raw_audio_format const &)
BPrivate::BTrackReader::BTrackReader(BFile *, media_raw_audio_format const &)
BPrivate::dec_load_hook(void *, long)
BPrivate::extractor_load_hook(void *, long)
rtm_create_pool_etc
rtm_get_pool

used by libmidi.so
BSubscriber::IsInStream(void) const
BSubscriber::BSubscriber(char const *)
00036a94 B BSubscriber type_info node
BDACStream::SamplingRate(float *) const
BDACStream::SetSamplingRate(float)
BDACStream::BDACStream(void)
00036a88 B BDACStream type_info node

used by BeIDE
BSubscriber::EnterStream(_sub_info *, bool, void *, bool (*)(void *, char *, unsigned long, void *), long (*)(void *, long), bool)
BSubscriber::ExitStream(bool)
BSubscriber::ProcessLoop(void)
BSubscriber::Subscribe(BAbstractBufferStream *)
BSubscriber::Unsubscribe(void)
BSubscriber::~BSubscriber(void)
BSubscriber::_ReservedSubscriber1(void)
BSubscriber::_ReservedSubscriber2(void)
BSubscriber::_ReservedSubscriber3(void)
BSubscriber::BSubscriber(char const *)
001132c0 W BSubscriber type_info function
00180484 B BSubscriber type_info node
BDACStream::SetSamplingRate(float)
BDACStream::~BDACStream(void)
BDACStream::BDACStream(void)
00180478 B BDACStream type_info node

used by 3dmiX
BDACStream::SetSamplingRate(float)
BDACStream::BDACStream(void)
000706c4 B BDACStream type_info node
BSubscriber::BSubscriber(char const *)
000706d0 B BSubscriber type_info node
*/

/** @brief Opaque subscriber info structure used by the GCC 2 BeIDE ABI. */
struct _sub_info
{
	uint32 dummy;
};

namespace BPrivate
{

/** @brief Global media debug flag (type uncertain; kept as int32 for ABI compatibility). */
int32 media_debug; /* is this a function, or a bool, or an int32 ???  */

//BPrivate::BTrackReader move to TrackReader.h & TrackReader.cpp

/*

Already in MediaFormats.cpp

void dec_load_hook(void *, long);
void extractor_load_hook(void *, long);

void dec_load_hook(void *, long)
{
}

void extractor_load_hook(void *, long)
{
}
*/

};

/**
 * @brief Internal media roster helper class exporting cleanup function hooks.
 *
 * _BMediaRosterP provides AddCleanupFunction/RemoveCleanupFunction to allow
 * add-ons to register teardown callbacks with the media roster. These stubs
 * ensure the vtable slot and symbol exist for old add-ons that call them.
 */
class _BMediaRosterP
{
	void AddCleanupFunction(void (*)(void *), void *);
	void RemoveCleanupFunction(void (*)(void *), void *);
};

/**
 * @brief Registers a cleanup callback with the media roster (unimplemented).
 *
 * @param func    Function to call during media roster teardown.
 * @param cookie  Opaque argument forwarded to \a func.
 */
void _BMediaRosterP::AddCleanupFunction(void (*)(void *), void *)
{
	UNIMPLEMENTED();
}

/**
 * @brief Unregisters a previously added cleanup callback (unimplemented).
 *
 * @param func    The function pointer to remove.
 * @param cookie  The cookie that was passed to AddCleanupFunction().
 */
void _BMediaRosterP::RemoveCleanupFunction(void (*)(void *), void *)
{
	UNIMPLEMENTED();
}

extern "C" {

/** @brief Workaround flag required for DOOM's MIDI initialisation path. */
int MIDIisInitializingWorkaroundForDoom;
/*

these two moved to RealtimeAlloc.cpp

void rtm_create_pool_etc(void);
int32 rtm_get_pool(int32,int32 **P);

void rtm_create_pool_etc(void)
{
	UNIMPLEMENTED();
}

int32 rtm_get_pool(int32,int32 **p)
{
	UNIMPLEMENTED();
	*p = (int32*)0x1199;
	return B_OK;
}

*/


}
