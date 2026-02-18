#include "bird_composer.h"

#ifndef ARDUINO
#include <iostream>
#include <fstream>
#include <string>
using std::string;
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>

#include "../../interface/console.h"

BirdComposer::BirdComposer() : Composer()
{
    println_to_console("bird composer");
    last_opened = 0;

    last_note = 0;
    last_velocity = 0;
    last_dur = 0;
    last_cc_value = 64;
    bars = 4; //In case length of bars is not set

    begin_loop();
}

void copy_file()
{
    std::ifstream src("files/live.bird", std::ios::binary);
    std::ofstream dst("files/live.temp", std::ios::binary);

    dst << src.rdbuf();
}

void destroy_file()
{
    std::remove("files/live.temp");
}

time_t updated_time()
{
    struct stat fileInfo;
    stat("files/live.bird", &fileInfo);
    time_t last_updated = fileInfo.st_mtime;
    return last_updated;
}

void BirdComposer::file_loop() 
{
    last_opened = 0;
    while (true) { 
        time_t last_updated = updated_time();
        if (last_opened < last_updated)
            read(last_updated);
        usleep(500000);
    }
}

void BirdComposer::begin_loop()
{
    file_thread = std::thread(&BirdComposer::file_loop, this);
    file_thread.detach();
}

void BirdComposer::read(time_t last_updated)
{
    copy_file();
    std::ifstream file;
    file.open("files/live.temp");
    if ( file.is_open() ) {
        println_to_console("--------------");
        println_to_console("reading");

        // clear any pending update as file has changed
        midiclock->clear_update_sequencers(); 

        // Reset section/arrangement state for fresh read
        sections.clear();
        arrangement.clear();
        current_section = "";

        // Read and process file, raising a read_exception when it occurs
        read_exception = false;
        while ( file ) { // equivalent to myfile.good()
            vector<string> chunk;
            std::string line;
            std::getline(file, line);
            while (file && line != "") { // this while loop groups things into chunks, separated by a newline
                chunk.push_back(line);
                std::getline(file, line);
            }
            process_chunk(chunk); //decoding work happens here!
        }
        file.close();
        destroy_file();
        last_opened = last_updated;

        // Only update the cycle if no read exception occurred
        if (!read_exception) {
            if (!arrangement.empty()) {
                // Arrangement mode: build sections and set arrangement
                build_arrangement();
            } else {
                midiclock->set_cycle_update(bars); // legacy single-section mode
            }
        } else {
            println_to_console("read exception");
            println_to_console("--------------");
            midiclock->clear_update_sequencers(); // clear pending update and wait for next save
        }
        
    }
    else {
        println_to_console("failed to read");
    }
}

static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

static inline vector<string> get_vector_from_string(std::string s) {
    std::vector<std::string>   result;
    std::stringstream  data(s);
    std::string line;
    while(std::getline(data,line,' '))
    {
        result.push_back(line); // Note: You may get a couple of blank lines
                                // When multiple underscores are beside each other.
    }
    return result;
}

void BirdComposer::process_chunk(vector<string> chunk)
{
    vector<vector<string>> sequence;

    for (string s : chunk) {
        ltrim(s);
        vector<string> line = get_vector_from_string(s);

        if (line.size() == 0 || line[0] == "#") {
            // ignore empty lines and comments
        } else if (line[0] == "b") {
            try
            {
                bars = stoi(line[1]);
                println_to_console(std::to_string(bars) + " bar cycle");
            }
            catch (...)
            {
                println_to_console("syntax error in # bars, setting to 4");
                bars = 4;
            }
        } else if (line[0] == "sec") {
            // Start a new named section
            // Syntax: sec <name>
            if (line.size() > 1) {
                current_section = line[1];
                println_to_console("section: " + current_section);
            }
        } else if (line[0] == "arr") {
            // Arrangement: arr <section_name> <bars> [<section_name> <bars> ...]
            // Syntax: arr verse 8 chorus 8 verse 8 chorus 8 bridge 4 outro 4
            arrangement.clear();
            for (int i = 1; i + 1 < (int)line.size(); i += 2) {
                string name = line[i];
                int section_bars = 4;
                try { section_bars = stoi(line[i+1]); } catch (...) {}
                arrangement.push_back(make_pair(name, section_bars));
                println_to_console("  arr: " + name + " (" + std::to_string(section_bars) + " bars)");
            }
        } else {
            if (!current_section.empty()) {
                // Collecting lines into the current section
                sections[current_section].push_back(line);
            } else {
                // No section defined — legacy single-section mode
                sequence.push_back(line);
            }
        }
    }

    if (sequence.size() > 0) {
        construct_sequencers(sequence);
    }
}

