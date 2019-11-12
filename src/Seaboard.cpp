#include "Roli.hpp"
#include "midi.hpp"
#include "dsp/digital.hpp"

#include <algorithm>


struct Seaboard : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(CV_OUTPUT, 4),
		ENUMS(GATE_OUTPUT, 4),
		ENUMS(ON_VELOCITY_OUTPUT, 4),
		ENUMS(OFF_VELOCITY_OUTPUT, 4),
		ENUMS(PRESSURE_OUTPUT, 4),
		ENUMS(Y_OUTPUT, 4),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	MidiInputQueue midiInput;

	enum PolyMode {
		ROTATE_MODE,
		REUSE_MODE,
		RESET_MODE,
		REASSIGN_MODE,
		UNISON_MODE,
		NUM_MODES
	};
	PolyMode polyMode = RESET_MODE;

	struct NoteData {
		uint8_t on_velocity = 0;
		uint8_t off_velocity = 0;
		uint8_t pressure = 0;
		uint16_t pitch_bend = 8192;
		uint8_t y_axis = 0;
		uint8_t pitch = 60;
	};

	NoteData noteData[16]; //one for each channel
	// cachedNotes : UNISON_MODE and REASSIGN_MODE cache all played channels. The other polyModes cache stolen channels (after the 4th one).
	std::vector<uint8_t> cachedChannels;
	uint8_t channels[4];
	bool gates[4];
	// gates set to TRUE by pedal and current gate. FALSE by pedal.
	bool pedalgates[4];
	bool pedal;
	int rotateIndex;
	int stealIndex;

	Seaboard() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS), cachedChannels(16) {
		onReset();
	}

	json_t *toJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "midi", midiInput.toJson());
		json_object_set_new(rootJ, "polyMode", json_integer(polyMode));
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		json_t *midiJ = json_object_get(rootJ, "midi");
		if (midiJ)
			midiInput.fromJson(midiJ);

		json_t *polyModeJ = json_object_get(rootJ, "polyMode");
		if (polyModeJ)
			polyMode = (PolyMode) json_integer_value(polyModeJ);
	}

	void onReset() override {
		for (int i = 0; i < 4; i++) {
			channels[i] = 0;
			gates[i] = false;
			pedalgates[i] = false;
		}
		pedal = false;
		rotateIndex = -1;
		cachedChannels.clear();
	}

	int getPolyIndex(int nowIndex) {
		for (int i = 0; i < 4; i++) {
			nowIndex++;
			if (nowIndex > 3)
				nowIndex = 0;
			if (!(gates[nowIndex] || pedalgates[nowIndex])) {
				stealIndex = nowIndex;
				return nowIndex;
			}
		}
		// All taken = steal (stealIndex always rotates)
		stealIndex++;
		if (stealIndex > 3)
			stealIndex = 0;
		if ((polyMode < REASSIGN_MODE) && (gates[stealIndex]))
			cachedChannels.push_back(channels[stealIndex]);
		return stealIndex;
	}

	void pressNote(uint8_t channel) {
		// Set channels and gates
		switch (polyMode) {
			case ROTATE_MODE: {
				rotateIndex = getPolyIndex(rotateIndex);
			} break;

			case REUSE_MODE: {
				bool reuse = false;
				for (int i = 0; i < 4; i++) {
					if (channels[i] == channel) {
						rotateIndex = i;
						reuse = true;
						break;
					}
				}
				if (!reuse)
					rotateIndex = getPolyIndex(rotateIndex);
			} break;

			case RESET_MODE: {
				rotateIndex = getPolyIndex(-1);
			} break;

			case REASSIGN_MODE: {
				cachedChannels.push_back(channel);
				rotateIndex = getPolyIndex(-1);
			} break;

			case UNISON_MODE: {
				cachedChannels.push_back(channel);
				for (int i = 0; i < 4; i++) {
					channels[i] = channel;
					gates[i] = true;
					pedalgates[i] = pedal;
					// reTrigger[i].trigger(1e-3);
				}
				return;
			} break;

			default: break;
		}
		// Set channels and gates
		// if (gates[rotateIndex] || pedalgates[rotateIndex])
		// 	reTrigger[rotateIndex].trigger(1e-3);
		channels[rotateIndex] = channel;
		gates[rotateIndex] = true;
		pedalgates[rotateIndex] = pedal;
	}

	void releaseNote(uint8_t channel) {
		// Remove the channel
		auto it = std::find(cachedChannels.begin(), cachedChannels.end(), channel);
		if (it != cachedChannels.end())
			cachedChannels.erase(it);

		switch (polyMode) {
			case REASSIGN_MODE: {
				for (int i = 0; i < 4; i++) {
					if (i < (int) cachedChannels.size()) {
						if (!pedalgates[i])
							channels[i] = cachedChannels[i];
						pedalgates[i] = pedal;
					}
					else {
						gates[i] = false;
					}
				}
			} break;

			case UNISON_MODE: {
				if (!cachedChannels.empty()) {
					uint8_t backchannel = cachedChannels.back();
					for (int i = 0; i < 4; i++) {
						channels[i] = backchannel;
						gates[i] = true;
					}
				}
				else {
					for (int i = 0; i < 4; i++) {
						gates[i] = false;
					}
				}
			} break;

			// default ROTATE_MODE REUSE_MODE RESET_MODE
			default: {
				for (int i = 0; i < 4; i++) {
					if (channels[i] == channel) {
						if (pedalgates[i]) {
							gates[i] = false;
						}
						else if (!cachedChannels.empty()) {
							channels[i] = cachedChannels.back();
							cachedChannels.pop_back();
						}
						else {
							gates[i] = false;
						}
					}
				}
			} break;
		}
	}

	void pressPedal() {
		pedal = true;
		for (int i = 0; i < 4; i++) {
			pedalgates[i] = gates[i];
		}
	}

	void releasePedal() {
		pedal = false;
		// When pedal is off, recover channels for pressed keys (if any) after they were already being "cycled" out by pedal-sustained notes.
		for (int i = 0; i < 4; i++) {
			pedalgates[i] = false;
			if (!cachedChannels.empty()) {
				if (polyMode < REASSIGN_MODE) {
					channels[i] = cachedChannels.back();
					cachedChannels.pop_back();
					gates[i] = true;
				}
			}
		}
		if (polyMode == REASSIGN_MODE) {
			for (int i = 0; i < 4; i++) {
				if (i < (int) cachedChannels.size()) {
					channels[i] = cachedChannels[i];
					gates[i] = true;
				}
				else {
					gates[i] = false;
				}
			}
		}
	}

	void step() override {
		MidiMessage msg;
		while (midiInput.shift(&msg)) {
			processMessage(msg);
		}

		for (int i = 0; i < 4; i++) {
			uint8_t lastChannel = channels[i];
			uint8_t lastGate = (gates[i] || pedalgates[i]);
			outputs[CV_OUTPUT + i].value = ((float)(noteData[lastChannel].pitch - 60) + ((noteData[lastChannel].pitch_bend - 8192) / 85.0)) / 12.f;
			outputs[GATE_OUTPUT + i].value = lastGate ? 10.f : 0.f;
			outputs[ON_VELOCITY_OUTPUT + i].value = rescale(noteData[lastChannel].on_velocity, 0, 127, 0.f, 10.f);
			outputs[OFF_VELOCITY_OUTPUT + i].value = rescale(noteData[lastChannel].off_velocity, 0, 127, 0.f, 10.f);
			outputs[PRESSURE_OUTPUT + i].value = rescale(noteData[lastChannel].pressure, 0, 127, 0.f, 10.f);
			outputs[Y_OUTPUT + i].value = rescale(noteData[lastChannel].y_axis, 0, 127, 0.f, 10.f);
		}
	}

	void processMessage(MidiMessage msg) {
		// filter MIDI channel
		if ((midiInput.channel > -1) && (midiInput.channel != msg.channel()))
			return;

		switch (msg.status()) {
			// note off
			case 0x8: {
				noteData[msg.channel()].off_velocity = msg.value();
				releaseNote(msg.channel());
			} break;
			// note on
			case 0x9: {
				if (msg.value() > 0) {
					noteData[msg.channel()].pitch = msg.note();
					noteData[msg.channel()].on_velocity = msg.value();
					pressNote(msg.channel());
				}
				else {
					releaseNote(msg.channel());
				}
			} break;
			// cc
			case 0xb: {
				processCC(msg);
			} break;
			// channel pressure
			case 0xd: {
				noteData[msg.channel()].pressure = msg.note();
			} break;
			// pitch bend
			case 0xe: {
				noteData[msg.channel()].pitch_bend = (msg.data2 << 7) | msg.data1;
			} break;
			default: break;
		}
	}

	void processCC(MidiMessage msg) {
		switch (msg.note()) {
			// sustain
			case 0x40: {
				if (msg.value() >= 64)
					pressPedal();
				else
					releasePedal();
			} break;
			// y axis
			case 0x4a: {
				noteData[msg.channel()].y_axis = msg.value();
			} break;
			default: break;
		}
	}
};


