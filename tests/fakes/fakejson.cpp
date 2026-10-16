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

#include "fakejson.h"

#include <dlfcn.h>
#include <cstdlib>
#include <cstdio>

#include <json.h>

/*
 * The json-c calls the daemon makes that can fail, wrapped: each forwards to
 * the real json-c (RTLD_NEXT) unless it is the one fakejson::failAt() picked,
 * which fails the way json-c does - NULL for a constructor, -1 for an add,
 * leaving the value with the caller.
 *
 * The daemon's object file binds to these at link time. json-c's own internal
 * calls may bind to them too; failures are only injected while armed, and the
 * tests arm them around the daemon's code alone.
 */

namespace {

long s_count = 0;
long s_failAt = 0;

bool fail()
{
    ++s_count;
    return s_failAt > 0 && s_count == s_failAt;
}

template <typename F>
F real(const char* name)
{
    void* p = dlsym(RTLD_NEXT, name);
    if (!p) {
        fprintf(stderr, "fakejson: no real %s\n", name);
        abort();
    }
    return reinterpret_cast<F>(p);
}

} // namespace

namespace fakejson {

void failAt(long n)
{
    s_count = 0;
    s_failAt = n;
}

long count()
{
    return s_count;
}

} // namespace fakejson

extern "C" {

json_object* json_object_new_object(void)
{
    static auto f = real<json_object* (*)(void)>(__func__);
    return fail() ? nullptr : f();
}

json_object* json_object_new_array(void)
{
    static auto f = real<json_object* (*)(void)>(__func__);
    return fail() ? nullptr : f();
}

json_object* json_object_new_string(const char* s)
{
    static auto f = real<json_object* (*)(const char*)>(__func__);
    return fail() ? nullptr : f(s);
}

json_object* json_object_new_boolean(json_bool b)
{
    static auto f = real<json_object* (*)(json_bool)>(__func__);
    return fail() ? nullptr : f(b);
}

int json_object_object_add(json_object* obj, const char* key, json_object* val)
{
    static auto f = real<int (*)(json_object*, const char*, json_object*)>(__func__);
    return fail() ? -1 : f(obj, key, val);
}

int json_object_array_add(json_object* obj, json_object* val)
{
    static auto f = real<int (*)(json_object*, json_object*)>(__func__);
    return fail() ? -1 : f(obj, val);
}

const char* json_object_to_json_string(json_object* obj)
{
    static auto f = real<const char* (*)(json_object*)>(__func__);
    return fail() ? nullptr : f(obj);
}

} // extern "C"
