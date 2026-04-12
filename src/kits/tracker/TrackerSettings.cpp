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
 * @file TrackerSettings.cpp
 * @brief Persistent Tracker preferences backed by a flat file.
 *
 * TTrackerState is a process-singleton that lazily loads and saves the
 * Tracker preference file.  TrackerSettings is a thin public facade that
 * forwards every get/set call to the singleton, isolating callers from the
 * internal storage layout.
 *
 * @see Settings, TrackerDefaults, TrackerSettingsWindow
 */


#include "TrackerSettings.h"

#include <Debug.h>

#include "Tracker.h"
#include "TrackerDefaults.h"
#include "WidgetAttributeText.h"


#define RGBTOHEX(c) ((c.alpha << 24) | (c.red << 16) | (c.green << 8) | (c.blue))


class TTrackerState : public Settings {
public:
	static TTrackerState* Get();
	void Release();

	void LoadSettingsIfNeeded();
	void SaveSettings(bool onlyIfNonDefault = true);

	TTrackerState();
	~TTrackerState();

private:
	friend class BPrivate::TrackerSettings;

	static void InitIfNeeded();
	TTrackerState(const TTrackerState&);

	BooleanValueSetting* fShowDisksIcon;
	BooleanValueSetting* fMountVolumesOntoDesktop;
	BooleanValueSetting* fDesktopFilePanelRoot;
	BooleanValueSetting* fMountSharedVolumesOntoDesktop;
	BooleanValueSetting* fEjectWhenUnmounting;

	BooleanValueSetting* fShowFullPathInTitleBar;
	BooleanValueSetting* fSingleWindowBrowse;
	BooleanValueSetting* fShowNavigator;
	BooleanValueSetting* fShowSelectionWhenInactive;
	BooleanValueSetting* fTransparentSelection;
	BooleanValueSetting* fSortFolderNamesFirst;
	BooleanValueSetting* fHideDotFiles;
	BooleanValueSetting* fTypeAheadFiltering;
	BooleanValueSetting* fGenerateImageThumbnails;

	ScalarValueSetting* fRecentApplicationsCount;
	ScalarValueSetting* fRecentDocumentsCount;
	ScalarValueSetting* fRecentFoldersCount;

	BooleanValueSetting* fShowVolumeSpaceBar;
	HexScalarValueSetting* fUsedSpaceColor;
	HexScalarValueSetting* fFreeSpaceColor;
	HexScalarValueSetting* fWarningSpaceColor;

	Benaphore fInitLock;
	bool fInited;
	bool fSettingsLoaded;

	int32 fUseCounter;

	typedef Settings _inherited;
};


static TTrackerState gTrackerState;


/**
 * @brief Convert a packed 32-bit ARGB integer to an rgb_color.
 *
 * @param value  Packed colour as (alpha << 24 | red << 16 | green << 8 | blue).
 * @return The equivalent rgb_color.
 */
rgb_color ValueToColor(int32 value)
{
	rgb_color color;
	color.alpha = static_cast<uchar>((value >> 24L) & 0xff);
	color.red = static_cast<uchar>((value >> 16L) & 0xff);
	color.green = static_cast<uchar>((value >> 8L) & 0xff);
	color.blue = static_cast<uchar>(value & 0xff);

	return color;
}


/**
 * @brief Pack an rgb_color into a 32-bit ARGB integer.
 *
 * @param color  The colour to pack.
 * @return Packed 32-bit integer (alpha << 24 | red << 16 | green << 8 | blue).
 */
int32 ColorToValue(rgb_color color)
{
	return color.alpha << 24L | color.red << 16L | color.green << 8L
		| color.blue;
}


//	#pragma mark - TTrackerState


/**
 * @brief Construct the TTrackerState singleton with all setting pointers nulled.
 */
