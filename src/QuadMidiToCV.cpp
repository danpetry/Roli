#include <list>
#include <algorithm>
#include "rtmidi/RtMidi.h"
#include "Roli.hpp"
#include "MidiIO.hpp"
#include "dsp/digital.hpp"
#include "MidiProcessor.h"


struct QuadMIDIToCVInterface : MidiIO, Module {

    
	MidiProcessor<WidgetComposite> midiproc;

	bool pedal = false;

	int mode = REASSIGN;

	int getMode() const;

	void setMode(int mode);

	std::list<int> open;

	SchmittTrigger resetTrigger;

	QuadMIDIToCVInterface() : MidiIO(), Module(midiproc.NUM_PARAMS, midiproc.NUM_INPUTS, midiproc.NUM_OUTPUTS, midiproc.NUM_LIGHTS) {

	}

	~QuadMIDIToCVInterface() {
	};

	void step() override;

	json_t *toJson() override {
		json_t *rootJ = json_object();
		addBaseJson(rootJ);
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		baseFromJson(rootJ);
	}

	void reset() override {
		resetMidi();
	}

	void resetMidi() override;

};


void QuadMIDIToCVInterface::step()
{
    midiproc.step();
}


int QuadMIDIToCVInterface::getMode() const {
	return mode;
}

void QuadMIDIToCVInterface::setMode(int mode) {
	resetMidi();
	QuadMIDIToCVInterface::mode = mode;
}

struct ModeItem : MenuItem {
	int mode;
	QuadMIDIToCVInterface *module;

	void onAction(EventAction &e) {
		module->setMode(mode);
	}
};

struct ModeChoice : ChoiceButton {
	QuadMIDIToCVInterface *module;
	const std::vector<std::string> modeNames = {"ROTATE", "RESET", "REASSIGN"};


	void onAction(EventAction &e) {
		Menu *menu = gScene->createMenu();
		menu->box.pos = getAbsoluteOffset(Vec(0, box.size.y)).round();
		menu->box.size.x = box.size.x;

		for (unsigned long i = 0; i < modeNames.size(); i++) {
			ModeItem *modeItem = new ModeItem();
			modeItem->mode = i;
			modeItem->module = module;
			modeItem->text = modeNames[i];
			menu->pushChild(modeItem);
		}
	}

	void step() {
		text = modeNames[module->getMode()];
	}
};


QuadMidiToCVWidget::QuadMidiToCVWidget() {
	QuadMIDIToCVInterface *module = new QuadMIDIToCVInterface();
	setModule(module);
	box.size = Vec(15 * 16, 380);

	{
		Panel *panel = new LightPanel();
		panel->box.size = box.size;
		addChild(panel);
	}

	float margin = 5;
	float labelHeight = 15;
	float yPos = margin;

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 365)));

	{
		Label *label = new Label();
		label->box.pos = Vec(box.size.x - margin - 12 * 15, margin);
		label->text = "Quad MIDI to CV";
		addChild(label);
		yPos = labelHeight * 2;
	}

	addParam(createParam<LEDButton>(Vec(12 * 15, labelHeight), module, QuadMIDIToCVInterface::RESET_PARAM, 0.0, 1.0,
									0.0));
	addChild(createLight<SmallLight<RedLight>>(Vec(12 * 15 + 5, labelHeight + 5), module, QuadMIDIToCVInterface::RESET_LIGHT));
	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "MIDI Interface";
		addChild(label);
		yPos += labelHeight + margin;

		MidiChoice *midiChoice = new MidiChoice();
		midiChoice->midiModule = dynamic_cast<MidiIO *>(module);
		midiChoice->box.pos = Vec(margin, yPos);
		midiChoice->box.size.x = box.size.x - 10;
		addChild(midiChoice);
		yPos += midiChoice->box.size.y + margin;
	}

	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "Channel";
		addChild(label);
		yPos += labelHeight + margin;

		ChannelChoice *channelChoice = new ChannelChoice();
		channelChoice->midiModule = dynamic_cast<MidiIO *>(module);
		channelChoice->box.pos = Vec(margin, yPos);
		channelChoice->box.size.x = box.size.x - 10;
		addChild(channelChoice);
		yPos += channelChoice->box.size.y + margin;
	}

	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "Mode";
		addChild(label);
		yPos += labelHeight + margin;

		ModeChoice *modeChoice = new ModeChoice();
		modeChoice->module = module;
		modeChoice->box.pos = Vec(margin, yPos);
		modeChoice->box.size.x = box.size.x - 10;
		addChild(modeChoice);
		yPos += modeChoice->box.size.y + margin + 15;
	}

	{
		Label *label = new Label();
		label->box.pos = Vec(84, yPos);
		label->text = "1";
		addChild(label);
	}
	{
		Label *label = new Label();
		label->box.pos = Vec(125, yPos);
		label->text = "2";
		addChild(label);
	}
	{
		Label *label = new Label();
		label->box.pos = Vec(164, yPos);
		label->text = "3";
		addChild(label);
	}
	{
		Label *label = new Label();
		label->box.pos = Vec(203, yPos);
		label->text = "4";
		addChild(label);
	}
	std::string labels[4] = {"1V/oct", "Gate", "Velocity", "Aftertouch"};

	yPos += labelHeight + margin * 2;
	for (int i = 0; i < 4; i++) {
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = labels[i];
		addChild(label);
		addOutput(createOutput<PJ3410Port>(Vec(2 * (40), yPos - 5), module, i * 4));
		addOutput(createOutput<PJ3410Port>(Vec(3 * (40), yPos - 5), module, i * 4 + 1));
		addOutput(createOutput<PJ3410Port>(Vec(4 * (40), yPos - 5), module, i * 4 + 2));
		addOutput(createOutput<PJ3410Port>(Vec(5 * (40), yPos - 5), module, i * 4 + 3));
		yPos += 40;
	}


}

void QuadMidiToCVWidget::step() {

	ModuleWidget::step();
}