struct SeaboardWidget : ModuleWidget {
	SeaboardWidget(Seaboard *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(pluginInstance, "res/Seaboard.svg")));

		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        const float pj301m_radius = (8.356 / 2);
        const float pj3410_radius = (10.357 / 2);

        const float row_spacing = 15.739;
        const float gate_y_pos = 37.399 - pj3410_radius; 
        const float cv_y_pos = (gate_y_pos + row_spacing);
        const float on_vel_y_pos = (gate_y_pos + (2 * row_spacing));
        const float off_vel_y_pos = (gate_y_pos + (3 * row_spacing));
        const float press_y_pos = (gate_y_pos + (4 * row_spacing));
        const float yaxis_y_pos = (gate_y_pos + (5 * row_spacing));

        const float note_spacing = 14.172; //Equal to the diameter of the circles at the ends of the rows on the panel 
        const float note4_x_pos = 65.611 - pj3410_radius;
        const float note3_x_pos = note4_x_pos - note_spacing;
        const float note2_x_pos = note4_x_pos - (2 * note_spacing);
        const float note1_x_pos = note4_x_pos - (3 * note_spacing);


        // Coordinates are in mm from the top left.
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note1_x_pos, gate_y_pos)), Port::OUTPUT, module, Seaboard::GATE_OUTPUT + 0));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note1_x_pos, cv_y_pos)), Port::OUTPUT, module, Seaboard::CV_OUTPUT + 0));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note1_x_pos, on_vel_y_pos)), Port::OUTPUT, module, Seaboard::ON_VELOCITY_OUTPUT + 0));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note1_x_pos, off_vel_y_pos)), Port::OUTPUT, module, Seaboard::OFF_VELOCITY_OUTPUT + 0));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note1_x_pos, press_y_pos)), Port::OUTPUT, module, Seaboard::PRESSURE_OUTPUT + 0));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note1_x_pos, yaxis_y_pos)), Port::OUTPUT, module, Seaboard::Y_OUTPUT + 0));

		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note2_x_pos, gate_y_pos)), Port::OUTPUT, module, Seaboard::GATE_OUTPUT + 1));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note2_x_pos, cv_y_pos)), Port::OUTPUT, module, Seaboard::CV_OUTPUT + 1));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note2_x_pos, on_vel_y_pos)), Port::OUTPUT, module, Seaboard::ON_VELOCITY_OUTPUT + 1));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note2_x_pos, off_vel_y_pos)), Port::OUTPUT, module, Seaboard::OFF_VELOCITY_OUTPUT + 1));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note2_x_pos, press_y_pos)), Port::OUTPUT, module, Seaboard::PRESSURE_OUTPUT + 1));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note2_x_pos, yaxis_y_pos)), Port::OUTPUT, module, Seaboard::Y_OUTPUT + 1));

		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note3_x_pos, gate_y_pos)), Port::OUTPUT, module, Seaboard::GATE_OUTPUT + 2));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note3_x_pos, cv_y_pos)), Port::OUTPUT, module, Seaboard::CV_OUTPUT + 2));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note3_x_pos, on_vel_y_pos)), Port::OUTPUT, module, Seaboard::ON_VELOCITY_OUTPUT + 2));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note3_x_pos, off_vel_y_pos)), Port::OUTPUT, module, Seaboard::OFF_VELOCITY_OUTPUT + 2));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note3_x_pos, press_y_pos)), Port::OUTPUT, module, Seaboard::PRESSURE_OUTPUT + 2));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note3_x_pos, yaxis_y_pos)), Port::OUTPUT, module, Seaboard::Y_OUTPUT + 2));

		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note4_x_pos, gate_y_pos)), Port::OUTPUT, module, Seaboard::GATE_OUTPUT + 3));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note4_x_pos, cv_y_pos)), Port::OUTPUT, module, Seaboard::CV_OUTPUT + 3));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note4_x_pos, on_vel_y_pos)), Port::OUTPUT, module, Seaboard::ON_VELOCITY_OUTPUT + 3));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note4_x_pos, off_vel_y_pos)), Port::OUTPUT, module, Seaboard::OFF_VELOCITY_OUTPUT + 3));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note4_x_pos, press_y_pos)), Port::OUTPUT, module, Seaboard::PRESSURE_OUTPUT + 3));
		addOutput(Port::create<PJ3410Port>(mm2px(Vec(note4_x_pos, yaxis_y_pos)), Port::OUTPUT, module, Seaboard::Y_OUTPUT + 3));

		MidiWidget *midiWidget = Widget::create<MidiWidget>(mm2px(Vec(16.00, 1.00)));
		midiWidget->box.size = mm2px(Vec(44, 28));
		midiWidget->midiIO = &module->midiInput;
		addChild(midiWidget);
	}

	void appendContextMenu(Menu *menu) override {
		Seaboard *module = dynamic_cast<Seaboard*>(this->module);

		struct PolyphonyItem : MenuItem {
			Seaboard *module;
			Seaboard::PolyMode polyMode;
			void onAction(EventAction &e) override {
				module->polyMode = polyMode;
				module->onReset();
			}
		};

		menu->addChild(MenuEntry::create());
		menu->addChild(MenuLabel::create("Polyphony mode"));

		auto addPolyphonyItem = [&](Seaboard::PolyMode polyMode, std::string name) {
			PolyphonyItem *item = MenuItem::create<PolyphonyItem>(name, CHECKMARK(module->polyMode == polyMode));
			item->module = module;
			item->polyMode = polyMode;
			menu->addChild(item);
		};

		addPolyphonyItem(Seaboard::RESET_MODE, "Reset");
		addPolyphonyItem(Seaboard::ROTATE_MODE, "Rotate");
		addPolyphonyItem(Seaboard::REUSE_MODE, "Reuse");
		addPolyphonyItem(Seaboard::REASSIGN_MODE, "Reassign");
		addPolyphonyItem(Seaboard::UNISON_MODE, "Unison");
	}
};


Model *modelSeaboard = Model::create<Seaboard, SeaboardWidget>("Seaboard");

