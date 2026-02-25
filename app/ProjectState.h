#pragma once

#include <juce_core/juce_core.h>

/**
 * Centralized project state manager using local git for undo/redo and LLM revert.
 * 
 * The project directory contains:
 *   - <name>.bird         — composition (notes, arrangement)
 *   - <name>.state.json   — UI state (mixer, chat)
 *   - <name>.edit.xml     — plugin state (VST3/AU presets)
 * 
 * Each meaningful change is committed with a source tag.
 * Undo/redo navigates commit history and restores files on disk.
 */
class ProjectState
{
public:
    ProjectState();

    /// Set the project directory (must be called before any other method)
    void setProjectDir(const juce::File& birdFile);

    // ------------------------------------------------------------------
    // Commit
    // ------------------------------------------------------------------

    enum Source { Autosave, LLM, User, Mixer };

    /** Commit current files with a message and source tag.
     *  If includeEditXml is false, .edit.xml is excluded (for frequent mixer saves).
     */
    void commit(const juce::String& message, Source source, bool includeEditXml = true);

    // ------------------------------------------------------------------
    // Undo / Redo
    // ------------------------------------------------------------------

    bool canUndo() const;
    bool canRedo() const;

    // Return structure for undo/redo
    struct ChangedFile {
        juce::String filename;
        bool isSoftReloadOnly = false; // True if ONLY patterns/velocities changed in a .bird file
    };

    /** Undo: restores files from the previous commit.
     *  Returns the list of files changed (caller uses this for fast-path reloads).
     */
    juce::Array<ChangedFile> undo();

    /** Redo: restores files from the next commit (after undo).
     *  Returns the list of files changed.
     */
    juce::Array<ChangedFile> redo();

    // ------------------------------------------------------------------
    // LLM Revert
    // ------------------------------------------------------------------

    /** Revert to the state before the last LLM commit.
     *  Returns the list of files changed.
     */
    juce::Array<ChangedFile> revertLastLLM();

    // ------------------------------------------------------------------
    // History
    // ------------------------------------------------------------------

    struct HistoryEntry
    {
        juce::String hash;
        juce::String message;
        juce::String timestamp;
        Source source;
    };

    juce::Array<HistoryEntry> getHistory(int maxEntries = 20) const;

    /** Returns the source tag prefix for commit messages */
    static juce::String sourceTag(Source s);

private:
    juce::File projectDir;
    bool initialized = false;

    // Undo position: 0 = HEAD, 1 = HEAD~1, etc.
    int undoPosition = 0;

    void initRepo();
    juce::String runGit(const juce::StringArray& args) const;
    juce::String runGitInternal(const juce::StringArray& fullArgs) const;
    bool restoreFilesFromCommit(const juce::String& commitHash);
    juce::String getCommitHash(int offset) const;
    juce::Array<ChangedFile> parseDiffOutput(const juce::String& diffText);
};
