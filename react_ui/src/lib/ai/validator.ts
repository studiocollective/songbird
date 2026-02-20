/**
 * Simple structural validator for .bird files.
 * This does not perform full semantic resolution (like C++), 
 * but catches common LLM hallucinations and syntax formatting errors.
 */

export function validateBirdSyntax(content: string): { isValid: boolean, error?: string } {
    const lines = content.split('\n');
    let inGlobal = true;
    let currentChannel = '';
    
    // Tracking defined globals to ensure sections aren't referencing missing channels
    const definedChannels = new Set<string>();
    
    // Arrays for tracking pattern alignment
    let currentPatternLength = 0;
    
    for (let i = 0; i < lines.length; i++) {
        const lineStr = lines[i];
        const trimmed = lineStr.trim();
        
        // Skip empty lines and comments
        if (!trimmed || trimmed.startsWith('#')) continue;
        
        const tokens = trimmed.split(/\s+/);
        const cmd = tokens[0];
        
        // --- Global Blocks ---
        if (cmd === 'ch') {
            if (tokens.length < 3) return { isValid: false, error: `Line ${i+1}: 'ch' command needs a number and name. (e.g. 'ch 1 bass')` };
            const chName = `${tokens[1]} ${tokens[2]}`;
            if (inGlobal) {
                definedChannels.add(chName);
            }
            currentChannel = chName;
            currentPatternLength = 0; // Reset alignment tracking
        }
        
        else if (cmd === 'sec') {
            inGlobal = false;
            if (tokens.length < 2) return { isValid: false, error: `Line ${i+1}: 'sec' command needs a name. (e.g. 'sec verse')` };
            currentChannel = '';
        }
        
        else if (cmd === 'arr') {
            inGlobal = false;
        }
        
        // --- Structural rules inside sections ---
        else if (['plugin', 'fx', 'strip'].includes(cmd)) {
            if (!inGlobal) {
                return { isValid: false, error: `Line ${i+1}: '${cmd}' assignments must be in the global 'ch' block at the top, not inside a 'sec' block.` };
            }
        }
        
        // --- Pattern Alignment Checks ---
        else if (cmd === 'p' || cmd === 'v' || cmd === 'n' || isMacro(cmd)) {
            if (inGlobal) {
                // Ignore if it's the continuous `[macro] ramp` global format
                if (isMacro(cmd) && tokens[1] === 'ramp') continue;
                return { isValid: false, error: `Line ${i+1}: Cannot define patterns ('${cmd}') in the global space. Move this into a 'sec' block.` };
            }
            if (!currentChannel) {
                return { isValid: false, error: `Line ${i+1}: Pattern defined without a preceding 'ch' block.` };
            }
            if (!definedChannels.has(currentChannel)) {
                return { isValid: false, error: `Line ${i+1}: Channel '${currentChannel}' is used in a section but wasn't defined in the global block at the top.` };
            }
            
            // Check alignment
            const paramCount = tokens.length - 1; // minus the command token
            if (cmd === 'p') {
                currentPatternLength = paramCount;
            } else if (cmd === 'v' || cmd === 'n' || isMacro(cmd)) {
                if (currentPatternLength > 0 && paramCount > 0 && paramCount !== currentPatternLength) {
                    if (cmd !== 'n' || (cmd === 'n' && paramCount !== currentPatternLength)) {
                         // Note blocks can have varying sizes if making chords vertically, but simple check for now: 
                         // If it's a single value it repeats, otherwise it should match pattern length.
                         if (paramCount !== 1) {
                             return { isValid: false, error: `Line ${i+1}: Alignment error. Length of '${cmd}' (${paramCount}) does not match the length of the 'p' pattern (${currentPatternLength}).` };
                         }
                    }
                }
            }
        }
        
        // Check for Markdown hallucination
        else if (cmd.startsWith('```')) {
            return { isValid: false, error: `Line ${i+1}: Found markdown formatting in the file. Do not include \`\`\`bird inside the file contents.` };
        }
    }
    
    return { isValid: true };
}

function isMacro(cmd: string): boolean {
    const macros = [
        'brightness', 'resonance', 'attack', 'release', 'sub_level',
        'space', 'decay', 'echo', 'feedback', 'drive', 
        'input_gain', 'low_cut', 'eq_mid_gain', 'comp_thresh'
    ];
    return macros.includes(cmd);
}