TTrackerState::TTrackerState()
	:
	Settings("TrackerSettings", "Tracker"),
	fShowDisksIcon(NULL),
	fMountVolumesOntoDesktop(NULL),
	fDesktopFilePanelRoot(NULL),
	fMountSharedVolumesOntoDesktop(NULL),
	fEjectWhenUnmounting(NULL),
	fShowFullPathInTitleBar(NULL),
	fSingleWindowBrowse(NULL),
	fShowNavigator(NULL),
	fShowSelectionWhenInactive(NULL),
	fTransparentSelection(NULL),
	fSortFolderNamesFirst(NULL),
	fHideDotFiles(NULL),
	fTypeAheadFiltering(NULL),
	fGenerateImageThumbnails(NULL),
	fRecentApplicationsCount(NULL),
	fRecentDocumentsCount(NULL),
	fRecentFoldersCount(NULL),
	fShowVolumeSpaceBar(NULL),
	fUsedSpaceColor(NULL),
	fFreeSpaceColor(NULL),
	fWarningSpaceColor(NULL),
	fInited(false),
	fSettingsLoaded(false)
{
}


/**
 * @brief Private copy constructor that intentionally asserts to prevent copying.
 */
TTrackerState::TTrackerState(const TTrackerState&)
	:
	Settings("", ""),
	fShowDisksIcon(NULL),
	fMountVolumesOntoDesktop(NULL),
	fDesktopFilePanelRoot(NULL),
	fMountSharedVolumesOntoDesktop(NULL),
	fEjectWhenUnmounting(NULL),
	fShowFullPathInTitleBar(NULL),
	fSingleWindowBrowse(NULL),
	fShowNavigator(NULL),
	fShowSelectionWhenInactive(NULL),
	fTransparentSelection(NULL),
	fSortFolderNamesFirst(NULL),
	fHideDotFiles(NULL),
	fTypeAheadFiltering(NULL),
	fGenerateImageThumbnails(NULL),
	fRecentApplicationsCount(NULL),
	fRecentDocumentsCount(NULL),
	fRecentFoldersCount(NULL),
	fShowVolumeSpaceBar(NULL),
	fUsedSpaceColor(NULL),
	fFreeSpaceColor(NULL),
	fWarningSpaceColor(NULL),
	fInited(false),
	fSettingsLoaded(false)
{
	// Placeholder copy constructor to prevent others from accidentally using
	// the default copy constructor.  Note, the DEBUGGER call is for the off
	// chance that a TTrackerState method (or friend) tries to make a copy.
	DEBUGGER("Don't make a copy of this!");
}


/**
 * @brief Destructor.
 */
TTrackerState::~TTrackerState()
{
}


/**
 * @brief Persist the current settings to disk if they have been loaded.
 *
 * @param onlyIfNonDefault  If true, skip writing settings that still have
 *                          their default values.
 */
void
TTrackerState::SaveSettings(bool onlyIfNonDefault)
{
	if (fSettingsLoaded)
		_inherited::SaveSettings(onlyIfNonDefault);
}


/**
 * @brief Load preferences from disk if not already loaded.
 *
 * Registers all setting objects with their defaults, reads the settings file,
 * and propagates the folder-sort preference to NameAttributeText.
 */
