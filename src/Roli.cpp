#include "Roli.hpp"


// The plugin-wide instance of the Plugin class
//Plugin *plugin;

void init(rack::Plugin *p) {
//	plugin = p;
	// The "slug" is the unique identifier for your plugin and must never change after release, so choose wisely.
	// It must only contain letters, numbers, and characters "-" and "_". No spaces.
	// To guarantee uniqueness, it is a good idea to prefix the slug by your name, alias, or company name if available, e.g. "MyCompany-MyPlugin".
	// The ZIP package must only contain one folder, with the name equal to the plugin's slug.
	p->slug = "danielpetry-Roli";
	p->version = TOSTRING(VERSION);

	// For each module, specify the ModuleWidget subclass, manufacturer slug (for saving in patches), manufacturer human-readable name, module slug, and module name
	p->addModel(createModel<QuadMidiToCVWidget>("Roli", "QuadMIDIToCVInterface", "Quad MIDI-toCV Interface", MIDI_TAG, EXTERNAL_TAG, QUAD_TAG));

	// Any other plugin initialization may go here.
	// As an alternative, consider lazy-loading assets and lookup tables when your module is created to reduce startup times of Rack.
}
