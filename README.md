luna-backupagent
================

Summary
-------
The system-preferences end of the LuneOS backup protocol, serving com.webos.service.backupagent

A backup service walks `/etc/palm/backup`, calls `preBackup` on every service
registered there, copies away the files each returns, and calls `postRestore`
once it has written them back. This daemon answers for the loose preference
files nothing else covers: Just Type preferences, `localeInfo`, and the
per-service files under `/var/preferences`.

History
-------

Split out of [luna-sysmgr](https://github.com/webOS-ports/luna-sysmgr), where
it was `BackupManager` answering `com.palm.sysMgrDataBackup`. The bus name
changed with the split; the registration file installed to
`/etc/palm/backup/com.webos.service.backupagent` is what a backup service
actually walks, so nothing else needs to know the old name.

Not covered here, deliberately:

* The launcher layout lives in db8 (`org.webosports.lunalauncher:1`,
  `org.webosports.lunalaunchertab:1`, both `"sync": true`) and travels with
  the database backup.
* `/var/luna/preferences/systemprefs.db` is already backed up by
  `com.webos.service.systemservice` as `systemprefs_backup.db`.

API
---

* `com.webos.service.backupagent/preBackup` — returns the list of files to back up.
* `com.webos.service.backupagent/postRestore` — acknowledges a completed restore.

Build
-----

Standard webOS CMake component; depends on glib-2.0, luna-service2 and json-c.

# Copyright and License Information

Copyright (c) 2009-2013 LG Electronics, Inc.
Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this content except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
