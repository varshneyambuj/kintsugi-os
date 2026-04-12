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
 *   Copyright 2008 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *   Authors: Julun, <host.haiku@gmx.de>
 */


/**
 * @file PrintPanel.cpp
 * @brief BPrintPanel — modal window base class for print dialogs.
 *
 * BPrintPanel provides the common frame (OK/Cancel buttons, semaphore-based
 * modal loop, Escape key filter) shared by BJobSetupPanel and page-setup panels.
 * Subclasses install their own content view via AddPanel(), which replaces the
 * placeholder group view.
 *
 * @see BJobSetupPanel
 */


#include <PrintPanel.h>

#include <Button.h>
#include <GroupLayoutBuilder.h>
#include <GroupView.h>
#include <Screen.h>


namespace BPrivate {
	namespace Print {


// #pragma mark -- _BPrintPanelFilter_


/**
 * @brief Constructs an Escape-key filter for \a panel.
 *
 * Registers as a B_KEY_DOWN message filter and keeps a back-pointer to the
 * owning BPrintPanel so it can post a quit request when Escape is pressed.
 *
 * @param panel  The BPrintPanel that owns this filter.
 */
BPrintPanel::_BPrintPanelFilter_::_BPrintPanelFilter_(BPrintPanel* panel)
	: BMessageFilter(B_KEY_DOWN)
	, fPrintPanel(panel)
{
}


/**
 * @brief Intercepts Escape key presses and cancels the print panel.
 *
 * If the incoming key-down message carries key code 1 (Escape), a
 * B_QUIT_REQUESTED message is posted to the panel and the original message is
 * consumed. All other key events are passed through.
 *
 * @param msg     The incoming B_KEY_DOWN BMessage.
 * @param target  The intended message target (unused).
 * @return B_SKIP_MESSAGE if the Escape key was pressed, B_DISPATCH_MESSAGE otherwise.
 */
filter_result
BPrintPanel::_BPrintPanelFilter_::Filter(BMessage* msg, BHandler** target)
{
	int32 key;
	filter_result result = B_DISPATCH_MESSAGE;
	if (msg->FindInt32("key", &key) == B_OK && key == 1) {
		fPrintPanel->PostMessage(B_QUIT_REQUESTED);
		result = B_SKIP_MESSAGE;
	}
	return result;
}


// #pragma mark -- BPrintPanel


/**
 * @brief Constructs a BPrintPanel modal window with the given title.
 *
 * Creates the standard OK and Cancel button bar, installs an Escape-key filter,
 * initialises the semaphore to -1 (not yet created), and sets the result to
 * B_CANCEL as the default.
 *
 * @param title  The window title string.
 */
BPrintPanel::BPrintPanel(const BString& title)
	: BWindow(BRect(0, 0, 640, 480), title.String(), B_TITLED_WINDOW_LOOK,
		B_MODAL_APP_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_RESIZABLE |
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
	, fPanel(new BGroupView)
	, fPrintPanelSem(-1)
	, fPrintPanelResult(B_CANCEL)
{
	BButton* ok = new BButton("OK", new BMessage('_ok_'));
	BButton* cancel = new BButton("Cancel", new BMessage('_cl_'));

	BGroupLayout *layout = new BGroupLayout(B_HORIZONTAL);
	SetLayout(layout);

	AddChild(BGroupLayoutBuilder(B_VERTICAL, 10.0)
			.Add(fPanel)
			.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10.0)
				.AddGlue()
				.Add(cancel)
				.Add(ok)
				.SetInsets(0.0, 0.0, 0.0, 0.0))
			.SetInsets(10.0, 10.0, 10.0, 10.0)
		);

