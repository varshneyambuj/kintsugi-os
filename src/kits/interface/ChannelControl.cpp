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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2005-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ChannelControl.cpp
 * @brief Implementation of BChannelControl, an abstract multi-channel value control
 *
 * BChannelControl is the abstract base class for controls that manage multiple
 * independent value channels (e.g., audio mixer channels). It extends BControl
 * with per-channel min/max/value storage and scripting support.
 *
 * @see BChannelSlider, BControl
 */


#include <ChannelControl.h>
#include <PropertyInfo.h>

#include <map>
#include <string>

/** @brief Per-channel label pair storing the min and max limit strings. */
struct limit_label {
	std::string min_label;
	std::string max_label;
};

/** @brief Map type keyed by channel index, holding per-channel limit labels. */
typedef std::map<int32, limit_label> label_map;

/** @brief Scripting property table for BChannelControl. */
static property_info
sPropertyInfo[] = {
	{ "ChannelCount",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, NULL, 0, { B_INT32_TYPE }
	},

	{ "CurrentChannel",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, NULL, 0, { B_INT32_TYPE }
	},

	{ "MaxLimitLabel",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, NULL, 0, { B_STRING_TYPE }
	},

	{ "MinLimitLabel",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, NULL, 0, { B_STRING_TYPE }
	},

	{ 0 }
};


/**
 * @brief Construct a BChannelControl with an explicit frame rectangle.
 *
 * Allocates per-channel min, max, and value arrays. All channel values are
 * initialised to 0 and all channel maxima to 100.
 *
 * @param frame         The view's frame rectangle in its parent's coordinate
 *                      system.
 * @param name          The internal name of the view.
 * @param label         The user-visible label drawn by the control.
 * @param model         The BMessage sent when the control is invoked, or NULL.
 * @param channel_count Number of independent channels to manage.
 * @param resizingMode  Resizing mode flags passed to BControl.
 * @param flags         View flags passed to BControl.
 * @see BControl::BControl()
 */
BChannelControl::BChannelControl(BRect frame, const char* name,
	const char* label, BMessage* model, int32 channel_count,
	uint32 resizingMode, uint32 flags)
	:
	BControl(frame, name, label, model, resizingMode, flags),
	fChannelCount(channel_count),
	fCurrentChannel(0),
	fChannelMin(NULL),
	fChannelMax(NULL),
	fChannelValues(NULL),
	fMultiLabels(NULL),
	fModificationMsg(NULL)
{
	fChannelMin = new int32[channel_count];
	memset(fChannelMin, 0, sizeof(int32) * channel_count);

	fChannelMax = new int32[channel_count];
	for (int32 i = 0; i < channel_count; i++)
		fChannelMax[i] = 100;

	fChannelValues = new int32[channel_count];
	memset(fChannelValues, 0, sizeof(int32) * channel_count);

	fMultiLabels = (void*)new label_map;
}


/**
 * @brief Construct a layout-friendly BChannelControl without an explicit frame.
 *
 * Allocates per-channel min, max, and value arrays. All channel values are
 * initialised to 0 and all channel maxima to 100.
 *
 * @param name         The internal name of the view.
 * @param label        The user-visible label drawn by the control.
 * @param model        The BMessage sent when the control is invoked, or NULL.
 * @param channelCount Number of independent channels to manage.
 * @param flags        View flags passed to BControl.
 * @see BControl::BControl()
 */
BChannelControl::BChannelControl(const char* name, const char* label,
	BMessage* model, int32 channelCount, uint32 flags)
	:
	BControl(name, label, model, flags),
	fChannelCount(channelCount),
	fCurrentChannel(0),
	fChannelMin(NULL),
	fChannelMax(NULL),
	fChannelValues(NULL),
	fMultiLabels(NULL),
	fModificationMsg(NULL)
{
	fChannelMin = new int32[channelCount];
	memset(fChannelMin, 0, sizeof(int32) * channelCount);

	fChannelMax = new int32[channelCount];
	for (int32 i = 0; i < channelCount; i++)
		fChannelMax[i] = 100;

	fChannelValues = new int32[channelCount];
	memset(fChannelValues, 0, sizeof(int32) * channelCount);

	fMultiLabels = (void*)new label_map;
}


