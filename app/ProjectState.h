#pragma once

#include <juce_core/juce_core.h>
#include <git2.h>

/**
 * Centralized project state manager using libgit2 for zero-fork undo/redo.
 * 
 * The project directory contains:
 *   - <name>.bird         — composition (notes, arrangement)
 *   - <name>.state.json   — UI state (mixer, chat)
 *   - <name>.edit.json    — plugin state (VST3/AU presets, structured JSON)
 * 
 * Each meaningful change is committed with a source tag.
 * Undo/redo moves HEAD on the 'main' branch.
 * A 'refs/redo-tip' ref preserves the forward history for redo.
 * All Git operations happen in-process via libgit2 (no fork/exec).
 */
class ProjectState
{
public:
    ProjectState();
    ~ProjectState();

    /// Set the project directory (must be called before any other method)
    void setProjectDir(const juce::File& birdFile);

    // ------------------------------------------------------------------
    // Commit
    // ------------------------------------------------------------------

    enum Source { Autosave, LLM, User, Mixer, Plugin };

    /** Commit current files with a message and source tag. */
    void commit(const juce::String& message, Source source, bool includeEditXml = true);

    // ------------------------------------------------------------------
    // Undo / Redo
    // ------------------------------------------------------------------

    bool canUndo() const;
    bool canRedo() const;

    struct ChangedFile {
        juce::String filename;
        bool isSoftReloadOnly = false;
    };

    juce::Array<ChangedFile> undo();
    juce::Array<ChangedFile> redo();

    // ------------------------------------------------------------------
    // LLM Revert
    // ------------------------------------------------------------------

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

    juce::Array<HistoryEntry> getHistory(int maxEntries, juce::String* outHeadHash = nullptr) const;

    /** Returns the source tag prefix for commit messages */
    static juce::String sourceTag(Source s);

    git_repository* getRepo() const { return repo; }

private:
    juce::File projectDir;
    git_repository* repo = nullptr;
    bool initialized = false;

    void initRepo();
    
    // Core Git helpers (all in-process, zero fork)
    git_oid createCommit(const juce::String& message, Source source = Autosave);
    bool hasUncommittedChanges() const;
    juce::Array<ChangedFile> diffAndRestore(const git_oid& targetOid);
    juce::Array<ChangedFile> getChangedFiles(const git_oid& fromOid, const git_oid& toOid) const;
    void restoreWorkdirFromCommit(const git_oid& commitOid);
    bool moveHead(const git_oid& targetOid);   // moves refs/heads/main
    bool getHeadOid(git_oid* out) const;
    bool getParentOid(const git_oid& commitOid, git_oid* parentOid) const;
    bool findChildCommit(const git_oid& parentOid, const git_oid& tipOid, git_oid* childOid) const;
    juce::String getCommitMessage(const git_oid& oid) const;
};
