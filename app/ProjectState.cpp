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
        // Initialize new repo with 'main' as default branch
        git_repository_init_options initOpts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
        initOpts.initial_head = "main";
        err = git_repository_init_ext(&repo, path.c_str(), &initOpts);
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
        
        // Migrate master -> main if needed
        git_reference* masterRef = nullptr;
        if (git_reference_lookup(&masterRef, repo, "refs/heads/master") == 0)
        {
            git_reference* mainRef = nullptr;
            git_branch_move(&mainRef, masterRef, "main", true);
            if (mainRef) git_reference_free(mainRef);
            git_reference_free(masterRef);
            DBG("ProjectState: Renamed branch master -> main");
        }
    }

    initialized = true;
    DBG("ProjectState: Ready");
}

//==============================================================================
// Core Git helpers
//==============================================================================

git_oid ProjectState::createCommit(const juce::String& message, Source source)
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
    
    // Get signature — use different author names per source
    const char* authorName = "Songbird";
    if (source == User || source == Mixer) authorName = "User";
    else if (source == LLM) authorName = "Songbird AI";
    git_signature* sig = nullptr;
    git_signature_now(&sig, authorName, "songbird@local");
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

bool ProjectState::getHeadOid(git_oid* out) const
{
    if (!repo || !out) return false;
    return git_reference_name_to_id(out, repo, "HEAD") == 0;
}

bool ProjectState::getParentOid(const git_oid& commitOid, git_oid* parentOid) const
{
    if (!repo || !parentOid) return false;
    git_commit* commit = nullptr;
    if (git_commit_lookup(&commit, repo, &commitOid) != 0) return false;
    bool hasParent = git_commit_parentcount(commit) > 0;
    if (hasParent)
        git_oid_cpy(parentOid, git_commit_parent_id(commit, 0));
    git_commit_free(commit);
    return hasParent;
}

juce::String ProjectState::getCommitMessage(const git_oid& oid) const
{
    if (!repo) return {};
    git_commit* commit = nullptr;
    if (git_commit_lookup(&commit, repo, &oid) != 0) return {};
    juce::String msg = git_commit_message(commit);
    git_commit_free(commit);
    return msg;
}

bool ProjectState::moveHead(const git_oid& targetOid)
{
    if (!repo) return false;
    git_reference* ref = nullptr;
    git_reference* newRef = nullptr;
    
    // Find the current branch ref (e.g. refs/heads/main)
    if (git_repository_head(&ref, repo) != 0) return false;
    
    int err = git_reference_set_target(&newRef, ref, &targetOid, "undo/redo");
    git_reference_free(ref);
    if (newRef) git_reference_free(newRef);
    return err == 0;
}

bool ProjectState::findChildCommit(const git_oid& parentOid, const git_oid& tipOid, git_oid* childOid) const
{
    if (!repo || !childOid) return false;
    
    // Walk backward from tip toward parent, building a path
    git_oid current = tipOid;
    git_oid child = tipOid;
    
    for (int i = 0; i < 1000; ++i)  // safety limit
    {
        if (git_oid_equal(&current, &parentOid))
        {
            git_oid_cpy(childOid, &child);
            return true;
        }
        child = current;
        git_oid parent;
        if (!getParentOid(current, &parent)) return false;
        current = parent;
    }
    return false;
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
                cf.isSoftReloadOnly = true;
                
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
    git_oid headOid;
    if (!getHeadOid(&headOid))
        return {};
    
    auto changedFiles = getChangedFiles(headOid, targetOid);
    restoreWorkdirFromCommit(targetOid);
    
    return changedFiles;
}

//==============================================================================
// Commit
//==============================================================================

void ProjectState::commit(const juce::String& message, Source source, bool includeEditXml)
{
    if (!initialized) return;
    
    // Never create empty commits
    if (!hasUncommittedChanges())
    {
        DBG("ProjectState: Skipping commit (no changes) - " + message);
        return;
    }
    
    // New change invalidates redo history
    git_reference_remove(repo, "refs/redo-tip");
    
    auto fullMessage = sourceTag(source) + " " + message;
    createCommit(fullMessage.toStdString().c_str(), source);
    
    DBG("ProjectState: Committed - " + fullMessage);
}

//==============================================================================
// Undo / Redo
//==============================================================================

bool ProjectState::canUndo() const
{
    if (!initialized) return false;
    git_oid headOid;
    if (!getHeadOid(&headOid)) return false;
    
    // Check if HEAD is the project-load boundary
    auto msg = getCommitMessage(headOid);
    if (msg.contains("Project loaded") || msg.contains("Initial project state"))
        return false;
    
    git_oid parentOid;
    return getParentOid(headOid, &parentOid);
}

