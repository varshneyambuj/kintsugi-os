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
 *   Copyright 2003-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2019, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file DefaultMediaTheme.cpp
 *  @brief Implements DefaultMediaTheme, the built-in BMediaTheme that
 *         generates standard Haiku UI controls (sliders, check boxes, pop-up
 *         menus, text fields) for BParameterWeb parameter graphs.
 */


#include "DefaultMediaTheme.h"

#include <Box.h>
#include <Button.h>
#include <ChannelSlider.h>
#include <CheckBox.h>
#include <GroupView.h>
#include <MediaRoster.h>
#include <MenuField.h>
#include <MessageFilter.h>
#include <OptionPopUp.h>
#include <ParameterWeb.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <Window.h>

#include "MediaDebug.h"


using namespace BPrivate;


namespace BPrivate {

namespace DefaultMediaControls {

/**
 * @brief A thin horizontal or vertical separator line drawn between controls.
 */
class SeparatorView : public BView {
	public:
		/** @brief Construct a separator with the given orientation. @param orientation B_HORIZONTAL or B_VERTICAL. */
		SeparatorView(orientation orientation);
		/** @brief Destructor. */
		virtual ~SeparatorView();

		/** @brief Draw the two-pixel raised/lowered line. @param updateRect The dirty rectangle. */
		virtual void Draw(BRect updateRect);

	private:
		bool	fVertical;
};

/**
 * @brief A centred title label rendered in a slightly tinted colour.
 */
class TitleView : public BView {
	public:
		/** @brief Construct a title view displaying @p title. @param title The text to display. */
		TitleView(const char *title);
		/** @brief Destructor. */
		virtual ~TitleView();

		/** @brief Draw the centred title string. @param updateRect The dirty rectangle. */
		virtual void Draw(BRect updateRect);
		/** @brief Return the preferred dimensions for this view. @param width Receives preferred width. @param height Receives preferred height. */
		virtual void GetPreferredSize(float *width, float *height);

	private:
		BString fTitle;
};

/**
 * @brief BCheckBox subclass that watches a BDiscreteParameter for value changes.
 */
class CheckBox : public BCheckBox {
	public:
		/** @brief Construct a CheckBox bound to @p parameter. @param name View name. @param label Display label. @param parameter The discrete parameter to reflect. */
		CheckBox(const char* name, const char* label,
			BDiscreteParameter &parameter);
		/** @brief Destructor. */
		virtual ~CheckBox();

		/** @brief Start watching the parameter for changes. */
		virtual void AttachedToWindow();
		/** @brief Stop watching the parameter for changes. */
		virtual void DetachedFromWindow();
	private:
		BDiscreteParameter &fParameter;
};

/**
 * @brief BOptionPopUp subclass that watches a BDiscreteParameter for value changes.
 */
class OptionPopUp : public BOptionPopUp {
	public:
		/** @brief Construct an OptionPopUp bound to @p parameter. @param name View name. @param label Display label. @param parameter The discrete parameter to reflect. */
		OptionPopUp(const char* name, const char* label,
			BDiscreteParameter &parameter);
		/** @brief Destructor. */
		virtual ~OptionPopUp();

		/** @brief Start watching the parameter for changes. */
		virtual void AttachedToWindow();
		/** @brief Stop watching the parameter for changes. */
		virtual void DetachedFromWindow();
	private:
		BDiscreteParameter &fParameter;
};

/**
 * @brief BSlider subclass that watches a BContinuousParameter for value changes.
 */
class Slider : public BSlider {
	public:
		/** @brief Construct a Slider bound to @p parameter. @param name View name. @param label Display label. @param minValue Minimum slider value (x1000). @param maxValue Maximum slider value (x1000). @param parameter The continuous parameter to reflect. */
		Slider(const char* name, const char*label, int32 minValue,
			int32 maxValue, BContinuousParameter &parameter);
		/** @brief Destructor. */
		virtual ~Slider();

		/** @brief Start watching the parameter for changes. */
		virtual void AttachedToWindow();
		/** @brief Stop watching the parameter for changes. */
		virtual void DetachedFromWindow();
	private:
		BContinuousParameter &fParameter;
};

/**
 * @brief BChannelSlider subclass that watches a multi-channel BContinuousParameter.
 */
class ChannelSlider : public BChannelSlider {
	public:
		/** @brief Construct a ChannelSlider bound to @p parameter. @param name View name. @param label Display label. @param orientation Slider orientation. @param channels Number of channels. @param parameter The continuous parameter to reflect. */
		ChannelSlider(const char* name, const char* label,
			orientation orientation, int32 channels,
			BContinuousParameter &parameter);
		/** @brief Destructor. */
		virtual ~ChannelSlider();

		/** @brief Start watching the parameter for changes. */
		virtual void AttachedToWindow();
		/** @brief Stop watching the parameter for changes. */
		virtual void DetachedFromWindow();
		/** @brief Update the tooltip with the formatted current value. @param currentValue Current slider value (x1000). */
		virtual void UpdateToolTip(int32 currentValue);
	private:
		BContinuousParameter &fParameter;
};

/**
 * @brief BTextControl subclass that watches a BTextParameter for value changes.
 */
class TextControl : public BTextControl {
	public:
		/** @brief Construct a TextControl bound to @p parameter. @param name View name. @param label Display label. @param parameter The text parameter to reflect. */
		TextControl(const char* name, const char* label,
			BTextParameter &parameter);
		/** @brief Destructor. */
		virtual ~TextControl();

