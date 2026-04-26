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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2015, Haiku, Inc.
 * Original authors: Axel Dörfler.
 */

/** @file ServiceView.h
    @brief Detail pane that shows a network service's title and description
           and exposes an Enable/Disable button. */

#ifndef SERVICE_VIEW_H
#define SERVICE_VIEW_H


#include <NetworkSettings.h>
#include <GroupView.h>


using namespace BNetworkKit;

class BButton;


/**
 * @brief Detail view for a single network service.
 *
 * Built around a title, a multi-line description, and a single
 * Enable/Disable button that toggles the service via BNetworkSettings.
 * Records the initial enabled state to support Revert().
 */
class ServiceView : public BGroupView {
public:
								ServiceView(const char* name,
									const char* executable, const char* title,
									const char* description,
									BNetworkSettings& settings);
	virtual						~ServiceView();

			bool				IsRevertable() const;
			status_t			Revert();

			void				SettingsUpdated(uint32 which);

	virtual	void				AttachedToWindow();
	virtual void				MessageReceived(BMessage* message);

protected:
	virtual	bool				IsEnabled() const;
	virtual	void				Enable();
	virtual	void				Disable();

private:
			void				_Toggle();
			void				_UpdateEnableButton();

protected:
			const char*			fName;
			const char*			fExecutable;
			BNetworkSettings&	fSettings;
			BButton*			fEnableButton;
			bool				fWasEnabled;
};


#endif // SERVICE_VIEW_H
