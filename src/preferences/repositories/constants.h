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
 * MIT License. Copyright 2017, Haiku Inc.
 * Original authors: Brian Hill.
 */

/** @file constants.h
    @brief Shared constants, message keys and BMessage codes for the
           Repositories preflet. */

#ifndef REPOSITORIES_CONSTANTS_H
#define REPOSITORIES_CONSTANTS_H


#include <Catalog.h>
#include <String.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Constants"

/** @brief Pixels between an alert and the window it is anchored to. */
static const float kAddWindowOffset = 10.0;
/** @brief Pixel offset between successively stacked timeout alerts. */
static const int16 kTimerAlertOffset = 15;
/** @brief Initial timeout, in seconds, before a slow task triggers a
    user prompt. */
static const int16 kTimerTimeoutSeconds = 10;
/** @brief Retry timeout, in seconds, after the user picks "Keep
    trying". */
static const int16 kTimerRetrySeconds = 20;

/** @brief Localized "OK" button label reused across the preflet. */
static const BString kOKLabel = B_TRANSLATE_COMMENT("OK", "Button label");
/** @brief Localized "Cancel" button label. */
static const BString kCancelLabel = B_TRANSLATE_COMMENT("Cancel",
	"Button label");
/** @brief Localized "Remove" button label. */
static const BString kRemoveLabel = B_TRANSLATE_COMMENT("Remove",
	"Button label");
/** @brief Display name used for repositories whose real name is not
    yet known (e.g. just-added URLs). */
static const BString kNewRepoDefaultName = B_TRANSLATE_COMMENT("Unknown",
	"Unknown repository name");


/**
 * @brief Lightweight aggregate describing a repository by name and URL.
 */
typedef struct {
	const char* name;
	const char* url;
} Repository;


// Message keys
#define key_frame "frame"
#define key_name "repo_name"
#define key_url "repo_url"
#define key_text "text"
#define key_details "details"
#define key_rowptr "row_ptr"
#define key_taskptr "task_ptr"
#define key_count "count"
#define key_ID "ID"


// Messages
/** @brief BMessage 'what' codes used by every component of the
    Repositories preflet. */
enum {
	ADD_REPO_WINDOW = 'BHRa',
	ADD_BUTTON_PRESSED,
	CANCEL_BUTTON_PRESSED,
	ADD_REPO_URL,
	ADD_WINDOW_CLOSED,
	REMOVE_REPOS,
	LIST_SELECTION_CHANGED,
	ENABLE_BUTTON_PRESSED,
	DISABLE_BUTTON_PRESSED,
	ITEM_INVOKED,
	DELETE_KEY_PRESSED,
	DO_TASK,
	STATUS_VIEW_COMPLETED_TIMEOUT,
	TASK_STARTED,
	TASK_COMPLETED,
	TASK_COMPLETED_WITH_ERRORS,
	TASK_CANCELED,
	UPDATE_LIST,
	NO_TASKS,
	ENABLE_REPO,
	DISABLE_REPO,
	TASK_TIMEOUT,
	TIMEOUT_ALERT_BUTTON_SELECTION,
	TASK_KILL_REQUEST
};


// Repo row task state
/** @brief Task lifecycle states tracked on each RepoRow. */
enum {
	STATE_NOT_IN_QUEUE = 0,
	STATE_IN_QUEUE_WAITING,
	STATE_IN_QUEUE_RUNNING
};


#endif
