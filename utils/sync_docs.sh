#!/bin/bash
# Syncs README.md files to the docs/ directory as named .mdx files for Mintlify
# Usage: ./utils/sync_docs.sh
#
# Naming convention:
#   Root README.md       → docs/index.mdx  (homepage, sidebarTitle: "Introduction")
#   app/README.md        → docs/app.mdx
#   app/bridge/README.md → docs/app/bridge.mdx
#
# Each file gets YAML frontmatter injected with a `title` extracted from the
# first `# Heading` in the README. This controls the Mintlify sidebar label.

set -e
cd "$(dirname "$0")/.."

echo "Syncing README.md files to docs/ folder for Mintlify..."

# Directories to search for README.md files
TARGET_DIRS=("app" "react_ui" "utils" "tools" "eval" "files" "embedded" "libraries")
EXCLUDED_DIRS=("libraries/json" "libraries/tracktion" "libraries/websocketpp" "libraries/asio" "libraries/magenta")

# Inject YAML frontmatter with a title extracted from the first # heading.
# Usage: inject_frontmatter <source_file> <dest_file> [sidebarTitle]
inject_frontmatter() {
    local src="$1"
    local dest="$2"
    local sidebar_title="$3"

    # Extract the first # heading (removing the leading "# ")
    local title
    title=$(grep -m1 '^# ' "$src" | sed 's/^# //')

    if [ -n "$title" ]; then
        {
            echo "---"
            echo "title: \"$title\""
            if [ -n "$sidebar_title" ]; then
                echo "sidebarTitle: \"$sidebar_title\""
            fi
            echo "---"
            echo ""
            cat "$src"
        } > "$dest"
    else
        # No heading found — copy as-is
        cp "$src" "$dest"
    fi
}

# Clean up previously synced files
for dir in "${TARGET_DIRS[@]}"; do
    rm -rf "docs/$dir"
done
rm -f "docs/index.mdx"

# Also clean up any top-level .mdx files that were previously synced from target dirs
for dir in "${TARGET_DIRS[@]}"; do
    rm -f "docs/$dir.mdx"
done

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
            
            # Extract the folder name (last component of the path)
            folder_name=$(basename "$dir_path")
            
            # Get the parent of the folder
            parent_path=$(dirname "$dir_path")
            
            # Create the parent directory in docs/
            mkdir -p "docs/$parent_path"
            
            # Copy README.md → <folder-name>.mdx with frontmatter
            # e.g. app/bridge/README.md → docs/app/bridge.mdx
            inject_frontmatter "$file" "docs/$parent_path/$folder_name.mdx"
            echo "  Copied $file -> docs/$parent_path/$folder_name.mdx"
        done
    fi
done

# Copy the root README.md to docs/index.mdx (homepage)
if [ -f "README.md" ]; then
    mkdir -p "docs"
    inject_frontmatter "README.md" "docs/index.mdx" "Introduction"
    echo "  Copied README.md -> docs/index.mdx"
fi

# Copy images/ into docs/ so relative image paths resolve in Mintlify
if [ -d "images" ]; then
    rm -rf "docs/images"
    cp -r "images" "docs/images"
    echo "  Copied images/ -> docs/images/"
fi

echo "Sync complete!"