	ok->MakeDefault(true);
	AddCommonFilter(new _BPrintPanelFilter_(this));
}


/**
 * @brief Destroys the BPrintPanel and releases the semaphore if still held.
 */
BPrintPanel::~BPrintPanel()
{
	if (fPrintPanelSem > 0)
		delete_sem(fPrintPanelSem);
}


/**
 * @brief Archive-reconstruction constructor (not yet implemented).
 *
 * @param data  The BMessage archive to restore from.
 */
BPrintPanel::BPrintPanel(BMessage* data)
	: BWindow(data)
{
	// TODO: implement
}


/**
 * @brief Instantiates a BPrintPanel from an archive (not yet implemented).
 *
 * @param data  The BMessage archive produced by Archive().
 * @return Always returns NULL until implemented.
 */
BArchivable*
BPrintPanel::Instantiate(BMessage* data)
{
	// TODO: implement
	return NULL;
}


/**
 * @brief Archives this panel into a BMessage (not yet implemented).
 *
 * @param data  The BMessage to write the archive into.
 * @param deep  If true, child views are archived recursively.
 * @return Always returns B_ERROR until implemented.
 */
status_t
BPrintPanel::Archive(BMessage* data, bool deep) const
{
	// TODO: implement
	return B_ERROR;
}


/**
 * @brief Returns the first child of the content container view.
 *
 * @return Pointer to the current content panel view, or NULL if none is set.
 */
BView*
BPrintPanel::Panel() const
{
	return fPanel->ChildAt(0);
}


/**
 * @brief Replaces the current content panel with \a panel.
 *
 * If a content view is already present it is removed and deleted before
 * \a panel is added. The window is resized to the new preferred layout size.
 *
 * @param panel  The new content BView to display.
 */
void
BPrintPanel::AddPanel(BView* panel)
{
	BView* child = Panel();
	if (child) {
		RemovePanel(child);
		delete child;
	}

	fPanel->AddChild(panel);

	BSize size = GetLayout()->PreferredSize();
	ResizeTo(size.Width(), size.Height());
}


/**
 * @brief Removes \a child from the content container if it is the current panel.
 *
 * @param child  The view to remove.
 * @return true if \a child was the current panel and was removed, false otherwise.
 */
bool
BPrintPanel::RemovePanel(BView* child)
{
	BView* panel = Panel();
	if (child == panel)
		return fPanel->RemoveChild(child);

	return false;
}


/**
 * @brief Handles '_ok_' and '_cl_' button messages to complete the modal loop.
 *
 * On '_ok_', sets fPrintPanelResult to B_OK then falls through to the cancel
 * handler which deletes the semaphore, waking the ShowPanel() loop.
 *
 * @param message  The incoming BMessage to dispatch.
 */
void
BPrintPanel::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case '_ok_': {
			fPrintPanelResult = B_OK;

		// fall through
		case '_cl_':
			delete_sem(fPrintPanelSem);
			fPrintPanelSem = -1;
		}	break;

		default:
			BWindow::MessageReceived(message);
	}
}


/**
 * @brief Forwards frame-resize events to the base class.
 *
 * @param newWidth   New window width in pixels.
 * @param newHeight  New window height in pixels.
 */
void
BPrintPanel::FrameResized(float newWidth, float newHeight)
{
	BWindow::FrameResized(newWidth, newHeight);
}


/**
 * @brief Forwards scripting specifier resolution to the base class.
 *
 * @param message    The scripting message.
 * @param index      Specifier index.
 * @param specifier  The specifier BMessage.
 * @param form       Specifier form constant.
 * @param property   Property name string.
 * @return The resolved BHandler, as returned by BWindow.
 */
BHandler*
BPrintPanel::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 form, const char* property)
{
	return BWindow::ResolveSpecifier(message, index, specifier, form, property);
}


/**
 * @brief Forwards suite query to the base class.
 *
 * @param data  The BMessage to populate with supported suite names.
 * @return B_OK on success.
 */
status_t
BPrintPanel::GetSupportedSuites(BMessage* data)
{
	return BWindow::GetSupportedSuites(data);
}


/**
 * @brief Forwards perform requests to the base class.
 *
 * @param d    The perform_code identifying the operation.
 * @param arg  Opaque argument pointer.
 * @return The result from BWindow::Perform().
 */
