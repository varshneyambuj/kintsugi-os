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
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 *   AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file FBCPadding.cpp
 * @brief Future Binary Compatibility (FBC) virtual-function padding stubs.
 *
 * Provides empty reserved virtual-method implementations for BRecentItemsList,
 * BRecentFilesList, BRecentFoldersList, BRecentAppsList, and BFilePanel to
 * preserve ABI stability across binary-compatible releases.
 *
 * @see BFilePanel, BRecentItemsList
 */


#include <FilePanel.h>

#include "FilePanelPriv.h"
#include "RecentItems.h"
#include "FSUtils.h"


// FBC fluff, stick it here to not pollute real .cpp files

void BRecentItemsList::_r1() {}
void BRecentItemsList::_r2() {}
void BRecentItemsList::_r3() {}
void BRecentItemsList::_r4() {}
void BRecentItemsList::_r5() {}
void BRecentItemsList::_r6() {}
void BRecentItemsList::_r7() {}
void BRecentItemsList::_r8() {}
void BRecentItemsList::_r9() {}
void BRecentItemsList::_r10() {}
void BRecentFilesList::_r11() {}
void BRecentFilesList::_r12() {}
void BRecentFilesList::_r13() {}
void BRecentFilesList::_r14() {}
void BRecentFilesList::_r15() {}
void BRecentFilesList::_r16() {}
void BRecentFilesList::_r17() {}
void BRecentFilesList::_r18() {}
void BRecentFilesList::_r19() {}
void BRecentFilesList::_r110() {}
void BRecentFoldersList::_r21() {}
void BRecentFoldersList::_r22() {}
void BRecentFoldersList::_r23() {}
void BRecentFoldersList::_r24() {}
void BRecentFoldersList::_r25() {}
void BRecentFoldersList::_r26() {}
void BRecentFoldersList::_r27() {}
void BRecentFoldersList::_r28() {}
void BRecentFoldersList::_r29() {}
void BRecentFoldersList::_r210() {}
void BRecentAppsList::_r31() {}
void BRecentAppsList::_r32() {}
void BRecentAppsList::_r33() {}
void BRecentAppsList::_r34() {}
void BRecentAppsList::_r35() {}
void BRecentAppsList::_r36() {}
void BRecentAppsList::_r37() {}
void BRecentAppsList::_r38() {}
void BRecentAppsList::_r39() {}
void BRecentAppsList::_r310() {}

void BFilePanel::_ReservedFilePanel1() {}
void BFilePanel::_ReservedFilePanel2() {}
void BFilePanel::_ReservedFilePanel3() {}
void BFilePanel::_ReservedFilePanel4() {}
void BFilePanel::_ReservedFilePanel5() {}
void BFilePanel::_ReservedFilePanel6() {}
void BFilePanel::_ReservedFilePanel7() {}
void BFilePanel::_ReservedFilePanel8() {}

// deprecated cruft

#if __GNUC__ && __GNUC__ < 3
extern "C" {

_EXPORT void
run_open_panel__Fv()
{
	(new TFilePanel())->Show();
}


_EXPORT void
run_save_panel__Fv()
{
	(new TFilePanel(B_SAVE_PANEL))->Show();
}

_EXPORT BFilePanel*
__10BFilePanel15file_panel_modeP10BMessengerP9entry_refUlbP8BMessageP10BRefFilterT5T5
(void* self,
	file_panel_mode mode, BMessenger* target,
	entry_ref* ref, uint32 nodeFlavors, bool multipleSelection,
	BMessage* message, BRefFilter* filter, bool modal,
	bool hideWhenDone)
{
	return new (self) BFilePanel(mode, target, ref, nodeFlavors,
								 multipleSelection, message, filter, modal,
								 hideWhenDone);
}

_EXPORT void
SetPanelDirectory__10BFilePanelP10BDirectory(BFilePanel* self, BDirectory* d)
{
	self->SetPanelDirectory(d);
}

_EXPORT void
SetPanelDirectory__10BFilePanelP6BEntry(BFilePanel* self, BEntry* e)
{
	self->SetPanelDirectory(e);
}

_EXPORT void
SetPanelDirectory__10BFilePanelP9entry_ref(BFilePanel* self, entry_ref* r)
{
	self->SetPanelDirectory(r);
}

_EXPORT status_t
FSGetDeskDir__8BPrivateP10BDirectoryl(BDirectory* deskDir, dev_t)
{
	// since we no longer keep a desktop directory on any volume other
	// than /boot, redirect to FSGetDeskDir ignoring the volume argument
	return FSGetDeskDir(deskDir);
}

_EXPORT status_t
FSGetDeskDir__FP10BDirectoryl(BDirectory* deskDir, dev_t)
{
	return FSGetDeskDir(deskDir);
}

_EXPORT status_t
FSCopyAttributesAndStats__8BPrivateP5BNodeT1(BNode* srcNode, BNode* destNode)
{
	return FSCopyAttributesAndStats(srcNode, destNode);
}

_EXPORT status_t
FSGetTrashDir__FP10BDirectoryl(BDirectory* trashDir, dev_t volume)
{
	return FSGetTrashDir(trashDir, volume);
}

}
#endif