/**
 * @brief Unarchive constructor — restore a BChannelControl from a BMessage.
 *
 * Reads the channel count, current channel, per-channel min/max/values, and
 * the global min/max limit labels from the archive. An optional modification
 * message stored under the "_mod_msg" key is also restored.
 *
 * @param archive The BMessage produced by a previous call to Archive().
 * @see Archive()
 * @see Instantiate()
 */
BChannelControl::BChannelControl(BMessage* archive)
	:
	BControl(archive),
	fChannelCount(0),
	fCurrentChannel(0),
	fChannelMin(NULL),
	fChannelMax(NULL),
	fChannelValues(NULL),
	fMultiLabels(NULL),
	fModificationMsg(NULL)
{
	archive->FindInt32("be:_m_channel_count", &fChannelCount);
	archive->FindInt32("be:_m_value_channel", &fCurrentChannel);

	if (fChannelCount > 0) {
		fChannelMin = new int32[fChannelCount];
		memset(fChannelMin, 0, sizeof(int32) * fChannelCount);

		fChannelMax = new int32[fChannelCount];
		for (int32 i = 0; i < fChannelCount; i++)
			fChannelMax[i] = 100;

		fChannelValues = new int32[fChannelCount];
		memset(fChannelValues, 0, sizeof(int32) * fChannelCount);

		for (int32 c = 0; c < fChannelCount; c++) {
			archive->FindInt32("be:_m_channel_min", c, &fChannelMin[c]);
			archive->FindInt32("be:_m_channel_max", c, &fChannelMax[c]);
			archive->FindInt32("be:_m_channel_val", c, &fChannelValues[c]);
		}
	}

	const char* label = NULL;
	if (archive->FindString("be:_m_min_label", &label) == B_OK)
		fMinLabel = label;

	if (archive->FindString("be:_m_max_label", &label) == B_OK)
		fMaxLabel = label;

	BMessage* modificationMessage = new BMessage;
	if (archive->FindMessage("_mod_msg", modificationMessage) == B_OK)
		fModificationMsg = modificationMessage;
	else
		delete modificationMessage;

	fMultiLabels = (void*)new label_map;
}


/**
 * @brief Destroy the BChannelControl and release all owned resources.
 *
 * Frees the per-channel min, max, and value arrays, the modification message,
 * and the per-channel label map.
 */
BChannelControl::~BChannelControl()
{
	delete[] fChannelMin;
	delete[] fChannelMax;
	delete[] fChannelValues;
	delete fModificationMsg;
	delete reinterpret_cast<label_map*>(fMultiLabels);
}


/**
 * @brief Archive the control's state into a BMessage.
 *
 * Stores the channel count, current channel index, global limit labels, and
 * per-channel min/max/value arrays so the control can be fully reconstructed
 * from the archive.
 *
 * @param data  The message to fill with archived data.
 * @param deep  If true, child views are archived as well (passed to BControl).
 * @return B_OK on success, or a negative error code on the first failure.
 * @see BChannelControl(BMessage*)
 */
status_t
BChannelControl::Archive(BMessage* data, bool deep) const
{
	status_t status = BControl::Archive(data, deep);
	if (status == B_OK)
		status = data->AddInt32("be:_m_channel_count", fChannelCount);

	if (status == B_OK)
		status = data->AddInt32("be:_m_value_channel", fCurrentChannel);

	if (status == B_OK)
		status = data->AddString("be:_m_min_label", fMinLabel.String());

	if (status == B_OK)
		status = data->AddString("be:_m_max_label", fMaxLabel.String());

	if (status == B_OK && fChannelValues != NULL
		&& fChannelMax != NULL && fChannelMin != NULL) {
		for (int32 i = 0; i < fChannelCount; i++) {
			status = data->AddInt32("be:_m_channel_min", fChannelMin[i]);
			if (status < B_OK)
				break;

			status = data->AddInt32("be:_m_channel_max", fChannelMax[i]);
			if (status < B_OK)
				break;

			status = data->AddInt32("be:_m_channel_val", fChannelValues[i]);
			if (status < B_OK)
				break;
		}
	}

	return status;
}


/**
 * @brief Hook called when the view's frame is resized.
 *
 * Delegates to BView::FrameResized() so that layout machinery is updated
 * correctly.
 *
 * @param newWidth  New width of the view in pixels.
 * @param newHeight New height of the view in pixels.
 */
void
BChannelControl::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
}


/**
 * @brief Set the font used to render the control's label.
 *
 * @param font The new font to apply.
 * @param mask Bitmask of font attributes to change (see BFont constants).
 */
