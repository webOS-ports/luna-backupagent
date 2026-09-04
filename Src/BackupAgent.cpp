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

#include "BackupAgent.h"

#include <json.h>
#include <glib.h>
#include <string>

/*
 * Continues luna-sysmgr's BackupManager, which itself was restored from
 * openwebos/luna-sysmgr after this fork had dropped it along with the old
 * launcher UI. Two things changed on the way into this daemon:
 *
 *  - The bus name. com.palm.sysMgrDataBackup named the process that served
 *    it; with LunaSysMgr gone the name would be a fossil. The registration
 *    file under /etc/palm/backup is what a backup service actually walks, so
 *    the name changes with the file and nothing else has to know.
 *
 *  - No LunaSysMgrCommon. The original leaned on HostBase for its main loop
 *    and on JSONUtils for schema validation; a daemon this size needs glib,
 *    luna-service2 and json-c and nothing else.
 *
 * The launcher layout is NOT missing from backups - do not add a file for it
 * here. Cardshell keeps it in db8, under the kinds
 * org.webosports.lunalauncher:1 (launch bar) and
 * org.webosports.lunalaunchertab:1 (full launcher tabs), both declared
 * "sync": true, which is precisely the flag db8's own dump() filters on when
 * the backup service calls com.palm.db/internal/preBackup. It travels with
 * the database. /etc/palm/default-launcher-page-layout.json is the read-only
 * default the image ships, not user state, and does not belong in a backup
 * either.
 *
 * So the list below is what this daemon's side of the system actually keeps
 * and nothing else already covers. Deliberately absent:
 * /var/luna/preferences/systemprefs.db, which com.webos.service.systemservice
 * already hands over as systemprefs_backup.db - backing it up twice would
 * restore an older copy over a newer one depending on file order.
 */

static const char* kServiceName = "com.webos.service.backupagent";

BackupAgent* BackupAgent::s_instance = NULL;

LSMethod BackupAgent::s_backupServerMethods[] = {
    { "preBackup",   BackupAgent::preBackupCallback },
    { "postRestore", BackupAgent::postRestoreCallback },
    { 0, 0 }
};

/**
 * Files handed to the backup service, in the order they are restored.
 *
 * Anything here must be safe to overwrite while the system is running:
 * restore asks the user to reboot afterwards, and nothing below is read
 * again before that.
 */
static const char* const s_candidateFiles[] = {
    "/var/luna/preferences/universalsearchprefs.db",  // Just Type preferences
    "/var/luna/preferences/localeInfo",               // locale, region, keyboard
    "/var/preferences/com.palm.display",
    "/var/preferences/com.palm.sleep",
    "/var/preferences/com.palm.telephony",
    "/var/preferences/com.webos.service.battery",
    "/var/preferences/com.webos.service.location",
    "/var/preferences/com.webos.service.wifi",
    NULL
};

BackupAgent::BackupAgent()
    : m_mainLoop(NULL)
    , m_service(NULL)
{
}

BackupAgent::~BackupAgent()
{
    if (m_service) {
        LSError error;
        LSErrorInit(&error);

        if (!LSUnregister(m_service, &error)) {
            g_warning("Failed unregistering backup service: %s", error.message);
            LSErrorFree(&error);
        }
        m_service = NULL;
    }
}

BackupAgent* BackupAgent::instance()
{
    if (NULL == s_instance)
        s_instance = new BackupAgent();

    return s_instance;
}

bool BackupAgent::init(GMainLoop* mainLoop)
{
    g_assert(m_mainLoop == NULL);    // Only initialize once.
    m_mainLoop = mainLoop;

    LSError error;
    LSErrorInit(&error);

    if (!LSRegister(kServiceName, &m_service, &error)) {
        g_warning("Failed registering on service bus: %s", error.message);
        LSErrorFree(&error);
        return false;
    }

    if (!LSRegisterCategory(m_service, "/", s_backupServerMethods, NULL, NULL, &error)) {
        g_warning("Failed registering with service bus category: %s", error.message);
        LSErrorFree(&error);
        return false;
    }

    if (!LSGmainAttach(m_service, m_mainLoop, &error)) {
        g_warning("Failed attaching to service bus: %s", error.message);
        LSErrorFree(&error);
        return false;
    }

    return true;
}

