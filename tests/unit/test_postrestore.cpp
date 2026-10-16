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

// postRestore: acknowledged for the protocol's {"files": array, "tempDir"},
// refused - but still answered - for anything else.

#include "fixture.h"

namespace {

typedef BackupAgentTest PostRestore;

// returnValue of a reply, which must be a JSON object with a boolean one.
bool returnValue(const std::string& reply)
{
    JsonPtr r = parse(reply);
    EXPECT_NE(r, nullptr) << reply;
    json_object* rv = member(r.get(), "returnValue", json_type_boolean);
    EXPECT_NE(rv, nullptr) << reply;
    if (rv && !json_object_get_boolean(rv)) {
        EXPECT_NE(member(r.get(), "errorText", json_type_string), nullptr) << reply;
    }
    return rv && json_object_get_boolean(rv);
}

TEST_F(PostRestore, AcceptsTheProtocolPayload)
{
    start();
    // What woce-backup's handlers/restore.js sends.
    EXPECT_TRUE(returnValue(call("postRestore",
        "{\"files\":[\"/var/luna/preferences/localeInfo\"],\"tempDir\":\"/media/internal/.restore/\"}")));
    EXPECT_TRUE(returnValue(call("postRestore", "{\"files\":[]}")));
}

TEST_F(PostRestore, RefusesAnythingElse)
{
    start();
    std::string deep(100000, '[');
    const char* payloads[] = {
        nullptr, "", "null", "[]", "[{\"files\":[]}]", "{}", "{\"files\":null}",
        "{\"files\":\"/var/luna/preferences/localeInfo\"}", "{\"files\":{}}", "{\"files\":7}",
        "{\"files\":[]", "not json", "{\"tempDir\":\"/tmp\"}", deep.c_str(),
    };
    for (const char* p : payloads)
        EXPECT_FALSE(returnValue(call("postRestore", p))) << (p ? std::string(p).substr(0, 40) : "(no payload)");
}

TEST_F(PostRestore, FilesKeyMatchedExactly)
{
    start();
    EXPECT_FALSE(returnValue(call("postRestore", "{\"Files\":[]}")));
    EXPECT_FALSE(returnValue(call("postRestore", "{\"file\":[]}")));
}

TEST_F(PostRestore, RepliesOnceOnEveryAllocationFailure)
{
    start();
    const char* payloads[] = { "{\"files\":[]}", "{}" };
    for (const char* p : payloads) {
        fakejson::failAt(0);
        call("postRestore", p);
        long n = fakejson::count();
        ASSERT_GT(n, 0);
        for (long k = 1; k <= n; ++k) {
            fakejson::failAt(k);
            std::string reply = call("postRestore", p);
            fakejson::failAt(0);
            JsonPtr r = parse(reply);
            ASSERT_NE(r, nullptr) << p << ", allocation " << k << ": " << reply;
            EXPECT_NE(member(r.get(), "returnValue", json_type_boolean), nullptr) << reply;
        }
    }
}

TEST_F(PostRestore, FailedReplyFreesItsError)
{
    start();
    fakebus::failNext("LSMessageReply");
    call("postRestore", "{\"files\":[]}");
}

} // namespace