void
BChannelControl::SetFont(const BFont* font, uint32 mask)
{
	BView::SetFont(font, mask);
}


/**
 * @brief Hook called when the view is attached to a window.
 *
 * Delegates to BControl::AttachedToWindow() so that the control registers
 * itself with the window and receives messages.
 */
void
BChannelControl::AttachedToWindow()
{
	BControl::AttachedToWindow();
}


/**
 * @brief Hook called when the view is detached from its window.
 *
 * Delegates to BControl::DetachedFromWindow().
 */
void
BChannelControl::DetachedFromWindow()
{
	BControl::DetachedFromWindow();
}


/**
 * @brief Resize the view to its preferred dimensions.
 *
 * Delegates to BControl::ResizeToPreferred().
 */
void
BChannelControl::ResizeToPreferred()
{
	BControl::ResizeToPreferred();
}


/**
 * @brief Dispatch an incoming BMessage to the appropriate handler.
 *
 * Delegates to BControl::MessageReceived() for all messages not handled by
 * a subclass.
 *
 * @param message The message to process.
 */
void
BChannelControl::MessageReceived(BMessage* message)
{
	BControl::MessageReceived(message);
}


/**
 * @brief Resolve a scripting specifier to the handler that owns the property.
 *
 * Checks the local property table first; if the property is not recognised,
 * the request is forwarded to BControl.
 *
 * @param message   The scripting message.
 * @param index     Index of the current specifier in the specifier stack.
 * @param specifier The current specifier message.
 * @param what      Specifier type constant.
 * @param property  Name of the property being addressed.
 * @return The BHandler responsible for the property (this or a parent handler).
 */
BHandler*
BChannelControl::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	BHandler* target = this;
	BPropertyInfo propertyInfo(sPropertyInfo);
	if (propertyInfo.FindMatch(message, index, specifier, what, property)
			< B_OK) {
		target = BControl::ResolveSpecifier(message, index, specifier,
			what, property);
	}

	return target;
}


/**
 * @brief Fill a BMessage with the scripting suites supported by this control.
 *
 * Adds the "suite/vnd.Be-channel-control" suite string and its property
 * descriptor list, then delegates to BControl::GetSupportedSuites().
 *
 * @param data The message to populate with suite information.
 * @return B_OK on success, B_BAD_VALUE if @p data is NULL, or a negative error
 *         code if adding suite information fails.
 */
status_t
BChannelControl::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t err = data->AddString("suites", "suite/vnd.Be-channel-control");

	BPropertyInfo propertyInfo(sPropertyInfo);
	if (err == B_OK)
		err = data->AddFlat("messages", &propertyInfo);

	if (err == B_OK)
		return BControl::GetSupportedSuites(data);

	return err;
}


/**
 * @brief Set the message sent when the user interactively moves a channel.
 *
 * The control takes ownership of @p message. The previously stored
 * modification message is deleted.
 *
 * @param message The new modification message, or NULL to disable modification
 *                notifications.
 */
void
BChannelControl::SetModificationMessage(BMessage* message)
{
	delete fModificationMsg;
	fModificationMsg = message;
}


/**
 * @brief Return the current modification message.
 *
 * @return A pointer to the BMessage sent during interactive modification, or
 *         NULL if none has been set.
 * @see SetModificationMessage()
 */
BMessage*
BChannelControl::ModificationMessage() const
{
	return fModificationMsg;
}


/**
 * @brief Invoke the control, appending the current channel index to the message.
 *
 * The "be:current_channel" field is added to the outgoing message before
 * it is delivered via BControl::Invoke().
 *
 * @param message The message to send, or NULL to use the control's own message.
 * @return B_OK on success, or an error code from BControl::Invoke().
 * @see InvokeChannel()
 */
status_t
BChannelControl::Invoke(BMessage* message)
{
	bool notify = false;
	BMessage invokeMessage(InvokeKind(&notify));

	if (message != NULL)
		invokeMessage = *message;
	else if (Message() != NULL)
		invokeMessage = *Message();

	invokeMessage.AddInt32("be:current_channel", fCurrentChannel);

	return BControl::Invoke(&invokeMessage);
}


