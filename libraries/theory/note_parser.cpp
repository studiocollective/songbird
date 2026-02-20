#include "note_parser.h"
#include <algorithm>

#ifdef ARDUINO
#include <note.h>
#else
#include "../sequencing/note.h"
#endif

// Maps note letter + accidental to semitone offset from C
static int semitone_from_name(string note_name) {
    if (note_name == "C")                   return 0;
    if (note_name == "C#" || note_name == "Db")  return 1;
    if (note_name == "D")                   return 2;
    if (note_name == "D#" || note_name == "Eb")  return 3;
    if (note_name == "E"  || note_name == "Fb")  return 4;
    if (note_name == "F"  || note_name == "E#")  return 5;
    if (note_name == "F#" || note_name == "Gb")  return 6;
    if (note_name == "G")                   return 7;
    if (note_name == "G#" || note_name == "Ab")  return 8;
    if (note_name == "A")                   return 9;
    if (note_name == "A#" || note_name == "Bb")  return 10;
    if (note_name == "B"  || note_name == "Cb")  return 11;
    return -1;
}

bool is_note_name(string s) {
    if (s.empty()) return false;
    return (s[0] >= 'A' && s[0] <= 'G');
}

bool is_chord_name(string s) {
    if (s.empty()) return false;
    return s[0] == '@';
}

int midi_from_note_name(string s) {
    if (s.empty()) return -1;

    // Parse note letter
    if (s[0] < 'A' || s[0] > 'G') return -1;

    // Extract note name part (letter + optional accidental) and octave
    string note_name;
    int octave = 4; // default
    int pos = 1;

    note_name = string(1, s[0]);

    // Check for accidental (# or b)
    if (pos < s.size() && (s[pos] == '#' || s[pos] == 'b')) {
        note_name += s[pos];
        pos++;
    }

    // Parse octave number
    if (pos < s.size()) {
        try {
            octave = stoi(s.substr(pos));
        } catch (...) {
            return -1;
        }
    }

    int semitone = semitone_from_name(note_name);
    if (semitone < 0) return -1;

    // MIDI note: C4 = 60, so octave 4 starts at 60
    // Formula: (octave + 1) * 12 + semitone
    return (octave + 1) * NOTES_PER_OCTAVE + semitone;
}

vector<int> midi_from_chord_name(string s) {
    vector<int> result;
    if (s.empty() || s[0] != '@') return result;

    // Remove @ prefix
    s = s.substr(1);
    if (s.empty()) return result;

    // Parse optional octave prefix (digit before letter)
    int octave = 4;
    int pos = 0;
    if (s[0] >= '0' && s[0] <= '9') {
        octave = s[0] - '0';
        pos = 1;
    }
    if (pos >= s.size()) return result;

    // Parse root note letter
    if (s[pos] < 'A' || s[pos] > 'G') return result;
    string root_name(1, s[pos]);
    pos++;

    // Check for root accidental (# or b), but only if not followed by ambiguous characters
    // We need to be careful: "Bb" could be B-flat or B + "b" (but there's no "b" quality)
    // "Db" could be D-flat or D + "b" 
    // Strategy: # is always sharp. 'b' after root is flat ONLY if followed by more characters
    // or if the remaining string would be a valid quality without it
    if (pos < s.size() && s[pos] == '#') {
        root_name += '#';
        pos++;
    } else if (pos < s.size() && s[pos] == 'b') {
        // 'b' is flat if: remaining after 'b' is empty OR starts with a valid quality char
        // Exception: if root is 'A' or 'E' or 'D' or 'G', "Ab", "Eb", "Db", "Gb" are common flats
        // Simple heuristic: if what comes after 'b' would still form a valid quality, treat 'b' as flat
        string after_b = s.substr(pos + 1);
        if (after_b.empty() || after_b[0] == 'm' || after_b[0] == 'M' || 
            after_b[0] == 'd' || after_b[0] == 'a' || after_b[0] == '7' ||
            after_b[0] == 's' || after_b == "7") {
            root_name += 'b';
            pos++;
        }
    }

    int root_semitone = semitone_from_name(root_name);
    if (root_semitone < 0) return result;

    int root_midi = (octave + 1) * NOTES_PER_OCTAVE + root_semitone;

    // Parse chord quality from remaining string
    string quality = s.substr(pos);

    // Define intervals for chord types
    // Intervals are in semitones from root
    int third = 0;
    int fifth = 7;  // perfect fifth default
    int seventh = -1; // -1 = no seventh
    int ninth = -1;

    if (quality.empty() || quality == "maj" || quality == "M") {
        // Major triad
        third = 4;
    } else if (quality == "m" || quality == "min" || quality == "-") {
        // Minor triad
        third = 3;
    } else if (quality == "7" || quality == "dom7") {
        // Dominant 7th
        third = 4;
        seventh = 10;
    } else if (quality == "maj7" || quality == "M7") {
        // Major 7th
        third = 4;
        seventh = 11;
    } else if (quality == "m7" || quality == "min7" || quality == "-7") {
        // Minor 7th
        third = 3;
        seventh = 10;
    } else if (quality == "dim" || quality == "o") {
        // Diminished triad
        third = 3;
        fifth = 6;
    } else if (quality == "dim7" || quality == "o7") {
        // Diminished 7th
        third = 3;
        fifth = 6;
        seventh = 9;
    } else if (quality == "m7b5" || quality == "ø" || quality == "ø7") {
        // Half-diminished 7th
        third = 3;
        fifth = 6;
        seventh = 10;
    } else if (quality == "aug" || quality == "+") {
        // Augmented triad
        third = 4;
        fifth = 8;
    } else if (quality == "aug7" || quality == "+7") {
        // Augmented 7th
        third = 4;
        fifth = 8;
        seventh = 10;
    } else if (quality == "sus2") {
        // Suspended 2nd
        third = 2;
    } else if (quality == "sus4" || quality == "sus") {
        // Suspended 4th
        third = 5;
    } else if (quality == "9") {
        // Dominant 9th
        third = 4;
        seventh = 10;
        ninth = 14;
    } else if (quality == "m9" || quality == "min9") {
        // Minor 9th
        third = 3;
        seventh = 10;
        ninth = 14;
    } else if (quality == "maj9" || quality == "M9") {
        // Major 9th
        third = 4;
        seventh = 11;
        ninth = 14;
    } else if (quality == "5" || quality == "power") {
        // Power chord (no third)
        third = -1;
    } else {
        // Unknown quality — try treating as major
        third = 4;
    }

    // Build chord
    result.push_back(root_midi);
    if (third >= 0)
        result.push_back(root_midi + third);
    result.push_back(root_midi + fifth);
    if (seventh >= 0)
        result.push_back(root_midi + seventh);
    if (ninth >= 0)
        result.push_back(root_midi + ninth);

    return result;
}

