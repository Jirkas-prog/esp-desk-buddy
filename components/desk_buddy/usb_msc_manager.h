#pragma once
// Questie eyes retain their original source. This device uses NVS/web settings,
// not Questie's USB disk or quest files, so the original defaults are used.
#define USB_MSC_MOUNT_POINT "/unused"
#define USB_MSC_SYSTEM_DIR "/unused/system"
inline bool usb_msc_ensure_vfs_mounted() { return false; }
inline void usb_msc_storage_lock() {}
inline void usb_msc_storage_unlock() {}