/**
 * @brief Invoke the control for a range of channels.
 *
 * Adds "be:current_channel", one "be:channel_value" entry per channel in the
 * range, and one "be:channel_changed" boolean per channel. If @p _mask is
 * NULL every channel in the range is marked as changed.
 *
 * @param message      Message to send, or NULL to use the control's own message.
 * @param fromChannel  First channel in the range to report (0-based).
 * @param channelCount Number of channels to include; pass -1 to include all
 *                     channels from @p fromChannel to the last channel.
 * @param _mask        Optional array of booleans indicating which channels
 *                     actually changed. Must have at least @p channelCount
 *                     elements if non-NULL.
 * @return B_OK on success, or an error code from BControl::Invoke().
 * @see Invoke()
 * @see InvokeNotifyChannel()
 */
status_t
BChannelControl::InvokeChannel(BMessage* message, int32 fromChannel,
	int32 channelCount, const bool* _mask)
{
	bool notify = false;
	BMessage invokeMessage(InvokeKind(&notify));

	if (message != NULL)
		invokeMessage = *message;
	else if (Message() != NULL)
		invokeMessage = *Message();

	invokeMessage.AddInt32("be:current_channel", fCurrentChannel);
	if (channelCount < 0)
		channelCount = fChannelCount - fromChannel;

	for (int32 i = 0; i < channelCount; i++) {
		invokeMessage.AddInt32("be:channel_value",
			fChannelValues[fromChannel + i]);
		invokeMessage.AddBool("be:channel_changed", _mask ? _mask[i] : true);
	}

	return BControl::Invoke(&invokeMessage);
}


/**
 * @brief Invoke a ranged channel notification bracketed by BeginInvokeNotify()
 *        and EndInvokeNotify().
 *
 * Wraps InvokeChannel() so that observers receive the correct notification kind.
 *
 * @param message      Message to send, or NULL to use the control's own message.
 * @param kind         The invoke-notify kind (e.g. B_CONTROL_INVOKED).
 * @param fromChannel  First channel in the range (0-based).
 * @param channelCount Number of channels to include; pass -1 for all remaining.
 * @param _mask        Optional per-channel change mask (see InvokeChannel()).
 * @return B_OK on success, or an error code from InvokeChannel().
 * @see InvokeChannel()
 */
status_t
BChannelControl::InvokeNotifyChannel(BMessage* message, uint32 kind,
	int32 fromChannel, int32 channelCount, const bool* _mask)
{
	BeginInvokeNotify(kind);
	status_t status = InvokeChannel(message, fromChannel, channelCount, _mask);
	EndInvokeNotify();

	return status;
}


/**
 * @brief Set the value of the current channel, clamping to its limits.
 *
 * If @p value falls outside [min, max] for the current channel it is clamped
 * to the nearest limit. The underlying BControl value is updated to reflect
 * the new current-channel value.
 *
 * @param value The desired new value for the current channel.
 * @see SetCurrentChannel()
 * @see SetValueFor()
 */
void
BChannelControl::SetValue(int32 value)
{
	// Get real
	if (value > fChannelMax[fCurrentChannel])
		value = fChannelMax[fCurrentChannel];

	if (value < fChannelMin[fCurrentChannel])
		value = fChannelMin[fCurrentChannel];

	if (value != fChannelValues[fCurrentChannel]) {
		StuffValues(fCurrentChannel, 1, &value);
		BControl::SetValue(value);
	}
}


/**
 * @brief Change the active channel reported by CurrentChannel().
 *
 * Switches the focus channel and synchronises the inherited BControl value
 * to the new channel's stored value.
 *
 * @param channel The 0-based index of the channel to activate.
 * @return B_OK on success, or B_BAD_INDEX if @p channel is out of range.
 * @see CurrentChannel()
 */
status_t
BChannelControl::SetCurrentChannel(int32 channel)
{
	if (channel < 0 || channel >= fChannelCount)
		return B_BAD_INDEX;

	if (channel != fCurrentChannel) {
		fCurrentChannel = channel;
		BControl::SetValue(fChannelValues[fCurrentChannel]);
	}

	return B_OK;
}


/**
 * @brief Return the index of the currently active channel.
 *
 * @return 0-based index of the active channel.
 * @see SetCurrentChannel()
 */
int32
BChannelControl::CurrentChannel() const
{
	return fCurrentChannel;
}


/**
 * @brief Return the total number of channels managed by this control.
 *
 * @return The channel count set during construction or by SetChannelCount().
 * @see SetChannelCount()
 */
int32
BChannelControl::CountChannels() const
{
	return fChannelCount;
}