int sharps_from_key_name(string s, bool& isMinor) {
    if (s.empty()) return -99;
    
    // Default to major
    isMinor = false;
    
    // Check for minor
    string lower_s = s;
    for (char& c : lower_s) c = tolower(c);
    
    if (lower_s.find("m") != string::npos) {
        // Simple check for "m", "min", "minor"
        // But beware of e.g. "Eb major" - "m" is in "major"? Wait, usually it's "maj" or just nothing for major.
        // Let's be more specific. If it ends with 'm', "min", "minor", or has it separated.
        if (lower_s.find("min") != string::npos || lower_s.find("minor") != string::npos || 
            (lower_s.length() >= 2 && lower_s.back() == 'm')) {
            isMinor = true;
        } else if (lower_s.find("m") != string::npos && lower_s.find("maj") == string::npos) {
            // E.g. "C m"
            isMinor = true;
        }
    }
    
    // Extract root
    if (s[0] < 'A' || s[0] > 'G') return -99;
    string root(1, s[0]);
    if (s.length() > 1 && (s[1] == '#' || s[1] == 'b')) {
        root += s[1];
    }
    
    // Map of major keys to sharps/flats
    // Positive = sharps, Negative = flats
    int sharps = 0;
    
    if (root == "C") sharps = 0;
    else if (root == "G") sharps = 1;
    else if (root == "D") sharps = 2;
    else if (root == "A") sharps = 3;
    else if (root == "E") sharps = 4;
    else if (root == "B" || root == "Cb") sharps = 5; // Cb is technically -7, B is 5
    else if (root == "F#" || root == "Gb") sharps = 6; // F# is 6, Gb is -6
    else if (root == "C#" || root == "Db") sharps = 7; // C# is 7, Db is -5
    else if (root == "F") sharps = -1;
    else if (root == "Bb" || root == "A#") sharps = -2;
    else if (root == "Eb" || root == "D#") sharps = -3;
    else if (root == "Ab" || root == "G#") sharps = -4;
    else return -99; // unknown
    
    // If Db, Gb, Cb, adjust to flats. Overlap with sharps for enharmonics.
    if (root == "F#") sharps = 6;
    else if (root == "Gb") sharps = -6;
    else if (root == "Db") sharps = -5;
    else if (root == "C#") sharps = 7;
    else if (root == "Cb") sharps = -7;
    
    if (isMinor) {
        // Minor keys have 3 fewer sharps (or 3 more flats) than their parallel major
        sharps -= 3;
    }
    
    return sharps;
}
