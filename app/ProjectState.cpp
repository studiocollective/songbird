#include "ProjectState.h"
#include <thread>

//==============================================================================
// ProjectState — local git wrapper for undo/redo
//==============================================================================

// macOS GUI apps have a minimal PATH, so use the full path to git
#if JUCE_MAC
static const char* GIT_PATH = "/usr/bin/git";
#else
static const char* GIT_PATH = "git";
#endif

ProjectState::ProjectState() {}

juce::String ProjectState::sourceTag(Source s)
{
    switch (s)
    {
        case Autosave: return "[auto]";
        case LLM:      return "[LLM]";
        case User:     return "[user]";
        case Mixer:    return "[mixer]";
    }
    return "[unknown]";
}

void ProjectState::setProjectDir(const juce::File& birdFile)
{
    projectDir = birdFile.getParentDirectory();
    // Run git init on a background thread to avoid blocking app launch
    std::thread([this]() { initRepo(); }).detach();
}

//==============================================================================
// Git helpers
//==============================================================================

juce::String ProjectState::runGit(const juce::StringArray& args) const
{
    if (!initialized && !args.contains("init")) return {};

    juce::StringArray fullArgs;
    fullArgs.add(GIT_PATH);
    fullArgs.add("-C");
    fullArgs.add(projectDir.getFullPathName());
    fullArgs.addArray(args);

    DBG("ProjectState: Running: " + fullArgs.joinIntoString(" "));

    juce::ChildProcess process;
    if (process.start(fullArgs))
    {
        auto output = process.readAllProcessOutput();
        auto exitCode = process.waitForProcessToFinish(5000);
        if (!exitCode)
            DBG("ProjectState: git command timed out");
        auto trimmed = output.trim();
        if (trimmed.isNotEmpty() && trimmed.length() < 200)
            DBG("ProjectState: Output: " + trimmed);
        return trimmed;
    }
    else
    {
        DBG("ProjectState: Failed to start git process! Is git installed?");
        return {};
    }
}

void ProjectState::initRepo()
{
    if (!projectDir.isDirectory()) return;

    auto gitDir = projectDir.getChildFile(".git");
    if (!gitDir.isDirectory())
    {
        DBG("ProjectState: Initializing git repo in " + projectDir.getFullPathName());

        // Initialize a new git repo using full path
        juce::ChildProcess process;
        juce::StringArray args = { GIT_PATH, "init", projectDir.getFullPathName() };
        if (process.start(args))
        {
            process.waitForProcessToFinish(5000);
            DBG("ProjectState: git init completed");
        }
        else
        {
            DBG("ProjectState: FAILED to run git init — git not found at " + juce::String(GIT_PATH));
            return;
        }

        initialized = true; // need this for runGit to work

        // Set git user for commits (required for git commit to work)
        runGit({ "config", "user.email", "songbird@local" });
        runGit({ "config", "user.name", "Songbird" });

        // Create .gitignore
        auto gitignore = projectDir.getChildFile(".gitignore");
        if (!gitignore.existsAsFile())
        {
            gitignore.replaceWithText(
                ".DS_Store\n"
                "*.wav\n"
                "*.mp3\n"
                "*.aif\n"
                "*.aiff\n"
            );
        }

        // Initial commit
        runGit({ "add", "-A" });
        runGit({ "commit", "-m", "[auto] Initial project state", "--allow-empty" });
        DBG("ProjectState: Initial commit done");
    }

    initialized = true;
    DBG("ProjectState: Ready in " + projectDir.getFullPathName());
}

//==============================================================================
// Commit
//==============================================================================

void ProjectState::commit(const juce::String& message, Source source, bool includeEditXml)
{
    if (!initialized) return;

    // Stage all files
    runGit({ "add", "-A" });

    // Optionally unstage .edit.xml
    if (!includeEditXml)
    {
        // Use explicit path pattern — find any .edit.xml files and unstage them
        auto editFile = projectDir.getChildFile(
            projectDir.findChildFiles(juce::File::findFiles, false, "*.edit.xml")
                .isEmpty() ? "" : "dummy");
        // Simpler: just use git reset for the specific file pattern
        runGit({ "reset", "HEAD", "--", "*.edit.xml" });
    }

    // Check if there's anything to commit
    auto status = runGit({ "status", "--porcelain" });
    if (status.isEmpty())
    {
        DBG("ProjectState: Nothing to commit");
        return;
    }

    auto fullMessage = sourceTag(source) + " " + message;
    runGit({ "commit", "-m", fullMessage });

    // Reset undo position — new commit invalidates redo history
    undoPosition = 0;

    DBG("ProjectState: Committed — " + fullMessage);
}

//==============================================================================
// Undo / Redo
//==============================================================================

int ProjectState::getCommitCount() const
{
    auto result = runGit({ "rev-list", "--count", "HEAD" });
    return result.getIntValue();
}

juce::String ProjectState::getCommitHash(int offset) const
{
    if (offset < 0) return {};
    if (offset == 0)
        return runGit({ "rev-parse", "--verify", "HEAD" });
    auto ref = "HEAD~" + juce::String(offset);
    return runGit({ "rev-parse", "--verify", ref });
}

