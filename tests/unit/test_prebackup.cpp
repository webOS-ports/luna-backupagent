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

// preBackup: the reply the backup service (woce-backup's handlers/backup.js)
// reads version, description and files from, and what happens to it when
// json-c cannot allocate.

#include "fixture.h"

#include <sys/stat.h>

#include <list>
#include <set>

namespace {

typedef BackupAgentTest PreBackup;

const std::list<std::string> kFiles = {
    "/var/preferences/com.palm.display/prefsDB.sl",
    "/var/luna/preferences/localeInfo",
    "/var/preferences/com.webos.service.wifi/prefsDB.sl",
};

const char* const kFallback = "{\"returnValue\":false,\"errorText\":\"out of memory\"}";

// The shape woce-backup relies on: version and description as strings, files
// as an array of strings, no returnValue:false. Returns the files.
std::list<std::string> checkResponse(json_object* r)
{
    std::list<std::string> files;
    EXPECT_TRUE(json_object_is_type(r, json_type_object));

    json_object* version = member(r, "version", json_type_string);
    EXPECT_NE(version, nullptr);
    if (version) {
        EXPECT_STREQ(json_object_get_string(version), "1.0");
    }
    EXPECT_NE(member(r, "description", json_type_string), nullptr);

    json_object* rv = nullptr;
    if (json_object_object_get_ex(r, "returnValue", &rv)) {
        EXPECT_TRUE(json_object_get_boolean(rv));
    }

    json_object* a = member(r, "files", json_type_array);
    EXPECT_NE(a, nullptr);
    for (size_t i = 0; a && i < json_object_array_length(a); ++i) {
        json_object* e = json_object_array_get_idx(a, i);
        EXPECT_TRUE(json_object_is_type(e, json_type_string));
        files.push_back(json_object_get_string(e));
    }
    return files;
}

TEST_F(PreBackup, ResponseListsTheFilesInOrder)
{
    JsonPtr r(BackupAgent::preBackupResponse(kFiles));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(checkResponse(r.get()), kFiles);
}

TEST_F(PreBackup, EmptyListStillHasAFilesArray)
{
    // Not a missing "files": an empty array says "nothing this time".
    JsonPtr r(BackupAgent::preBackupResponse(std::list<std::string>()));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(checkResponse(r.get()).empty());
}

TEST_F(PreBackup, NoHasMore)
{
    // hasMore:true would make the service call again expecting a new
    // incrementalKey, which this daemon never hands out.
    JsonPtr r(BackupAgent::preBackupResponse(kFiles));
    ASSERT_NE(r, nullptr);
    json_object* more = nullptr;
    if (json_object_object_get_ex(r.get(), "hasMore", &more)) {
        EXPECT_FALSE(json_object_get_boolean(more));
    }
}

TEST_F(PreBackup, EveryAllocationFailureGivesNull)
{
    // Count the json-c calls a full response takes, then fail each in turn.
    // Each one is needed, so each failure must give NULL - never a reply that
    // would be taken for a complete backup of fewer files. Leaks and double
    // frees on the way out are the sanitizers' and valgrind's to catch.
    fakejson::failAt(0);
    JsonPtr(BackupAgent::preBackupResponse(kFiles));
    long n = fakejson::count();
    ASSERT_GE(n, static_cast<long>(kFiles.size()) * 2 + 4);

    for (long k = 1; k <= n; ++k) {
        fakejson::failAt(k);
        json_object* r = BackupAgent::preBackupResponse(kFiles);
        fakejson::failAt(0);
        EXPECT_EQ(r, nullptr) << "allocation " << k << " of " << n << " failed and a reply was still built";
        if (r)
            json_object_put(r);
    }
}

TEST_F(PreBackup, CallbackRepliesWithTheExistingCandidates)
{
    start();
    std::string reply = call("preBackup",
        "{\"incrementalKey\":null,\"maxTempBytes\":10485760,\"tempDir\":\"/media/internal/.backup/\"}");
    JsonPtr r = parse(reply);
    ASSERT_NE(r, nullptr) << reply;

    // What exists depends on the machine the test runs on; what is listed
    // must be absolute, unique, and a regular file that is not a link.
    std::set<std::string> seen;
    for (const std::string& f : checkResponse(r.get())) {
        EXPECT_EQ(f[0], '/') << f;
        EXPECT_TRUE(seen.insert(f).second) << f << " listed twice";
        struct stat st;
        EXPECT_EQ(lstat(f.c_str(), &st), 0) << f;
        EXPECT_TRUE(S_ISREG(st.st_mode)) << f;
        // Backed up by com.webos.service.systemservice; listing it here too
        // would restore one copy over the other.
        EXPECT_EQ(f.find("systemprefs.db"), std::string::npos) << f;
    }
}

TEST_F(PreBackup, CallbackAnswersWhateverThePayload)
{
    start();
    const char* payloads[] = { nullptr, "", "[]", "null", "garbage", "{\"tempDir\":42}", "{}" };
    for (const char* p : payloads) {
        std::string reply = call("preBackup", p);
        JsonPtr r = parse(reply);
        ASSERT_NE(r, nullptr) << (p ? p : "(no payload)") << " -> " << reply;
        checkResponse(r.get());
    }
}

TEST_F(PreBackup, CallbackRepliesOnceOnEveryAllocationFailure)
{
    start();
    fakejson::failAt(0);
    call("preBackup", "{}");
    long n = fakejson::count();
    ASSERT_GT(n, 0);

    // The serialisation for the log message is one of the calls counted:
    // failing it must not cost the reply.
    for (long k = 1; k <= n; ++k) {
        fakejson::failAt(k);
        std::string reply = call("preBackup", "{}");
        fakejson::failAt(0);
        JsonPtr r = parse(reply);
        ASSERT_NE(r, nullptr) << "allocation " << k << ": " << reply;
        json_object* rv = nullptr;
        if (json_object_object_get_ex(r.get(), "returnValue", &rv) && !json_object_get_boolean(rv)) {
            EXPECT_EQ(reply, kFallback) << "allocation " << k;
        } else {
            checkResponse(r.get());
        }
    }
}

TEST_F(PreBackup, FailedReplyFreesItsError)
{
    start();
    fakebus::failNext("LSMessageReply");
    call("preBackup", "{}");
    // TearDown checks the LSError was freed.
}

} // namespace
