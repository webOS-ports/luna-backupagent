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

#include "fakebus.h"

#include <glib.h>

#include <set>

struct LSHandle {
    std::string name;
    std::map<std::string, std::vector<LSMethod> > categories;
};

struct LSMessage {
    const char* payload;
};

namespace {

std::set<std::string> s_failNext;
std::vector<std::string> s_replies;
std::map<std::string, std::vector<std::string> > s_categories;
std::map<std::string, int> s_calls;
std::string s_name;
LSHandle* s_handle = nullptr;
int s_errors = 0;
int s_handles = 0;

// Records the call; true if the test armed a failure for it.
bool enter(const char* function, LSError* error)
{
    ++s_calls[function];
    if (!s_failNext.erase(function))
        return false;

    if (error) {
        error->error_code = -1;
        error->message = g_strdup_printf("fakebus: %s failed as asked", function);
        error->file = __FILE__;
        error->line = __LINE__;
        error->func = function;
        ++s_errors;
    }
    return true;
}

} // namespace

namespace fakebus {

void reset()
{
    s_failNext.clear();
    s_replies.clear();
    s_categories.clear();
    s_calls.clear();
    s_name.clear();
    s_errors = 0;
    // A handle the daemon leaked stays leaked, so the leak checkers see it;
    // s_handles keeps counting it.
    s_handle = nullptr;
}

void failNext(const std::string& function)
{
    s_failNext.insert(function);
}

bool callMethod(const std::string& method, const char* payload)
{
    if (!s_handle)
        return false;

    std::map<std::string, std::vector<LSMethod> >::const_iterator c = s_handle->categories.find("/");
    if (c == s_handle->categories.end())
        return false;

    for (const LSMethod& m : c->second) {
        if (method == m.name) {
            LSMessage message = { payload };
            return m.function(s_handle, &message, nullptr);
        }
    }
    return false;
}

const std::vector<std::string>& replies() { return s_replies; }
const std::map<std::string, std::vector<std::string> >& categories() { return s_categories; }
const std::string& registeredName() { return s_name; }
int calls(const std::string& function) { return s_calls[function]; }
int errorsOutstanding() { return s_errors; }
int handlesOutstanding() { return s_handles; }

} // namespace fakebus

bool LSErrorInit(LSError* error)
{
    error->error_code = 0;
    error->message = nullptr;
    error->file = nullptr;
    error->line = 0;
    error->func = nullptr;
    error->padding = nullptr;
    error->magic = 0;
    return true;
}

void LSErrorFree(LSError* error)
{
    if (error && error->message) {
        g_free(error->message);
        error->message = nullptr;
        --s_errors;
    }
}

bool LSRegister(const char* name, LSHandle** sh, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;

    *sh = new LSHandle();
    (*sh)->name = name;
    s_name = name;
    s_handle = *sh;
    ++s_handles;
    return true;
}

bool LSRegisterCategory(LSHandle* sh, const char* category, LSMethod* methods,
                        LSSignal* /*langis*/, LSProperty* /*properties*/, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;

    std::vector<LSMethod>& table = sh->categories[category];
    std::vector<std::string>& names = s_categories[category];
    for (LSMethod* m = methods; m && m->name; ++m) {
        table.push_back(*m);
        names.push_back(m->name);
    }
    return true;
}

bool LSGmainAttach(LSHandle* /*sh*/, GMainLoop* /*mainLoop*/, LSError* lserror)
{
    return !enter(__func__, lserror);
}

bool LSUnregister(LSHandle* service, LSError* lserror)
{
    // The real library frees the handle whatever it reports.
    if (s_handle == service)
        s_handle = nullptr;
    delete service;
    --s_handles;
    return !enter(__func__, lserror);
}

const char* LSMessageGetPayload(LSMessage* message)
{
    ++s_calls[__func__];
    return message->payload;
}

bool LSMessageReply(LSHandle* /*sh*/, LSMessage* /*lsmsg*/, const char* replyPayload, LSError* lserror)
{
    // Kept even when the send "fails": the test wants to see what was tried.
    s_replies.push_back(replyPayload ? replyPayload : "(null)");
    return !enter(__func__, lserror);
}
