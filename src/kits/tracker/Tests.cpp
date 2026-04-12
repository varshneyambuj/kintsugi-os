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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the Be Sample Code License.
 */


/**
 * @file Tests.cpp
 * @brief Debug-only icon-cache stress-test infrastructure.
 *
 * Provides a simple visual benchmark that continuously iterates the
 * filesystem and draws icons using the Tracker icon cache.  Only compiled
 * when DEBUG is defined.  The main entry point is RunIconCacheTests().
 *
 * @see IconCache, TNodeWalker, CachedEntryIterator
 */


#if DEBUG

#include "Tests.h"

#include <Debug.h>
#include <Locker.h>
#include <Path.h>
#include <String.h>
#include <Window.h>

#include <directories.h>

#include "EntryIterator.h"
#include "IconCache.h"
#include "Model.h"
#include "NodeWalker.h"
#include "StopWatch.h"
#include "Thread.h"


const char* pathsToSearch[] = {
//	"/boot/home/config/settings/NetPositive/Bookmarks/",
	kSystemDirectory,
	kAppsDirectory,
	kUserDirectory,
	0
};


namespace BTrackerPrivate {

class IconSpewer : public SimpleThread {
public:
	IconSpewer(bool newCache = true);
	~IconSpewer();
	void SetTarget(BWindow* target)
		{ this->target = target; }

	void Quit();
	void Run();

protected:
	void DrawSomeNew();
	void DrawSomeOld();
	const entry_ref* NextRef();
private:
	BLocker locker;
	bool quitting;
	BWindow* target;
	TNodeWalker* walker;
	CachedEntryIterator* cachingIterator;
	int32 searchPathIndex;
	bigtime_t cycleTime;
	bigtime_t lastCycleLap;
	int32 numDrawn;
	BStopWatch watch;
	bool newCache;
	BPath currentPath;

	entry_ref ref;
};


class IconTestWindow : public BWindow {
public:
	IconTestWindow();
	bool QuitRequested();
private:
	IconSpewer iconSpewer;
};

}	// namespace BTrackerPrivate


//	#pragma mark - IconSpewer


/**
 * @brief Construct an IconSpewer that walks the filesystem and draws icons continuously.
 *
 * @param newCache  If true, use the new CachedEntryIterator; otherwise fall back
 *                  to the legacy icon-cache path.
 */
IconSpewer::IconSpewer(bool newCache)
	:
	quitting(false),
	cachingIterator(0),
	searchPathIndex(0),
	cycleTime(0),
	lastCycleLap(0),
	numDrawn(0),
	watch("", true),
	newCache(newCache)
{
	walker = new TNodeWalker(pathsToSearch[searchPathIndex++]);
	if (newCache)
		cachingIterator = new CachedEntryIterator(walker, 40);
}


/**
 * @brief Destructor; releases the walker and caching iterator.
 */
IconSpewer::~IconSpewer()
{
	delete walker;
	delete cachingIterator;
}


/**
 * @brief Thread entry point; loops drawing icons until quit.
 */
void
IconSpewer::Run()
{
	BStopWatch watch("", true);
	for (;;) {
		AutoLock<BLocker> lock(locker);

		if (!lock || quitting)
			break;

		lock.Unlock();
		if (newCache)
			DrawSomeNew();
		else
			DrawSomeOld();
	}
}


/**
 * @brief Stop the icon-drawing thread immediately.
 */
void
IconSpewer::Quit()
{
	kill_thread(fScanThread);
	fScanThread = -1;
}


const icon_size kIconSize = B_LARGE_ICON;
const int32 kRowCount = 10;
const int32 kColumnCount = 10;


/**
 * @brief Draw a grid of icons using the new icon cache.
 *
 * Clears the test window, prints timing statistics, and draws
 * kRowCount x kColumnCount icons from the iterator into the view.
 */
void
IconSpewer::DrawSomeNew()
{
	target->Lock();
	BView* view = target->FindView("iconView");
	ASSERT(view);

	BRect bounds(target->Bounds());
	view->SetHighColor(255, 255, 255);
	view->FillRect(bounds);

	view->SetHighColor(0, 0, 0);
	char buffer[256];
	if (cycleTime) {
		sprintf(buffer, "last cycle time %" B_PRId64 " ms", cycleTime/1000);
		view->DrawString(buffer, BPoint(20, bounds.bottom - 20));
	}

	if (numDrawn) {
		sprintf(buffer, "average draw time %" B_PRId64 " us per icon",
			watch.ElapsedTime() / numDrawn);
		view->DrawString(buffer, BPoint(20, bounds.bottom - 30));
	}

	sprintf(buffer, "directory: %s", currentPath.Path());
	view->DrawString(buffer, BPoint(20, bounds.bottom - 40));

	target->Unlock();

	for (int32 row = 0; row < kRowCount; row++) {
		for (int32 column = 0; column < kColumnCount; column++) {
			BEntry entry(NextRef());
			Model model(&entry, true);

			if (!target->Lock())
				return;

			if (model.IsDirectory())
				entry.GetPath(&currentPath);

			IconCache::sIconCache->Draw(&model, view,
				BPoint(column * (kIconSize + 2), row * (kIconSize + 2)),
				kNormalIcon, BSize(kIconSize - 1, kIconSize - 1), true);
			target->Unlock();
			numDrawn++;
		}
	}
}