void
TTrackerState::LoadSettingsIfNeeded()
{
	if (fSettingsLoaded)
		return;

	// Set default settings before reading from disk

	Add(fShowDisksIcon = new BooleanValueSetting("ShowDisksIcon", kDefaultShowDisksIcon));
	Add(fMountVolumesOntoDesktop
		= new BooleanValueSetting("MountVolumesOntoDesktop", kDefaultMountVolumesOntoDesktop));
	Add(fMountSharedVolumesOntoDesktop = new BooleanValueSetting(
		"MountSharedVolumesOntoDesktop", kDefaultMountSharedVolumesOntoDesktop));
	Add(fEjectWhenUnmounting
		= new BooleanValueSetting("EjectWhenUnmounting", kDefaultEjectWhenUnmounting));

	Add(fDesktopFilePanelRoot // deprecated
		= new BooleanValueSetting("DesktopFilePanelRoot", kDefaultDesktopFilePanelRoot));
	Add(fShowSelectionWhenInactive // deprecated
		= new BooleanValueSetting("ShowSelectionWhenInactive", kDefaultShowSelectionWhenInactive));

	Add(fShowFullPathInTitleBar
		= new BooleanValueSetting("ShowFullPathInTitleBar", kDefaultShowFullPathInTitleBar));
	Add(fSingleWindowBrowse
		= new BooleanValueSetting("SingleWindowBrowse", kDefaultSingleWindowBrowse));
	Add(fShowNavigator = new BooleanValueSetting("ShowNavigator", kDefaultShowNavigator));
	Add(fTransparentSelection
		= new BooleanValueSetting("TransparentSelection", kDefaultTransparentSelection));
	Add(fSortFolderNamesFirst
		= new BooleanValueSetting("SortFolderNamesFirst", kDefaultSortFolderNamesFirst));
	Add(fHideDotFiles = new BooleanValueSetting("HideDotFiles", kDefaultHideDotFiles));
	Add(fTypeAheadFiltering
		= new BooleanValueSetting("TypeAheadFiltering", kDefaultTypeAheadFiltering));
	Add(fGenerateImageThumbnails
		= new BooleanValueSetting("GenerateImageThumbnails", kDefaultGenerateImageThumbnails));

	Add(fRecentApplicationsCount
		= new ScalarValueSetting("RecentApplications", kDefaultRecentApplications, "", ""));
	Add(fRecentDocumentsCount
		= new ScalarValueSetting("RecentDocuments", kDefaultRecentDocuments, "", ""));
	Add(fRecentFoldersCount
		= new ScalarValueSetting("RecentFolders", kDefaultRecentFolders, "", ""));

	Add(fShowVolumeSpaceBar
		= new BooleanValueSetting("ShowVolumeSpaceBar", kDefaultShowVolumeSpaceBar));
	Add(fUsedSpaceColor
		= new HexScalarValueSetting("UsedSpaceColor", RGBTOHEX(kDefaultUsedSpaceColor), "", ""));
	Add(fFreeSpaceColor
		= new HexScalarValueSetting("FreeSpaceColor", RGBTOHEX(kDefaultFreeSpaceColor), "", ""));
	Add(fWarningSpaceColor
		= new HexScalarValueSetting("WarningSpaceColor", RGBTOHEX(kDefaultWarningSpaceColor),
			"", ""));

	TryReadingSettings();

	NameAttributeText::SetSortFolderNamesFirst(
		fSortFolderNamesFirst->Value());
	RealNameAttributeText::SetSortFolderNamesFirst(
		fSortFolderNamesFirst->Value());

	fSettingsLoaded = true;
}


//	#pragma mark - TrackerSettings


/**
 * @brief Construct a TrackerSettings facade and ensure the state is loaded.
 */
TrackerSettings::TrackerSettings()
{
	gTrackerState.LoadSettingsIfNeeded();
}


/**
 * @brief Save settings to disk via the singleton state.
 *
 * @param onlyIfNonDefault  If true, only non-default values are written.
 */
void
TrackerSettings::SaveSettings(bool onlyIfNonDefault)
{
	gTrackerState.SaveSettings(onlyIfNonDefault);
}


/**
 * @brief Return whether the Disks icon is shown on the Desktop.
 *
 * @return true if the Disks icon is visible.
 */
bool
TrackerSettings::ShowDisksIcon()
{
	return gTrackerState.fShowDisksIcon->Value();
}


/**
 * @brief Set whether the Disks icon is shown on the Desktop.
 *
 * Enabling the Disks icon also disables the MountVolumesOntoDesktop option.
 *
 * @param enabled  true to show the Disks icon.
 */
void
TrackerSettings::SetShowDisksIcon(bool enabled)
{
	gTrackerState.fShowDisksIcon->SetValue(enabled);
	gTrackerState.fMountVolumesOntoDesktop->SetValue(!enabled);
}


