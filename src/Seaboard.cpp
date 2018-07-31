#include <list>
#include <algorithm>
#include "rtmidi/RtMidi.h"
#include "Roli.hpp"
#include "MidiIO.hpp"
#include "dsp/digital.hpp"

struct MidiKey {
	int pitch = 60;
	int pressure = 0; // channel pressure
	int onVel = 0; // note on velocity
	int offVel = 0; //note off velocity
	int yAxis = 0; // y axis - up and down the key
	bool gate = false;
	int channel = 0;
};

struct SeaboardInterface : MidiIO, Module {
	enum ParamIds {
		RESET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		PITCH_OUTPUT = 0, // Pitch bend is output as part of 1V/oct output TODO - is this the best way?
		GATE_OUTPUT = 4,
		ON_VELOCITY_OUTPUT = 8,
		OFF_VELOCITY_OUTPUT = 12,
		PRESSURE_OUTPUT = 16,
		Y_OUTPUT = 20,
		NUM_OUTPUTS = 24 // 4 channels x 6 outputs TODO - expand polyphony from 4 to 8
	};
	enum LightIds {
		RESET_LIGHT,
		NUM_LIGHTS
	};

	enum Modes {
		ROTATE,
		RESET,
		REASSIGN
	};

	bool pedal = false;

	int mode = REASSIGN;

	int getMode() const;

	void setMode(int mode);

	MidiKey activeKeys[4];
	std::list<int> open;

	SchmittTrigger resetTrigger;

	SeaboardInterface() : MidiIO(), Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {

	}

	~SeaboardInterface() {
	};

	void step() override;

	void processMidi(std::vector<unsigned char> msg);

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

void SeaboardInterface::resetMidi() {

	for (int i = 0; i < 4; i++) {
		outputs[GATE_OUTPUT + i].value = 0.0;
		activeKeys[i].gate = false;
		activeKeys[i].onVel = 0;
		activeKeys[i].offVel = 0;
		activeKeys[i].pressure = 0;
		activeKeys[i].yAxis = 0;
		activeKeys[i].channel = 0;
	}

	open.clear();

	pedal = false;
	lights[RESET_LIGHT].value = 1.0;
}

void SeaboardInterface::step() {
	if (isPortOpen()) {
		std::vector<unsigned char> message;
		int msgsProcessed = 0;

		// midiIn->getMessage returns empty vector if there are no messages in the queue
		// NOTE: For the quadmidi we will process max 4 midi messages per step to avoid
		// problems with parallel input.
		getMessage(&message);
		while (msgsProcessed < 4 && message.size() > 0) {
			processMidi(message);
			getMessage(&message);
			msgsProcessed++;
		}
	}


	for (int i = 0; i < 4; i++) { //TODO - expand polyphony from 4 to 8
		outputs[GATE_OUTPUT + i].value = activeKeys[i].gate ? 10.0 : 0;
		outputs[PITCH_OUTPUT + i].value = (activeKeys[i].pitch - 60) / 12.0;
		outputs[ON_VELOCITY_OUTPUT + i].value = activeKeys[i].onVel / 127.0 * 10.0;
		outputs[OFF_VELOCITY_OUTPUT + i].value = activeKeys[i].offVel / 127.0 * 10.0;
		outputs[PRESSURE_OUTPUT + i].value = activeKeys[i].pressure / 127.0 * 10.0;
		outputs[Y_OUTPUT + i].value = activeKeys[i].yAxis / 127.0 * 10.0;
	}

	if (resetTrigger.process(params[RESET_PARAM].value)) {
		resetMidi();
		return;
	}

	lights[RESET_LIGHT].value -= lights[RESET_LIGHT].value / 0.55 / engineGetSampleRate(); // fade out light
}


void SeaboardInterface::processMidi(std::vector<unsigned char> msg) {
	int channel = msg[0] & 0xf;
	int status = (msg[0] >> 4) & 0xf;
	int data1 = msg[1];
	int data2 = msg[2];
	bool gate;
	
	printf("Chan: %3d  |  ", channel);
	printf("Stat: %3d  |  ", status);
	printf("Data1: %3d  |  ", data1);
	printf("Data2: %3d  |  ", data2);
	printf("Gate: %3d\n", gate);

	// Filter channels
	if (this->channel >= 0 && this->channel != channel)
		return;

	switch (status) {
		// note off
		case 0x8: {
			gate = false;
		}
			break;
		case 0x9: // note on
			if (data2 > 0) {
				gate = true;
			} else {
				// For some reason, some keyboards send a "note on" event with a velocity of 0 to signal that the key has been released.
				gate = false;
			}
			break;
		case 0xd: // channel aftertouch
			for (int i = 0; i < 4; i++) {
				if (activeKeys[i].channel == channel) activeKeys[i].pressure = data1;
			}
			return;
		case 0xb: // cc
			if (data1 == 0x40) { // pedal
				pedal = (data2 >= 64);
				if (!pedal) {
					open.clear();
					for (int i = 0; i < 4; i++) {
						activeKeys[i].gate = false;
						open.push_back(i);
					}
				}
			}
			if (data1 == 0x4a) { //y axis, cc 74
				for (int i = 0; i < 4; i++) {
					if (activeKeys[i].channel == channel) activeKeys[i].yAxis = data2;
				}

			}
			return;
		default:
			return;
	}

	// All the stuff after this is only handled if it's a note on or off command.

	if (pedal && !gate) {
		return;
	}

	// If it's a note off command for this channel
	if (!gate) {
		for (int i = 0; i < 4; i++) {
			if (activeKeys[i].channel == channel) {
				activeKeys[i].gate = false;
				activeKeys[i].offVel = data2;
				if (std::find(open.begin(), open.end(), i) != open.end()) {
					open.remove(i);
				}
				open.push_front(i);
			}
		}
		return;
	}

	if (open.empty()) {
		for (int i = 0; i < 4; i++) {
			open.push_back(i);
		}
	}

	if (!activeKeys[0].gate && !activeKeys[1].gate &&
		!activeKeys[2].gate && !activeKeys[3].gate) {
		open.sort();
	}


	switch (mode) {
		case RESET:
			if (open.size() >= 4) {
				for (int i = 0; i < 4; i++) {
					activeKeys[i].gate = false;
					open.push_back(i);
				}
			}
			break;
		case REASSIGN:
			open.push_back(open.front());
			break;
		case ROTATE:
			break;
	}

	int next = open.front();
	open.pop_front();

	for (int i = 0; i < 4; i++) {
		if (activeKeys[i].channel == channel && activeKeys[i].gate) {
			activeKeys[i].onVel = data2;
			if (std::find(open.begin(), open.end(), i) != open.end())
				open.remove(i);

			open.push_front(i);
			activeKeys[i].gate = false;
		}
	}

	activeKeys[next].gate = true;
	activeKeys[next].pitch = data1;
	activeKeys[next].onVel = data2;
	activeKeys[next].channel = channel;
}

int SeaboardInterface::getMode() const {
	return mode;
}

void SeaboardInterface::setMode(int mode) {
	resetMidi();
	SeaboardInterface::mode = mode;
}

struct ModeItem : MenuItem {
	int mode;
	SeaboardInterface *module;

