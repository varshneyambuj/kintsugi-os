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
 *   Copyright 2007-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ryan Leavengood <leavengood@gmail.com>
 *       John Scipione <jscipione@gmail.com>
 *       Joseph Groover <looncraz@looncraz.net>
 */


/**
 * @file AboutWindow.cpp
 * @brief Implementation of BAboutWindow, a standard application "About" dialog
 *
 * BAboutWindow provides a ready-made modal window that displays application
 * name, version, copyright, and contributor information in a consistent style.
 *
 * @see BWindow, BView
 */


#include <AboutWindow.h>

#include <stdarg.h>
#include <time.h>

#include <Alert.h>
#include <AppFileInfo.h>
#include <Bitmap.h>
#include <Button.h>
#include <File.h>
#include <Font.h>
#include <GroupLayoutBuilder.h>
#include <GroupView.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <MessageFilter.h>
#include <Point.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <Size.h>
#include <String.h>
#include <StringView.h>
#include <SystemCatalog.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>


/** @brief Width of the decorative left stripe rendered by StripeView. */
static const float kStripeWidth = 30.0;

using BPrivate::gSystemCatalog;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AboutWindow"


namespace BPrivate {

class StripeView : public BView {
public:
							StripeView(BBitmap* icon);
	virtual					~StripeView();

	virtual	void			Draw(BRect updateRect);

			BBitmap*		Icon() const { return fIcon; };
			void			SetIcon(BBitmap* icon);

private:
			BBitmap*		fIcon;
};


class AboutView : public BGroupView {
public:
							AboutView(const char* name,
								const char* signature);
	virtual					~AboutView();

	virtual	void			AllAttached();

			BTextView*		InfoView() const { return fInfoView; };

			BBitmap*		Icon();
			status_t		SetIcon(BBitmap* icon);

			const char*		Name();
			status_t		SetName(const char* name);

			const char*		Version();
			status_t		SetVersion(const char* version);

private:
			BString			_GetVersionFromSignature(const char* signature);
			BBitmap*		_GetIconFromSignature(const char* signature);

private:
			BStringView*	fNameView;
			BStringView*	fVersionView;
			BTextView*		fInfoView;
			StripeView*		fStripeView;
};


//	#pragma mark - StripeView


/**
 * @brief Constructs a StripeView that displays the given application icon.
 *
 * Computes the preferred width from the icon bounds and sets up the panel
 * background color. If @a icon is NULL, the stripe is rendered without an icon.
 *
 * @param icon The application icon bitmap to display, or NULL for no icon.
 */
StripeView::StripeView(BBitmap* icon)
	:
	BView("StripeView", B_WILL_DRAW),
	fIcon(icon)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	float width = 0.0f;
	if (icon != NULL)
		width += icon->Bounds().Width() + 24.0f;

	SetExplicitSize(BSize(width, B_SIZE_UNSET));
	SetExplicitPreferredSize(BSize(width, B_SIZE_UNLIMITED));
}


/**
 * @brief Destroys the StripeView; does not free the icon bitmap.
 */
StripeView::~StripeView()
{
}


/**
 * @brief Draws the decorative stripe and the application icon.
 *
 * Fills the background with the panel color, renders a darker vertical stripe
 * on the left side (@c kStripeWidth pixels wide), then composites the icon
 * using alpha blending at a fixed offset.
 *
 * @param updateRect The portion of the view that needs to be redrawn.
 */
void
StripeView::Draw(BRect updateRect)
{
	if (fIcon == NULL)
		return;

	SetHighColor(ViewColor());
	FillRect(updateRect);

	BRect stripeRect = Bounds();
	stripeRect.right = kStripeWidth;
	SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
	FillRect(stripeRect);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	DrawBitmapAsync(fIcon, BPoint(15.0f, 10.0f));
}


/**
 * @brief Replaces the displayed icon and updates the view's explicit size.
 *
 * The previous icon (if any) is deleted. The view's explicit width is
 * recalculated to accommodate the new icon, and a layout invalidation is
 * triggered automatically.
 *
 * @param icon The new icon bitmap to display, or NULL to remove the icon.
 */
