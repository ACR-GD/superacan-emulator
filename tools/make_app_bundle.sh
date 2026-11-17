#!/bin/bash
# Usage: make_app_bundle.sh <binary> <bundle path>
set -euo pipefail
BIN="$1"; APP="$2"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/SuperACan"
[ -f resources/AppIcon.icns ] || python3 tools/make_icon.py resources/AppIcon.icns
cp resources/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
cat > "$APP/Contents/Info.plist" << 'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>               <string>Super A'Can</string>
    <key>CFBundleDisplayName</key>        <string>Super A'Can Emulator</string>
    <key>CFBundleIdentifier</key>         <string>com.superacan.emulator</string>
    <key>CFBundleExecutable</key>         <string>SuperACan</string>
    <key>CFBundleIconFile</key>           <string>AppIcon</string>
    <key>CFBundlePackageType</key>        <string>APPL</string>
    <key>CFBundleShortVersionString</key> <string>3.0</string>
    <key>CFBundleVersion</key>            <string>3</string>
    <key>LSMinimumSystemVersion</key>     <string>12.0</string>
    <key>LSApplicationCategoryType</key>  <string>public.app-category.games</string>
    <key>NSHighResolutionCapable</key>    <true/>
    <key>NSPrincipalClass</key>           <string>NSApplication</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>       <string>Super A'Can ROM</string>
            <key>CFBundleTypeRole</key>       <string>Viewer</string>
            <key>LSHandlerRank</key>          <string>Alternate</string>
            <key>CFBundleTypeExtensions</key> <array><string>bin</string></array>
            <key>LSItemContentTypes</key>     <array><string>public.data</string></array>
        </dict>
    </array>
</dict>
</plist>
PLIST
printf 'APPL????' > "$APP/Contents/PkgInfo"
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
echo "Built $APP"
