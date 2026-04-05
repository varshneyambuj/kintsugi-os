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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2011, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Erik Jaesler (erik@cgsoftware.com)
 */
#ifndef LOOPER_LIST_H
#define LOOPER_LIST_H


#include <vector>

#include <OS.h>
#include <locks.h>


class BList;
class BLooper;


namespace BPrivate {


class BLooperList {
public:
								BLooperList();

			bool				Lock();
			void				Unlock();
			bool				IsLocked();

			void				AddLooper(BLooper* l);
			bool				IsLooperValid(const BLooper* l);
			bool				RemoveLooper(BLooper* l);
			void				GetLooperList(BList* list);
			int32				CountLoopers();
			BLooper*			LooperAt(int32 index);
			BLooper*			LooperForThread(thread_id tid);
			BLooper*			LooperForName(const char* name);
			BLooper*			LooperForPort(port_id port);

			void				InitAfterFork();

private:
	struct LooperData {
		LooperData();
		LooperData(BLooper* looper);
		LooperData(const LooperData& rhs);
		LooperData& operator=(const LooperData& rhs);

		BLooper*	looper;
	};
	typedef std::vector<BLooperList::LooperData>::iterator LooperDataIterator;
	struct FindLooperPred {
		FindLooperPred(const BLooper* loop) : looper(loop) {}
		bool operator()(LooperData& Data);
		const BLooper* looper;
	};
	struct FindThreadPred {
		FindThreadPred(thread_id tid) : thread(tid) {}
		bool operator()(LooperData& Data);
		thread_id thread;
	};
	struct FindNamePred {
		FindNamePred(const char* n) : name(n) {}
		bool operator()(LooperData& Data);
		const char* name;
	};
	struct FindPortPred {
		FindPortPred(port_id pid) : port(pid) {}
		bool operator()(LooperData& Data);
		port_id port;
	};

	static	bool				EmptySlotPred(LooperData& Data);
			void				AssertLocked();

private:
			rw_lock					fLock;
			std::vector<LooperData>	fData;
};


extern BLooperList gLooperList;


}	// namespace BPrivate


#endif	// LOOPER_LIST_H