	void onAction(EventAction &e) {
		module->setMode(mode);
	}
};

struct ModeChoice : ChoiceButton {
	SeaboardInterface *module;
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


SeaboardWidget::SeaboardWidget() {
	SeaboardInterface *module = new SeaboardInterface();
	setModule(module);
	box.size = Vec(15 * 16, 380);

	{
		Panel *panel = new LightPanel();
		panel->box.size = box.size;
		addChild(panel);
	}

	float margin = 5;
	float labelHeight = 12;
	float yPos = margin;

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 365)));

	{
		Label *label = new Label();
		label->box.pos = Vec(box.size.x - margin - 12 * 15, margin);
		label->text = "Seaboard";
		addChild(label);
		yPos = labelHeight * 2;
	}

	addParam(createParam<LEDButton>(Vec(12 * 15, labelHeight), module, SeaboardInterface::RESET_PARAM, 0.0, 1.0,
									0.0));
	addChild(createLight<SmallLight<RedLight>>(Vec(12 * 15 + 5, labelHeight + 5), module, SeaboardInterface::RESET_LIGHT));
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
		yPos += midiChoice->box.size.y;
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
		yPos += channelChoice->box.size.y;
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
		yPos += modeChoice->box.size.y;
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
	std::string labels[6] = {"1V/oct", "Gate", "On Vel", "Off Vel", "Pressure", "Y axis"};

	yPos += labelHeight + margin * 2;
	for (int i = 0; i < 6; i++) {
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = labels[i];
		addChild(label);
		addOutput(createOutput<PJ3410Port>(Vec(2 * (40), yPos - 5), module, i * 4));
		addOutput(createOutput<PJ3410Port>(Vec(3 * (40), yPos - 5), module, i * 4 + 1));
		addOutput(createOutput<PJ3410Port>(Vec(4 * (40), yPos - 5), module, i * 4 + 2));
		addOutput(createOutput<PJ3410Port>(Vec(5 * (40), yPos - 5), module, i * 4 + 3));
		yPos += 35;
	}


}

void SeaboardWidget::step() {

	ModuleWidget::step();
}