/**
 * @brief Return whether the Desktop is the root for file panels (deprecated).
 *
 * @return true if the Desktop acts as the file-panel root.
 */
bool
TrackerSettings::DesktopFilePanelRoot()
{
	return gTrackerState.fDesktopFilePanelRoot->Value();
}


/**
 * @brief Set whether the Desktop is the root for file panels (deprecated).
 *
 * @param enabled  true to use the Desktop as the file-panel root.
 */
void
TrackerSettings::SetDesktopFilePanelRoot(bool enabled)
{
	gTrackerState.fDesktopFilePanelRoot->SetValue(enabled);
}


/**
 * @brief Return whether volumes are mounted directly onto the Desktop.
 *
 * @return true if volumes appear as icons on the Desktop.
 */
bool
TrackerSettings::MountVolumesOntoDesktop()
{
	return gTrackerState.fMountVolumesOntoDesktop->Value();
}


/**
 * @brief Set whether volumes are mounted directly onto the Desktop.
 *
 * Enabling this option also disables the ShowDisksIcon option.
 *
 * @param enabled  true to mount volumes onto the Desktop.
 */
void
TrackerSettings::SetMountVolumesOntoDesktop(bool enabled)
{
	gTrackerState.fShowDisksIcon->SetValue(!enabled);
	gTrackerState.fMountVolumesOntoDesktop->SetValue(enabled);
}


/**
 * @brief Return whether shared (network) volumes are mounted onto the Desktop.
 *
 * @return true if shared volumes appear on the Desktop.
 */
bool
TrackerSettings::MountSharedVolumesOntoDesktop()
{
	return gTrackerState.fMountSharedVolumesOntoDesktop->Value();
}


/**
 * @brief Set whether shared (network) volumes are mounted onto the Desktop.
 *
 * @param enabled  true to show shared volumes on the Desktop.
 */
void
TrackerSettings::SetMountSharedVolumesOntoDesktop(bool enabled)
{
	gTrackerState.fMountSharedVolumesOntoDesktop->SetValue(enabled);
}


/**
 * @brief Return whether the volume is ejected when it is unmounted.
 *
 * @return true if ejection occurs on unmount.
 */
bool
TrackerSettings::EjectWhenUnmounting()
{
	return gTrackerState.fEjectWhenUnmounting->Value();
}


/**
 * @brief Set whether the volume is ejected when it is unmounted.
 *
 * @param enabled  true to eject on unmount.
 */
void
TrackerSettings::SetEjectWhenUnmounting(bool enabled)
{
	gTrackerState.fEjectWhenUnmounting->SetValue(enabled);
}


/**
 * @brief Return whether a disk-space bar is drawn on volume icons.
 *
 * @return true if space bars are enabled.
 */
bool
TrackerSettings::ShowVolumeSpaceBar()
{
	return gTrackerState.fShowVolumeSpaceBar->Value();
}


/**
 * @brief Set whether a disk-space bar is drawn on volume icons.
 *
 * @param enabled  true to show volume space bars.
 */
void
TrackerSettings::SetShowVolumeSpaceBar(bool enabled)
{
	gTrackerState.fShowVolumeSpaceBar->SetValue(enabled);
}


/**
 * @brief Return the colour used to indicate used disk space in the space bar.
 *
 * @return The current used-space colour.
 */
rgb_color
TrackerSettings::UsedSpaceColor()
{
	return ValueToColor(gTrackerState.fUsedSpaceColor->Value());
}


/**
 * @brief Set the colour used to indicate used disk space in the space bar.
 *
 * @param color  The desired used-space colour.
 */
void
TrackerSettings::SetUsedSpaceColor(rgb_color color)
{
	gTrackerState.fUsedSpaceColor->ValueChanged(ColorToValue(color));
}


/**
 * @brief Return the colour used to indicate free disk space in the space bar.
 *
 * @return The current free-space colour.
 */