		/** @brief Start watching the parameter for changes. */
		virtual void AttachedToWindow();
		/** @brief Stop watching the parameter for changes. */
		virtual void DetachedFromWindow();
	private:
		BTextParameter &fParameter;
};

/**
 * @brief Abstract base message filter that connects a BControl to a BParameter.
 */
class MessageFilter : public BMessageFilter {
	public:
		/**
		 * @brief Factory method: create the correct filter type for @p parameter.
		 *
		 * @param view       The BControl to attach the filter to.
		 * @param parameter  The parameter to bind to.
		 * @return A newly allocated MessageFilter, or NULL for null parameters.
		 */
		static MessageFilter *FilterFor(BView *view, BParameter &parameter);

	protected:
		/** @brief Construct the base filter accepting any message delivery/source. */
		MessageFilter();
};

/**
 * @brief Message filter for BContinuousParameter controls (sliders).
 */
class ContinuousMessageFilter : public MessageFilter {
	public:
		/**
		 * @brief Construct the filter, set the initial control value, and
		 *        configure modification messages.
		 *
		 * @param control    The slider control to bind.
		 * @param parameter  The continuous parameter to bind to.
		 */
		ContinuousMessageFilter(BControl *control,
			BContinuousParameter &parameter);
		/** @brief Destructor. */
		virtual ~ContinuousMessageFilter();

		/**
		 * @brief Forward control changes to the parameter and parameter changes to the control.
		 *
		 * @param message  Incoming message.
		 * @param target   Message target handler.
		 * @return B_DISPATCH_MESSAGE or B_SKIP_MESSAGE.
		 */
		virtual filter_result Filter(BMessage *message, BHandler **target);

	private:
		/** @brief Refresh the control to reflect the parameter's current value. */
		void _UpdateControl();

		BControl				*fControl;
		BContinuousParameter	&fParameter;
};

/**
 * @brief Message filter for BDiscreteParameter controls (check boxes, pop-ups).
 */
class DiscreteMessageFilter : public MessageFilter {
	public:
		/**
		 * @brief Construct the filter and set the initial control value.
		 *
		 * @param control    The check box or pop-up control to bind.
		 * @param parameter  The discrete parameter to bind to.
		 */
		DiscreteMessageFilter(BControl *control, BDiscreteParameter &parameter);
		/** @brief Destructor. */
		virtual ~DiscreteMessageFilter();

		/**
		 * @brief Forward control changes to the parameter and parameter changes to the control.
		 *
		 * @param message  Incoming message.
		 * @param target   Message target handler.
		 * @return B_DISPATCH_MESSAGE or B_SKIP_MESSAGE.
		 */
		virtual filter_result Filter(BMessage *message, BHandler **target);

	private:
		BDiscreteParameter	&fParameter;
};

/**
 * @brief Message filter for BTextParameter controls (text fields).
 */
class TextMessageFilter : public MessageFilter {
	public:
		/**
		 * @brief Construct the filter and populate the text control with the
		 *        current parameter value.
		 *
		 * @param control    The text control to bind.
		 * @param parameter  The text parameter to bind to.
		 */
		TextMessageFilter(BControl *control, BTextParameter &parameter);
		/** @brief Destructor. */
		virtual ~TextMessageFilter();

		/**
		 * @brief Forward control changes to the parameter and parameter changes to the control.
		 *
		 * @param message  Incoming message.
		 * @param target   Message target handler.
		 * @return B_DISPATCH_MESSAGE or B_SKIP_MESSAGE.
		 */
		virtual filter_result Filter(BMessage *message, BHandler **target);

	private:
		BTextParameter	&fParameter;
};

};

using namespace DefaultMediaControls;

}	// namespace BPrivate


const uint32 kMsgParameterChanged = '_mPC';


/**
 * @brief Return true if @p parameter should not be shown in the UI.
 *
 * Hides B_NULL_PARAMETER/B_WEB_PHYSICAL_INPUT parameters whose only
 * output feeds a B_INPUT_MUX, matching the R5 media theme behaviour.
 *
 * @param parameter  The parameter to test.
 * @return true if the parameter should be hidden, false otherwise.
 */
static bool
parameter_should_be_hidden(BParameter &parameter)
{
	// ToDo: note, this is probably completely stupid, but it's the only
	// way I could safely remove the null parameters that are not shown
	// by the R5 media theme
	if (parameter.Type() != BParameter::B_NULL_PARAMETER
		|| strcmp(parameter.Kind(), B_WEB_PHYSICAL_INPUT))
		return false;

	for (int32 i = 0; i < parameter.CountOutputs(); i++) {
		if (!strcmp(parameter.OutputAt(0)->Kind(), B_INPUT_MUX))
			return true;
	}

	return false;
}


/**
 * @brief Start watching for B_MEDIA_NEW_PARAMETER_VALUE messages for @p parameter.
 *
 * @param control    The BControl that should receive update messages.
 * @param parameter  The parameter to watch.
 */
