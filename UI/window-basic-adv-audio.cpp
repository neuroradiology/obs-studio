#include "window-basic-adv-audio.hpp"
#include "window-basic-main.hpp"
#include "item-widget-helpers.hpp"
#include "adv-audio-control.hpp"
#include "obs-app.hpp"
#include "qt-wrappers.hpp"

#include "ui_OBSAdvAudio.h"

Q_DECLARE_METATYPE(OBSSource);

OBSBasicAdvAudio::OBSBasicAdvAudio(QWidget *parent)
	: QDialog(parent),
	  ui(new Ui::OBSAdvAudio),
	  sourceAddedSignal(obs_get_signal_handler(), "source_audio_activate",
			    OBSSourceAdded, this),
	  sourceRemovedSignal(obs_get_signal_handler(),
			      "source_audio_deactivate", OBSSourceRemoved,
			      this),
	  sourceActivatedSignal(obs_get_signal_handler(), "source_activate",
				OBSSourceActivated, this),
	  sourceDeactivatedSignal(obs_get_signal_handler(), "source_deactivate",
				  OBSSourceRemoved, this),
	  showInactive(false)
{
	ui->setupUi(this);

	VolumeType volType = (VolumeType)config_get_int(
		GetGlobalConfig(), "BasicWindow", "AdvAudioVolumeType");

	if (volType == VolumeType::Percent)
		ui->usePercent->setChecked(true);

	installEventFilter(CreateShortcutFilter());

	/* enum user scene/sources */
	obs_enum_sources(EnumSources, this);

	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setAttribute(Qt::WA_DeleteOnClose, true);
}

OBSBasicAdvAudio::~OBSBasicAdvAudio()
{
	OBSBasic *main = reinterpret_cast<OBSBasic *>(parent());

	for (size_t i = 0; i < controls.size(); ++i)
		delete controls[i];

	main->SaveProject();
}

bool OBSBasicAdvAudio::EnumSources(void *param, obs_source_t *source)
{
	OBSBasicAdvAudio *dialog = reinterpret_cast<OBSBasicAdvAudio *>(param);
	uint32_t flags = obs_source_get_output_flags(source);

	if ((flags & OBS_SOURCE_AUDIO) != 0 &&
	    (dialog->showInactive ||
	     (obs_source_active(source) && obs_source_audio_active(source))))
		dialog->AddAudioSource(source);

	return true;
}

void OBSBasicAdvAudio::OBSSourceAdded(void *param, calldata_t *calldata)
{
	OBSSource source((obs_source_t *)calldata_ptr(calldata, "source"));

	QMetaObject::invokeMethod(reinterpret_cast<OBSBasicAdvAudio *>(param),
				  "SourceAdded", Q_ARG(OBSSource, source));
}

void OBSBasicAdvAudio::OBSSourceRemoved(void *param, calldata_t *calldata)
{
	OBSSource source((obs_source_t *)calldata_ptr(calldata, "source"));

	QMetaObject::invokeMethod(reinterpret_cast<OBSBasicAdvAudio *>(param),
				  "SourceRemoved", Q_ARG(OBSSource, source));
}

void OBSBasicAdvAudio::OBSSourceActivated(void *param, calldata_t *calldata)
{
	OBSSource source((obs_source_t *)calldata_ptr(calldata, "source"));

	if (obs_source_audio_active(source))
		QMetaObject::invokeMethod(
			reinterpret_cast<OBSBasicAdvAudio *>(param),
			"SourceAdded", Q_ARG(OBSSource, source));
}

inline void OBSBasicAdvAudio::AddAudioSource(obs_source_t *source)
{
	for (size_t i = 0; i < controls.size(); i++) {
		if (controls[i]->GetSource() == source)
			return;
	}
	OBSAdvAudioCtrl *control = new OBSAdvAudioCtrl(ui->mainLayout, source);

	InsertQObjectByName(controls, control);

	for (auto control : controls) {
		control->ShowAudioControl(ui->mainLayout);
	}
}

void OBSBasicAdvAudio::SourceAdded(OBSSource source)
{
	uint32_t flags = obs_source_get_output_flags(source);

	if ((flags & OBS_SOURCE_AUDIO) == 0)
		return;

	AddAudioSource(source);
}

void OBSBasicAdvAudio::SourceRemoved(OBSSource source)
{
	uint32_t flags = obs_source_get_output_flags(source);

	if ((flags & OBS_SOURCE_AUDIO) == 0)
		return;

	for (size_t i = 0; i < controls.size(); i++) {
		if (controls[i]->GetSource() == source) {
			delete controls[i];
			controls.erase(controls.begin() + i);
			break;
		}
	}
}

void OBSBasicAdvAudio::on_usePercent_toggled(bool checked)
{
	VolumeType type;

	if (checked)
		type = VolumeType::Percent;
	else
		type = VolumeType::dB;

	for (size_t i = 0; i < controls.size(); i++)
		controls[i]->SetVolumeWidget(type);

	config_set_int(GetGlobalConfig(), "BasicWindow", "AdvAudioVolumeType",
		       (int)type);
}

void OBSBasicAdvAudio::on_activeOnly_toggled(bool checked)
{
	SetShowInactive(!checked);
}

void OBSBasicAdvAudio::SetShowInactive(bool show)
{
	if (showInactive == show)
		return;

	showInactive = show;

	sourceAddedSignal.Disconnect();
	sourceRemovedSignal.Disconnect();
	sourceActivatedSignal.Disconnect();
	sourceDeactivatedSignal.Disconnect();

	if (showInactive) {
		sourceAddedSignal.Connect(obs_get_signal_handler(),
					  "source_create", OBSSourceAdded,
					  this);
		sourceRemovedSignal.Connect(obs_get_signal_handler(),
					    "source_remove", OBSSourceRemoved,
					    this);

		obs_enum_sources(EnumSources, this);

		SetIconsVisible(showVisible);
	} else {
		sourceAddedSignal.Connect(obs_get_signal_handler(),
					  "source_audio_activate",
					  OBSSourceAdded, this);
		sourceRemovedSignal.Connect(obs_get_signal_handler(),
					    "source_audio_deactivate",
					    OBSSourceRemoved, this);
		sourceActivatedSignal.Connect(obs_get_signal_handler(),
					      "source_activate",
					      OBSSourceActivated, this);
		sourceDeactivatedSignal.Connect(obs_get_signal_handler(),
						"source_deactivate",
						OBSSourceRemoved, this);

		for (size_t i = 0; i < controls.size(); i++) {
			const auto source = controls[i]->GetSource();
			if (!(obs_source_active(source) &&
			      obs_source_audio_active(source))) {
				delete controls[i];
				controls.erase(controls.begin() + i);
				i--;
			}
		}
	}
}

void OBSBasicAdvAudio::SetIconsVisible(bool visible)
{
	showVisible = visible;

	QLayoutItem *item = ui->mainLayout->itemAtPosition(0, 0);
	QLabel *headerLabel = qobject_cast<QLabel *>(item->widget());
	visible ? headerLabel->show() : headerLabel->hide();

	for (size_t i = 0; i < controls.size(); i++) {
		controls[i]->SetIconVisible(visible);
	}
}