/**
 * @brief Change the number of active channels.
 *
 * If the new count is larger than the current count, the internal arrays are
 * grown and existing values are preserved. Shrinking is recorded in the count
 * but the backing arrays are not trimmed.
 *
 * @param channel_count The desired new channel count. Must be >= 0 and
 *                      < MaxChannelCount().
 * @return B_OK on success, or B_BAD_VALUE if the count is out of range.
 * @see CountChannels()
 * @see MaxChannelCount()
 */
status_t
BChannelControl::SetChannelCount(int32 channel_count)
{
	if (channel_count < 0 || channel_count >= MaxChannelCount())
		return B_BAD_VALUE;

	// TODO: Currently we only grow the buffer. Test what BeOS does
	if (channel_count > fChannelCount) {
		int32* newMin = new int32[channel_count];
		int32* newMax = new int32[channel_count];
		int32* newVal = new int32[channel_count];

		memcpy(newMin, fChannelMin, fChannelCount);
		memcpy(newMax, fChannelMax, fChannelCount);
		memcpy(newVal, fChannelValues, fChannelCount);

		delete[] fChannelMin;
		delete[] fChannelMax;
		delete[] fChannelValues;

		fChannelMin = newMin;
		fChannelMax = newMax;
		fChannelValues = newVal;
	}

	fChannelCount = channel_count;

	return B_OK;
}


/**
 * @brief Return the current value of a single channel.
 *
 * @param channel 0-based index of the channel to query.
 * @return The channel's current value, or -1 if @p channel is invalid.
 * @see GetValue()
 * @see SetValueFor()
 */
int32
BChannelControl::ValueFor(int32 channel) const
{
	int32 value = 0;
	if (GetValue(&value, channel, 1) <= 0)
		return -1;

	return value;
}


/**
 * @brief Copy a range of channel values into a caller-supplied array.
 *
 * @param outValues    Caller-allocated array to receive the values. Must have
 *                     room for at least @p channelCount elements.
 * @param fromChannel  Index of the first channel to read (0-based).
 * @param channelCount Number of consecutive channels to read.
 * @return The number of values actually written into @p outValues.
 * @see ValueFor()
 * @see SetValue(int32, int32, const int32*)
 */
int32
BChannelControl::GetValue(int32* outValues, int32 fromChannel,
	int32 channelCount) const
{
	int32 i = 0;
	for (i = 0; i < channelCount; i++)
		outValues[i] = fChannelValues[fromChannel + i];

	return i;
}


/**
 * @brief Set the value of a single channel.
 *
 * Convenience wrapper around SetValue(int32, int32, const int32*) for a
 * single-channel update.
 *
 * @param channel The 0-based index of the channel to update.
 * @param value   The new value for the channel.
 * @return B_OK on success, or an error code from StuffValues().
 * @see ValueFor()
 * @see SetValue(int32, int32, const int32*)
 */
status_t
BChannelControl::SetValueFor(int32 channel, int32 value)
{
	return SetValue(channel, 1, &value);
}


/**
 * @brief Set values for a contiguous range of channels.
 *
 * Delegates directly to StuffValues() which applies per-channel clamping.
 *
 * @param fromChannel  0-based index of the first channel to update.
 * @param channelCount Number of consecutive channels to update.
 * @param values       Array of new values; must contain at least
 *                     @p channelCount elements.
 * @return B_OK on success, or an error code from StuffValues().
 * @see StuffValues()
 * @see SetValueFor()
 */
status_t
BChannelControl::SetValue(int32 fromChannel, int32 channelCount,
	const int32* values)
{
	return StuffValues(fromChannel, channelCount, values);
}


/**
 * @brief Apply the same value to every channel, clamping to each channel's limits.
 *
 * Each channel receives the value clamped to its own [min, max] range, so
 * channels with different limits may end up with different stored values.
 *
 * @param values The desired value to apply to all channels.
 * @return B_OK on success.
 * @see SetValueFor()
 */
status_t
BChannelControl::SetAllValue(int32 values)
{
	int32* newValues = new int32[fChannelCount];
	for (int32 i = 0; i < fChannelCount; i++) {
		int32 limitedValue = max_c(values, MinLimitList()[i]);
		limitedValue = min_c(limitedValue, MaxLimitList()[i]);

		newValues[i] = limitedValue;
	}

	delete[] fChannelValues;
	fChannelValues = newValues;
	BControl::SetValue(fChannelValues[fCurrentChannel]);

	return B_OK;
}


