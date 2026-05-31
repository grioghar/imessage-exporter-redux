// Reads files out of an iTunes/Finder (MobileSync) device backup. Modern
// backups (iOS 10+) keep a plaintext `Manifest.db` (SQLite) mapping each backed
// up file to a content-addressed blob; this extracts a named file from an
// *unencrypted* backup. Encrypted backups are detected but not decrypted.
#pragma once

#include <string>
#include <vector>

namespace imsg {

struct BackupInfo {
    std::string udid;        // backup directory name (the device identifier)
    std::string path;        // absolute path to the backup directory
    bool valid = false;      // a usable Manifest.db is present
    bool encrypted = false;  // Manifest.db is encrypted (cannot read file list)
};

// Default per-platform locations that contain per-device backup folders.
std::vector<std::string> default_backup_roots();

// Backups found under the default roots plus `extra_root` (if non-empty), most
// recently modified first.
std::vector<BackupInfo> list_backups(const std::string& extra_root = "");

// Inspects a single backup directory (validity + encryption).
BackupInfo inspect_backup(const std::string& dir);

// Resolves a --backup argument to a backup directory path: an explicit path, a
// device UDID located under a default root, or "latest" for the most recently
// modified backup. Returns "" if nothing matches.
std::string resolve_backup(const std::string& spec);

// Standard (domain, relative path) of the iMessage/SMS database and the iOS
// Contacts database within a backup.
extern const char* kMessagesDomain;
extern const char* kMessagesRelativePath;
extern const char* kContactsDomain;
extern const char* kContactsRelativePath;

// Extracts the file identified by (domain, relative_path) from an unencrypted
// backup at `backup_dir`, copying it to `dest`. Returns true on success; on
// failure returns false and sets `err`.
bool extract_backup_file(const std::string& backup_dir, const std::string& domain,
                         const std::string& relative_path, const std::string& dest,
                         std::string& err);

}  // namespace imsg