static void
start_watching_for_parameter_changes(BControl* control, BParameter &parameter)
{
	BMediaRoster* roster = BMediaRoster::CurrentRoster();
	if (roster == NULL)
		return;

	if (roster->StartWatching(control, parameter.Web()->Node(),
			B_MEDIA_NEW_PARAMETER_VALUE) != B_OK) {
		fprintf(stderr, "DefaultMediaTheme: Failed to start watching parameter"
			"\"%s\"\n", parameter.Name());
		return;
	}
}


/**
 * @brief Stop watching for B_MEDIA_NEW_PARAMETER_VALUE messages for @p parameter.
 *
 * @param control    The BControl that was previously registered to watch.
 * @param parameter  The parameter to stop watching.
 */
static void
stop_watching_for_parameter_changes(BControl* control, BParameter &parameter)
{
	BMediaRoster* roster = BMediaRoster::CurrentRoster();
	if (roster == NULL)
		return;

	roster->StopWatching(control, parameter.Web()->Node(),
		B_MEDIA_NEW_PARAMETER_VALUE);
}


//	#pragma mark -


/**
 * @brief Construct a SeparatorView with the given orientation.
 *
 * @param orientation  B_VERTICAL or B_HORIZONTAL.
 */
SeparatorView::SeparatorView(orientation orientation)
	: BView("-", B_WILL_DRAW),
	fVertical(orientation == B_VERTICAL)
{
	if (fVertical) {
		SetExplicitMinSize(BSize(5, 0));
		SetExplicitMaxSize(BSize(5, MaxSize().height));
	} else {
		SetExplicitMinSize(BSize(0, 5));
		SetExplicitMaxSize(BSize(MaxSize().width, 5));
	}
}


/**
 * @brief Destructor.
 */
SeparatorView::~SeparatorView()
{
}


/**
 * @brief Draw the two-pixel separator line.
 *
 * @param updateRect  The dirty region to repaint.
 */
void
SeparatorView::Draw(BRect updateRect)
{
	rgb_color color = ui_color(B_PANEL_BACKGROUND_COLOR);
	BRect rect = updateRect & Bounds();

	SetHighColor(tint_color(color, B_DARKEN_1_TINT));
	if (fVertical)
		StrokeLine(BPoint(0, rect.top), BPoint(0, rect.bottom));
	else
		StrokeLine(BPoint(rect.left, 0), BPoint(rect.right, 0));

	SetHighColor(tint_color(color, B_LIGHTEN_1_TINT));
	if (fVertical)
		StrokeLine(BPoint(1, rect.top), BPoint(1, rect.bottom));
	else
		StrokeLine(BPoint(rect.left, 1), BPoint(rect.right, 1));
}


//	#pragma mark -


/**
 * @brief Construct a TitleView that displays @p title centred.
 *
 * @param title  Text to display as the group title.
 */
TitleView::TitleView(const char *title)
	: BView(title, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fTitle(title)
{
	AdoptSystemColors();
}


/**
 * @brief Destructor.
 */
TitleView::~TitleView()
{
}


/**
 * @brief Draw the centred title string.
 *
 * @param updateRect  The dirty region to repaint.
 */
void
TitleView::Draw(BRect updateRect)
{
	BRect rect(Bounds());
	rect.left = (rect.Width() - StringWidth(fTitle)) / 2;

	SetDrawingMode(B_OP_OVER);
	SetHighColor(mix_color(ui_color(B_PANEL_TEXT_COLOR), make_color(255, 0, 0), 100));
	DrawString(fTitle, BPoint(rect.left, rect.bottom - 9));
}


/**
 * @brief Return the preferred width and height for the title view.
 *
 * @param _width   Receives the preferred width.
 * @param _height  Receives the preferred height.
 */
void
TitleView::GetPreferredSize(float *_width, float *_height)
{
	if (_width)
		*_width = StringWidth(fTitle) + 2;

	if (_height) {
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		*_height = fontHeight.ascent + fontHeight.descent + fontHeight.leading + 8;
	}
}


//	#pragma mark -


/**
 * @brief Construct a CheckBox and bind it to @p parameter.
 *
 * @param name       View name.
 * @param label      Display label.
 * @param parameter  Discrete parameter to control.
 */
CheckBox::CheckBox(const char* name, const char* label,
	BDiscreteParameter &parameter)
	: BCheckBox(name, label, NULL),
	fParameter(parameter)
{
}


/**
 * @brief Destructor.
 */
CheckBox::~CheckBox()
{
}


/**
 * @brief Called when attached to a window; begins watching for parameter changes.
 */
void
CheckBox::AttachedToWindow()
{
	BCheckBox::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


/**
 * @brief Called when detached from a window; stops watching for parameter changes.
 */
void
CheckBox::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);
}


/**
 * @brief Construct an OptionPopUp and bind it to @p parameter.
 *
 * @param name       View name.
 * @param label      Display label.
 * @param parameter  Discrete parameter to control.
 */
OptionPopUp::OptionPopUp(const char* name, const char* label,
	BDiscreteParameter &parameter)
	: BOptionPopUp(name, label, NULL),
	fParameter(parameter)
{
}


/**
 * @brief Destructor.
 */
OptionPopUp::~OptionPopUp()
{
}


/**
 * @brief Called when attached to a window; begins watching for parameter changes.
 */
void
OptionPopUp::AttachedToWindow()
{
	BOptionPopUp::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


/**
 * @brief Called when detached from a window; stops watching for parameter changes.
 */
void
OptionPopUp::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);
}


