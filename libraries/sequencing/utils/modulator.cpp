// #LFOs // polyrythmic patterns to modulate parameters on synths
#include "modulator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Modulator::Modulator(mod_type type, int cycle_length, double max_mod, bool step_based) :
                    type(type), cycle_length(cycle_length), max_mod(max_mod), step_based(step_based)
{
    
}

int Modulator::mod_for_step(int step) 
{
    if (type == NO_MOD || cycle_length == 0)
        return 100;

    double phase = (double)(step % cycle_length) / (double)cycle_length;
    double mod = 0.0;

    if (type == SIN_WAVE) {
        mod = sin(2.0 * M_PI * phase);
    } else if (type == TRI_WAVE) {
        if (phase < 0.25)
            mod = phase * 4.0;
        else if (phase < 0.75)
            mod = 2.0 - phase * 4.0;
        else
            mod = phase * 4.0 - 4.0;
    }

    // Returns percentage: 100 = no change, 80 = 80% of base velocity
    // max_mod of 0.2 means velocity varies between 80% and 120%
    double multiplier = 1.0 + mod * max_mod;
    if (multiplier < 0.0) multiplier = 0.0;
    return (int)(multiplier * 100.0);
}

int Modulator::mod_for_tick(int tick) 
{
    if (type == NO_MOD || cycle_length == 0)
        return 100;

    double phase = (double)(tick % cycle_length) / (double)cycle_length;
    double mod = 0.0;

    if (type == SIN_WAVE) {
        mod = sin(2.0 * M_PI * phase);
    } else if (type == TRI_WAVE) {
        if (phase < 0.25)
            mod = phase * 4.0;
        else if (phase < 0.75)
            mod = 2.0 - phase * 4.0;
        else
            mod = phase * 4.0 - 4.0;
    }

    double multiplier = 1.0 + mod * max_mod;
    if (multiplier < 0.0) multiplier = 0.0;
    return (int)(multiplier * 100.0);
}
