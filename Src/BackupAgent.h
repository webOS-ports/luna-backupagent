/* @@@LICENSE
*
*      Copyright (c) 2009-2013 LG Electronics, Inc.
*      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#ifndef BACKUP_AGENT_H
#define BACKUP_AGENT_H

#include <list>
#include <string>

#include <glib.h>
#include <luna-service2/lunaservice.h>

/**
 * Serves com.webos.service.backupagent: the system-preferences end of the
 * platform backup protocol described by
 * /etc/palm/backup/com.webos.service.backupagent.
 *
 * A backup service walks /etc/palm/backup, calls preBackup on each registered
 * service, copies away whatever files come back, and calls postRestore with
 * that same list once it has put them back.
 *
 * Split out of luna-sysmgr (where it was BackupManager, answering
 * com.palm.sysMgrDataBackup) when that component was dissolved into
 * per-function daemons.
 */
class BackupAgent
{
public:
    static BackupAgent* instance();
    static void destroy();

    bool init(GMainLoop* mainLoop);

private:
    BackupAgent();
    ~BackupAgent();

    static std::list<std::string> filesForBackup();

    static bool preBackupCallback(LSHandle* lshandle, LSMessage* message, void* user_data);
    static bool postRestoreCallback(LSHandle* lshandle, LSMessage* message, void* user_data);

    static LSMethod s_backupServerMethods[];
    static BackupAgent* s_instance;

    LSHandle* m_service;
};

#endif // BACKUP_AGENT_H
