#include "ProjectState.h"
#include <thread>

//==============================================================================
// ProjectState — libgit2 in-process Git for undo/redo
//==============================================================================

ProjectState::ProjectState()
{
    git_libgit2_init();
}

ProjectState::~ProjectState()
{
    if (repo)
        git_repository_free(repo);
    git_libgit2_shutdown();
}

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
    std::thread([this]() { initRepo(); }).detach();
}

//==============================================================================
// Init
//==============================================================================

void ProjectState::initRepo()
{
    if (!projectDir.isDirectory()) return;

    auto path = projectDir.getFullPathName().toStdString();
    
    // Try to open existing repo first
    int err = git_repository_open(&repo, path.c_str());
    
    if (err < 0)
    {
        // Initialize new repo
        err = git_repository_init(&repo, path.c_str(), 0);
        if (err < 0)
        {
            DBG("ProjectState: Failed to init repo: " + juce::String(git_error_last()->message));
            return;
        }
        
        DBG("ProjectState: Initialized new repo in " + projectDir.getFullPathName());
        
        // Set user config
        git_config* cfg = nullptr;
        git_repository_config(&cfg, repo);
        if (cfg)
        {
            git_config_set_string(cfg, "user.email", "songbird@local");
            git_config_set_string(cfg, "user.name", "Songbird");
            git_config_free(cfg);
        }
        
        // Create .gitignore
        auto gitignore = projectDir.getChildFile(".gitignore");
        if (!gitignore.existsAsFile())
        {
            gitignore.replaceWithText(
                ".DS_Store\n"
                "*.edit.xml\n"
                "*.session.json\n"
                "*.wav\n"
                "*.mp3\n"
                "*.aif\n"
                "*.aiff\n"
            );
        }
        
        // Initial commit
        initialized = true;
        createCommit("[auto] Initial project state");
        DBG("ProjectState: Initial commit done");
    }
    else
    {
        DBG("ProjectState: Opened existing repo in " + projectDir.getFullPathName());
    }

    initialized = true;
    DBG("ProjectState: Ready");
}

//==============================================================================
// Core Git helpers
//==============================================================================

git_oid ProjectState::createCommit(const juce::String& message)
{
    git_oid oid = {};
    if (!repo) return oid;
    
    // Stage all files
    git_index* index = nullptr;
    git_repository_index(&index, repo);
    if (!index) return oid;
    
    git_strarray pathspec = { nullptr, 0 };
    const char* matchAll = ".";
    pathspec.strings = const_cast<char**>(&matchAll);
    pathspec.count = 1;
    
    git_index_add_all(index, &pathspec, 0, nullptr, nullptr);
    git_index_write(index);
    
    // Write index to tree
    git_oid treeOid;
    git_index_write_tree(&treeOid, index);
    git_index_free(index);
    
    git_tree* tree = nullptr;
    git_tree_lookup(&tree, repo, &treeOid);
    if (!tree) return oid;
    
    // Get signature
    git_signature* sig = nullptr;
    git_signature_now(&sig, "Songbird", "songbird@local");
    if (!sig) { git_tree_free(tree); return oid; }
    
    // Get parent commit (HEAD), if any
    git_commit* parent = nullptr;
    git_reference* headRef = nullptr;
    const git_commit* parents[1] = { nullptr };
    int nParents = 0;
    
    if (git_repository_head(&headRef, repo) == 0)
    {
        git_oid parentOid;
        git_reference_name_to_id(&parentOid, repo, "HEAD");
        git_commit_lookup(&parent, repo, &parentOid);
        parents[0] = parent;
        nParents = 1;
    }
    
    auto msgStr = message.toStdString();
    
    git_commit_create(
        &oid,
        repo,
        "HEAD",
        sig, sig,
        nullptr,
        msgStr.c_str(),
        tree,
        nParents,
        parents);
    
    if (parent) git_commit_free(parent);
    if (headRef) git_reference_free(headRef);
    git_tree_free(tree);
    git_signature_free(sig);
    
    return oid;
}

bool ProjectState::resolveCommitOid(int offset, git_oid* out) const
{
    if (!repo || !out) return false;
    
    juce::String spec = "HEAD";
    if (offset > 0)
        spec += "~" + juce::String(offset);
    
    git_object* obj = nullptr;
    int err = git_revparse_single(&obj, repo, spec.toStdString().c_str());
    
    if (err < 0)
        return false;
    
    git_oid_cpy(out, git_object_id(obj));
    git_object_free(obj);
    return true;
}