status_t
BPrintPanel::Perform(perform_code d, void* arg)
{
	return BWindow::Perform(d, arg);
}


/**
 * @brief Quits the panel window by delegating to BWindow::Quit().
 */
void
BPrintPanel::Quit()
{
	BWindow::Quit();
}


/**
 * @brief Returns true, allowing the panel to be closed.
 *
 * @return The result from BWindow::QuitRequested().
 */
bool
BPrintPanel::QuitRequested()
{
	return BWindow::QuitRequested();
}


/**
 * @brief Forwards message dispatch to the base class.
 *
 * @param message  The message to dispatch.
 * @param handler  The target handler.
 */
void
BPrintPanel::DispatchMessage(BMessage* message, BHandler* handler)
{
	BWindow::DispatchMessage(message, handler);
}


/**
 * @brief Shows the panel modally and blocks until the user closes it.
 *
 * Creates a semaphore, centres the window on the main screen, calls Show(),
 * then spins on acquire_sem_etc() with a 50 ms timeout so that any owning
 * BWindow can call UpdateIfNeeded() while waiting. Returns fPrintPanelResult
 * once the semaphore is deleted by MessageReceived().
 *
 * @return B_OK if the user confirmed, B_CANCEL if they dismissed the dialog,
 *         or B_CANCEL if the semaphore could not be created.
 */
status_t
BPrintPanel::ShowPanel()
{
	fPrintPanelSem = create_sem(0, "PrintPanel");
	if (fPrintPanelSem < 0) {
		Quit();
		return B_CANCEL;
	}

	BWindow* window = dynamic_cast<BWindow*> (BLooper::LooperForThread(find_thread(NULL)));

	{
		BRect bounds(Bounds());
		BRect frame(BScreen(B_MAIN_SCREEN_ID).Frame());
		MoveTo((frame.Width() - bounds.Width()) / 2.0,
			(frame.Height() - bounds.Height()) / 2.0);
	}

	Show();

	if (window) {
		status_t err;
		while (true) {
			do {
				err = acquire_sem_etc(fPrintPanelSem, 1, B_RELATIVE_TIMEOUT, 50000);
			} while (err == B_INTERRUPTED);

			if (err == B_BAD_SEM_ID)
				break;
			window->UpdateIfNeeded();
		}
	} else {
		while (acquire_sem(fPrintPanelSem) == B_INTERRUPTED) {}
	}

	return fPrintPanelResult;
}


/**
 * @brief Forwards AddChild to the base BWindow implementation.
 *
 * @param child   The view to add.
 * @param before  Optional sibling before which to insert the child.
 */
void
BPrintPanel::AddChild(BView* child, BView* before)
{
	BWindow::AddChild(child, before);
}


/**
 * @brief Forwards RemoveChild to the base BWindow implementation.
 *
 * @param child  The view to remove.
 * @return true if the view was removed successfully.
 */
bool
BPrintPanel::RemoveChild(BView* child)
{
	return BWindow::RemoveChild(child);
}


/**
 * @brief Forwards ChildAt to the base BWindow implementation.
 *
 * @param index  Zero-based index of the child to retrieve.
 * @return The child BView at \a index, or NULL if out of range.
 */
BView*
BPrintPanel::ChildAt(int32 index) const
{
	return BWindow::ChildAt(index);
}


/** @brief Reserved virtual slot 1 for future binary compatibility. */
void BPrintPanel::_ReservedBPrintPanel1() {}
/** @brief Reserved virtual slot 2 for future binary compatibility. */
void BPrintPanel::_ReservedBPrintPanel2() {}
/** @brief Reserved virtual slot 3 for future binary compatibility. */
void BPrintPanel::_ReservedBPrintPanel3() {}
/** @brief Reserved virtual slot 4 for future binary compatibility. */
void BPrintPanel::_ReservedBPrintPanel4() {}
/** @brief Reserved virtual slot 5 for future binary compatibility. */
void BPrintPanel::_ReservedBPrintPanel5() {}


	}	// namespace Print
}	// namespace BPrivate
