#!/bin/bash
# Syncs README.md files to the docs/ directory as index.mdx for Mintlify
# Usage: ./utils/sync_docs.sh

set -e
cd "$(dirname "$0")/.."

echo "Syncing README.md files to docs/ folder for Mintlify..."

# Directories to search for README.md files
TARGET_DIRS=("app" "react_ui" "utils" "tools" "eval" "files" "embedded" "libraries")
EXCLUDED_DIRS=("libraries/json" "libraries/tracktion" "libraries/websocketpp" "libraries/asio" "libraries/magenta")

# Clean up previously synced files
for dir in "${TARGET_DIRS[@]}"; do
    rm -rf "docs/$dir"
done
rm -f "docs/index.mdx"

for dir in "${TARGET_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        # Find all README.md files within the target directory, excluding node_modules
        find "$dir" -name "node_modules" -prune -o -type f -name "README.md" -print | while read -r file; do
            # Check exclusions
            exclude=false
            for ex in "${EXCLUDED_DIRS[@]}"; do
                if [[ "$file" == "$ex"* ]]; then
                    exclude=true
                    break
                fi
            done
            
            if [ "$exclude" = true ]; then
                continue
            fi
            
            # Get the directory path relative to the repository root
            dir_path=$(dirname "$file")
            
            # Create the corresponding directory in docs/
            mkdir -p "docs/$dir_path"
            
            # Copy the file and rename it to index.mdx
            # We use index.mdx so the URL will be cleanly mapped to the directory name
            cp "$file" "docs/$dir_path/index.mdx"
            echo "  Copied $file -> docs/$dir_path/index.mdx"
        done
    fi
done

# Copy the root README.md to docs/index.mdx as well
if [ -f "README.md" ]; then
    mkdir -p "docs"
    cp "README.md" "docs/index.mdx"
    echo "  Copied README.md -> docs/index.mdx"
fi

echo "Sync complete!"
