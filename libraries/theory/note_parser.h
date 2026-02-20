#ifndef THEORY_NOTE_PARSER
#define THEORY_NOTE_PARSER

#include <string>
#include <vector>
using std::string;
using std::vector;

// Parse a note name like "C4", "Eb3", "F#5", "Db2" into a MIDI number.
// Returns -1 if the string is not a valid note name.
int midi_from_note_name(string s);

// Parse a chord name like "Cm", "Cm7", "Fmaj7", "G7", "Adim", "Bdim7"
// into a vector of MIDI note numbers (root position, octave 4 default).
// Prefix with octave number: "3Cm7" for octave 3.
// Returns empty vector if the string is not a valid chord name.
vector<int> midi_from_chord_name(string s);

// Check if a string looks like a note name (starts with A-G)
bool is_note_name(string s);

// Check if a string looks like a chord name (starts with @)
bool is_chord_name(string s);

// Returns the number of sharps (positive) or flats (negative) for a key signature string
// e.g. "F# minor", "C", "G maj"
// Returns -99 if parsing fails. Sets isMinor appropriately.
int sharps_from_key_name(string s, bool& isMinor);

#endif // THEORY_NOTE_PARSER