void
StripeView::SetIcon(BBitmap* icon)
{
	if (fIcon != NULL)
		delete fIcon;

	fIcon = icon;

	float width = 0.0f;
	if (icon != NULL)
		width += icon->Bounds().Width() + 24.0f;

	SetExplicitSize(BSize(width, B_SIZE_UNSET));
	SetExplicitPreferredSize(BSize(width, B_SIZE_UNLIMITED));
};


//	#pragma mark - AboutView


/**
 * @brief Constructs the AboutView, populating it with application metadata.
 *
 * Builds the complete view hierarchy: a StripeView on the left (populated
 * from the application's icon resource via @a signature), a bold name label,
 * a version string label, a scrollable info text view, and a close button.
 * Version and icon data are looked up automatically using @a signature.
 *
 * @param appName   The application name shown in the large bold label.
 * @param signature The application MIME signature used to look up version
 *                  info and icon from the application's resources.
 */
AboutView::AboutView(const char* appName, const char* signature)
	:
	BGroupView("AboutView", B_VERTICAL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fNameView = new BStringView("name", appName);
	BFont font;
	fNameView->GetFont(&font);
	font.SetFace(B_BOLD_FACE);
	font.SetSize(font.Size() * 2.0);
	fNameView->SetFont(&font, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE
		| B_FONT_FLAGS);
	fNameView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fVersionView = new BStringView("version",
		_GetVersionFromSignature(signature).String());
	fVersionView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	rgb_color documentColor = ui_color(B_DOCUMENT_TEXT_COLOR);
	fInfoView = new BTextView("info", NULL, &documentColor, B_WILL_DRAW);
	fInfoView->SetExplicitMinSize(BSize(210.0, 160.0));
	fInfoView->MakeEditable(false);
	fInfoView->SetWordWrap(true);
	fInfoView->SetInsets(5.0, 5.0, 5.0, 5.0);
	fInfoView->SetStylable(true);

	BScrollView* infoViewScroller = new BScrollView(
		"infoViewScroller", fInfoView, B_WILL_DRAW | B_FRAME_EVENTS,
		false, true, B_PLAIN_BORDER);

	fStripeView = new StripeView(_GetIconFromSignature(signature));

	const char* ok = B_TRANSLATE_MARK("OK");
	BButton* closeButton = new BButton("ok",
		gSystemCatalog.GetString(ok, "AboutWindow"),
		new BMessage(B_QUIT_REQUESTED));

	GroupLayout()->SetSpacing(0);
	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
		.Add(fStripeView)
		.AddGroup(B_VERTICAL)
			.SetInsets(0, B_USE_DEFAULT_SPACING,
				B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
			.AddGroup(B_VERTICAL, 0)
				.Add(fNameView)
				.Add(fVersionView)
				.AddStrut(B_USE_SMALL_SPACING)
				.Add(infoViewScroller)
				.End()
			.AddGroup(B_HORIZONTAL, 0)
				.AddGlue()
				.Add(closeButton)
				.End()
			.End()
		.View()->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


/**
 * @brief Destroys the AboutView.
 */
AboutView::~AboutView()
{
}


/**
 * @brief Applies UI colors to child views once they are attached to a window.
 *
 * Sets the correct semantic UI colors on the name, version, and info views
 * so they respond correctly to system color scheme changes.
 */
void
AboutView::AllAttached()
{
	fNameView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fInfoView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	fVersionView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


//	#pragma mark - AboutView private methods


/**
 * @brief Retrieves the human-readable version string from the application's resources.
 *
 * Uses @a signature to locate the application file via the roster, then reads
 * the @c B_APP_VERSION_KIND version info from the file's resources. Builds a
 * string in the form "Version X.Y[-variety]". Returns an empty BString if the
 * signature is NULL, the app cannot be found, or the version is all zeros.
 *
 * @param signature The application MIME signature.
 * @return A BString containing the formatted version string, or an empty
 *         string if the version cannot be determined.
 */
BString
AboutView::_GetVersionFromSignature(const char* signature)
{
	if (signature == NULL)
		return NULL;

	entry_ref ref;
	if (be_roster->FindApp(signature, &ref) != B_OK)
		return NULL;

	BFile file(&ref, B_READ_ONLY);
	BAppFileInfo appMime(&file);
	if (appMime.InitCheck() != B_OK)
		return NULL;

	version_info versionInfo;
	if (appMime.GetVersionInfo(&versionInfo, B_APP_VERSION_KIND) == B_OK) {
		if (versionInfo.major == 0 && versionInfo.middle == 0
			&& versionInfo.minor == 0) {
			return NULL;
		}

		const char* version = B_TRANSLATE_MARK("Version");
		version = gSystemCatalog.GetString(version, "AboutWindow");
		BString appVersion(version);
		appVersion << " " << versionInfo.major << "." << versionInfo.middle;
		if (versionInfo.minor > 0)
			appVersion << "." << versionInfo.minor;

		// Add the version variety
		const char* variety = NULL;
		switch (versionInfo.variety) {
			case B_DEVELOPMENT_VERSION:
				variety = B_TRANSLATE_MARK("development");
				break;
			case B_ALPHA_VERSION:
				variety = B_TRANSLATE_MARK("alpha");
				break;
			case B_BETA_VERSION:
				variety = B_TRANSLATE_MARK("beta");
				break;
			case B_GAMMA_VERSION:
				variety = B_TRANSLATE_MARK("gamma");
				break;
			case B_GOLDEN_MASTER_VERSION:
				variety = B_TRANSLATE_MARK("gold master");
				break;
		}

		if (variety != NULL) {
			variety = gSystemCatalog.GetString(variety, "AboutWindow");
			appVersion << "-" << variety;
		}

		return appVersion;
	}

	return NULL;
}


/**
 * @brief Loads the application icon from the application's resource file.
 *
 * Uses @a signature to locate the application via the roster, opens its file,
 * and extracts a 64x64 RGBA icon via BAppFileInfo. Returns NULL if the
 * signature is NULL, the application cannot be found, or the icon is missing.
 *
 * @param signature The application MIME signature.
 * @return A newly allocated BBitmap containing the 64x64 icon, or NULL on
 *         failure. The caller takes ownership of the returned bitmap.
 */
BBitmap*
AboutView::_GetIconFromSignature(const char* signature)
{
	if (signature == NULL)
		return NULL;

	entry_ref ref;
	if (be_roster->FindApp(signature, &ref) != B_OK)
		return NULL;

	BFile file(&ref, B_READ_ONLY);
	BAppFileInfo appMime(&file);
	if (appMime.InitCheck() != B_OK)
		return NULL;

	BBitmap* icon = new BBitmap(BRect(0.0, 0.0, 63.0, 63.0), B_RGBA32);
	if (appMime.GetIcon(icon, (icon_size)64) == B_OK)
		return icon;

	delete icon;
	return NULL;
}


//	#pragma mark - AboutView public methods


/**
 * @brief Returns the currently displayed application icon.
 *
 * @return A pointer to the icon BBitmap owned by the StripeView, or NULL if
 *         no icon is set or the StripeView has not been created.
 */
BBitmap*
AboutView::Icon()
{
	if (fStripeView == NULL)
		return NULL;

	return fStripeView->Icon();
}


/**
 * @brief Replaces the displayed application icon.
 *
 * Delegates to StripeView::SetIcon(). The StripeView takes ownership of
 * the bitmap and deletes any previously set icon.
 *
 * @param icon The new icon bitmap to display, or NULL to remove the icon.
 * @return B_OK on success, or B_NO_INIT if the StripeView is NULL.
 */
status_t
AboutView::SetIcon(BBitmap* icon)
{
	if (fStripeView == NULL)
		return B_NO_INIT;

	fStripeView->SetIcon(icon);

	return B_OK;
}


/**
 * @brief Returns the application name currently shown in the name label.
 *
 * @return A pointer to the name string owned by the internal BStringView.
 *         The pointer is valid as long as the view exists.
 */
const char*
AboutView::Name()
{
	return fNameView->Text();
}


/**
 * @brief Updates the application name label.
 *
 * @param name The new name string to display.
 * @return B_OK always.
 */
status_t
AboutView::SetName(const char* name)
{
	fNameView->SetText(name);

	return B_OK;
}


/**
 * @brief Returns the version string currently shown in the version label.
 *
 * @return A pointer to the version string owned by the internal BStringView.
 */
const char*
AboutView::Version()
{
	return fVersionView->Text();
}


/**
 * @brief Updates the version label text.
 *
 * @param version The new version string to display (e.g. "Version 1.2-beta").
 * @return B_OK always.
 */
status_t
AboutView::SetVersion(const char* version)
{
	fVersionView->SetText(version);

	return B_OK;
}

} // namespace BPrivate


//	#pragma mark - BAboutWindow


/**
 * @brief Constructs a BAboutWindow for the given application.
 *
 * Creates a modal window titled "About <appName>", builds the internal
 * AboutView (which auto-loads the icon and version from the application's
 * resources via @a signature), and positions the window at the recommended
 * on-screen location returned by AboutPosition().
 *
 * @param appName   The human-readable application name shown in the title bar
 *                  and the bold name label.
 * @param signature The application's MIME signature used to look up version
 *                  info and the application icon.
 *
 * @see AboutPosition(), BAboutWindow::AddCopyright(), BAboutWindow::Show()
 */
BAboutWindow::BAboutWindow(const char* appName, const char* signature)
	:
	BWindow(BRect(0.0, 0.0, 400.0, 200.0), appName, B_MODAL_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
{
	SetLayout(new BGroupLayout(B_VERTICAL));

	const char* about = B_TRANSLATE_MARK("About %app%");
	about = gSystemCatalog.GetString(about, "AboutWindow");

	BString title(about);
	title.ReplaceFirst("%app%", appName);
	SetTitle(title.String());

	fAboutView = new BPrivate::AboutView(appName, signature);
	AddChild(fAboutView);

	MoveTo(AboutPosition(Frame().Width(), Frame().Height()));
}


/**
 * @brief Destroys the BAboutWindow and releases the AboutView.
 *
 * Removes the AboutView from the window's child list before deleting it,
 * preventing a double-free when BWindow tears down its child views.
 */
BAboutWindow::~BAboutWindow()
{
	fAboutView->RemoveSelf();
	delete fAboutView;
	fAboutView = NULL;
}


//	#pragma mark - BAboutWindow virtual methods


/**
 * @brief Shows the about window, moving it to the current workspace first.
 *
 * If the window is currently hidden, it is reassigned to the active workspace
 * before being made visible, so it always appears on the user's current desktop.
 *
 * @see BWindow::Show(), BWindow::SetWorkspaces()
 */
void
BAboutWindow::Show()
{
	if (IsHidden()) {
		// move to current workspace
		SetWorkspaces(B_CURRENT_WORKSPACE);
	}

	BWindow::Show();
}


//	#pragma mark - BAboutWindow public methods


/**
 * @brief Computes the recommended on-screen position for the about window.
 *
 * Centers the window horizontally on the screen of the calling thread's window,
 * and places it approximately one quarter of the way down vertically. Falls back
 * to a 640x480 screen frame if the screen object is invalid.
 *
 * @param width  The desired width of the about window.
 * @param height The desired height of the about window.
 * @return The recommended top-left BPoint at which to place the window.
 */
BPoint
BAboutWindow::AboutPosition(float width, float height)
{
	BPoint result(100, 100);

	BWindow* window =
		dynamic_cast<BWindow*>(BLooper::LooperForThread(find_thread(NULL)));

	BScreen screen(window);
	BRect screenFrame(0, 0, 640, 480);
	if (screen.IsValid())
		screenFrame = screen.Frame();

	// Horizontally, we're smack in the middle
	result.x = screenFrame.left + (screenFrame.Width() / 2.0) - (width / 2.0);

	// This is probably sooo wrong, but it looks right on 1024 x 768
	result.y = screenFrame.top + (screenFrame.Height() / 4.0)
		- ceil(height / 3.0);

	return result;
}


/**
 * @brief Appends a plain-text description paragraph to the info view.
 *
 * The description is inserted into the scrollable info text area. If the info
 * view already contains text, the description is preceded by a blank line.
 * Does nothing if @a description is NULL.
 *
 * @param description The description text to display.
 *
 * @see AddText()
 */
void
BAboutWindow::AddDescription(const char* description)
{
	if (description == NULL)
		return;

	AddText(description);
}


/**
 * @brief Appends copyright information to the info view.
 *
 * Inserts a copyright line in the form "© <year range> <holder>", where the
 * year range spans from @a firstCopyrightYear to the current year. Additional
 * copyright holders may be supplied via @a extraCopyrights. The "All Rights
 * Reserved." string is appended and localized automatically.
 *
 * @param firstCopyrightYear The first year of the copyright (e.g. 2007).
 * @param copyrightHolder    The primary copyright holder string.
 * @param extraCopyrights    A NULL-terminated array of additional copyright
 *                           strings, or NULL if there are none.
 */
void
BAboutWindow::AddCopyright(int32 firstCopyrightYear,
	const char* copyrightHolder, const char** extraCopyrights)
{
	BString copyright(B_UTF8_COPYRIGHT " %years% %holder%");

	// Get current year
	time_t tp;
	time(&tp);
	char currentYear[5];
	strftime(currentYear, 5, "%Y", localtime(&tp));
	BString copyrightYears;
	copyrightYears << firstCopyrightYear;
	if (copyrightYears != currentYear)
		copyrightYears << "-" << currentYear;

	BString text("");
	if (fAboutView->InfoView()->TextLength() > 0)
		text << "\n\n";

	text << copyright;

	// Fill out the copyright year placeholder
	text.ReplaceAll("%years%", copyrightYears.String());

	// Fill in the copyright holder placeholder
	text.ReplaceAll("%holder%", copyrightHolder);

	// Add extra copyright strings
	if (extraCopyrights != NULL) {
		// Add optional extra copyright information
		for (int32 i = 0; extraCopyrights[i]; i++)
			text << "\n" << B_UTF8_COPYRIGHT << " " << extraCopyrights[i];
	}

	const char* allRightsReserved = B_TRANSLATE_MARK("All Rights Reserved.");
	allRightsReserved = gSystemCatalog.GetString(allRightsReserved,
		"AboutWindow");
	text << "\n    " << allRightsReserved;

	fAboutView->InfoView()->Insert(text.String());
}


/**
 * @brief Appends a "Written by:" section listing the given authors.
 *
 * Inserts a bold "Written by:" header followed by the indented author names.
 * Does nothing if @a authors is NULL.
 *
 * @param authors A NULL-terminated array of author name strings.
 *
 * @see AddText()
 */
void
BAboutWindow::AddAuthors(const char** authors)
{
	if (authors == NULL)
		return;

	const char* writtenBy = B_TRANSLATE_MARK("Written by:");
	writtenBy = gSystemCatalog.GetString(writtenBy, "AboutWindow");

	AddText(writtenBy, authors);
}


/**
 * @brief Appends a "Special thanks:" section to the info view.
 *
 * Inserts a bold "Special thanks:" header followed by the indented entries.
 * Does nothing if @a thanks is NULL.
 *
 * @param thanks A NULL-terminated array of acknowledgement strings.
 *
 * @see AddText()
 */
void
BAboutWindow::AddSpecialThanks(const char** thanks)
{
	if (thanks == NULL)
		return;

	const char* specialThanks = B_TRANSLATE_MARK("Special thanks:");
	specialThanks = gSystemCatalog.GetString(specialThanks, "AboutWindow");

	AddText(specialThanks, thanks);
}


/**
 * @brief Appends a "Version history:" section to the info view.
 *
 * Inserts a bold "Version history:" header followed by the indented history
 * entries. Does nothing if @a history is NULL.
 *
 * @param history A NULL-terminated array of version history strings.
 *
 * @see AddText()
 */
void
BAboutWindow::AddVersionHistory(const char** history)
{
	if (history == NULL)
		return;

	const char* versionHistory = B_TRANSLATE_MARK("Version history:");
	versionHistory = gSystemCatalog.GetString(versionHistory, "AboutWindow");

	AddText(versionHistory, history);
}


/**
 * @brief Appends arbitrary extra text to the info view.
 *
 * The string is passed through the system catalog for localization and then
 * appended to the scrollable info area, preceded by a blank line if the view
 * already contains text. Does nothing if @a extraInfo is NULL.
 *
 * @param extraInfo The supplementary text to append.
 */
void
BAboutWindow::AddExtraInfo(const char* extraInfo)
{
	if (extraInfo == NULL)
		return;

	const char* appExtraInfo = B_TRANSLATE_MARK(extraInfo);
	appExtraInfo = gSystemCatalog.GetString(extraInfo, "AboutWindow");

	BString extra("");
	if (fAboutView->InfoView()->TextLength() > 0)
		extra << "\n\n";

	extra << appExtraInfo;

	fAboutView->InfoView()->Insert(extra.String());
}


/**
 * @brief Appends a section with an optional bold header and indented content lines.
 *
 * If the info view already contains text, a blank line is inserted first.
 * When @a header is non-NULL it is rendered in bold; each entry in @a contents
 * (if any) is indented beneath it. When both @a header and @a contents are
 * provided the header is styled with the bold font.
 *
 * @param header   The section header string, or NULL for no header.
 * @param contents A NULL-terminated array of content strings, or NULL for none.
 */
void
BAboutWindow::AddText(const char* header, const char** contents)
{
	BTextView* infoView = fAboutView->InfoView();
	int32 textLength = infoView->TextLength();
	BString text("");

	if (textLength > 0) {
		text << "\n\n";
		textLength += 2;
	}

	const char* indent = "";
	if (header != NULL) {
		indent = "    ";
		text << header;
	}

	if (contents != NULL) {
		text << "\n";
		for (int32 i = 0; contents[i]; i++)
			text << indent << contents[i] << "\n";
	}

	infoView->Insert(text.String());

	if (contents != NULL && header != NULL) {
		infoView->SetFontAndColor(textLength, textLength + strlen(header),
			be_bold_font);
	}
}


/**
 * @brief Returns the application icon currently shown in the stripe view.
 *
 * @return A pointer to the icon BBitmap, or NULL if no icon is set.
 *
 * @see SetIcon()
 */
BBitmap*
BAboutWindow::Icon()
{
	return fAboutView->Icon();
}


/**
 * @brief Sets the application icon displayed in the stripe view.
 *
 * Replaces the auto-loaded icon with @a icon. The internal StripeView takes
 * ownership of the bitmap and deletes the previous one.
 *
 * @param icon The new icon bitmap, or NULL to remove the icon.
 *
 * @see Icon()
 */
void
BAboutWindow::SetIcon(BBitmap* icon)
{
	fAboutView->SetIcon(icon);
}


/**
 * @brief Returns the application name currently displayed in the name label.
 *
 * @return A pointer to the name string; valid for the lifetime of the window.
 *
 * @see SetName()
 */
const char*
BAboutWindow::Name()
{
	return fAboutView->Name();
}


/**
 * @brief Overrides the application name shown in the about window.
 *
 * @param name The new name string to display in the large bold label.
 *
 * @see Name()
 */
void
BAboutWindow::SetName(const char* name)
{
	fAboutView->SetName(name);
}


/**
 * @brief Returns the version string currently displayed below the name label.
 *
 * @return A pointer to the version string; valid for the lifetime of the window.
 *
 * @see SetVersion()
 */
const char*
BAboutWindow::Version()
{
	return fAboutView->Version();
}


/**
 * @brief Overrides the version string shown below the application name.
 *
 * @param version The new version string (e.g. "Version 2.0-beta").
 *
 * @see Version()
 */
void
BAboutWindow::SetVersion(const char* version)
{
	fAboutView->SetVersion(version);
}


// FBC padding

void BAboutWindow::_ReservedAboutWindow20() {}
void BAboutWindow::_ReservedAboutWindow19() {}
void BAboutWindow::_ReservedAboutWindow18() {}
void BAboutWindow::_ReservedAboutWindow17() {}
void BAboutWindow::_ReservedAboutWindow16() {}
void BAboutWindow::_ReservedAboutWindow15() {}
void BAboutWindow::_ReservedAboutWindow14() {}
void BAboutWindow::_ReservedAboutWindow13() {}
void BAboutWindow::_ReservedAboutWindow12() {}
void BAboutWindow::_ReservedAboutWindow11() {}
void BAboutWindow::_ReservedAboutWindow10() {}
void BAboutWindow::_ReservedAboutWindow9() {}
void BAboutWindow::_ReservedAboutWindow8() {}
void BAboutWindow::_ReservedAboutWindow7() {}
void BAboutWindow::_ReservedAboutWindow6() {}
void BAboutWindow::_ReservedAboutWindow5() {}
void BAboutWindow::_ReservedAboutWindow4() {}
void BAboutWindow::_ReservedAboutWindow3() {}
void BAboutWindow::_ReservedAboutWindow2() {}
void BAboutWindow::_ReservedAboutWindow1() {}
