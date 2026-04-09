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
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file Controllable.h
 *  @brief Defines BControllable, the mixin for nodes that expose parameter controls.
 */

#ifndef _CONTROLLABLE_H
#define _CONTROLLABLE_H


#include <MediaNode.h>


class BParameterWeb;


/** @brief Mixin base class for media nodes that expose adjustable parameters.
 *
 *  BControllable allows a media node to publish a BParameterWeb describing its
 *  controllable settings (gain, mute, format, etc.) so that control panels and
 *  other nodes can read and modify those values through the Media Kit.
 */
class BControllable : public virtual BMediaNode {
protected:
	virtual						~BControllable();

public:

	/** @brief Returns the parameter web published by this node.
	 *  @return Pointer to the BParameterWeb, or NULL if not yet set.
	 */
			BParameterWeb*		Web();

	/** @brief Acquires the parameter-web lock; must be paired with UnlockParameterWeb().
	 *  @return True if the lock was successfully acquired.
	 */
			bool				LockParameterWeb();

	/** @brief Releases the parameter-web lock acquired by LockParameterWeb(). */
			void				UnlockParameterWeb();

protected:

	/** @brief Default constructor; call SetParameterWeb() from your subclass constructor. */
								BControllable();

	/** @brief Associates a parameter web with this node.
	 *  @param web The BParameterWeb to publish; the node takes ownership.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetParameterWeb(BParameterWeb* web);

	/** @brief Dispatches an incoming port message to the appropriate handler.
	 *  @param message The message code.
	 *  @param data Pointer to the message payload.
	 *  @param size Size of the payload in bytes.
	 *  @return B_OK if handled, or an error code.
	 */
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

	/** @brief Notifies interested parties that a control's definition changed.
	 *  @param id The ID of the control that changed.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			BroadcastChangedParameter(int32 id);

	/** @brief Broadcasts a new value for a parameter to all interested parties.
	 *  @param performanceTime The performance time at which the value takes effect.
	 *  @param parameterID The ID of the changed parameter.
	 *  @param newValue Pointer to the new value data.
	 *  @param valueSize Size of the new value in bytes.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			BroadcastNewParameterValue(
									bigtime_t performanceTime,
									int32 parameterID, void* newValue,
									size_t valueSize);

	/** @brief Called by the Media Kit to read the current value of a parameter.
	 *  @param id The parameter ID to query.
	 *  @param lastChange On return, the performance time of the last value change.
	 *  @param value Buffer that receives the current value.
	 *  @param ioSize In: capacity of value buffer; Out: bytes written.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetParameterValue(int32 id,
									bigtime_t* lastChange,
									void* value, size_t* ioSize) = 0;

	/** @brief Called by the Media Kit to apply a new value to a parameter.
	 *  @param id The parameter ID to change.
	 *  @param when The performance time at which the change should take effect.
	 *  @param value Pointer to the new value data.
	 *  @param size Size of the value in bytes.
	 */
	virtual	void				SetParameterValue(int32 id, bigtime_t when,
									const void* value, size_t size) = 0;

	/** @brief Launches the control panel for this node.
	 *  @param _messenger On return, a BMessenger targeting the control panel.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			StartControlPanel(BMessenger* _messenger);

	/** @brief Applies parameter data from a B_MEDIA_PARAMETERS buffer to this node.
	 *  @param value Pointer to the parameter buffer data.
	 *  @param size Size of the buffer in bytes.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ApplyParameterData(const void* value,
									size_t size);

	/** @brief Builds a B_MEDIA_PARAMETERS-format buffer for a set of controls.
	 *  @param controls Array of control IDs to include.
	 *  @param count Number of entries in the controls array.
	 *  @param buffer Destination buffer for the encoded parameter data.
	 *  @param ioSize In: buffer capacity; Out: bytes written.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			MakeParameterData(const int32* controls,
									int32 count, void* buffer, size_t* ioSize);

	// TODO: Needs a Perform() virtual method!

private:
	// FBC padding and forbidden methods
								BControllable(const BControllable& other);
			BControllable&		operator=(const BControllable& other);

	virtual	status_t				_Reserved_Controllable_0(void*);
	virtual	status_t				_Reserved_Controllable_1(void*);
	virtual	status_t				_Reserved_Controllable_2(void*);
	virtual	status_t				_Reserved_Controllable_3(void*);
	virtual	status_t				_Reserved_Controllable_4(void*);
	virtual	status_t				_Reserved_Controllable_5(void*);
	virtual	status_t				_Reserved_Controllable_6(void*);
	virtual	status_t				_Reserved_Controllable_7(void*);
	virtual	status_t				_Reserved_Controllable_8(void*);
	virtual	status_t				_Reserved_Controllable_9(void*);
	virtual	status_t				_Reserved_Controllable_10(void*);
	virtual	status_t				_Reserved_Controllable_11(void*);
	virtual	status_t				_Reserved_Controllable_12(void*);
	virtual	status_t				_Reserved_Controllable_13(void*);
	virtual	status_t				_Reserved_Controllable_14(void*);
	virtual	status_t				_Reserved_Controllable_15(void*);

private:
			friend class BMediaNode;

			BParameterWeb*		fWeb;
			sem_id				fSem;
			int32				fBen;

			uint32				_reserved_controllable_[14];
};


#endif // _CONTROLLABLE_H

