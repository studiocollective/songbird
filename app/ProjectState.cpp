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

    auto fullMessage = sourceTag(source) + " " + message;
    
    // Combine add and commit into a single shell sub-process to save fork time.
    // We use --allow-empty to avoid needing a separate 'status' check process.
    juce::StringArray commitCmd;
    commitCmd.add("/bin/sh");
    commitCmd.add("-c");
    
    juce::String script = "git add -A";
    if (!includeEditXml)
        script += " && git reset HEAD -- *.edit.xml";
    
    script += " && git commit -m " + juce::JSON::toString(fullMessage) + " --allow-empty";
    
    commitCmd.add(script);
    runGitInternal(commitCmd);

    undoPosition = 0;
    DBG("ProjectState: Committed - " + fullMessage);
}

//==============================================================================
// Undo / Redo
//==============================================================================

juce::String ProjectState::getCommitHash(int offset) const
{
    if (offset < 0) return {};
    auto ref = "HEAD" + (offset == 0 ? "" : "~" + juce::String(offset));
    return runGit({ "rev-parse", "--verify", ref });
}

bool ProjectState::canUndo() const
{
    if (!initialized) return false;
    // Faster check than rev-list --count: just see if HEAD~1 exists
    auto res = runGit({ "rev-parse", "--verify", "HEAD~" + juce::String(undoPosition + 1) });
    return res.isNotEmpty();
}

bool ProjectState::canRedo() const
{
    return initialized && undoPosition > 0;
}

bool ProjectState::restoreFilesFromCommit(const juce::String& commitHash)
{
    if (commitHash.isEmpty()) return false;

    // Use -f to overwrite and clear index in one fork
    runGit({ "checkout", "-f", commitHash, "--", "." });
    DBG("ProjectState: Restored files from " + commitHash.substring(0, 8));
    return true;
}

juce::Array<ProjectState::ChangedFile> ProjectState::undo()
{
    if (!initialized) return {};

    // 1. Snapshot current state (1 process)
    commit("Save before undo", Autosave);

    // 2. Diff and Restore (Combined command = 1 process)
    // We run diff first to get the hotswap info, then immediately checkout.
    auto ref = "HEAD~" + juce::String(undoPosition + 1);
    
    juce::StringArray undoCmd;
    undoCmd.add("/bin/sh");
    undoCmd.add("-c");
    undoCmd.add("git diff -U0 HEAD " + ref + " && git checkout -f " + ref + " -- .");
    
    auto output = runGitInternal(undoCmd);
    if (output.startsWith("fatal:")) 
    {
        DBG("ProjectState: Cannot undo (ref " + ref + " missing)");
        return {};
    }

    auto changedFiles = parseDiffOutput(output);
    undoPosition++;
    
    return changedFiles;
}

// Internal helper that allows direct access to the process runner
juce::String ProjectState::runGitInternal(const juce::StringArray& fullArgs) const
{
    DBG("ProjectState: Executing: " + fullArgs.joinIntoString(" "));

    juce::ChildProcess process;
    if (process.start(fullArgs))
    {
        auto output = process.readAllProcessOutput();
        process.waitForProcessToFinish(5000);
        return output.trim();
    }
    return {};
}

juce::Array<ProjectState::ChangedFile> ProjectState::parseDiffOutput(const juce::String& diffText)
{
    juce::Array<ChangedFile> changedFiles;
    if (diffText.isEmpty()) return changedFiles;

    juce::StringArray lines;
    lines.addTokens(diffText, "\n", "");

    ChangedFile* currentFile = nullptr;
    for (const auto& line : lines) {
        if (line.startsWith("diff --git")) {
            auto parts = juce::StringArray::fromTokens(line, " ", "");
            if (parts.size() >= 3) {
                juce::String bPath = parts[parts.size()-1];
                if (bPath.startsWith("b/")) bPath = bPath.substring(2);
                
                ChangedFile cf;
                cf.filename = bPath;
                cf.isSoftReloadOnly = bPath.endsWith(".bird");
                changedFiles.add(cf);
                currentFile = &changedFiles.getReference(changedFiles.size() - 1);
            }
        }
        else if (currentFile && currentFile->filename.endsWith(".bird") && (line.startsWith("+") || line.startsWith("-"))) {
            if (!line.startsWith("+++") && !line.startsWith("---")) {
                juce::String content = line.substring(1).trimStart();
                if (content.startsWith("plugin ") || content.startsWith("fx ") || content.startsWith("strip ")) {
                    currentFile->isSoftReloadOnly = false;
                }
            }
        }
    }
    return changedFiles;
}

juce::Array<ProjectState::ChangedFile> ProjectState::redo()
{
    if (!initialized || !canRedo()) return {};

    // For redo, we don't commit current state but we do need the diff info and restoration.
    auto ref = "HEAD~" + juce::String(undoPosition - 1);
    
    juce::StringArray redoCmd;
    redoCmd.add("/bin/sh");
    redoCmd.add("-c");
    redoCmd.add("git diff -U0 HEAD " + ref + " && git checkout -f " + ref + " -- .");
    
    auto output = runGitInternal(redoCmd);
    if (output.startsWith("fatal:")) 
    {
        DBG("ProjectState: Cannot redo (ref " + ref + " missing)");
        return {};
    }

    auto changedFiles = parseDiffOutput(output);
    undoPosition--;
    
    return changedFiles;
}

//==============================================================================
// LLM Revert
//==============================================================================

juce::Array<ProjectState::ChangedFile> ProjectState::revertLastLLM()
{
    if (!initialized) return {};

    // 1. Snapshot or Reset context
    if (undoPosition == 0) {
        commit("Save before revert", Autosave);
    } else {
        undoPosition = 0;
        restoreFilesFromCommit(getCommitHash(0));
    }

    // 2. Find the most recent LLM commit
    auto history = getHistory(50);
    juce::String targetRef;
    for (const auto& entry : history) {
        if (entry.source == LLM) {
            targetRef = entry.hash + "^";
            break;
        }
    }

    if (targetRef.isEmpty()) return {};

    // 3. Diff, Restore, and Snapshot Reversion (Combined pass)
    juce::StringArray revertCmd;
    revertCmd.add("/bin/sh");
    revertCmd.add("-c");
    
    juce::String script = "git diff -U0 HEAD " + targetRef + " && git checkout -f " + targetRef + " -- . && git reset --soft HEAD && git commit -a -m \"[user] Reverted last AI change\"";
    revertCmd.add(script);
    
    auto output = runGitInternal(revertCmd);
    auto changedFiles = parseDiffOutput(output);
    
    undoPosition = 0;
    return changedFiles;
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