/**
 * @brief Construct a Slider and bind it to @p parameter.
 *
 * @param name       View name.
 * @param label      Display label.
 * @param minValue   Minimum integer value (parameter value * 1000).
 * @param maxValue   Maximum integer value (parameter value * 1000).
 * @param parameter  Continuous parameter to control.
 */
Slider::Slider(const char* name, const char* label, int32 minValue,
	int32 maxValue, BContinuousParameter &parameter)
	: BSlider(name, label, NULL, minValue, maxValue, B_HORIZONTAL),
	fParameter(parameter)
{
}


/**
 * @brief Destructor.
 */
Slider::~Slider()
{
}


/**
 * @brief Called when attached to a window; begins watching for parameter changes.
 */
void
Slider::AttachedToWindow()
{
	BSlider::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


/**
 * @brief Called when detached from a window; stops watching for parameter changes.
 */
void
Slider::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);
}


/**
 * @brief Construct a ChannelSlider and bind it to @p parameter.
 *
 * @param name       View name.
 * @param label      Display label.
 * @param orientation  B_VERTICAL or B_HORIZONTAL.
 * @param channels   Number of independent channels.
 * @param parameter  Continuous parameter to control.
 */
ChannelSlider::ChannelSlider(const char* name, const char* label,
	orientation orientation, int32 channels, BContinuousParameter &parameter)
	: BChannelSlider(name, label, NULL, orientation, channels),
	fParameter(parameter)
{
}


/**
 * @brief Destructor.
 */
ChannelSlider::~ChannelSlider()
{
}


/**
 * @brief Called when attached to a window; begins watching for parameter changes.
 */
void
ChannelSlider::AttachedToWindow()
{
	BChannelSlider::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


/**
 * @brief Called when detached from a window; stops watching for parameter changes.
 */
void
ChannelSlider::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);

	BChannelSlider::DetachedFromWindow();
}


/**
 * @brief Update the tooltip with a formatted version of the current value.
 *
 * @param currentValue  Current slider value (stored as integer * 1000).
 */
void
ChannelSlider::UpdateToolTip(int32 currentValue)
{
	BString valueString;
	valueString.SetToFormat("%.1f", currentValue / 1000.0);
	SetToolTip(valueString);
}


/**
 * @brief Construct a TextControl and bind it to @p parameter.
 *
 * @param name       View name.
 * @param label      Display label.
 * @param parameter  Text parameter to control.
 */
TextControl::TextControl(const char* name, const char* label,
	BTextParameter &parameter)
	: BTextControl(name, label, "", NULL),
	fParameter(parameter)
{
}


/**
 * @brief Destructor.
 */
TextControl::~TextControl()
{
}


/**
 * @brief Called when attached to a window; begins watching for parameter changes.
 */
void
TextControl::AttachedToWindow()
{
	BTextControl::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


/**
 * @brief Called when detached from a window; stops watching for parameter changes.
 */
void
TextControl::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);
}


//	#pragma mark -


/**
 * @brief Base constructor: accept messages from any delivery/source.
 */
MessageFilter::MessageFilter()
	: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE)
{
}


/**
 * @brief Create the correct filter subclass for @p parameter.
 *
 * @param view       The BControl to attach the filter to.
 * @param parameter  Parameter whose type determines the filter class.
 * @return A new ContinuousMessageFilter, DiscreteMessageFilter, or
 *         TextMessageFilter, or NULL for null parameters or non-control views.
 */
MessageFilter *
MessageFilter::FilterFor(BView *view, BParameter &parameter)
{
	BControl *control = dynamic_cast<BControl *>(view);
	if (control == NULL)
		return NULL;

	switch (parameter.Type()) {
		case BParameter::B_CONTINUOUS_PARAMETER:
			return new ContinuousMessageFilter(control,
				static_cast<BContinuousParameter &>(parameter));

		case BParameter::B_DISCRETE_PARAMETER:
			return new DiscreteMessageFilter(control,
				static_cast<BDiscreteParameter &>(parameter));

		case BParameter::B_TEXT_PARAMETER:
			return new TextMessageFilter(control,
				static_cast<BTextParameter &>(parameter));

		case BParameter::B_NULL_PARAMETER: /* fall through */
		default:
			return NULL;
	}
}


//	#pragma mark -


/**
 * @brief Construct the filter for @p control and @p parameter and set initial value.
 *
 * Installs a kMsgParameterChanged message on the control and calls _UpdateControl()
 * to sync the UI to the current parameter value.
 *
 * @param control    The slider control to bind.
 * @param parameter  The continuous parameter to reflect.
 */
ContinuousMessageFilter::ContinuousMessageFilter(BControl *control,
		BContinuousParameter &parameter)
	: MessageFilter(),
	fControl(control),
	fParameter(parameter)
{
	// initialize view for us
	control->SetMessage(new BMessage(kMsgParameterChanged));

	if (BSlider *slider = dynamic_cast<BSlider *>(fControl))
		slider->SetModificationMessage(new BMessage(kMsgParameterChanged));
	else if (BChannelSlider *slider = dynamic_cast<BChannelSlider *>(fControl))
		slider->SetModificationMessage(new BMessage(kMsgParameterChanged));
	else
		ERROR("ContinuousMessageFilter: unknown continuous parameter view\n");

	// set initial value
	_UpdateControl();
}


/**
 * @brief Destructor.
 */
