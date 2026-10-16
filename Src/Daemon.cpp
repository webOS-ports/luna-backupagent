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

#include "Daemon.h"

#include <glib.h>
#include <glib-unix.h>

#include "BackupAgent.h"

static gboolean onTermination(gpointer user_data)
{
    g_main_loop_quit(static_cast<GMainLoop*>(user_data));
    // Kept until runBackupAgent() removes both sources itself: removing this
    // one here would leave the other signal's source behind, still pointing
    // at the main loop about to be freed.
    return G_SOURCE_CONTINUE;
}

int runBackupAgent()
{
    GMainLoop* mainLoop = g_main_loop_new(NULL, FALSE);

    guint sigterm = g_unix_signal_add(SIGTERM, onTermination, mainLoop);
    guint sigint = g_unix_signal_add(SIGINT, onTermination, mainLoop);

    int status = 0;
    if (BackupAgent::instance()->init(mainLoop)) {
        g_main_loop_run(mainLoop);
    }
    else {
        g_critical("Failed to start the backup agent; com.webos.service.backupagent will not answer");
        status = 1;
    }

    // Unregisters the bus name; after a failed init(), drops a partial
    // registration (name taken, category refused) too.
    BackupAgent::destroy();

    g_source_remove(sigterm);
    g_source_remove(sigint);
    g_main_loop_unref(mainLoop);
    return status;
}