/**
 * @brief Set the min/max limits for a single channel.
 *
 * Convenience wrapper around SetLimitsFor(int32, int32, const int32*,
 * const int32*) for a single channel.
 *
 * @param channel The 0-based channel index.
 * @param minimum The new minimum value for @p channel.
 * @param maximum The new maximum value for @p channel.
 * @return B_OK on success, or B_BAD_VALUE if minimum > maximum.
 * @see GetLimitsFor(int32, int32*, int32*) const
 */
status_t
BChannelControl::SetLimitsFor(int32 channel, int32 minimum, int32 maximum)
{
	return SetLimitsFor(channel, 1, &minimum, &maximum);
}


/**
 * @brief Retrieve the min/max limits for a single channel.
 *
 * Convenience wrapper around GetLimitsFor(int32, int32, int32*, int32*) const
 * for a single channel.
 *
 * @param channel  The 0-based channel index.
 * @param minimum  Receives the minimum limit for @p channel.
 * @param maximum  Receives the maximum limit for @p channel.
 * @return B_OK on success, or an error code if the arrays are uninitialised.
 * @see SetLimitsFor(int32, int32, int32)
 */
status_t
BChannelControl::GetLimitsFor(int32 channel, int32* minimum,
	int32* maximum) const
{
	return GetLimitsFor(channel, 1, minimum, maximum);
}


/**
 * @brief Set the min/max limits for a contiguous range of channels.
 *
 * Each channel's stored value is clamped to its new range immediately. If
 * @p fromChannel + @p channelCount would exceed the total channel count, the
 * range is silently truncated.
 *
 * @param fromChannel  0-based index of the first channel to configure.
 * @param channelCount Number of consecutive channels to configure.
 * @param minimum      Array of minimum values; must contain at least
 *                     @p channelCount elements.
 * @param maximum      Array of maximum values; must contain at least
 *                     @p channelCount elements.
 * @return B_OK on success, or B_BAD_VALUE if any minimum[i] > maximum[i].
 * @see GetLimitsFor(int32, int32, int32*, int32*) const
 */
status_t
BChannelControl::SetLimitsFor(int32 fromChannel, int32 channelCount,
	const int32* minimum, const int32* maximum)
{
	if (fromChannel + channelCount > CountChannels())
		channelCount = CountChannels() - fromChannel;

	for (int i = 0; i < channelCount; i++) {
		if (minimum[i] > maximum[i])
			return B_BAD_VALUE;

		fChannelMin[fromChannel + i] = minimum[i];
		fChannelMax[fromChannel + i] = maximum[i];
		if (fChannelValues[fromChannel + i] < minimum[i])
			fChannelValues[fromChannel + i] = minimum[i];
		else if (fChannelValues[fromChannel + i] > maximum[i])
			fChannelValues[fromChannel + i] = maximum[i];
	}

	return B_OK;
}


/**
 * @brief Retrieve the min/max limits for a contiguous range of channels.
 *
 * If @p fromChannel + @p channelCount exceeds the total channel count, the
 * range is silently truncated.
 *
 * @param fromChannel  0-based index of the first channel to query.
 * @param channelCount Number of consecutive channels to query.
 * @param minimum      Caller-allocated array; receives the minimum for each
 *                     channel. Must have at least @p channelCount elements.
 * @param maximum      Caller-allocated array; receives the maximum for each
 *                     channel. Must have at least @p channelCount elements.
 * @return B_OK on success, B_BAD_VALUE if either pointer is NULL, or B_ERROR
 *         if the internal arrays have not been allocated.
 * @see SetLimitsFor(int32, int32, const int32*, const int32*)
 */
status_t
BChannelControl::GetLimitsFor(int32 fromChannel, int32 channelCount,
	int32* minimum, int32* maximum) const
{
	if (minimum == NULL || maximum == NULL)
		return B_BAD_VALUE;

	if (fChannelMin == NULL || fChannelMax == NULL)
		return B_ERROR;
	if (fromChannel + channelCount > CountChannels())
		channelCount = CountChannels() - fromChannel;

	for (int i = 0; i < channelCount; i++) {
		minimum[i] = fChannelMin[fromChannel + i];
		maximum[i] = fChannelMax[fromChannel + i];
	}

	return B_OK;
}


