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

// runBackupAgent(), which is all main() does: serve until SIGTERM or SIGINT,
// exit 1 when the bus will not have us, and leave nothing behind either way.
// The signals are real ones, raised at this process.

#include "fixture.h"

#include <csignal>

#include "Daemon.h"

namespace {

typedef BackupAgentTest Daemon;

struct Script {
    int signal;
    std::string reply;  // what preBackup answered while the loop ran
};

// Runs once the loop is up: calls the service as a client, then signals.
gboolean whileRunning(gpointer data)
{
    Script* s = static_cast<Script*>(data);
    if (fakebus::callMethod("preBackup", "{}") && !fakebus::replies().empty())
        s->reply = fakebus::replies().back();
    raise(s->signal);
    return G_SOURCE_REMOVE;
}

int runUntil(int signal, std::string* reply = nullptr)
{
    Script s = { signal, std::string() };
    g_timeout_add(10, whileRunning, &s);
    int status = runBackupAgent();
    if (reply)
        *reply = s.reply;
    return status;
}

TEST_F(Daemon, ServesUntilSigterm)
{
    std::string reply;
    EXPECT_EQ(runUntil(SIGTERM, &reply), 0);
    JsonPtr r = parse(reply);
    ASSERT_NE(r, nullptr) << "preBackup did not answer while the loop ran: " << reply;
    EXPECT_NE(member(r.get(), "files", json_type_array), nullptr) << reply;
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
    EXPECT_EQ(fakebus::handlesOutstanding(), 0);
}

TEST_F(Daemon, StopsOnSigint)
{
    EXPECT_EQ(runUntil(SIGINT), 0);
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
}

// Each init() failure: exit status 1, and the partial registration dropped
// (TearDown checks no handle and no LSError is left).
class DaemonInitFailure : public BackupAgentTest, public ::testing::WithParamInterface<const char*> {};

TEST_P(DaemonInitFailure, ExitsOneAndCleansUp)
{
    fakebus::failNext(GetParam());
    EXPECT_EQ(runBackupAgent(), 1);
    EXPECT_EQ(fakebus::handlesOutstanding(), 0) << "handle left registered after " << GetParam() << " failed";
}

INSTANTIATE_TEST_SUITE_P(EveryStep, DaemonInitFailure,
    ::testing::Values("LSRegister", "LSRegisterCategory", "LSGmainAttach"));

// Nothing is left on the default main context after a run: a signal source
// from an earlier run, still pointing at its freed main loop, would be
// dispatched by the next run's signal.
TEST_F(Daemon, NoSignalSourceOutlivesARun)
{
    EXPECT_EQ(runUntil(SIGTERM), 0);
    fakebus::reset();
    EXPECT_EQ(runUntil(SIGINT), 0);
    fakebus::reset();
    EXPECT_EQ(runUntil(SIGTERM), 0);
}

TEST_F(Daemon, NoSignalSourceOutlivesAFailedStart)
{
    fakebus::failNext("LSRegister");
    EXPECT_EQ(runBackupAgent(), 1);
    fakebus::reset();
    EXPECT_EQ(runUntil(SIGTERM), 0);
    fakebus::reset();
    EXPECT_EQ(runUntil(SIGINT), 0);
}

TEST_F(Daemon, ContextIsEmptyAfterwards)
{
    EXPECT_EQ(runUntil(SIGTERM), 0);
    // Anything still attached would be pending or dispatchable here.
    EXPECT_FALSE(g_main_context_pending(nullptr));
}

} // namespace