rgb_color
TrackerSettings::FreeSpaceColor()
{
	return ValueToColor(gTrackerState.fFreeSpaceColor->Value());
}


/**
 * @brief Set the colour used to indicate free disk space in the space bar.
 *
 * @param color  The desired free-space colour.
 */
void
TrackerSettings::SetFreeSpaceColor(rgb_color color)
{
	gTrackerState.fFreeSpaceColor->ValueChanged(ColorToValue(color));
}


/**
 * @brief Return the colour used to indicate the low-space warning threshold.
 *
 * @return The current warning-space colour.
 */
rgb_color
TrackerSettings::WarningSpaceColor()
{
	return ValueToColor(gTrackerState.fWarningSpaceColor->Value());
}


/**
 * @brief Set the colour used to indicate the low-space warning threshold.
 *
 * @param color  The desired warning-space colour.
 */
void
TrackerSettings::SetWarningSpaceColor(rgb_color color)
{
	gTrackerState.fWarningSpaceColor->ValueChanged(ColorToValue(color));
}


/**
 * @brief Return whether windows display the full path in their title bars.
 *
 * @return true if the full path is shown.
 */
bool
TrackerSettings::ShowFullPathInTitleBar()
{
	return gTrackerState.fShowFullPathInTitleBar->Value();
}


/**
 * @brief Set whether windows display the full path in their title bars.
 *
 * @param enabled  true to show the full path.
 */
void
TrackerSettings::SetShowFullPathInTitleBar(bool enabled)
{
	gTrackerState.fShowFullPathInTitleBar->SetValue(enabled);
}


/**
 * @brief Return whether folder names sort before file names.
 *
 * @return true if folders are listed first.
 */
bool
TrackerSettings::SortFolderNamesFirst()
{
	return gTrackerState.fSortFolderNamesFirst->Value();
}


/**
 * @brief Set whether folder names sort before file names.
 *
 * Also propagates the change to NameAttributeText and RealNameAttributeText.
 *
 * @param enabled  true to place folders first.
 */
void
TrackerSettings::SetSortFolderNamesFirst(bool enabled)
{
	gTrackerState.fSortFolderNamesFirst->SetValue(enabled);
	NameAttributeText::SetSortFolderNamesFirst(enabled);
	RealNameAttributeText::SetSortFolderNamesFirst(enabled);
}


/**
 * @brief Return whether files and directories starting with '.' are hidden.
 *
 * @return true if dot files are hidden.
 */
bool
TrackerSettings::HideDotFiles()
{
	return gTrackerState.fHideDotFiles->Value();
}


/**
 * @brief Set whether files and directories starting with '.' are hidden.
 *
 * @param hide  true to hide dot files.
 */
void
TrackerSettings::SetHideDotFiles(bool hide)
{
	gTrackerState.fHideDotFiles->SetValue(hide);
}


/**
 * @brief Return whether type-ahead filtering is active in Tracker windows.
 *
 * @return true if type-ahead filtering is enabled.
 */
bool
TrackerSettings::TypeAheadFiltering()
{
	return gTrackerState.fTypeAheadFiltering->Value();
}


/**
 * @brief Set whether type-ahead filtering is active in Tracker windows.
 *
 * @param enabled  true to enable type-ahead filtering.
 */
void
TrackerSettings::SetTypeAheadFiltering(bool enabled)
{
	gTrackerState.fTypeAheadFiltering->SetValue(enabled);
}


/**
 * @brief Return whether image thumbnails are generated and cached.
 *
 * @return true if thumbnail generation is enabled.
 */
bool
TrackerSettings::GenerateImageThumbnails()
{
	return gTrackerState.fGenerateImageThumbnails->Value();
}


/**
 * @brief Set whether image thumbnails are generated and cached.
 *
 * @param enabled  true to enable thumbnail generation.
 */
