#!/bin/bash

set -e  # Exit on error

# Get current date in YYYY_MM_DD format
DATE=$(date +%Y_%m_%d)
APPIMAGE_NAME="vulkan_sdl3_app-${DATE}-x86_64.AppImage"

echo "Creating AppImage for Vulkan SDL3 app..."
echo ""
echo "IMPORTANT: For maximum portability, build on an older system (e.g., Ubuntu 20.04)"
echo "or use a container with older GLIBC to avoid version conflicts on target systems."
echo ""

# Compile shaders
echo "Compiling shaders..."
./compile_shaders.sh

# Build the application with release optimizations (validation layers disabled)
echo "Building application (Release mode without validation layers)..."
./update_file_list.sh
mkdir -p build
cd build
cmake -D CMAKE_BUILD_TYPE=Release -D ENABLE_VALIDATION_LAYERS=OFF .. || { echo "ERROR: cmake failed"; cd ..; exit 1; }
make || { echo "ERROR: make failed"; cd ..; exit 2; }
cd ..

# Check if build was successful
if [ ! -f "dist/main" ]; then
    echo "ERROR: Build failed - dist/main not found"
    exit 1
fi

# Create AppDir structure in build directory
APPDIR="build/vulkan_app.AppDir"
echo "Creating AppDir structure..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

# Copy the application binary
echo "Copying application binary..."
cp "dist/main" "$APPDIR/usr/bin/vulkan_app"

# Copy shaders
echo "Copying shaders..."
cp -r dist/shaders "$APPDIR/usr/bin/"

# Copy required libraries
echo "Bundling dependencies..."
# Helper function to copy library and its dependencies
copy_lib() {
    local lib="$1"
    # Try /usr/lib first, then /lib/x86_64-linux-gnu
    if [ -f "/usr/lib/$lib" ]; then
        cp "/usr/lib/$lib" "$APPDIR/usr/lib/" && echo "  Copied $lib from /usr/lib"
    elif [ -f "/lib/x86_64-linux-gnu/$lib" ]; then
        cp "/lib/x86_64-linux-gnu/$lib" "$APPDIR/usr/lib/" && echo "  Copied $lib from /lib/x86_64-linux-gnu"
    else
        echo "  Warning: $lib not found (skipping)"
    fi
}

# Try to copy libraries
copy_lib "libSDL3.so.0"
copy_lib "libsndio.so.7"
copy_lib "libxkbcommon.so.0"
copy_lib "libdecor-0.so.0"
copy_lib "libwayland-client.so.0"
copy_lib "libwayland-cursor.so.0"
copy_lib "libm.so.6"
copy_lib "libstdc++.so.6"
copy_lib "libgcc_s.so.1"

# Note: Vulkan libraries are typically not bundled as they should come from the driver
echo "  Note: Vulkan libraries not bundled (should use system drivers)"
echo ""
echo "Checking GLIBC version used by binary..."
objdump -T "$APPDIR/usr/bin/vulkan_app" | grep GLIBC | sed 's/.*GLIBC_/GLIBC_/' | sort -V | uniq | tail -1

# Create a simple icon (placeholder - ideally you'd have a proper icon)
echo "Creating placeholder icon..."
# This creates a simple 256x256 PNG with ImageMagick if available
if command -v convert &> /dev/null; then
    convert -size 256x256 xc:blue -gravity center \
        -pointsize 40 -fill white -annotate +0+0 "Vulkan\nSDL3" \
        "$APPDIR/usr/share/icons/hicolor/256x256/apps/vulkan_app.png"
    cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/vulkan_app.png" "$APPDIR/vulkan_app.png"
else
    echo "  Warning: ImageMagick not found - no icon will be created"
    echo "  You may want to add a proper icon later"
fi

# Create .desktop file
echo "Creating desktop file..."
cat > "$APPDIR/vulkan_app.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Vulkan SDL3 App
Exec=vulkan_app
Icon=vulkan_app
Categories=Graphics;
Terminal=false
EOF

# Create AppRun script
echo "Creating AppRun script..."
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash

# Get the directory containing this script (the AppImage mount point)
APPDIR="$(dirname "$(readlink -f "$0")")"

# Set library path to include bundled libraries
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH}"

# Change to the directory containing the binary so relative paths work
cd "${APPDIR}/usr/bin"

# Run the application
exec "${APPDIR}/usr/bin/vulkan_app" "$@"
EOF

# Make AppRun executable
chmod +x "$APPDIR/AppRun"

# Download appimagetool if not present (to build directory)
if [ ! -f "build/appimagetool-x86_64.AppImage" ]; then
    echo "Downloading appimagetool..."
    wget -q "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" -O build/appimagetool-x86_64.AppImage
    chmod +x build/appimagetool-x86_64.AppImage
fi

# Build the AppImage in build directory
echo "Building AppImage..."
ARCH=x86_64 ./build/appimagetool-x86_64.AppImage "$APPDIR" "build/$APPIMAGE_NAME"

# Clean up AppDir
rm -rf "$APPDIR"

# Create dist directory if it doesn't exist and copy AppImage
mkdir -p dist
echo "Copying AppImage to dist..."
cp "build/$APPIMAGE_NAME" "dist/$APPIMAGE_NAME"

echo ""
echo "========================================"
echo "AppImage created successfully!"
echo "Location: dist/$APPIMAGE_NAME"
echo "========================================"
echo ""
echo "IMPORTANT NOTES:"
echo "1. This AppImage requires Vulkan drivers on the target system"
echo "2. If you get GLIBC version errors on older systems:"
echo "   - Build the AppImage on an older Linux distribution (e.g., Ubuntu 20.04)"
echo "   - Or use a Docker container with an older base image"
echo "   - Example: docker run -v \$(pwd):/workspace ubuntu:20.04 /workspace/make_appimage.sh"
echo ""
echo "To run: ./dist/$APPIMAGE_NAME"
