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

#ifndef BACKUPAGENT_TEST_FIXTURE_H
#define BACKUPAGENT_TEST_FIXTURE_H

#include <gtest/gtest.h>

#include <glib.h>
#include <json.h>

#include <memory>
#include <string>

#include "BackupAgent.h"
#include "fakebus.h"
#include "fakejson.h"

//! A json-c object that is put when it goes out of scope.
struct JsonPut {
    void operator()(json_object* o) const { json_object_put(o); }
};
typedef std::unique_ptr<json_object, JsonPut> JsonPtr;

//! Parses text; NULL if it is not JSON.
inline JsonPtr parse(const std::string& text)
{
    return JsonPtr(json_tokener_parse(text.c_str()));
}

//! The member key of o if it has that type, else NULL.
inline json_object* member(json_object* o, const char* key, json_type type)
{
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !json_object_is_type(v, type))
        return nullptr;
    return v;
}

/**
 * A clean bus and json-c for every test, and a check afterwards that the
 * daemon gave back every handle and every LSError it was handed.
 */
class BackupAgentTest : public ::testing::Test
{
protected:
    // glib's own criticals and warnings are broken contracts - a g_return_if_fail()
    // tripping on a freed or NULL argument - and the only report of a misuse
    // inside glib, which the sanitizers do not instrument. Each one fails the
    // test. The daemon's own messages (default domain) are not caught.
    static void glibComplaint(const gchar* domain, GLogLevelFlags, const gchar* message, gpointer)
    {
        ADD_FAILURE() << domain << " complained: " << message;
    }

    void SetUp() override
    {
        m_glibHandler = g_log_set_handler("GLib",
            static_cast<GLogLevelFlags>(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING), glibComplaint, nullptr);
        fakebus::reset();
        fakejson::failAt(0);
        m_loop = g_main_loop_new(nullptr, FALSE);
    }

    void TearDown() override
    {
        fakejson::failAt(0);
        BackupAgent::destroy();
        EXPECT_EQ(fakebus::handlesOutstanding(), 0) << "an LSHandle was not unregistered";
        EXPECT_EQ(fakebus::errorsOutstanding(), 0) << "an LSError was not freed";
        g_main_loop_unref(m_loop);
        g_log_remove_handler("GLib", m_glibHandler);
    }

    //! Registers the daemon on the fake bus.
    void start()
    {
        ASSERT_TRUE(BackupAgent::instance()->init(m_loop));
    }

    //! Calls method with payload; the one reply it must produce.
    std::string call(const char* method, const char* payload)
    {
        size_t before = fakebus::replies().size();
        EXPECT_TRUE(fakebus::callMethod(method, payload)) << method << " did not return true";
        EXPECT_EQ(fakebus::replies().size(), before + 1) << method << " must reply exactly once";
        return fakebus::replies().size() > before ? fakebus::replies().back() : std::string();
    }

    GMainLoop* m_loop = nullptr;
    guint m_glibHandler = 0;
};

#endif // BACKUPAGENT_TEST_FIXTURE_H
