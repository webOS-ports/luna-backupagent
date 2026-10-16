/* @@@LICENSE
*
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

#ifndef BACKUP_AGENT_DAEMON_H
#define BACKUP_AGENT_DAEMON_H

/**
 * The daemon's whole life: registers com.webos.service.backupagent, serves
 * it on the default main context until SIGTERM or SIGINT, and unregisters.
 * Returns the process exit status: 0 after a signal, 1 if the service could
 * not be registered.
 *
 * Leaves nothing behind - no main loop, no signal source, no bus handle - so
 * it can run again in the same process, as the unit tests run it.
 */
int runBackupAgent();

#endif // BACKUP_AGENT_DAEMON_H