/**
 * @brief Apply the same min/max limits to all channels simultaneously.
 *
 * Each channel's stored value is clamped to [minimum, maximum] immediately.
 *
 * @param minimum The new minimum value applied to every channel.
 * @param maximum The new maximum value applied to every channel.
 * @return B_OK on success, or B_BAD_VALUE if minimum > maximum.
 * @see SetLimitsFor()
 * @see GetLimits()
 */
status_t
BChannelControl::SetLimits(int32 minimum, int32 maximum)
{
	if (minimum > maximum)
		return B_BAD_VALUE;

	int32 numChannels = CountChannels();

	for (int32 c = 0; c < numChannels; c++) {
		fChannelMin[c] = minimum;
		fChannelMax[c] = maximum;
		if (fChannelValues[c] < minimum)
			fChannelValues[c] = minimum;
		else if (fChannelValues[c] > maximum)
			fChannelValues[c] = maximum;
	}

	return B_OK;
}


/**
 * @brief Read the min/max limits for every channel into caller-supplied arrays.
 *
 * @param outMinimum Caller-allocated array; receives each channel's minimum.
 *                   Must have at least CountChannels() elements.
 * @param outMaximum Caller-allocated array; receives each channel's maximum.
 *                   Must have at least CountChannels() elements.
 * @return B_OK on success, B_BAD_VALUE if either pointer is NULL, or B_ERROR
 *         if the internal arrays have not been allocated.
 * @see SetLimits()
 */
status_t
BChannelControl::GetLimits(int32* outMinimum, int32* outMaximum) const
{
	if (outMinimum == NULL || outMaximum == NULL)
		return B_BAD_VALUE;

	if (fChannelMin == NULL || fChannelMax == NULL)
		return B_ERROR;

	int32 numChannels = CountChannels();
	for (int32 c = 0; c < numChannels; c++) {
		outMinimum[c] = fChannelMin[c];
		outMaximum[c] = fChannelMax[c];
	}

	return B_OK;
}


/**
 * @brief Set the global min and max limit labels drawn by the control.
 *
 * The labels are shown at the minimum and maximum ends of the control's range
 * indicator. Triggers an Invalidate() so the new labels are rendered
 * immediately.
 *
 * @param minLabel String to display at the minimum end, or NULL to clear.
 * @param maxLabel String to display at the maximum end, or NULL to clear.
 * @return B_OK on success.
 * @see MinLimitLabel()
 * @see MaxLimitLabel()
 */
status_t
BChannelControl::SetLimitLabels(const char* minLabel, const char* maxLabel)
{
	if (minLabel != fMinLabel)
		fMinLabel = minLabel;

	if (maxLabel != fMaxLabel)
		fMaxLabel = maxLabel;

	Invalidate();

	return B_OK;
}


/**
 * @brief Return the global minimum limit label string.
 *
 * @return A pointer to the null-terminated min label string, or an empty
 *         string if none has been set.
 * @see SetLimitLabels()
 */
const char*
BChannelControl::MinLimitLabel() const
{
	return fMinLabel.String();
}


/**
 * @brief Return the global maximum limit label string.
 *
 * @return A pointer to the null-terminated max label string, or an empty
 *         string if none has been set.
 * @see SetLimitLabels()
 */
const char*
BChannelControl::MaxLimitLabel() const
{
	return fMaxLabel.String();
}


/**
 * @brief Set per-channel min/max limit labels for a single channel.
 *
 * Per-channel labels take precedence over the global labels set by
 * SetLimitLabels() when drawing the individual channel.
 *
 * @param channel  0-based index of the channel to configure.
 * @param minLabel Min label string for this channel.
 * @param maxLabel Max label string for this channel.
 * @return B_OK on success.
 * @see SetLimitLabelsFor(int32, int32, const char*, const char*)
 * @see MinLimitLabelFor()
 */
status_t
BChannelControl::SetLimitLabelsFor(int32 channel, const char* minLabel,
	const char* maxLabel)
{
	(*(label_map*)fMultiLabels)[channel].max_label = maxLabel;
	(*(label_map*)fMultiLabels)[channel].min_label = minLabel;
	return B_OK;
}


/**
 * @brief Set per-channel min/max limit labels for a range of channels.
 *
 * Applies the same @p minLabel and @p maxLabel to every channel in the range
 * [@p fromChannel, @p fromChannel + @p channelCount).
 *
 * @param fromChannel  0-based index of the first channel to configure.
 * @param channelCount Number of consecutive channels to configure.
 * @param minLabel     Min label string to apply.
 * @param maxLabel     Max label string to apply.
 * @return B_OK on success.
 * @see SetLimitLabelsFor(int32, const char*, const char*)
 */
