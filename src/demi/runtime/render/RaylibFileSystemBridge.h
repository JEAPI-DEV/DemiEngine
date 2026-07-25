#pragma once

namespace demi::runtime {

// Raylib's Android loader assumes every path belongs to the APK asset manager.
// DemiEngine extracts its bundled project to writable internal storage, so
// renderer resources must instead resolve through the normal filesystem.
void installRaylibFileSystemBridge();

} // namespace demi::runtime