void BirdComposer::build_arrangement()
{
    println_to_console("building arrangement...");
    
    vector<pair<vector<Sequencer*>, int>> arr_data;
    
    for (auto& entry : arrangement) {
        string name = entry.first;
        int section_bars = entry.second;
        
        if (sections.find(name) == sections.end()) {
            println_to_console("!! section not found: " + name);
            read_exception = true;
            return;
        }
        
        // Build sequencers for this section
        // Save and restore the clock's update_sequencers since construct_sequencers 
        // registers directly to the clock
        midiclock->clear_update_sequencers();
        construct_sequencers(sections[name]);
        
        // Collect the sequencers that were registered
        vector<Sequencer*> section_seqs = midiclock->transport.update_sequencers;
        midiclock->transport.update_sequencers.clear();
        
        arr_data.push_back(make_pair(section_seqs, section_bars));
        println_to_console("  built section: " + name + " (" + std::to_string(section_seqs.size()) + " sequencers, " + std::to_string(section_bars) + " bars)");
    }
    
    midiclock->set_arrangement(arr_data);
}

void BirdComposer::construct_sequencers(vector<vector<string>> sequence)
{
    //TODO: graceful failure for syntax issues
    Sequencer* s = new Sequencer();

    //velocity vector, since it needs to be preserved
    vector<int> velocity;
    
    for (vector<string> line : sequence) {
        try
        {
            if (line[0] == "ch") {
                int channel = stoi(line[1])-1;
                if (channel < 0 || channel > 15)
                    throw 3;
                s->set_channel(channel);
            } else if (line[0] == "p") {
                s->pattern = construct_pattern(line);
            } else if (line[0] == "sw") {
                s->swing = construct_swing(line);
            } else if (line[0] == "m") {
                s->mod = construct_modulator(line);
            } else if (line[0] == "v") {
                velocity.clear();
                velocity = construct_velocities(line);
            } else if (line[0] == "n") {
                vector<vector<int>> note_groups = construct_notes(line);
                if (!read_exception)
                    s->gen_notes_sequence(note_groups, velocity);
            } else if (line[0] == "cc") {
                int cc_num = stoi(line[1]);
                vector<int> values = construct_cc_values(line, 2);
                if (!read_exception)
                    s->gen_cc_sequence(cc_num, values);
            } else if (line[0] == "var") {
                int cc_num = cc_from_name(line[1]);
                if (cc_num < 0) {
                    println_to_console("!! unknown var name: " + line[1]);
                    read_exception = true;
                } else {
                    vector<int> values = construct_cc_values(line, 2);
                    if (!read_exception)
                        s->gen_cc_sequence(cc_num, values);
                }
            } else if (line[0] == "mix") {
                int cc_num = mix_cc_from_name(line[1]);
                if (cc_num < 0) {
                    println_to_console("!! unknown mix param: " + line[1]);
                    read_exception = true;
                } else {
                    vector<int> values = construct_cc_values(line, 2);
                    if (!read_exception)
                        s->gen_cc_sequence(cc_num, values);
                }
            }
        }
        catch (...) // Raise read exception if anything fails
        {
            read_exception = true;
            println_to_console("!! syntax error on following line");
            for (string s : line)
                print_to_console(s+" ");
            println_to_console("--");
        }
    }

    if (!read_exception) {
        midiclock->register_update_sequencer(s);
    }
}

vector<int> BirdComposer::construct_pattern(vector<string> data)
{
    vector<int> pattern;
    for(int i = 1; i < data.size(); i++)
    {
        //TODO: test this functionality
        if (data[i] == "_") {
            int rest_length = 0 - abs(last_dur);
            pattern.push_back(rest_length);
        } else {
            last_dur = dur_from_string(data[i]);
            pattern.push_back(last_dur);
        }
    }

    if (pattern.size() == 0)
        throw 16;

    return pattern;
}

vector<int> BirdComposer::construct_velocities(vector<string> data)
{
    vector<int> velocity;
    for(int i = 1; i < data.size(); i++)
    {
        //TODO: test this functionality (incrementing velocities)
        if (data[i] == "-"){
            velocity.push_back(last_velocity);
        } else if (data[i][0] == '+' || data[i][0] == '-') {
            int new_velocity = last_velocity + stoi(data[i]);
            velocity.push_back(new_velocity);
        } else {
            last_velocity = stoi(data[i]);
            velocity.push_back(last_velocity);
        }
    }

    if (velocity.size() == 0)
        throw 22;

    return velocity;
}

