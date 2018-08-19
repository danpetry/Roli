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
		uint8_t pitch_bend = 8192;
		uint8_t y_axis = 0;
	};

	NoteData noteData[128];
	// cachedNotes : UNISON_MODE and REASSIGN_MODE cache all played notes. The other polyModes cache stolen notes (after the 4th one).
	std::vector<uint8_t> cachedNotes;
	uint8_t notes[4];
	bool gates[4];
	// gates set to TRUE by pedal and current gate. FALSE by pedal.
	bool pedalgates[4];
	bool pedal;
	int rotateIndex;
	int stealIndex;

	Seaboard() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS), cachedNotes(128) {
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
			notes[i] = 60;
			gates[i] = false;
			pedalgates[i] = false;
		}
		pedal = false;
		rotateIndex = -1;
		cachedNotes.clear();
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
			cachedNotes.push_back(notes[stealIndex]);
		return stealIndex;
	}

	void pressNote(uint8_t note) {
		// Set notes and gates
		switch (polyMode) {
			case ROTATE_MODE: {
				rotateIndex = getPolyIndex(rotateIndex);
			} break;

			case REUSE_MODE: {
				bool reuse = false;
				for (int i = 0; i < 4; i++) {
					if (notes[i] == note) {
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
				cachedNotes.push_back(note);
				rotateIndex = getPolyIndex(-1);
			} break;

			case UNISON_MODE: {
				cachedNotes.push_back(note);
				for (int i = 0; i < 4; i++) {
					notes[i] = note;
					gates[i] = true;
					pedalgates[i] = pedal;
					// reTrigger[i].trigger(1e-3);
				}
				return;
			} break;

			default: break;
		}
		// Set notes and gates
		// if (gates[rotateIndex] || pedalgates[rotateIndex])
		// 	reTrigger[rotateIndex].trigger(1e-3);
		notes[rotateIndex] = note;
		gates[rotateIndex] = true;
		pedalgates[rotateIndex] = pedal;
	}

	void releaseNote(uint8_t note) {
		// Remove the note
		auto it = std::find(cachedNotes.begin(), cachedNotes.end(), note);
		if (it != cachedNotes.end())
			cachedNotes.erase(it);

		switch (polyMode) {
			case REASSIGN_MODE: {
				for (int i = 0; i < 4; i++) {
					if (i < (int) cachedNotes.size()) {
						if (!pedalgates[i])
							notes[i] = cachedNotes[i];
						pedalgates[i] = pedal;
					}
					else {
						gates[i] = false;
					}
				}
			} break;

			case UNISON_MODE: {
				if (!cachedNotes.empty()) {
					uint8_t backnote = cachedNotes.back();
					for (int i = 0; i < 4; i++) {
						notes[i] = backnote;
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
					if (notes[i] == note) {
						if (pedalgates[i]) {
							gates[i] = false;
						}
						else if (!cachedNotes.empty()) {
							notes[i] = cachedNotes.back();
							cachedNotes.pop_back();
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
		// When pedal is off, recover notes for pressed keys (if any) after they were already being "cycled" out by pedal-sustained notes.
		for (int i = 0; i < 4; i++) {
			pedalgates[i] = false;
			if (!cachedNotes.empty()) {
				if (polyMode < REASSIGN_MODE) {
					notes[i] = cachedNotes.back();
					cachedNotes.pop_back();
					gates[i] = true;
				}
			}
		}
		if (polyMode == REASSIGN_MODE) {
			for (int i = 0; i < 4; i++) {
				if (i < (int) cachedNotes.size()) {
					notes[i] = cachedNotes[i];
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
			uint8_t lastNote = notes[i];
			uint8_t lastGate = (gates[i] || pedalgates[i]);
			outputs[CV_OUTPUT + i].value = ((float)(lastNote - 60) + ((noteData[lastNote].pitch_bend - 8192) / 85.0)) / 12.f;
			outputs[GATE_OUTPUT + i].value = lastGate ? 10.f : 0.f;
			outputs[ON_VELOCITY_OUTPUT + i].value = rescale(noteData[lastNote].on_velocity, 0, 127, 0.f, 10.f);
			outputs[OFF_VELOCITY_OUTPUT + i].value = rescale(noteData[lastNote].off_velocity, 0, 127, 0.f, 10.f);
			outputs[PRESSURE_OUTPUT + i].value = rescale(noteData[lastNote].pressure, 0, 127, 0.f, 10.f);
			outputs[Y_OUTPUT + i].value = rescale(noteData[lastNote].y_axis, 0, 127, 0.f, 10.f);
		}
	}

	void processMessage(MidiMessage msg) {
		// filter MIDI channel
		if ((midiInput.channel > -1) && (midiInput.channel != msg.channel()))
			return;

		switch (msg.status()) {
			// note off
			case 0x8: {
				noteData[msg.note()].off_velocity = msg.value();
				releaseNote(msg.note());
			} break;
			// note on
			case 0x9: {
				if (msg.value() > 0) {
					noteData[msg.note()].on_velocity = msg.value();
					pressNote(msg.note());
				}
				else {
					releaseNote(msg.note());
				}
			} break;
			// cc
			case 0xb: {
				processCC(msg);
			} break;
			// channel pressure
			case 0xd: {
				noteData[msg.note()].pressure = msg.value();
			} break;
			// pitch bend
			case 0xe: {
				noteData[msg.note()].pitch_bend = (msg.data2 << 7) | msg.data1;
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
				noteData[msg.note()].y_axis = msg.value();
			} break;
			default: break;
		}
	}
};


struct SeaboardWidget : ModuleWidget {
	SeaboardWidget(Seaboard *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/Seaboard.svg")));

		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addOutput(Port::create<PJ301MPort>(mm2px(Vec(3.894335, 60.144478)), Port::OUTPUT, module, Seaboard::CV_OUTPUT + 0));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(15.494659, 60.144478)), Port::OUTPUT, module, Seaboard::GATE_OUTPUT + 0));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(27.094986, 60.144478)), Port::OUTPUT, module, Seaboard::ON_VELOCITY_OUTPUT + 0));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(27.094986, 60.144478)), Port::OUTPUT, module, Seaboard::OFF_VELOCITY_OUTPUT + 0));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(38.693935, 60.144478)), Port::OUTPUT, module, Seaboard::PRESSURE_OUTPUT + 0));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(38.693935, 60.144478)), Port::OUTPUT, module, Seaboard::Y_OUTPUT + 0));

		addOutput(Port::create<PJ301MPort>(mm2px(Vec(3.894335, 76.144882)), Port::OUTPUT, module, Seaboard::CV_OUTPUT + 1));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(15.494659, 76.144882)), Port::OUTPUT, module, Seaboard::GATE_OUTPUT + 1));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(27.094986, 76.144882)), Port::OUTPUT, module, Seaboard::VELOCITY_OUTPUT + 1));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(38.693935, 76.144882)), Port::OUTPUT, module, Seaboard::AFTERTOUCH_OUTPUT + 1));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(3.894335, 92.143906)), Port::OUTPUT, module, Seaboard::CV_OUTPUT + 2));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(15.494659, 92.143906)), Port::OUTPUT, module, Seaboard::GATE_OUTPUT + 2));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(27.094986, 92.143906)), Port::OUTPUT, module, Seaboard::VELOCITY_OUTPUT + 2));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(38.693935, 92.143906)), Port::OUTPUT, module, Seaboard::AFTERTOUCH_OUTPUT + 2));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(3.894335, 108.1443)), Port::OUTPUT, module, Seaboard::CV_OUTPUT + 3));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(15.494659, 108.1443)), Port::OUTPUT, module, Seaboard::GATE_OUTPUT + 3));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(27.094986, 108.1443)), Port::OUTPUT, module, Seaboard::VELOCITY_OUTPUT + 3));
		addOutput(Port::create<PJ301MPort>(mm2px(Vec(38.693935, 108.1443)), Port::OUTPUT, module, Seaboard::AFTERTOUCH_OUTPUT + 3));

		MidiWidget *midiWidget = Widget::create<MidiWidget>(mm2px(Vec(3.4009969, 14.837336)));
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


Model *modelSeaboard = Model::create<Seaboard, SeaboardWidget>("Roli", "Seaboard", "Seaboard", MIDI_TAG, EXTERNAL_TAG, QUAD_TAG);