ContinuousMessageFilter::~ContinuousMessageFilter()
{
}


/**
 * @brief Dispatch kMsgParameterChanged (control -> parameter) and
 *        B_MEDIA_NEW_PARAMETER_VALUE (parameter -> control) messages.
 *
 * @param message  Incoming message.
 * @param target   Message target handler.
 * @return B_DISPATCH_MESSAGE to pass on, B_SKIP_MESSAGE to consume.
 */
filter_result
ContinuousMessageFilter::Filter(BMessage *message, BHandler **target)
{
	if (*target != fControl)
		return B_DISPATCH_MESSAGE;

	if (message->what == kMsgParameterChanged) {
		// update parameter from control
		// TODO: support for response!

		float value[fParameter.CountChannels()];

		if (BSlider *slider = dynamic_cast<BSlider *>(fControl)) {
			value[0] = (float)(slider->Value() / 1000.0);
		} else if (BChannelSlider *slider
				= dynamic_cast<BChannelSlider *>(fControl)) {
			for (int32 i = 0; i < fParameter.CountChannels(); i++)
				value[i] = (float)(slider->ValueFor(i) / 1000.0);
		}

		TRACE("ContinuousMessageFilter::Filter: update view %s, %" B_PRId32
			" channels\n", fControl->Name(), fParameter.CountChannels());

		if (fParameter.SetValue((void *)value, sizeof(value),
				-1) < B_OK) {
			ERROR("ContinuousMessageFilter::Filter: Could not set parameter "
				"value for %p\n", &fParameter);
			return B_DISPATCH_MESSAGE;
		}
		return B_SKIP_MESSAGE;
	}
	if (message->what == B_MEDIA_NEW_PARAMETER_VALUE) {
		// update view from parameter -- if the message concerns us
		const media_node* node;
		int32 parameterID;
		ssize_t size;
		if (message->FindInt32("parameter", &parameterID) != B_OK
			|| fParameter.ID() != parameterID
			|| message->FindData("node", B_RAW_TYPE, (const void**)&node,
					&size) != B_OK
			|| fParameter.Web()->Node() != *node)
			return B_DISPATCH_MESSAGE;

		_UpdateControl();
		return B_SKIP_MESSAGE;
	}

	return B_DISPATCH_MESSAGE;
}


/**
 * @brief Refresh the slider(s) to match the parameter's current value.
 */
void
ContinuousMessageFilter::_UpdateControl()
{
	// TODO: response support!

	float value[fParameter.CountChannels()];
	size_t size = sizeof(value);
	if (fParameter.GetValue((void *)&value, &size, NULL) < B_OK) {
		ERROR("ContinuousMessageFilter: Could not get value for continuous "
			"parameter %p (name '%s', node %d)\n", &fParameter,
			fParameter.Name(), (int)fParameter.Web()->Node().node);
		return;
	}

	if (BSlider *slider = dynamic_cast<BSlider *>(fControl)) {
		slider->SetValue((int32) (1000 * value[0]));
		slider->SetModificationMessage(new BMessage(kMsgParameterChanged));
	} else if (BChannelSlider *slider
			= dynamic_cast<BChannelSlider *>(fControl)) {
		for (int32 i = 0; i < fParameter.CountChannels(); i++) {
			slider->SetValueFor(i, (int32) (1000 * value[i]));
		}
	}
}


//	#pragma mark -


/**
 * @brief Construct the filter for @p control and @p parameter and set initial value.
 *
 * @param control    The check box or pop-up to bind.
 * @param parameter  The discrete parameter to reflect.
 */
DiscreteMessageFilter::DiscreteMessageFilter(BControl *control,
		BDiscreteParameter &parameter)
	: MessageFilter(),
	fParameter(parameter)
{
	// initialize view for us
	control->SetMessage(new BMessage(kMsgParameterChanged));

	// set initial value
	size_t size = sizeof(int32);
	int32 value;
	if (parameter.GetValue((void *)&value, &size, NULL) < B_OK) {
		ERROR("DiscreteMessageFilter: Could not get value for discrete "
			"parameter %p (name '%s', node %d)\n", &parameter,
			parameter.Name(), (int)(parameter.Web()->Node().node));
		return;
	}

	if (BCheckBox *checkBox = dynamic_cast<BCheckBox *>(control)) {
		checkBox->SetValue(value);
	} else if (BOptionPopUp *popUp = dynamic_cast<BOptionPopUp *>(control)) {
		popUp->SelectOptionFor(value);
	} else
		ERROR("DiscreteMessageFilter: unknown discrete parameter view\n");
}


/**
 * @brief Destructor.
 */
DiscreteMessageFilter::~DiscreteMessageFilter()
{
}


/**
 * @brief Dispatch kMsgParameterChanged (control -> parameter) and
 *        B_MEDIA_NEW_PARAMETER_VALUE (parameter -> control) messages.
 *
 * @param message  Incoming message.
 * @param target   Message target handler.
 * @return B_DISPATCH_MESSAGE to pass on, B_SKIP_MESSAGE to consume.
 */