status_t
BChannelControl::SetLimitLabelsFor(int32 fromChannel, int32 channelCount,
	const char* minLabel, const char* maxLabel)
{
	for (int32 i = fromChannel; i < fromChannel + channelCount; i++) {
		SetLimitLabelsFor(i, minLabel, maxLabel);
	}
	return B_OK;
}


/**
 * @brief Return the per-channel minimum label for a single channel.
 *
 * @param channel 0-based index of the channel to query.
 * @return The per-channel min label string, or NULL if no per-channel label
 *         has been set for this channel.
 * @see SetLimitLabelsFor(int32, const char*, const char*)
 */
const char*
BChannelControl::MinLimitLabelFor(int32 channel) const
{
	if (fMultiLabels != NULL) {
		label_map::const_iterator iter = ((label_map*)fMultiLabels)->find(channel);
		if (iter != ((label_map*)fMultiLabels)->end())
			return (*iter).second.min_label.c_str();
	}
	return NULL;
}


/**
 * @brief Return the per-channel maximum label for a single channel.
 *
 * @param channel 0-based index of the channel to query.
 * @return The per-channel max label string, or NULL if no per-channel label
 *         has been set for this channel.
 * @see SetLimitLabelsFor(int32, const char*, const char*)
 */
const char*
BChannelControl::MaxLimitLabelFor(int32 channel) const
{
	if (fMultiLabels != NULL) {
		label_map::const_iterator iter = ((label_map*)fMultiLabels)->find(channel);
		if (iter != ((label_map*)fMultiLabels)->end())
			return (*iter).second.max_label.c_str();
	}
	return NULL;
}


/**
 * @brief Store new values into a range of channel slots, clamping to limits.
 *
 * Values that fall outside a channel's [min, max] range are silently ignored;
 * only in-range values are written. If the current channel falls within the
 * updated range, the inherited BControl value is also synchronised.
 *
 * @param fromChannel  0-based index of the first channel to update.
 * @param channelCount Number of consecutive channels to update.
 * @param values       Array of new values; must contain at least
 *                     @p channelCount elements.
 * @return B_OK on success, B_BAD_VALUE if @p values is NULL, or B_BAD_INDEX
 *         if the channel range is invalid.
 * @see SetValue(int32, int32, const int32*)
 */
status_t
BChannelControl::StuffValues(int32 fromChannel, int32 channelCount,
	const int32* values)
{
	if (values == NULL)
		return B_BAD_VALUE;

	if (fromChannel < 0 || fromChannel > fChannelCount
		|| fromChannel + channelCount > fChannelCount) {
		return B_BAD_INDEX;
	}

	for (int32 i = 0; i < channelCount; i++) {
		if (values[i] <= fChannelMax[fromChannel + i]
			&& values[i] >= fChannelMin[fromChannel + i]) {
			fChannelValues[fromChannel + i] = values[i];
		}
	}

	// if the current channel was updated, update also the control value
	if (fCurrentChannel >= fromChannel
		&& fCurrentChannel <= fromChannel + channelCount) {
		BControl::SetValue(fChannelValues[fCurrentChannel]);
	}

	return B_OK;
}


/** @brief Reserved virtual slot 0 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_0(void*, ...) {}
/** @brief Reserved virtual slot 1 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_1(void*, ...) {}
/** @brief Reserved virtual slot 2 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_2(void*, ...) {}
/** @brief Reserved virtual slot 3 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_3(void*, ...) {}
/** @brief Reserved virtual slot 4 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_4(void*, ...) {}
/** @brief Reserved virtual slot 5 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_5(void*, ...) {}
/** @brief Reserved virtual slot 6 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_6(void*, ...) {}
/** @brief Reserved virtual slot 7 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_7(void*, ...) {}
/** @brief Reserved virtual slot 8 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_8(void*, ...) {}
/** @brief Reserved virtual slot 9 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_9(void*, ...) {}
/** @brief Reserved virtual slot 10 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_10(void*, ...) {}
/** @brief Reserved virtual slot 11 for future binary compatibility. */
void BChannelControl::_Reserverd_ChannelControl_11(void*, ...) {}
