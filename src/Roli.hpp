#pragma once
#include "rack.hpp"


using namespace rack;


extern Plugin *plugin;
extern Model *modelQuadMidiModule;
////////////////////
// module widgets
////////////////////

struct QuadMidiToCVWidget : ModuleWidget {
	QuadMidiToCVWidget();
	void step() override;
};