filter_result
DiscreteMessageFilter::Filter(BMessage *message, BHandler **target)
{
	BControl *control;

	if ((control = dynamic_cast<BControl *>(*target)) == NULL)
		return B_DISPATCH_MESSAGE;

	if (message->what == B_MEDIA_NEW_PARAMETER_VALUE) {
		TRACE("DiscreteMessageFilter::Filter: Got a new parameter value\n");
		const media_node* node;
		int32 parameterID;
		ssize_t size;
		if (message->FindInt32("parameter", &parameterID) != B_OK
			|| fParameter.ID() != parameterID
			|| message->FindData("node", B_RAW_TYPE, (const void**)&node,
					&size) != B_OK
			|| fParameter.Web()->Node() != *node)
			return B_DISPATCH_MESSAGE;

		int32 value = 0;
		size_t valueSize = sizeof(int32);
		if (fParameter.GetValue((void*)&value, &valueSize, NULL) < B_OK) {
			ERROR("DiscreteMessageFilter: Could not get value for continuous "
			"parameter %p (name '%s', node %d)\n", &fParameter,
			fParameter.Name(), (int)fParameter.Web()->Node().node);
			return B_SKIP_MESSAGE;
		}
		if (BCheckBox* checkBox = dynamic_cast<BCheckBox*>(control)) {
			checkBox->SetValue(value);
		} else if (BOptionPopUp* popUp = dynamic_cast<BOptionPopUp*>(control)) {
			popUp->SetValue(value);
		}

		return B_SKIP_MESSAGE;
	}

	if (message->what != kMsgParameterChanged)
		return B_DISPATCH_MESSAGE;

	// update view

	int32 value = 0;

	if (BCheckBox *checkBox = dynamic_cast<BCheckBox *>(control)) {
		value = checkBox->Value();
	} else if (BOptionPopUp *popUp = dynamic_cast<BOptionPopUp *>(control)) {
		popUp->SelectedOption(NULL, &value);
	}

	TRACE("DiscreteMessageFilter::Filter: update view %s, value = %"
		B_PRId32 "\n", control->Name(), value);

	if (fParameter.SetValue((void *)&value, sizeof(value), -1) < B_OK) {
		ERROR("DiscreteMessageFilter::Filter: Could not set parameter value for %p\n", &fParameter);
		return B_DISPATCH_MESSAGE;
	}

	return B_SKIP_MESSAGE;
}


//	#pragma mark -


/**
 * @brief Construct the filter for @p control and @p parameter and set initial text.
 *
 * @param control    The text control to bind.
 * @param parameter  The text parameter to reflect.
 */
TextMessageFilter::TextMessageFilter(BControl *control,
		BTextParameter &parameter)
	: MessageFilter(),
	fParameter(parameter)
{
	// initialize view for us
	control->SetMessage(new BMessage(kMsgParameterChanged));

	// set initial value
	if (BTextControl *textControl = dynamic_cast<BTextControl *>(control)) {
		size_t valueSize = parameter.MaxBytes();
		char* value = new char[valueSize + 1];

		if (parameter.GetValue((void *)value, &valueSize, NULL) < B_OK) {
			ERROR("TextMessageFilter: Could not get value for text "
				"parameter %p (name '%s', node %d)\n", &parameter,
				parameter.Name(), (int)(parameter.Web()->Node().node));
		} else {
			textControl->SetText(value);
		}

		delete[] value;
	}

	ERROR("TextMessageFilter: unknown text parameter view\n");
}


/**
 * @brief Destructor.
 */
TextMessageFilter::~TextMessageFilter()
{
}


/**
 * @brief Dispatch kMsgParameterChanged (control -> parameter) and
 *        B_MEDIA_NEW_PARAMETER_VALUE (parameter -> control) messages.
 *
 * @param message  Incoming message.
 * @param target   Message target handler.
 * @return B_DISPATCH_MESSAGE to pass on, B_SKIP_MESSAGE to consume.
 */
filter_result
TextMessageFilter::Filter(BMessage *message, BHandler **target)
{
	BControl *control;

	if ((control = dynamic_cast<BControl *>(*target)) == NULL)
		return B_DISPATCH_MESSAGE;

	if (message->what == B_MEDIA_NEW_PARAMETER_VALUE) {
		TRACE("TextMessageFilter::Filter: Got a new parameter value\n");
		const media_node* node;
		int32 parameterID;
		ssize_t size;
		if (message->FindInt32("parameter", &parameterID) != B_OK
			|| fParameter.ID() != parameterID
			|| message->FindData("node", B_RAW_TYPE, (const void**)&node,
					&size) != B_OK
			|| fParameter.Web()->Node() != *node)
			return B_DISPATCH_MESSAGE;

		if (BTextControl *textControl = dynamic_cast<BTextControl *>(control)) {
			size_t valueSize = fParameter.MaxBytes();
			char* value = new char[valueSize + 1];
			if (fParameter.GetValue((void *)value, &valueSize, NULL) < B_OK) {
				ERROR("TextMessageFilter: Could not get value for text "
					"parameter %p (name '%s', node %d)\n", &fParameter,
					fParameter.Name(), (int)(fParameter.Web()->Node().node));
			} else {
				textControl->SetText(value);
			}

			delete[] value;

			return B_SKIP_MESSAGE;
		}

		return B_DISPATCH_MESSAGE;
	}

	if (message->what != kMsgParameterChanged)
		return B_DISPATCH_MESSAGE;

	// update parameter value

	if (BTextControl *textControl = dynamic_cast<BTextControl *>(control)) {
		BString value = textControl->Text();
		TRACE("TextMessageFilter::Filter: update view %s, value = %s\n",
			control->Name(), value.String());
		if (fParameter.SetValue((void *)value.String(), value.Length() + 1, -1) < B_OK) {
			ERROR("TextMessageFilter::Filter: Could not set parameter value for %p\n", &fParameter);
			return B_DISPATCH_MESSAGE;
		}
	}

	return B_SKIP_MESSAGE;
}


