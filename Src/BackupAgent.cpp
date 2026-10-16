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

#include <sys/stat.h>

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

static const char* const kServiceName = "com.webos.service.backupagent";

BackupAgent* BackupAgent::s_instance = NULL;

LSMethod BackupAgent::s_backupServerMethods[] = {
    { "preBackup",   BackupAgent::preBackupCallback,   LUNA_METHOD_FLAGS_NONE },
    { "postRestore", BackupAgent::postRestoreCallback, LUNA_METHOD_FLAGS_NONE },
    { NULL, NULL, LUNA_METHOD_FLAGS_NONE }
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
    // luna-prefs keeps each service's data in a per-service sqlite file
    // (lunaprefs.c: "/var/preferences/%s/prefsDB.sl"). The backup protocol
    // wants files, not the directories around them.
    "/var/preferences/com.palm.display/prefsDB.sl",
    "/var/preferences/com.palm.sleep/prefsDB.sl",
    "/var/preferences/com.palm.telephony/prefsDB.sl",
    "/var/preferences/com.webos.service.battery/prefsDB.sl",
    "/var/preferences/com.webos.service.location/prefsDB.sl",
    "/var/preferences/com.webos.service.wifi/prefsDB.sl",
    NULL
};

BackupAgent::BackupAgent()
    : m_service(NULL)
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

void BackupAgent::destroy()
{
    delete s_instance;
    s_instance = NULL;
}

bool BackupAgent::init(GMainLoop* mainLoop)
{
    g_assert(m_service == NULL);    // Only initialize once.

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

    if (!LSGmainAttach(m_service, mainLoop, &error)) {
        g_warning("Failed attaching to service bus: %s", error.message);
        LSErrorFree(&error);
        return false;
    }

    return true;
}

std::list<std::string> BackupAgent::filesForBackup(const char* const candidates[])
{
    std::list<std::string> files;

    // lstat, not stat (or g_file_test, which follows links): restore writes
    // each file back to the path reported here, as root, so a symlink in
    // place of one of these files would redirect that write to wherever it
    // points. None of them is ever a link on a sane system; skip one if it is.
    // This does not close the window between this check and the copy - the
    // backup service has to open what it reads and writes without following
    // links for that - it only keeps us from vouching for a link.
    for (int i = 0; candidates[i] != NULL; ++i) {
        struct stat st;
        if (lstat(candidates[i], &st) != 0)
            continue;

        if (S_ISREG(st.st_mode)) {
            files.push_back(candidates[i]);
            g_debug("%s: backing up %s", __FUNCTION__, candidates[i]);
        }
        else if (S_ISLNK(st.st_mode)) {
            g_warning("%s: %s is a symbolic link, not backing it up", __FUNCTION__, candidates[i]);
        }
    }

    g_message("%s: %zu file(s) to back up", __FUNCTION__, files.size());
    return files;
}

// The backup service must always get an answer, even on allocation failure -
// an unanswered call stalls its whole run until timeout. Takes ownership of
// response; NULL sends the fallback.
static void replyOrFallback(LSHandle* lshandle, LSMessage* message, json_object* response)
{
    static const char* const fallback = "{\"returnValue\":false,\"errorText\":\"out of memory\"}";

    LSError lserror;
    LSErrorInit(&lserror);

    const char* payload = response ? json_object_to_json_string(response) : fallback;
    if (!payload)
        payload = fallback;

    if (!LSMessageReply(lshandle, message, payload, &lserror)) {
        g_warning("Can't send reply: %s", lserror.message);
        LSErrorFree(&lserror);
    }

    if (response)
        json_object_put(response);
}

// json_object_object_add() with the failures folded in: a NULL value (its
// allocation failed) or a failed insert. Takes ownership of value either way.
static bool addMember(json_object* object, const char* key, json_object* value)
{
    if (!value)
        return false;

    if (json_object_object_add(object, key, value) != 0) {
        json_object_put(value);
        return false;
    }
    return true;
}

/**
 * The payload carries incrementalKey, maxTempBytes and tempDir. We stage
 * nothing and return absolute paths that already exist, so none of them apply.
 *
 * Returns NULL if any part could not be allocated: a reply missing a file, or
 * with a null version, would be taken for a complete backup of less.
 */
json_object* BackupAgent::preBackupResponse(const std::list<std::string>& backupFiles)
{
    json_object* response = json_object_new_object();
    if (!response)
        return NULL;

    // The array is filled first and handed over before anything else, so
    // there is exactly one point where its ownership moves to response.
    json_object* files = json_object_new_array();
    bool ok = (files != NULL);
    for (std::list<std::string>::const_iterator i = backupFiles.begin(); ok && i != backupFiles.end(); ++i) {
        json_object* entry = json_object_new_string(i->c_str());
        if (!entry || json_object_array_add(files, entry) != 0) {
            if (entry)
                json_object_put(entry);
            ok = false;
        }
    }
    if (!ok) {
        if (files)
            json_object_put(files);
        json_object_put(response);
        return NULL;
    }

    ok = addMember(response, "files", files)
      && addMember(response, "description",
             json_object_new_string("Backup of system preferences: Just Type, locale and per-service settings"))
      && addMember(response, "version", json_object_new_string("1.0"));
    if (!ok) {
        json_object_put(response);
        return NULL;
    }
    return response;
}

bool BackupAgent::preBackupCallback(LSHandle* lshandle, LSMessage* message, void* /*user_data*/)
{
    // Built per request, not once at startup: a preference file the user has
    // never touched does not exist yet, and may by the next backup.
    json_object* response = preBackupResponse(filesForBackup(s_candidateFiles));
    if (response) {
        const char* text = json_object_to_json_string(response);
        g_message("Sending response to preBackupCallback: %s", text ? text : "(unprintable)");
    }
    else {
        g_warning("Unable to allocate the preBackup response");
    }

    replyOrFallback(lshandle, message, response);
    return true;
}

/**
 * The backup service has already written every file back to the path preBackup
 * reported. Nothing here needs to move them, so this only acknowledges - but it
 * has to acknowledge, or restore stalls on us the same way backup used to.
 */
bool BackupAgent::postRestoreCallback(LSHandle* lshandle, LSMessage* message, void* /*user_data*/)
{
    const char* str = LSMessageGetPayload(message);
    if (str)
        g_message("%s: received %s", __FUNCTION__, str);

    // The protocol sends {"files": array}; nothing in it changes what we do,
    // but reject a payload that is not even that so a misdirected call is
    // visible to its sender.
    bool valid = false;
    json_object* root = str ? json_tokener_parse(str) : NULL;
    if (root) {
        json_object* files = NULL;
        valid = json_object_object_get_ex(root, "files", &files)
             && json_object_is_type(files, json_type_array);
        json_object_put(root);
    }

    json_object* response = json_object_new_object();
    if (response) {
        bool ok = addMember(response, "returnValue", json_object_new_boolean(valid));
        if (ok && !valid)
            ok = addMember(response, "errorText", json_object_new_string("expected {\"files\": array}"));
        if (!ok) {
            json_object_put(response);
            response = NULL;
        }
    }
    if (!response)
        g_warning("Unable to allocate the postRestore response");

    replyOrFallback(lshandle, message, response);
    return true;
}