bool ProjectState::hasUncommittedChanges() const
{
    if (!repo) return false;
    
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED;
    
    git_status_list* statusList = nullptr;
    git_status_list_new(&statusList, repo, &opts);
    
    if (!statusList) return false;
    
    size_t count = git_status_list_entrycount(statusList);
    git_status_list_free(statusList);
    
    return count > 0;
}

juce::Array<ProjectState::ChangedFile> ProjectState::getChangedFiles(const git_oid& fromOid, const git_oid& toOid) const
{
    juce::Array<ChangedFile> changedFiles;
    if (!repo) return changedFiles;
    
    git_commit* fromCommit = nullptr;
    git_commit* toCommit = nullptr;
    git_tree* fromTree = nullptr;
    git_tree* toTree = nullptr;
    
    git_commit_lookup(&fromCommit, repo, &fromOid);
    git_commit_lookup(&toCommit, repo, &toOid);
    
    if (fromCommit) git_commit_tree(&fromTree, fromCommit);
    if (toCommit) git_commit_tree(&toTree, toCommit);
    
    git_diff* diff = nullptr;
    git_diff_options diffOpts = GIT_DIFF_OPTIONS_INIT;
    diffOpts.context_lines = 0;
    
    git_diff_tree_to_tree(&diff, repo, fromTree, toTree, &diffOpts);
    
    if (diff)
    {
        size_t numDeltas = git_diff_num_deltas(diff);
        for (size_t i = 0; i < numDeltas; i++)
        {
            const git_diff_delta* delta = git_diff_get_delta(diff, i);
            
            ChangedFile cf;
            cf.filename = delta->new_file.path;
            
            if (cf.filename.endsWith(".bird"))
            {
                // Check if only note/velocity lines changed (soft reload)
                cf.isSoftReloadOnly = true;
                
                // Get the patch to inspect line content
                git_patch* patch = nullptr;
                git_patch_from_diff(&patch, diff, i);
                
                if (patch)
                {
                    size_t nHunks = git_patch_num_hunks(patch);
                    for (size_t h = 0; h < nHunks && cf.isSoftReloadOnly; h++)
                    {
                        size_t nLines = 0;
                        const git_diff_hunk* hunk = nullptr;
                        git_patch_get_hunk(&hunk, &nLines, patch, h);
                        
                        for (size_t l = 0; l < nLines && cf.isSoftReloadOnly; l++)
                        {
                            const git_diff_line* line = nullptr;
                            git_patch_get_line_in_hunk(&line, patch, h, l);
                            
                            if (line && (line->origin == GIT_DIFF_LINE_ADDITION || 
                                         line->origin == GIT_DIFF_LINE_DELETION))
                            {
                                juce::String content(line->content, (size_t)line->content_len);
                                content = content.trimStart();
                                
                                if (content.startsWith("plugin ") || 
                                    content.startsWith("fx ") || 
                                    content.startsWith("strip "))
                                {
                                    cf.isSoftReloadOnly = false;
                                }
                            }
                        }
                    }
                    git_patch_free(patch);
                }
            }
            
            changedFiles.add(cf);
        }
        git_diff_free(diff);
    }
    
    if (fromTree) git_tree_free(fromTree);
    if (toTree) git_tree_free(toTree);
    if (fromCommit) git_commit_free(fromCommit);
    if (toCommit) git_commit_free(toCommit);
    
    return changedFiles;
}

void ProjectState::restoreWorkdirFromCommit(const git_oid& commitOid)
{
    if (!repo) return;
    
    git_commit* commit = nullptr;
    git_commit_lookup(&commit, repo, &commitOid);
    if (!commit) return;
    
    git_tree* tree = nullptr;
    git_commit_tree(&tree, commit);
    
    if (tree)
    {
        git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
        opts.checkout_strategy = GIT_CHECKOUT_FORCE;
        
        git_checkout_tree(repo, (git_object*)tree, &opts);
        git_tree_free(tree);
    }
    
    git_commit_free(commit);
}

juce::Array<ProjectState::ChangedFile> ProjectState::diffAndRestore(const git_oid& targetOid)
{
    // Get current HEAD oid
    git_oid headOid;
    if (!resolveCommitOid(0, &headOid))
        return {};
    
    // Get the diff (what changed between HEAD and target)
    auto changedFiles = getChangedFiles(headOid, targetOid);
    
    // Restore the working directory
    restoreWorkdirFromCommit(targetOid);
    
    return changedFiles;
}

//==============================================================================
// Commit
//==============================================================================

void ProjectState::commit(const juce::String& message, Source source, bool includeEditXml)
{
    if (!initialized) return;
    
    auto fullMessage = sourceTag(source) + " " + message;
    createCommit(fullMessage.toStdString().c_str());
    
    // Only reset undo position for user-initiated or AI commits.
    if (source != Autosave)
        undoPosition = 0;
    
    DBG("ProjectState: Committed - " + fullMessage);
}