bool oldIconCacheInited = false;


/**
 * @brief Draw a grid of icons using the legacy icon cache (stub; code disabled).
 */
void
IconSpewer::DrawSomeOld()
{
#if 0
	if (!oldIconCacheInited)
		BIconCache::InitIconCaches();

	target->Lock();
	target->SetTitle("old cache");
	BView* view = target->FindView("iconView");
	ASSERT(view);

	BRect bounds(target->Bounds());
	view->SetHighColor(255, 255, 255);
	view->FillRect(bounds);

	view->SetHighColor(0, 0, 0);
	char buffer[256];
	if (cycleTime) {
		sprintf(buffer, "last cycle time %lld ms", cycleTime/1000);
		view->DrawString(buffer, BPoint(20, bounds.bottom - 20));
	}
	if (numDrawn) {
		sprintf(buffer, "average draw time %lld us per icon",
			watch.ElapsedTime() / numDrawn);
		view->DrawString(buffer, BPoint(20, bounds.bottom - 30));
	}
	sprintf(buffer, "directory: %s", currentPath.Path());
	view->DrawString(buffer, BPoint(20, bounds.bottom - 40));

	target->Unlock();

	for (int32 row = 0; row < kRowCount; row++) {
		for (int32 column = 0; column < kColumnCount; column++) {
			BEntry entry(NextRef());
			BModel model(&entry, true);

			if (!target->Lock())
				return;

			if (model.IsDirectory())
				entry.GetPath(&currentPath);

			BIconCache::LockIconCache();
			BIconCache* iconCache
				= BIconCache::GetIconCache(&model, kIconSize);
			iconCache->Draw(view, BPoint(column * (kIconSize + 2),
				row * (kIconSize + 2)), B_NORMAL_ICON, kIconSize, true);
			BIconCache::UnlockIconCache();

			target->Unlock();
			numDrawn++;
		}
	}
#endif
}


/**
 * @brief Advance to the next filesystem entry, wrapping around as needed.
 *
 * When the current walker is exhausted it cycles to the next search path,
 * wrapping back to the first path when all paths are consumed.
 *
 * @return Pointer to the next entry_ref.
 */
const entry_ref*
IconSpewer::NextRef()
{
	status_t result;
	if (newCache)
		result = cachingIterator->GetNextRef(&ref);
	else
		result = walker->GetNextRef(&ref);

	if (result == B_OK)
		return &ref;

	delete walker;
	if (!pathsToSearch[searchPathIndex]) {
		bigtime_t now = watch.ElapsedTime();
		cycleTime = now - lastCycleLap;
		lastCycleLap = now;
		PRINT(("**************************hit end of disk, starting over\n"));
		searchPathIndex = 0;
	}

	walker = new TNodeWalker(pathsToSearch[searchPathIndex++]);
	if (newCache) {
		cachingIterator->SetTo(walker);
		result = cachingIterator->GetNextRef(&ref);
	} else
		result = walker->GetNextRef(&ref);

	ASSERT(result == B_OK);
		// we don't expect and cannot deal with any problems here
	return &ref;
}


//	#pragma mark - IconTestWindow


/**
 * @brief Construct the icon test window and start the spewer thread.
 *
 * If no modifier keys are held the new cache is tested; otherwise the legacy
 * path is exercised.
 */
IconTestWindow::IconTestWindow()
	:
	BWindow(BRect(100, 100, 500, 600), "icon cache test",
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, 0),
	iconSpewer(modifiers() == 0)
{
	iconSpewer.SetTarget(this);
	BView* view = new BView(Bounds(), "iconView", B_FOLLOW_ALL, B_WILL_DRAW);
	AddChild(view);
	iconSpewer.Go();
}


/**
 * @brief Stop the spewer thread and allow the window to quit.
 *
 * @return true always.
 */
bool
IconTestWindow::QuitRequested()
{
	iconSpewer.Quit();
	return true;
}


/**
 * @brief Launch the icon-cache stress-test window.
 *
 * Creates and shows an IconTestWindow that continuously draws icons from
 * the cache to measure draw performance.
 */
void
RunIconCacheTests()
{
	(new IconTestWindow())->Show();
}

#endif	// DEBUG
