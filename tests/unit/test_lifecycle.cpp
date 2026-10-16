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

// Registration on the bus, every way it can fail, and teardown; and that the
// files installed for the bus and the backup service name what the daemon
// actually registers.

#include "fixture.h"

#include <set>
#include <vector>

namespace {

typedef BackupAgentTest Lifecycle;

const char* const kName = "com.webos.service.backupagent";
const std::vector<std::string> kMethods = { "preBackup", "postRestore" };

TEST_F(Lifecycle, RegistersBothMethodsAtTheRoot)
{
    start();
    EXPECT_EQ(fakebus::registeredName(), kName);
    ASSERT_EQ(fakebus::categories().count("/"), 1u);
    EXPECT_EQ(fakebus::categories().at("/"), kMethods);
    EXPECT_EQ(fakebus::calls("LSGmainAttach"), 1);
    EXPECT_EQ(fakebus::handlesOutstanding(), 1);
}

TEST_F(Lifecycle, InstanceIsOne)
{
    EXPECT_EQ(BackupAgent::instance(), BackupAgent::instance());
}

TEST_F(Lifecycle, DestroyUnregistersOnce)
{
    start();
    BackupAgent::destroy();
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
    EXPECT_EQ(fakebus::handlesOutstanding(), 0);
    BackupAgent::destroy();     // and again, as TearDown does
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
}

TEST_F(Lifecycle, DestroyWithoutInitIsHarmless)
{
    BackupAgent::instance();
    BackupAgent::destroy();
    EXPECT_EQ(fakebus::calls("LSUnregister"), 0);
}

TEST_F(Lifecycle, CanStartAgainAfterDestroy)
{
    start();
    BackupAgent::destroy();
    start();
    EXPECT_EQ(fakebus::calls("LSRegister"), 2);
}

// Each step of init() failing: init() says so, the error is freed (TearDown),
// and a handle already obtained is unregistered by destroy(), as Main.cpp
// calls it on that path.
class InitFailure : public BackupAgentTest, public ::testing::WithParamInterface<const char*> {};

TEST_P(InitFailure, ReportedAndCleanedUp)
{
    fakebus::failNext(GetParam());
    EXPECT_FALSE(BackupAgent::instance()->init(m_loop));
    EXPECT_EQ(fakebus::errorsOutstanding(), 0) << "LSError from " << GetParam() << " not freed";
    BackupAgent::destroy();
    EXPECT_EQ(fakebus::handlesOutstanding(), 0);
}

INSTANTIATE_TEST_SUITE_P(EveryStep, InitFailure,
    ::testing::Values("LSRegister", "LSRegisterCategory", "LSGmainAttach"));

TEST_F(Lifecycle, FailedUnregisterFreesItsError)
{
    start();
    fakebus::failNext("LSUnregister");
    BackupAgent::destroy();
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
}

// --- the installed files agree with the code ---------------------------------

// The source tree: where the build put it, or $BA_SOURCE_DIR when the tests
// run somewhere else (on a device, with files/ copied next to them).
std::string sourcePath(const char* relative)
{
    const char* dir = g_getenv("BA_SOURCE_DIR");
    return std::string(dir ? dir : BA_SOURCE_DIR) + "/" + relative;
}

JsonPtr readJson(const char* relative)
{
    std::string path = sourcePath(relative);
    char* text = nullptr;
    EXPECT_TRUE(g_file_get_contents(path.c_str(), &text, nullptr, nullptr)) << path;
    JsonPtr j = parse(text ? text : "");
    g_free(text);
    EXPECT_NE(j, nullptr) << path << " is not JSON";
    return j;
}

std::string text(const char* relative)
{
    std::string path = sourcePath(relative);
    char* t = nullptr;
    EXPECT_TRUE(g_file_get_contents(path.c_str(), &t, nullptr, nullptr)) << path;
    std::string s = t ? t : "";
    g_free(t);
    return s;
}

std::string stringMember(json_object* o, const char* key)
{
    json_object* v = member(o, key, json_type_string);
    EXPECT_NE(v, nullptr) << key;
    return v ? json_object_get_string(v) : "";
}

TEST_F(Lifecycle, BackupRegistrationNamesTheRegisteredMethods)
{
    // The file the backup service walks /etc/palm/backup for.
    start();
    JsonPtr reg = readJson("files/backup/com.webos.service.backupagent");
    ASSERT_NE(reg, nullptr);
    EXPECT_EQ(stringMember(reg.get(), "id"), fakebus::registeredName());
    const std::vector<std::string>& registered = fakebus::categories().at("/");
    std::set<std::string> methods(registered.begin(), registered.end());
    EXPECT_EQ(methods.count(stringMember(reg.get(), "preBackup")), 1u);
    EXPECT_EQ(methods.count(stringMember(reg.get(), "postRestore")), 1u);
}

TEST_F(Lifecycle, ApiFileCoversExactlyTheRegisteredMethods)
{
    start();
    JsonPtr api = readJson("files/sysbus/luna-backupagent.api.json");
    ASSERT_NE(api, nullptr);
    std::set<std::string> listed;
    json_object_object_foreach(api.get(), group, entries) {
        (void) group;
        ASSERT_TRUE(json_object_is_type(entries, json_type_array));
        for (size_t i = 0; i < json_object_array_length(entries); ++i)
            listed.insert(json_object_get_string(json_object_array_get_idx(entries, i)));
    }
    std::set<std::string> registered;
    for (const std::string& m : fakebus::categories().at("/"))
        registered.insert(fakebus::registeredName() + "/" + m);
    EXPECT_EQ(listed, registered);
}

TEST_F(Lifecycle, BusFilesNameTheRegisteredService)
{
    start();
    const std::string name = fakebus::registeredName();
    JsonPtr role = readJson("files/sysbus/luna-backupagent.role.json.in");
    ASSERT_NE(role, nullptr);
    json_object* allowed = member(role.get(), "allowedNames", json_type_array);
    ASSERT_NE(allowed, nullptr);
    ASSERT_EQ(json_object_array_length(allowed), 1u);
    EXPECT_EQ(std::string(json_object_get_string(json_object_array_get_idx(allowed, 0))), name);

    EXPECT_NE(text("files/sysbus/luna-backupagent.service.in").find("Name=" + name + "\n"), std::string::npos);

    // Every group the API file declares has a trust level in groups.json and
    // is granted to the service itself in perm.json.
    JsonPtr api = readJson("files/sysbus/luna-backupagent.api.json");
    JsonPtr groups = readJson("files/sysbus/luna-backupagent.groups.json");
    JsonPtr perm = readJson("files/sysbus/luna-backupagent.perm.json");
    ASSERT_TRUE(api && groups && perm);
    json_object_object_foreach(api.get(), group, entries) {
        (void) entries;
        EXPECT_NE(member(groups.get(), group, json_type_array), nullptr) << group << " has no trust level";
    }
    EXPECT_NE(member(perm.get(), name.c_str(), json_type_array), nullptr);
}

} // namespace