vector<vector<int>> BirdComposer::construct_notes(vector<string> data)
{
    vector<vector<int>> note_groups;
    for(int i = 1; i < data.size(); i++)
    {
        if (data[i] == "-"){
            // Repeat last note/chord
            note_groups.push_back({last_note});
        } else if (data[i][0] == '+' || (data[i][0] == '-' && data[i].size() > 1)) {
            // Relative offset from last note
            int new_note = last_note + stoi(data[i]);
            last_note = new_note;
            note_groups.push_back({new_note});
        } else if (is_chord_name(data[i])) {
            // Chord name: @Cm7, @G7, @Fmaj7
            vector<int> chord_notes = midi_from_chord_name(data[i]);
            if (chord_notes.empty()) {
                println_to_console("!! unknown chord: " + data[i]);
                throw 15;
            }
            last_note = chord_notes[0]; // Track root for relative offsets
            note_groups.push_back(chord_notes);
        } else if (is_note_name(data[i])) {
            // Note name: C4, Eb3, F#5
            int midi = midi_from_note_name(data[i]);
            if (midi < 0) {
                println_to_console("!! invalid note name: " + data[i]);
                throw 15;
            }
            last_note = midi;
            note_groups.push_back({midi});
        } else {
            // MIDI number (original behavior)
            last_note = stoi(data[i]);
            note_groups.push_back({last_note});
        }
    }

    if (note_groups.size() == 0)
        throw 14;

    return note_groups;
}

Swing BirdComposer::construct_swing(vector<string> data)
{
    // Syntax: sw <amount_token> [~]
    // amount_token: - (straight), < (drag), > (rush), or number (percentage)
    // ~ = humanize (add random timing/velocity jitter)
    // Optional: base note value (x, xx, q, etc.)
    // Examples:
    //   sw -        (straight, no humanize)
    //   sw - ~      (straight with humanize)
    //   sw < ~      (drag 20% with humanize)
    //   sw > ~      (rush 20% with humanize)
    //   sw 25       (drag 25%)
    //   sw -25      (rush 25%)
    //   sw < xx ~   (drag on 8th note grid with humanize)
    
    int swing_amount = STRAIGHT;
    int base = dur::x; // Default: 16th note grid
    bool humanize = false;
    
    for (int i = 1; i < data.size(); i++) {
        if (data[i] == "-") {
            swing_amount = STRAIGHT;
        } else if (data[i] == "~") {
            humanize = true;
        } else if (data[i] == "<") {
            swing_amount = DRAG_20; // Default drag
        } else if (data[i] == ">") {
            swing_amount = RUSH_20; // Default rush
        } else if (data[i] == "<<") {
            swing_amount = DRAG_33;
        } else if (data[i] == ">>") {
            swing_amount = RUSH_TRIPLET;
        } else {
            // Try as a duration base (x, xx, q, etc.)
            int d = dur_from_string(data[i]);
            if (d != 0) {
                base = d;
            } else {
                // Try as numeric swing amount 
                try {
                    swing_amount = stoi(data[i]);
                } catch (...) {
                    println_to_console("!! unknown swing token: " + data[i]);
                }
            }
        }
    }
    
    return Swing(swing_amount, base, humanize);
}

Modulator BirdComposer::construct_modulator(vector<string> data)
{
    // Syntax: m <type> [cycle_length] [max_mod%] [step]
    // type: sin, tri
    // cycle_length: number of steps/ticks per full cycle (default: 8)
    // max_mod%: depth as percentage, e.g. 20 means ±20% (default: 20)
    // step: if present, modulate per-step instead of per-tick
    // Examples:
    //   m sin           (sine wave, 8-step cycle, ±20%)
    //   m tri 4         (triangle, 4-step cycle, ±20%)
    //   m sin 8 30      (sine, 8-step cycle, ±30%)
    //   m tri 16 10 step (triangle, 16-step cycle, ±10%, step-based)
    
    mod_type type = NO_MOD;
    int cycle_length = 8;
    double max_mod = 0.2;
    bool step_based = true; // Default to step-based (more musical)
    
    for (int i = 1; i < data.size(); i++) {
        if (data[i] == "sin") {
            type = SIN_WAVE;
        } else if (data[i] == "tri") {
            type = TRI_WAVE;
        } else if (data[i] == "step") {
            step_based = true;
        } else if (data[i] == "tick") {
            step_based = false;
        } else {
            // First number = cycle_length, second = max_mod percentage
            try {
                int val = stoi(data[i]);
                if (cycle_length == 8 && i <= 2) {
                    cycle_length = val;
                } else {
                    max_mod = val / 100.0;
                }
            } catch (...) {
                println_to_console("!! unknown modulator token: " + data[i]);
            }
        }
    }
    
    return Modulator(type, cycle_length, max_mod, step_based);
}

vector<int> BirdComposer::construct_cc_values(vector<string> data, int start_index)
{
    vector<int> values;
    for(int i = start_index; i < data.size(); i++)
    {
        if (data[i] == "-"){
            values.push_back(last_cc_value);
        } else if (data[i][0] == '+' || (data[i][0] == '-' && data[i].size() > 1)) {
            int new_value = last_cc_value + stoi(data[i]);
            // Clamp to 0-127
            if (new_value < 0) new_value = 0;
            if (new_value > 127) new_value = 127;
            last_cc_value = new_value;
            values.push_back(last_cc_value);
        } else {
            last_cc_value = stoi(data[i]);
            values.push_back(last_cc_value);
        }
    }

    if (values.size() == 0)
        throw 30;

    return values;
}



#endif