void BackupAgent::initFilesForBackup()
{
    m_backupFiles.clear();

    GFileTest fileTest = static_cast<GFileTest>(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR);

    for (int i = 0; s_candidateFiles[i] != NULL; ++i) {
        if (g_file_test(s_candidateFiles[i], fileTest)) {
            m_backupFiles.push_back(s_candidateFiles[i]);
            g_debug("%s: backing up %s", __FUNCTION__, s_candidateFiles[i]);
        }
    }

    g_message("%s: %zu file(s) to back up", __FUNCTION__, m_backupFiles.size());
}

/**
 * The payload carries incrementalKey, maxTempBytes and tempDir. We stage
 * nothing and return absolute paths that already exist, so none of them apply.
 */
bool BackupAgent::preBackupCallback(LSHandle* lshandle, LSMessage* message, void* user_data)
{
    BackupAgent* pThis = BackupAgent::instance();

    struct json_object* response = json_object_new_object();
    if (!response) {
        g_warning("Unable to allocate json object");
        return true;
    }

    json_object_object_add(response, "description",
        json_object_new_string("Backup of system preferences: Just Type, locale and per-service settings"));
    json_object_object_add(response, "version", json_object_new_string("1.0"));

    // Built per request, not once at startup: a preference file the user has
    // never touched does not exist yet, and may by the next backup.
    pThis->initFilesForBackup();

    struct json_object* files = json_object_new_array();
    std::list<std::string>::const_iterator i;
    for (i = pThis->m_backupFiles.begin(); i != pThis->m_backupFiles.end(); ++i)
        json_object_array_add(files, json_object_new_string(i->c_str()));

    json_object_object_add(response, "files", files);

    LSError lserror;
    LSErrorInit(&lserror);

    g_message("Sending response to preBackupCallback: %s", json_object_to_json_string(response));
    if (!LSMessageReply(lshandle, message, json_object_to_json_string(response), &lserror)) {
        g_warning("Can't send reply to preBackupCallback error: %s", lserror.message);
        LSErrorFree(&lserror);
    }

    json_object_put(response);
    return true;
}

/**
 * The backup service has already written every file back to the path preBackup
 * reported. Nothing here needs to move them, so this only acknowledges - but it
 * has to acknowledge, or restore stalls on us the same way backup used to.
 */
bool BackupAgent::postRestoreCallback(LSHandle* lshandle, LSMessage* message, void* user_data)
{
    const char* str = LSMessageGetPayload(message);
    if (str)
        g_message("%s: received %s", __FUNCTION__, str);

    // The protocol sends {"files": array}; nothing in it changes what we do,
    // but reject a payload that is not even that so a misdirected call is
    // visible to its sender.
    bool valid = false;
    struct json_object* root = str ? json_tokener_parse(str) : NULL;
    if (root) {
        struct json_object* files = json_object_object_get(root, "files");
        valid = (files != NULL) && json_object_is_type(files, json_type_array);
        json_object_put(root);
    }

    LSError lserror;
    LSErrorInit(&lserror);

    struct json_object* response = json_object_new_object();
    if (!response) {
        g_warning("Unable to allocate json object");
        return true;
    }

    json_object_object_add(response, "returnValue", json_object_new_boolean(valid));
    if (!valid)
        json_object_object_add(response, "errorText",
            json_object_new_string("expected {\"files\": array}"));

    if (!LSMessageReply(lshandle, message, json_object_to_json_string(response), &lserror)) {
        g_warning("Can't send reply to postRestoreCallback error: %s", lserror.message);
        LSErrorFree(&lserror);
    }

    json_object_put(response);
    return true;
}