//==============================================================================
// Undo / Redo
//==============================================================================

bool ProjectState::canUndo() const
{
    if (!initialized) return false;
    git_oid oid;
    return resolveCommitOid(undoPosition + 1, &oid);
}

bool ProjectState::canRedo() const
{
    return initialized && undoPosition > 0;
}

juce::Array<ProjectState::ChangedFile> ProjectState::undo()
{
    if (!initialized) return {};
    auto t0 = juce::Time::getMillisecondCounterHiRes();

    // 1. Snapshot if at tip
    if (undoPosition == 0 && hasUncommittedChanges())
        commit("Save before undo", Autosave);
    DBG("  git: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] commit");

    // 2. Resolve target
    int targetIndex = undoPosition + 1;
    git_oid targetOid;
    if (!resolveCommitOid(targetIndex, &targetOid))
    {
        DBG("ProjectState: Cannot undo - reached beginning of history");
        return {};
    }
    DBG("  git: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] resolve");

    // 3. Diff and restore (all in-process, zero fork)
    auto changedFiles = diffAndRestore(targetOid);
    undoPosition = targetIndex;
    DBG("  git: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] diff+restore");
    
    return changedFiles;
}

juce::Array<ProjectState::ChangedFile> ProjectState::redo()
{
    if (!initialized || undoPosition <= 0) return {};

    int targetIndex = undoPosition - 1;
    git_oid targetOid;
    if (!resolveCommitOid(targetIndex, &targetOid))
        return {};

    auto changedFiles = diffAndRestore(targetOid);
    undoPosition = targetIndex;
    
    return changedFiles;
}

//==============================================================================
// LLM Revert
//==============================================================================

juce::Array<ProjectState::ChangedFile> ProjectState::revertLastLLM()
{
    if (!initialized) return {};

    // 1. Snapshot or reset
    if (undoPosition == 0)
        commit("Save before revert", Autosave);
    else
    {
        undoPosition = 0;
        git_oid headOid;
        if (resolveCommitOid(0, &headOid))
            restoreWorkdirFromCommit(headOid);
    }

    // 2. Find most recent LLM commit
    auto history = getHistory(50);
    juce::String targetHashStr;
    for (const auto& entry : history)
    {
        if (entry.source == LLM)
        {
            // Want the parent of the LLM commit
            git_oid llmOid;
            git_oid_fromstr(&llmOid, entry.hash.toStdString().c_str());
            
            git_commit* llmCommit = nullptr;
            git_commit_lookup(&llmCommit, repo, &llmOid);
            
            if (llmCommit && git_commit_parentcount(llmCommit) > 0)
            {
                const git_oid* parentOid = git_commit_parent_id(llmCommit, 0);
                auto changedFiles = diffAndRestore(*parentOid);
                git_commit_free(llmCommit);
                
                // Commit reversion as new tip
                createCommit("[user] Reverted last AI change");
                undoPosition = 0;
                return changedFiles;
            }
            
            if (llmCommit) git_commit_free(llmCommit);
            break;
        }
    }

    return {};
}

//==============================================================================
// History
//==============================================================================

juce::Array<ProjectState::HistoryEntry> ProjectState::getHistory(int maxEntries) const
{
    juce::Array<HistoryEntry> entries;
    if (!initialized || !repo) return entries;

    git_revwalk* walker = nullptr;
    git_revwalk_new(&walker, repo);
    git_revwalk_push_head(walker);
    git_revwalk_sorting(walker, GIT_SORT_TIME);

    git_oid oid;
    int count = 0;
    
    while (git_revwalk_next(&oid, walker) == 0 && count < maxEntries)
    {
        git_commit* commit = nullptr;
        if (git_commit_lookup(&commit, repo, &oid) != 0)
            continue;

        HistoryEntry entry;
        
        char hashBuf[GIT_OID_SHA1_HEXSIZE + 1];
        git_oid_tostr(hashBuf, sizeof(hashBuf), &oid);
        entry.hash = hashBuf;
        
        entry.message = git_commit_message(commit);
        
        git_time_t time = git_commit_time(commit);
        entry.timestamp = juce::Time(time * 1000).toString(true, true);
        
        if (entry.message.startsWith("[LLM]"))       entry.source = LLM;
        else if (entry.message.startsWith("[user]"))  entry.source = User;
        else if (entry.message.startsWith("[mixer]")) entry.source = Mixer;
        else                                          entry.source = Autosave;

        entries.add(entry);
        git_commit_free(commit);
        count++;
    }

    git_revwalk_free(walker);
    return entries;
}
