# Roli VCV plugin

These plugins support Roli's product range in VCV Rack.

# Plugins

## Seaboard Block MIDI

This plugin takes MIDI from the Seaboard Block, and outputs:

- 1V/oct for the note value
- Gate
- Note on velocity
- Note off velocity
- Pitch bend
- CV 70 (?) for up and down the notes
- Pressure

On a per note basis. There are three modes:

Rotate - Rotate through the outputs
Reset - Playing a 5th note will reset all notes (= gate off) and start with the first
Reassign - Reassign the note which was held the longest.

# Acknowledgements

The Seaboard Block MIDI module is based on VCV Core's Quad MIDI to CV.
