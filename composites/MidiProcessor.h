#pragma once

#include <list>
#include <algorithm>
#include "rtmidi/RtMidi.h"
#include "Roli.hpp"
#include "MidiIO.hpp"
#include "dsp/digital.hpp"


struct MidiKey {
	int pitch = 60;
	int at = 0; // aftertouch
	int vel = 0; // velocity
	bool gate = false;
};
/**
 * Complete Quad Midi composite
 *
 * If TBase is WidgetComposite, this class is used as the implementation part of the Quad Midi to CV module.
 * If TBase is TestComposite, this class may stand alone for unit tests.
 */
template <class TBase>
class MidiProcessor : public TBase
{
public:
	MidiProcessor(): TBase()
	{
	}
    // Define all the enums here. This will let the tests and the widget access them.
	enum ParamIds {
		RESET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	
	enum OutputIds {
		PITCH_OUTPUT = 0,
		GATE_OUTPUT = 4,
		VELOCITY_OUTPUT = 8,
		AT_OUTPUT = 12,
		NUM_OUTPUTS = 16
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
	
	MidiKey activeKeys[4];
	void step();

	std::list<int> open;
	void processMidi(std::vector<unsigned char> msg);
        void resetMidi(); 
	bool pedal = false;
	int mode = REASSIGN;
}

template <class TBase>
void MidiProcessor<TBase>::step() {
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


	for (int i = 0; i < 4; i++) {
		outputs[GATE_OUTPUT + i].value = activeKeys[i].gate ? 10.0 : 0;
		outputs[PITCH_OUTPUT + i].value = (activeKeys[i].pitch - 60) / 12.0;
		outputs[VELOCITY_OUTPUT + i].value = activeKeys[i].vel / 127.0 * 10.0;
		outputs[AT_OUTPUT + i].value = activeKeys[i].at / 127.0 * 10.0;
	}

	if (resetTrigger.process(params[RESET_PARAM].value)) {
		resetMidi();
		return;
	}

}

template <class TBase>
void MidiProcessor<TBase>::processMidi(std::vector<unsigned char> msg) {
	int channel = msg[0] & 0xf;
	int status = (msg[0] >> 4) & 0xf;
	int data1 = msg[1];
	int data2 = msg[2];
	bool gate;

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
		case 0xa: // channel aftertouch
			for (int i = 0; i < 4; i++) {
				if (activeKeys[i].pitch == data1) {
					activeKeys[i].at = data2;
				}
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
			return;
		default:
			return;
	}

	if (pedal && !gate) {
		return;
	}

	if (!gate) {
		for (int i = 0; i < 4; i++) {
			if (activeKeys[i].pitch == data1) {
				activeKeys[i].gate = false;
				activeKeys[i].vel = data2;
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
		if (activeKeys[i].pitch == data1 && activeKeys[i].gate) {
			activeKeys[i].vel = data2;
			if (std::find(open.begin(), open.end(), i) != open.end())
				open.remove(i);

			open.push_front(i);
			activeKeys[i].gate = false;
		}
	}

	activeKeys[next].gate = true;
	activeKeys[next].pitch = data1;
	activeKeys[next].vel = data2;
}
template <class TBase>
void MidiProcessor<TBase>::resetMidi() {

	for (int i = 0; i < 4; i++) {
		outputs[GATE_OUTPUT + i].value = 0.0;
		activeKeys[i].gate = false;
		activeKeys[i].vel = 0;
		activeKeys[i].at = 0;
	}

	open.clear();

	pedal = false;
	lights[RESET_LIGHT].value = 1.0;
}
