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
 *   Copyright 2001-2007, Haiku Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Erik Jaesler
 */
#ifndef _APP_DEFS_H
#define _APP_DEFS_H

/**
 * @file AppDefs.h
 * @brief Defines system message codes, scripting command codes, and cursor
 *        constants used throughout the Application Kit.
 */


#include <BeBuild.h>


/** @name Old-Style Cursor Constants
 *  Raw cursor data arrays for legacy cursor support.
 *  @{
 */
extern const unsigned char B_HAND_CURSOR[];   /**< Hand/pointer cursor bitmap data. */
extern const unsigned char B_I_BEAM_CURSOR[]; /**< Text I-beam cursor bitmap data. */
/** @} */

/** @name New-Style Cursor Constants
 *  BCursor object pointers for modern cursor support.
 *  @{
 */
#ifdef  __cplusplus
class BCursor;
extern const BCursor *B_CURSOR_SYSTEM_DEFAULT; /**< The default system pointer cursor. */
extern const BCursor *B_CURSOR_I_BEAM;         /**< The text insertion I-beam cursor. */
#endif
/** @} */


/**
 * @enum system_message_code
 * @brief System-defined message codes delivered by the application server.
 *
 * These codes appear as the @c what field of BMessages delivered to
 * BApplication, BWindow, and BView handlers in response to system events
 * such as user input, window management, and application lifecycle changes.
 */
enum system_message_code {
	B_ABOUT_REQUESTED			= '_ABR', /**< The user selected "About" from the application menu. */
	B_WINDOW_ACTIVATED			= '_ACT', /**< A window has been activated or deactivated. */
	B_APP_ACTIVATED				= '_ACT', /**< The application has been activated or deactivated (same value as B_WINDOW_ACTIVATED). */
	B_ARGV_RECEIVED				= '_ARG', /**< Command-line arguments were received by the application. */
	B_QUIT_REQUESTED			= '_QRQ', /**< The user or system requested the application/window to quit. */
	B_CLOSE_REQUESTED			= '_QRQ', /**< Obsolete; use B_QUIT_REQUESTED instead. */
	B_CANCEL					= '_CNC', /**< A cancel action was triggered (e.g. Escape key in a dialog). */
	B_INVALIDATE				= '_IVL', /**< A view needs to be redrawn. */
	B_KEY_DOWN					= '_KYD', /**< A keyboard key was pressed. */
	B_KEY_UP					= '_KYU', /**< A keyboard key was released. */
	B_UNMAPPED_KEY_DOWN			= '_UKD', /**< A key with no character mapping was pressed. */
	B_UNMAPPED_KEY_UP			= '_UKU', /**< A key with no character mapping was released. */
	B_KEY_MAP_LOADED			= '_KML', /**< A new keyboard map has been loaded. */
	B_LAYOUT_WINDOW				= '_LAY', /**< The window layout needs to be recalculated. */
	B_MODIFIERS_CHANGED			= '_MCH', /**< Modifier keys (Shift, Ctrl, Alt, etc.) changed state. */
	B_MINIMIZE					= '_WMN', /**< The window should be minimized or restored. */
	B_MOUSE_DOWN				= '_MDN', /**< A mouse button was pressed. */
	B_MOUSE_MOVED				= '_MMV', /**< The mouse pointer was moved. */
	B_MOUSE_ENTER_EXIT			= '_MEX', /**< The mouse entered or exited a view. */
	B_MOUSE_IDLE				= '_MSI', /**< The mouse has been idle over a view. */
	B_MOUSE_UP					= '_MUP', /**< A mouse button was released. */
	B_MOUSE_WHEEL_CHANGED		= '_MWC', /**< The mouse scroll wheel was moved. */
	B_OPEN_IN_WORKSPACE			= '_OWS', /**< Open the window in a specific workspace. */
	B_PACKAGE_UPDATE			= '_PKU', /**< A package has been installed, updated, or removed. */
	B_PRINTER_CHANGED			= '_PCH', /**< The default printer has changed. */
	B_PULSE						= '_PUL', /**< Periodic pulse message delivered to views with B_PULSE_NEEDED. */
	B_READY_TO_RUN				= '_RTR', /**< Application initialization is complete; ready to run the event loop. */
	B_REFS_RECEIVED				= '_RRC', /**< File references were dropped on or sent to the application. */
	B_RELEASE_OVERLAY_LOCK		= '_ROV', /**< Release the video overlay lock. */
	B_ACQUIRE_OVERLAY_LOCK		= '_AOV', /**< Acquire the video overlay lock. */
	B_SCREEN_CHANGED			= '_SCH', /**< The screen resolution or color depth has changed. */
	B_VALUE_CHANGED				= '_VCH', /**< A control's value has changed. */
	B_TRANSLATOR_ADDED			= '_ART', /**< A new translator add-on has been installed. */
	B_TRANSLATOR_REMOVED		= '_RRT', /**< A translator add-on has been removed. */
	B_DELETE_TRANSLATOR			= '_DRT', /**< Request to delete a translator. */
	B_VIEW_MOVED				= '_VMV', /**< A view has been moved within its parent. */
	B_VIEW_RESIZED				= '_VRS', /**< A view has been resized. */
	B_WINDOW_MOVED				= '_WMV', /**< A window has been moved on screen. */
	B_WINDOW_RESIZED			= '_WRS', /**< A window has been resized. */
	B_WORKSPACES_CHANGED		= '_WCG', /**< The set of workspaces assigned to a window has changed. */
	B_WORKSPACE_ACTIVATED		= '_WAC', /**< A workspace has been activated or deactivated. */
	B_ZOOM						= '_WZM', /**< The user clicked the window's zoom button. */
	B_COLORS_UPDATED			= '_CLU', /**< System UI colors have been updated. */
	B_FONTS_UPDATED				= '_FNU', /**< System fonts have been updated. */
	B_TRACKER_ADDON_MESSAGE		= '_TAM', /**< Message from a Tracker add-on. */
	_APP_MENU_					= '_AMN', /**< @internal Application menu event. */
	_BROWSER_MENUS_				= '_BRM', /**< @internal Browser menus event. */
	_MENU_EVENT_				= '_MEV', /**< @internal Menu event. */
	_PING_						= '_PBL', /**< @internal Ping/heartbeat message. */
	_QUIT_						= '_QIT', /**< @internal Quit command. */
	_VOLUME_MOUNTED_			= '_NVL', /**< @internal A volume was mounted. */
	_VOLUME_UNMOUNTED_			= '_VRM', /**< @internal A volume was unmounted. */
	_MESSAGE_DROPPED_			= '_MDP', /**< @internal A drag-and-drop message was dropped. */
	_DISPOSE_DRAG_				= '_DPD', /**< @internal Dispose of drag data. */
	_MENUS_DONE_				= '_MND', /**< @internal Menu tracking ended. */
	_SHOW_DRAG_HANDLES_			= '_SDH', /**< @internal Show drag handles. */
	_EVENTS_PENDING_			= '_EVP', /**< @internal Events are pending in the queue. */
	_UPDATE_					= '_UPD', /**< @internal View update request. */
	_UPDATE_IF_NEEDED_			= '_UPN', /**< @internal Conditional view update request. */
	_PRINTER_INFO_				= '_PIN', /**< @internal Printer information request. */
	_SETUP_PRINTER_				= '_SUP', /**< @internal Printer setup request. */
	_SELECT_PRINTER_			= '_PSL'  /**< @internal Printer selection request. */
	// Media Kit reserves all reserved codes starting in '_TR'
};


