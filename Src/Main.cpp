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

#include <glib.h>
#include <glib-unix.h>

#include "BackupAgent.h"

static gboolean onTermination(gpointer user_data)
{
    g_main_loop_quit(static_cast<GMainLoop*>(user_data));
    return G_SOURCE_REMOVE;
}

int main(int argc, char** argv)
{
    GMainLoop* mainLoop = g_main_loop_new(NULL, FALSE);

    g_unix_signal_add(SIGTERM, onTermination, mainLoop);
    g_unix_signal_add(SIGINT, onTermination, mainLoop);

    if (!BackupAgent::instance()->init(mainLoop)) {
        g_critical("Failed to start the backup agent; com.webos.service.backupagent will not answer");
        g_main_loop_unref(mainLoop);
        return 1;
    }

    g_main_loop_run(mainLoop);

    g_main_loop_unref(mainLoop);
    return 0;
}