//	#pragma mark -


/**
 * @brief Construct the DefaultMediaTheme with the hard-coded Haiku theme name.
 */
DefaultMediaTheme::DefaultMediaTheme()
	: BMediaTheme("Haiku theme", "Haiku built-in theme version 0.1")
{
	CALLED();
}


/**
 * @brief Create a BControl for @p parameter using MakeViewFor().
 *
 * @param parameter  The parameter to create a control for.
 * @return A BControl appropriate for the parameter type, or NULL.
 */
BControl *
DefaultMediaTheme::MakeControlFor(BParameter *parameter)
{
	CALLED();

	return MakeViewFor(parameter);
}


/**
 * @brief Create a scrollable view hierarchy for the entire parameter web.
 *
 * If the web has more than one group a BTabView is used; otherwise a
 * plain BScrollView is returned. Each group is rendered by MakeViewFor(group).
 *
 * @param web       The parameter web to render.
 * @param hintRect  Optional hint for initial position and size.
 * @return A BView containing all rendered groups, or NULL if @p web is NULL.
 */
BView *
DefaultMediaTheme::MakeViewFor(BParameterWeb *web, const BRect *hintRect)
{
	CALLED();

	if (web == NULL)
		return NULL;

	// do we have more than one attached parameter group?
	// if so, use a tabbed view with a tab for each group

	BTabView *tabView = NULL;
	if (web->CountGroups() > 1)
		tabView = new BTabView("web");

	for (int32 i = 0; i < web->CountGroups(); i++) {
		BParameterGroup *group = web->GroupAt(i);
		if (group == NULL)
			continue;

		BView *groupView = MakeViewFor(*group);
		if (groupView == NULL)
			continue;

		BScrollView *scrollView = new BScrollView(groupView->Name(), groupView, 0,
			true, true, B_NO_BORDER);
		scrollView->SetExplicitMinSize(BSize(B_V_SCROLL_BAR_WIDTH,
			B_H_SCROLL_BAR_HEIGHT));
		if (tabView == NULL) {
			if (hintRect != NULL) {
				scrollView->MoveTo(hintRect->LeftTop());
				scrollView->ResizeTo(hintRect->Size());
			} else {
				scrollView->ResizeTo(600, 400);
					// See comment below.
			}
			return scrollView;
		}
		tabView->AddTab(scrollView);
	}

	if (hintRect != NULL) {
		tabView->MoveTo(hintRect->LeftTop());
		tabView->ResizeTo(hintRect->Size());
	} else {
		// Apps not using layouted views may expect PreferredSize() to return
		// a sane value right away, and use this to set the maximum size of
		// things. Layouted views return their current size until the view has
		// been attached to the window, so in order to prevent breakage, we set
		// a default view size here.
		tabView->ResizeTo(600, 400);
	}
	return tabView;
}


/**
 * @brief Create a horizontal BGroupView containing all parameters and sub-groups.
 *
 * Hidden groups (B_HIDDEN_PARAMETER flag) return NULL. Parameters are stacked
 * vertically on the left; sub-groups are separated by SeparatorViews.
 *
 * @param group  The parameter group to render.
 * @return A BGroupView, or NULL if the group should be hidden or is empty.
 */
BView *
DefaultMediaTheme::MakeViewFor(BParameterGroup& group)
{
	CALLED();

	if (group.Flags() & B_HIDDEN_PARAMETER)
		return NULL;

	BGroupView *view = new BGroupView(group.Name(), B_HORIZONTAL,
		B_USE_HALF_ITEM_SPACING);
	BGroupLayout *layout = view->GroupLayout();
	layout->SetInsets(B_USE_HALF_ITEM_INSETS);

	// Create and add the parameter views
	if (group.CountParameters() > 0) {
		BGroupView *paramView = new BGroupView(group.Name(), B_VERTICAL,
			B_USE_HALF_ITEM_SPACING);
		BGroupLayout *paramLayout = paramView->GroupLayout();
		paramLayout->SetInsets(0);

		for (int32 i = 0; i < group.CountParameters(); i++) {
			BParameter *parameter = group.ParameterAt(i);
			if (parameter == NULL)
				continue;

			BView *parameterView = MakeSelfHostingViewFor(*parameter);
			if (parameterView == NULL)
				continue;

			paramLayout->AddView(parameterView);
		}
		paramLayout->AddItem(BSpaceLayoutItem::CreateHorizontalStrut(10));
		layout->AddView(paramView);
	}

	// Add the sub-group views
	for (int32 i = 0; i < group.CountGroups(); i++) {
		BParameterGroup *subGroup = group.GroupAt(i);
		if (subGroup == NULL)
			continue;

		BView *groupView = MakeViewFor(*subGroup);
		if (groupView == NULL)
			continue;

		if (i > 0)
			layout->AddView(new SeparatorView(B_VERTICAL));

		layout->AddView(groupView);
	}

	layout->AddItem(BSpaceLayoutItem::CreateGlue());
	return view;
}


