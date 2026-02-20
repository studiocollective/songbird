#!/bin/bash
# Songbird Player — build and launch script
# Usage: ./utils/launch.sh [--skip-build]

set -e
cd "$(dirname "$0")/.."

SKIP_BUILD=false
SKETCH_NAME=""

for arg in "$@"; do
    if [ "$arg" == "--skip-build" ]; then
        SKIP_BUILD=true
    else
        SKETCH_NAME="$arg"
    fi
done

# Colors
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}🐦 Songbird Player${NC}"
echo ""

# Step 1: Install React UI deps if needed
if [ ! -d "react_ui/node_modules" ]; then
    echo -e "${GREEN}Installing React UI dependencies...${NC}"
    cd react_ui && npm install && cd ..
fi

# Step 2: CMake configure if needed
if [ ! -d "build" ]; then
    echo -e "${GREEN}Configuring CMake...${NC}"
    cmake -B build -DCMAKE_BUILD_TYPE=Debug
fi

# Step 3: Build JUCE app
if [ "$SKIP_BUILD" = false ]; then
    echo -e "${GREEN}Building Songbird Player...${NC}"
    cmake --build build --target SongbirdPlayer -j$(sysctl -n hw.ncpu)
fi

# Step 3b: Inject NSAppTransportSecurity so WebView can load http://localhost
APP_PLIST="build/SongbirdPlayer_artefacts/Songbird Player.app/Contents/Info.plist"
if [ -f "$APP_PLIST" ]; then
    /usr/libexec/PlistBuddy -c "Add :NSAppTransportSecurity dict" "$APP_PLIST" 2>/dev/null || true
    /usr/libexec/PlistBuddy -c "Add :NSAppTransportSecurity:NSAllowsArbitraryLoads bool true" "$APP_PLIST" 2>/dev/null || true
    /usr/libexec/PlistBuddy -c "Add :NSAppTransportSecurity:NSAllowsLocalNetworking bool true" "$APP_PLIST" 2>/dev/null || true
    echo "  ✓ App Transport Security configured for localhost"
fi

# Step 3c: Ad-hoc sign with entitlements to prevent repeated microphone prompts
if [ -f "app/Entitlements.plist" ]; then
    echo -e "${GREEN}Signing app with entitlements...${NC}"
    codesign -s - --force --deep --entitlements "app/Entitlements.plist" "build/SongbirdPlayer_artefacts/Debug/Songbird Player.app"
    echo "  ✓ App signed with microphone entitlements"
fi

# Step 4: Start Vite dev server in background
echo -e "${GREEN}Starting React UI dev server...${NC}"
# Kill any lingering Songbird Player and Vite servers from previous runs
pkill -f "Songbird Player" 2>/dev/null || true
pkill -f "node.*vite" 2>/dev/null || true
sleep 0.3
cd react_ui
npm run dev &
VITE_PID=$!
cd ..

# Wait for Vite to be ready
echo -n "Waiting for Vite..."
for i in {1..20}; do
    if curl -s http://localhost:5173 > /dev/null 2>&1; then
        echo " ready!"
        break
    fi
    echo -n "."
    sleep 0.5
done

# Step 5: Launch the app (run binary directly so logs appear in terminal)
APP_BIN="build/SongbirdPlayer_artefacts/Debug/Songbird Player.app/Contents/MacOS/Songbird Player"
echo -e "${GREEN}Launching Songbird Player...${NC}"
if [ -n "$SKETCH_NAME" ]; then
    "$APP_BIN" "$SKETCH_NAME" &
    APP_PID=$!
    echo "  Opening sketch: $SKETCH_NAME"
else
    "$APP_BIN" &
    APP_PID=$!
fi

echo ""
echo -e "${CYAN}Songbird Player is running!${NC}"
echo "  React UI: http://localhost:5173 (hot reload)"
echo "  Press Ctrl+C to stop"
echo ""

# Keep script running; Ctrl+C kills both app and Vite
trap "kill $APP_PID $VITE_PID 2>/dev/null; exit" INT TERM
wait $APP_PID $VITE_PID