void
TrackerSettings::SetGenerateImageThumbnails(bool enabled)
{
	gTrackerState.fGenerateImageThumbnails->SetValue(enabled);
}


/**
 * @brief Return whether the selection highlight is shown even when the window is inactive (deprecated).
 *
 * @return true if selections are visible in inactive windows.
 */
bool
TrackerSettings::ShowSelectionWhenInactive()
{
	return gTrackerState.fShowSelectionWhenInactive->Value();
}


/**
 * @brief Set whether the selection highlight is shown in inactive windows (deprecated).
 *
 * @param enabled  true to keep the selection visible when the window loses focus.
 */
void
TrackerSettings::SetShowSelectionWhenInactive(bool enabled)
{
	gTrackerState.fShowSelectionWhenInactive->SetValue(enabled);
}


/**
 * @brief Return whether the selection lasso is drawn with transparency.
 *
 * @return true if selection drawing is transparent.
 */
bool
TrackerSettings::TransparentSelection()
{
	return gTrackerState.fTransparentSelection->Value();
}


/**
 * @brief Set whether the selection lasso is drawn with transparency.
 *
 * @param enabled  true to use transparent selection drawing.
 */
void
TrackerSettings::SetTransparentSelection(bool enabled)
{
	gTrackerState.fTransparentSelection->SetValue(enabled);
}


/**
 * @brief Return whether all Tracker windows share a single browsing window.
 *
 * @return true if single-window browse mode is active.
 */
bool
TrackerSettings::SingleWindowBrowse()
{
	return gTrackerState.fSingleWindowBrowse->Value();
}


/**
 * @brief Set whether all Tracker windows share a single browsing window.
 *
 * @param enabled  true to enable single-window browse mode.
 */
void
TrackerSettings::SetSingleWindowBrowse(bool enabled)
{
	gTrackerState.fSingleWindowBrowse->SetValue(enabled);
}


/**
 * @brief Return whether the path-navigation bar is shown in Tracker windows.
 *
 * @return true if the navigator bar is visible.
 */
bool
TrackerSettings::ShowNavigator()
{
	return gTrackerState.fShowNavigator->Value();
}


/**
 * @brief Set whether the path-navigation bar is shown in Tracker windows.
 *
 * @param enabled  true to display the navigator bar.
 */
void
TrackerSettings::SetShowNavigator(bool enabled)
{
	gTrackerState.fShowNavigator->SetValue(enabled);
}


/**
 * @brief Retrieve the counts of recent applications, documents, and folders.
 *
 * Any of the output pointer arguments may be NULL if that count is not needed.
 *
 * @param applications  Output pointer for the recent-applications count.
 * @param documents     Output pointer for the recent-documents count.
 * @param folders       Output pointer for the recent-folders count.
 */
void
TrackerSettings::RecentCounts(int32* applications, int32* documents,
	int32* folders)
{
	if (applications != NULL)
		*applications = gTrackerState.fRecentApplicationsCount->Value();

	if (documents != NULL)
		*documents = gTrackerState.fRecentDocumentsCount->Value();

	if (folders != NULL)
		*folders = gTrackerState.fRecentFoldersCount->Value();
}


/**
 * @brief Set the maximum number of recent applications shown in the menu.
 *
 * @param count  New maximum count.
 */
void
TrackerSettings::SetRecentApplicationsCount(int32 count)
{
	gTrackerState.fRecentApplicationsCount->ValueChanged(count);
}


/**
 * @brief Set the maximum number of recent documents shown in the menu.
 *
 * @param count  New maximum count.
 */
void
TrackerSettings::SetRecentDocumentsCount(int32 count)
{
	gTrackerState.fRecentDocumentsCount->ValueChanged(count);
}


/**
 * @brief Set the maximum number of recent folders shown in the menu.
 *
 * @param count  New maximum count.
 */
void
TrackerSettings::SetRecentFoldersCount(int32 count)
{
	gTrackerState.fRecentFoldersCount->ValueChanged(count);
}