/**
 * @enum command_code
 * @brief Standard scripting and editing command codes.
 *
 * These codes are used as the @c what field of BMessages for scripting
 * suite operations (property get/set/create/delete), standard editing
 * commands (cut/copy/paste/undo/redo), and other common application
 * messages.
 */
enum command_code {
	B_SET_PROPERTY				= 'PSET', /**< Set a scripting property value. */
	B_GET_PROPERTY				= 'PGET', /**< Get a scripting property value. */
	B_CREATE_PROPERTY			= 'PCRT', /**< Create a new scripting property. */
	B_DELETE_PROPERTY			= 'PDEL', /**< Delete a scripting property. */
	B_COUNT_PROPERTIES			= 'PCNT', /**< Count the number of scripting properties. */
	B_EXECUTE_PROPERTY			= 'PEXE', /**< Execute a scripting property action. */
	B_GET_SUPPORTED_SUITES		= 'SUIT', /**< Query the supported scripting suites. */
	B_UNDO						= 'UNDO', /**< Undo the last editing operation. */
	B_REDO						= 'REDO', /**< Redo a previously undone editing operation. */
	B_CUT						= 'CCUT', /**< Cut the selection to the clipboard. */
	B_COPY						= 'COPY', /**< Copy the selection to the clipboard. */
	B_PASTE						= 'PSTE', /**< Paste from the clipboard. */
	B_SELECT_ALL				= 'SALL', /**< Select all content. */
	B_SAVE_REQUESTED			= 'SAVE', /**< The user requested a file save operation. */
	B_MESSAGE_NOT_UNDERSTOOD	= 'MNOT', /**< The recipient did not understand the message. */
	B_NO_REPLY					= 'NONE', /**< No reply is expected for this message. */
	B_REPLY						= 'RPLY', /**< A reply to a previously sent message. */
	B_SIMPLE_DATA				= 'DATA', /**< Simple drag-and-drop data. */
	B_MIME_DATA					= 'MIME', /**< MIME-typed drag-and-drop data. */
	B_ARCHIVED_OBJECT			= 'ARCV', /**< An archived BArchivable object. */
	B_UPDATE_STATUS_BAR			= 'SBUP', /**< Update the status bar display. */
	B_RESET_STATUS_BAR			= 'SBRS', /**< Reset the status bar to its default state. */
	B_NODE_MONITOR				= 'NDMN', /**< Node monitor notification. */
	B_QUERY_UPDATE				= 'QUPD', /**< A live query result has changed. */
	B_ENDORSABLE				= 'ENDO', /**< Message endorsement marker. */
	B_COPY_TARGET				= 'DDCP', /**< Drag-and-drop copy target negotiation. */
	B_MOVE_TARGET				= 'DDMV', /**< Drag-and-drop move target negotiation. */
	B_TRASH_TARGET				= 'DDRM', /**< Drag-and-drop trash target negotiation. */
	B_LINK_TARGET				= 'DDLN', /**< Drag-and-drop link target negotiation. */
	B_INPUT_DEVICES_CHANGED		= 'IDCH', /**< Input devices have been added or removed. */
	B_INPUT_METHOD_EVENT		= 'IMEV', /**< An input method event (e.g. IME composition). */
	B_WINDOW_MOVE_TO			= 'WDMT', /**< Move a window to an absolute position. */
	B_WINDOW_MOVE_BY			= 'WDMB', /**< Move a window by a relative offset. */
	B_SILENT_RELAUNCH			= 'AREL', /**< The application was silently relaunched (single-launch mode). */
	B_OBSERVER_NOTICE_CHANGE	= 'NTCH', /**< An observed value has changed (observer pattern). */
	B_CONTROL_INVOKED			= 'CIVK', /**< A control was invoked (e.g. button clicked). */
	B_CONTROL_MODIFIED			= 'CMOD'  /**< A control's value was modified interactively. */

	// Media Kit reserves all reserved codes starting in 'TRI'
};

#endif	// _APP_DEFS_H
