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
 * Copyright (c) 1997 by Be Incorporated. All Rights Reserved.
 */

/** @file OldAudioModule.h
    @brief Legacy R5 audio-module classes layered on the BMediaRenderer/
           BEventStream framework: BAudioEvent, BDACRenderer, BAudioFileStream,
           and BADCSource. Deprecated; retained for binary compatibility. */

#ifndef _AUDIO_MODULE_H
#define _AUDIO_MODULE_H

#include <File.h>

#include "OldMediaModule.h"
#include "OldMediaDefs.h"

class BADCStream;
class BDACStream;
class BSubscriber;

/** @brief Legacy media event carrying a block of float audio samples (mono or
           stereo) with a destination channel and gain. Deprecated. */
class BAudioEvent : public BMediaEvent {
public:
  BAudioEvent(int32 frames, bool stereo, float* samples = NULL);
  ~BAudioEvent();

  virtual mk_time		Start();
  virtual void			SetStart(mk_time);
  virtual mk_time		Duration();
  virtual int32			Frames();
  virtual float*		Samples();
  virtual int32			ChannelCount();
  virtual float			Gain();
  virtual void			SetGain(float);
  virtual int32			Destination();
  virtual void			SetDestination(int32);
  virtual bool			MixIn (float* dst, int32 frames, mk_time time);
  virtual BMediaEvent*	Clone();
  virtual bigtime_t		CaptureTime();
  virtual void			SetCaptureTime(bigtime_t);

private:
  mk_time	fStart;
  int32		fFrames;
  float*	fSamples;
  float		fGain;
  int32		fDestination;
  bigtime_t	fCaptureTime;
  bool		fStereo;
  bool		fFreeHuey;
};


/** @brief Legacy renderer that mixes active BAudioEvents and writes the result
           to the system DAC via a BDACStream subscriber. Deprecated. */
class BDACRenderer : public BMediaRenderer {
public:
  BDACRenderer(const char* name = NULL);
  ~BDACRenderer();

  mk_rate		Units();
  mk_time		Latency();
  mk_time		Start();
  mk_time		Duration();
  BTimeBase*	TimeBase();
  void			Open();
  void			Close();
  void			Wakeup();
  void			TransportChanged(mk_time time, mk_rate rate,
								 transport_status status);
  void			StreamChanged();

  virtual BMediaChannel*	Channel();

private:
  static bool	_WriteDAC(void* arg, char* buf, uint32 bytes, void* header);
  bool			WriteDAC(short* buf, int32 frames, audio_buffer_header* header);
  bool			MixActiveSegments(mk_time start);
  void			MixOutput(short* dst);

  BMediaChannel*	fChannel;
  BDACStream*		fDACStream;
  BSubscriber*		fSubscriber;
  float*			fBuffer;
  int32				fBufferFrames;
  BList				fActiveSegments;
  mk_time			fLatency;
  mk_time			fNextTime;
  bool				fRunning;
  BTimeBase			fDACTimeBase;
};


/** @brief Legacy event stream that decodes audio frames from a BFile and
           delivers them as BAudioEvents. Deprecated. */
class BAudioFileStream : public BEventStream {
public:
  BAudioFileStream(BMediaChannel* channel, BFile* file,
				   mk_time start = B_UNDEFINED_MK_TIME);
  ~BAudioFileStream();

  BMediaEvent*				GetEvent(BMediaChannel* channel);
  BMediaEvent*				PeekEvent(BMediaChannel* channel, mk_time asap = 0);
  status_t					SeekToTime(BMediaChannel* channel, mk_time time);
  void						SetStart(mk_time start);

  virtual bigtime_t 		CaptureTime();
  virtual BMediaChannel*	Channel();

private:
  BMediaChannel*	fChannel;
  BFile*			fFile;
  mk_time			fTime;
  BAudioEvent*		fCurrentEvent;
  short*			fBuffer;
};


/** @brief Legacy event stream that captures audio from the ADC via a
           BADCStream subscriber and yields BAudioEvents. Deprecated. */
class BADCSource : public BEventStream {
public:
  BADCSource(BMediaChannel* channel, mk_time start = 0);
  ~BADCSource();

  BMediaEvent*				GetEvent(BMediaChannel* channel);
  BMediaEvent*				PeekEvent(BMediaChannel* channel, mk_time asap = 0);
  status_t					SeekToTime(BMediaChannel* channel, mk_time time);
  void						SetStart(mk_time start);

  virtual BMediaChannel*	Channel();

private:
  static bool	_ReadADC(void* arg, char* buf, uint32 bytes, void* header);
  void			ReadADC(short* buf, int32 frames, audio_buffer_header* header);

  BMediaChannel*	fChannel;
  BFile*			fFile;
  mk_time			fTime;
  BAudioEvent*		fCurrentEvent;
  BAudioEvent*		fNextEvent;
  BLocker			fEventLock;
  BADCStream*		fADCStream;
  BSubscriber*		fSubscriber;
};


#endif
