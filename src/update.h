#ifndef UPDATE_H_
#define UPDATE_H_

#include <string>

/**
 * @brief Check GitHub releases for a newer version and optionally download +
 * replace.
 *
 * @param current_version  The current version string (e.g. "v1.0.0" or git
 *                         describe output).
 * @param repo_owner       GitHub repository owner.
 * @param repo_name        GitHub repository name.
 * @param asset_name       The asset filename to look for in the release
 *                         (e.g. "vsrun.exe").
 * @return true if an update was performed (process should exit afterwards).
 * @return false if no update is needed or the user declined.
 */
bool check_for_update(const std::string& current_version,
                      const std::string& repo_owner,
                      const std::string& repo_name,
                      const std::string& asset_name);

#endif  // UPDATE_H_
