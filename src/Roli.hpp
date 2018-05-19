#include "rack.hpp"


using namespace rack;


//extern Plugin *plugin;

////////////////////
// module widgets
////////////////////

struct QuadMidiToCVWidget : ModuleWidget {
	QuadMidiToCVWidget();
	void step() override;
};
