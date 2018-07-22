#include "rack.hpp"


using namespace rack;


//extern Plugin *plugin;

////////////////////
// module widgets
////////////////////

struct SeaboardWidget : ModuleWidget {
	SeaboardWidget();
	void step() override;
};
