#include "Roli.hpp"


Plugin *plugin;
void init(Plugin *p) {
	plugin = p;

	p->addModel(modelSeaboard);
}