bool ProjectState::canUndo() const
{
    return initialized && (undoPosition + 1) < getCommitCount();
}

bool ProjectState::canRedo() const
{
    return initialized && undoPosition > 0;
}

bool ProjectState::restoreFilesFromCommit(const juce::String& commitHash)
{
    if (commitHash.isEmpty())
    {
        DBG("ProjectState: Empty commit hash, can't restore");
        return false;
    }

    // Use git checkout to restore the working directory to a specific commit's state
    // This is simpler and more reliable than git show for each file
    auto result = runGit({ "checkout", commitHash, "--", "." });
    DBG("ProjectState: Restored files from " + commitHash.substring(0, 8));

    // Unstage the changes so they don't affect future commits
    runGit({ "reset", "HEAD" });

    return true;
}

bool ProjectState::undo()
{
    if (!canUndo())
    {
        DBG("ProjectState: Cannot undo (position=" + juce::String(undoPosition) + 
            ", commits=" + juce::String(getCommitCount()) + ")");
        return false;
    }

    // Before first undo, save current state so redo can get back here
    if (undoPosition == 0)
    {
        runGit({ "add", "-A" });
        auto status = runGit({ "status", "--porcelain" });
        if (status.isNotEmpty())
        {
            runGit({ "commit", "-m", "[auto] Save before undo" });
        }
    }

    undoPosition++;
    auto hash = getCommitHash(undoPosition);
    if (hash.isEmpty())
    {
        DBG("ProjectState: Could not get hash for HEAD~" + juce::String(undoPosition));
        undoPosition--;
        return false;
    }

    bool ok = restoreFilesFromCommit(hash);
    if (ok) DBG("ProjectState: Undo → position " + juce::String(undoPosition));
    return ok;
}

bool ProjectState::redo()
{
    if (!canRedo())
    {
        DBG("ProjectState: Cannot redo (position=" + juce::String(undoPosition) + ")");
        return false;
    }

    undoPosition--;
    auto hash = getCommitHash(undoPosition);
    if (hash.isEmpty())
    {
        undoPosition++;
        return false;
    }

    bool ok = restoreFilesFromCommit(hash);
    if (ok) DBG("ProjectState: Redo → position " + juce::String(undoPosition));
    return ok;
}

//==============================================================================
// LLM Revert
//==============================================================================

bool ProjectState::revertLastLLM()
{
    if (!initialized) return false;

    // Walk through commits to find the last [LLM] and restore the commit before it
    auto log = runGit({ "log", "--oneline", "-n", "50" });
    juce::StringArray lines;
    lines.addTokens(log, "\n", "");

    DBG("ProjectState: Searching for LLM commit in " + juce::String(lines.size()) + " entries");

    for (int i = 0; i < lines.size(); i++)
    {
        if (lines[i].contains("[LLM]"))
        {
            DBG("ProjectState: Found LLM commit at index " + juce::String(i) + ": " + lines[i]);
            // Found LLM commit — we want the state BEFORE it (i.e., the commit after it in the list = i+1)
            if (i + 1 < lines.size())
            {
                auto prevLine = lines[i + 1];
                auto hash = prevLine.upToFirstOccurrenceOf(" ", false, false);
                if (hash.isNotEmpty())
                {
                    // Save current state first
                    commit("Save before LLM revert", Autosave);

                    bool ok = restoreFilesFromCommit(hash);
                    if (ok)
                    {
                        // Commit the reverted state
                        runGit({ "add", "-A" });
                        runGit({ "commit", "-m", "[user] Reverted LLM change" });
                        undoPosition = 0;
                        DBG("ProjectState: Reverted to pre-LLM state " + hash.substring(0, 8));
                    }
                    return ok;
                }
            }
            break;
        }
    }

    DBG("ProjectState: No LLM commit found to revert");
    return false;
}

//==============================================================================
// History
//==============================================================================

juce::Array<ProjectState::HistoryEntry> ProjectState::getHistory(int maxEntries) const
{
    juce::Array<HistoryEntry> entries;
    if (!initialized) return entries;

    // Format: hash|timestamp|message
    auto log = runGit({ "log", "--format=%H|%ai|%s", "-n", juce::String(maxEntries) });
    juce::StringArray lines;
    lines.addTokens(log, "\n", "");

    for (auto& line : lines)
    {
        if (line.isEmpty()) continue;

        auto parts = juce::StringArray::fromTokens(line, "|", "");
        if (parts.size() < 3) continue;

        HistoryEntry entry;
        entry.hash = parts[0];
        entry.timestamp = parts[1];
        entry.message = parts[2];

        // Parse source from message tag
        if (entry.message.startsWith("[LLM]"))       entry.source = LLM;
        else if (entry.message.startsWith("[user]"))  entry.source = User;
        else if (entry.message.startsWith("[mixer]")) entry.source = Mixer;
        else                                          entry.source = Autosave;

        entries.add(entry);
    }

    return entries;
}
