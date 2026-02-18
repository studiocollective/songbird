#ifndef SEQUENCING_CLOCK
#define SEQUENCING_CLOCK

#ifdef ARDUINO
#include <time_constants.h>
#else
#include "utils/time_constants.h"
#endif

#include "sequencer.h"

#include <thread>
#include <vector>
#include <string>
#include <utility>
using std::vector;
using std::string;
using std::pair;

struct Transport {
        bool playing;
        vector<Sequencer*> sequencers;

    #ifndef ARDUINO // no update sequencer on arduino
        bool cycle_refresh;
        int cycle_pulses;
        vector<Sequencer*> update_sequencers;

        // Arrangement: ordered list of (section_sequencers, bar_count)
        vector<pair<vector<Sequencer*>, int>> arrangement;
        int arr_index; // current position in arrangement
        bool has_arrangement;
    #endif

        Transport();
        inline void pulse();
        inline void tick();
        inline void start();
        inline void stop();
};

class Clock {
    private:
        
        double ms_per_pulse;
        
        int delta_ms;
        int ticks;
        inline void calc_miliseconds(double bpm=0.0);
        inline void estimate_BPM(double delta_time);
        std::thread clock_thread;
        Clock();

    public:
        
        bool internal;
        double BPM;
        double estimated_BPM;
        int pulses;
        double ms_per_tick;
        double midi_time;
        double time_since_pulse;
        double time_since_tick;

        #ifdef ARDUINO
        unsigned long time;
        #else
        std::chrono::high_resolution_clock::time_point time;
        #endif

        Transport transport;
        static Clock& getInstance()
        {
            static Clock instance;
            return instance;
        }
        
        void register_sequencer(Sequencer* sequencer);

    #ifndef ARDUINO // no update sequencer on arduino
        void register_update_sequencer(Sequencer* sequencer);
        void clear_update_sequencers();
        void set_cycle_update(int bars);
        void cycle_update();
        
        // Arrangement support
        void set_arrangement(vector<pair<vector<Sequencer*>, int>> arr);
        void advance_arrangement();
    #endif

        // void set_transport_callback(void func());
        inline double update_time();
        void pulse();
        inline void tick();
        void begin_loop();
        void start();
        void stop();
};

#ifndef MIDI_CLOCK
#define MIDI_CLOCK
static Clock* midiclock = &Clock::getInstance();
#endif // !MIDI_CLOCK


#endif // SEQUENCING_CLOCK