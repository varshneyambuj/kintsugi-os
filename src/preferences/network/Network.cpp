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
 *   Copyright 2004-2015 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Network.cpp
 * @brief Entry point for the Kintsugi OS Network preference application.
 *
 * Hosts a tiny BApplication subclass that creates and shows the main
 * NetworkWindow as soon as the runtime is ready. The Network preflet is
 * the user-facing front end for net_server / net_preflet, exposing
 * interface addressing, DNS, and service configuration.
 *
 * @see NetworkWindow
 */


#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Locale.h>
#include <Window.h>

#include "NetworkWindow.h"


/** @brief MIME signature used to launch the Network preference application. */
static const char* kSignature = "application/x-vnd.Haiku-Network";


/**
 * @brief Minimal BApplication subclass that owns the Network preflet window.
 */
class Application : public BApplication {
public:
								Application();

public:
	virtual	void				ReadyToRun();
};


/**
 * @brief Constructs the application and registers its MIME signature.
 */
Application::Application()
	:
	BApplication(kSignature)
{
}


/**
 * @brief Creates and shows the top-level NetworkWindow once the runtime
 *        is ready to dispatch messages.
 */
void
Application::ReadyToRun()
{
	NetworkWindow* window = new NetworkWindow();
	window->Show();
}


// #pragma mark -


/**
 * @brief Process entry point: instantiates the application, runs the message
 *        loop, and tears the app down on exit.
 *
 * @return Zero on normal termination.
 */
int
main()
{
	Application* app = new Application();
	app->Run();
	delete app;
	return 0;
}