bool ProjectState::canRedo() const
{
    if (!initialized) return false;
    git_reference* ref = nullptr;
    bool exists = git_reference_lookup(&ref, repo, "refs/redo-tip") == 0;
    if (ref) git_reference_free(ref);
    return exists;
}

juce::Array<ProjectState::ChangedFile> ProjectState::undo()
{
    if (!initialized) return {};
    auto t0 = juce::Time::getMillisecondCounterHiRes();

    git_oid headOid;
    if (!getHeadOid(&headOid))
    {
        DBG("ProjectState: Cannot undo - no HEAD");
        return {};
    }

    // Block undo at project-load boundary
    auto msg = getCommitMessage(headOid);
    if (msg.contains("Project loaded") || msg.contains("Initial project state"))
    {
        DBG("ProjectState: Cannot undo past project load boundary");
        return {};
    }

    // Get parent
    git_oid parentOid;
    if (!getParentOid(headOid, &parentOid))
    {
        DBG("ProjectState: Cannot undo - no parent commit");
        return {};
    }

    // Save redo-tip if not already set (first undo in a sequence)
    git_reference* redoRef = nullptr;
    if (git_reference_lookup(&redoRef, repo, "refs/redo-tip") != 0)
    {
        // Create redo-tip pointing to current HEAD
        git_reference_create(&redoRef, repo, "refs/redo-tip", &headOid, false, "save redo tip");
    }
    if (redoRef) git_reference_free(redoRef);

    // Diff, restore workdir, and move HEAD
    auto changedFiles = diffAndRestore(parentOid);
    moveHead(parentOid);

    DBG("ProjectState: Undo -> " + getCommitMessage(parentOid).trimEnd());
    DBG("  git: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms]");
    
    return changedFiles;
}

juce::Array<ProjectState::ChangedFile> ProjectState::redo()
{
    if (!initialized) return {};

    // Get redo-tip ref
    git_reference* redoRef = nullptr;
    if (git_reference_lookup(&redoRef, repo, "refs/redo-tip") != 0)
        return {};  // nothing to redo
    
    git_oid tipOid = *git_reference_target(redoRef);
    git_reference_free(redoRef);

    git_oid headOid;
    if (!getHeadOid(&headOid)) return {};

    // Find the child of HEAD on the path to redo-tip
    git_oid childOid;
    if (!findChildCommit(headOid, tipOid, &childOid))
    {
        DBG("ProjectState: Cannot redo - child commit not found");
        return {};
    }

    // Diff, restore workdir, and move HEAD
    auto changedFiles = diffAndRestore(childOid);
    moveHead(childOid);

    // If we reached the tip, delete redo-tip ref
    if (git_oid_equal(&childOid, &tipOid))
        git_reference_remove(repo, "refs/redo-tip");

    DBG("ProjectState: Redo -> " + getCommitMessage(childOid).trimEnd());
    
    return changedFiles;
}

//==============================================================================
// LLM Revert
//==============================================================================

juce::Array<ProjectState::ChangedFile> ProjectState::revertLastLLM()
{
    if (!initialized) return {};

    // Find most recent LLM commit
    auto history = getHistory(50);
    for (const auto& entry : history)
    {
        if (entry.source == LLM)
        {
            git_oid llmOid;
            git_oid_fromstr(&llmOid, entry.hash.toStdString().c_str());
            
            git_oid parentOid;
            if (!getParentOid(llmOid, &parentOid))
                break;
            
            auto changedFiles = diffAndRestore(parentOid);
            
            // Commit reversion as new tip
            git_reference_remove(repo, "refs/redo-tip");
            createCommit("[user] Reverted last AI change");
            return changedFiles;
        }
    }

    return {};
}

//==============================================================================
// History
//==============================================================================

juce::Array<ProjectState::HistoryEntry> ProjectState::getHistory(int maxEntries, juce::String* outHeadHash) const
{
    juce::Array<HistoryEntry> entries;
    if (!initialized || !repo) return entries;

    // Get current HEAD hash
    git_oid headOid;
    if (getHeadOid(&headOid) && outHeadHash)
    {
        char buf[GIT_OID_SHA1_HEXSIZE + 1];
        git_oid_tostr(buf, sizeof(buf), &headOid);
        *outHeadHash = buf;
    }

    // Walk from redo-tip if it exists (shows future commits after undo),
    // otherwise walk from HEAD
    git_oid startOid = headOid;
    git_reference* redoRef = nullptr;
    if (git_reference_lookup(&redoRef, repo, "refs/redo-tip") == 0)
    {
        startOid = *git_reference_target(redoRef);
        git_reference_free(redoRef);
    }

    git_revwalk* walker = nullptr;
    git_revwalk_new(&walker, repo);
    git_revwalk_push(walker, &startOid);
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