/*!	This creates a view that handles all incoming messages itself - that's
	what is meant with self-hosting.
*/
/**
 * @brief Create a self-hosting view for @p parameter (view handles its own messages).
 *
 * Calls MakeViewFor() to get a BControl, then installs the appropriate
 * MessageFilter. For null parameters a TitleView or BStringView is returned.
 *
 * @param parameter  The parameter to create a view for.
 * @return A BView with an installed message filter, or a label view for null
 *         parameters, or NULL if the parameter should be hidden.
 */
BView *
DefaultMediaTheme::MakeSelfHostingViewFor(BParameter& parameter)
{
	if (parameter.Flags() & B_HIDDEN_PARAMETER
		|| parameter_should_be_hidden(parameter))
		return NULL;

	BView *view = MakeViewFor(&parameter);
	if (view == NULL) {
		// The MakeViewFor() method above returns a BControl - which we
		// don't need for a null parameter; that's why it returns NULL.
		// But we want to see something anyway, so we add a string view
		// here.
		if (parameter.Type() == BParameter::B_NULL_PARAMETER) {
			if (parameter.Group()->ParameterAt(0) == &parameter) {
				// this is the first parameter in this group, so
				// let's use a nice title view
				return new TitleView(parameter.Name());
			}
			BStringView *stringView = new BStringView(parameter.Name(),
				parameter.Name());
			stringView->SetAlignment(B_ALIGN_CENTER);

			return stringView;
		}

		return NULL;
	}

	MessageFilter *filter = MessageFilter::FilterFor(view, parameter);
	if (filter != NULL)
		view->AddFilter(filter);

	return view;
}


/**
 * @brief Create a BControl appropriate for @p parameter's type and kind.
 *
 * - B_NULL_PARAMETER: returns NULL (handled by MakeSelfHostingViewFor).
 * - B_DISCRETE_PARAMETER with 0 items or B_ENABLE/B_MUTE kind: CheckBox.
 * - B_DISCRETE_PARAMETER otherwise: OptionPopUp with all items added.
 * - B_CONTINUOUS_PARAMETER with B_GAIN/B_MASTER_GAIN kind: ChannelSlider.
 * - B_CONTINUOUS_PARAMETER otherwise: Slider.
 * - B_TEXT_PARAMETER: TextControl.
 *
 * @param parameter  The parameter to create a control for.
 * @return A BControl subclass, or NULL for null parameters or unknown types.
 */
BControl *
DefaultMediaTheme::MakeViewFor(BParameter *parameter)
{
	switch (parameter->Type()) {
		case BParameter::B_NULL_PARAMETER:
			// there is no default view for a null parameter
			return NULL;

		case BParameter::B_DISCRETE_PARAMETER:
		{
			BDiscreteParameter &discrete
				= static_cast<BDiscreteParameter &>(*parameter);

			if (!strcmp(discrete.Kind(), B_ENABLE)
				|| !strcmp(discrete.Kind(), B_MUTE)
				|| discrete.CountItems() == 0) {
				return new CheckBox(discrete.Name(), discrete.Name(), discrete);
			} else {
				BOptionPopUp *popUp = new OptionPopUp(discrete.Name(),
					discrete.Name(), discrete);

				for (int32 i = 0; i < discrete.CountItems(); i++) {
					popUp->AddOption(discrete.ItemNameAt(i),
						discrete.ItemValueAt(i));
				}

				return popUp;
			}
		}

		case BParameter::B_CONTINUOUS_PARAMETER:
		{
			BContinuousParameter &continuous
				= static_cast<BContinuousParameter &>(*parameter);

			if (!strcmp(continuous.Kind(), B_MASTER_GAIN)
				|| !strcmp(continuous.Kind(), B_GAIN)) {
				BChannelSlider *slider = new ChannelSlider(
					continuous.Name(), continuous.Name(), B_VERTICAL,
					continuous.CountChannels(), continuous);

				BString minLabel, maxLabel;
				const char *unit = continuous.Unit();
				if (unit[0]) {
					// if we have a unit, print it next to the limit values
					minLabel.SetToFormat("%g %s", continuous.MinValue(), continuous.Unit());
					maxLabel.SetToFormat("%g %s", continuous.MaxValue(), continuous.Unit());
				} else {
					minLabel.SetToFormat("%g", continuous.MinValue());
					maxLabel.SetToFormat("%g", continuous.MaxValue());
				}
				slider->SetLimitLabels(minLabel, maxLabel);

				// ToDo: take BContinuousParameter::GetResponse() & ValueStep() into account!

				for (int32 i = 0; i < continuous.CountChannels(); i++) {
					slider->SetLimitsFor(i, int32(continuous.MinValue() * 1000),
						int32(continuous.MaxValue() * 1000));
				}

				return slider;
			}

			BSlider *slider = new Slider(parameter->Name(),
				parameter->Name(), int32(continuous.MinValue() * 1000),
				int32(continuous.MaxValue() * 1000), continuous);

			return slider;
		}

		case BParameter::B_TEXT_PARAMETER:
		{
			BTextParameter &text
				= static_cast<BTextParameter &>(*parameter);
			return new TextControl(text.Name(), text.Name(), text);
		}

		default:
			ERROR("BMediaTheme: Don't know parameter type: 0x%x\n",
				parameter->Type());
	}
	return NULL;
}
