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

#ifndef FAKEJSON_H
#define FAKEJSON_H

/**
 * Allocation failure for the json-c calls the daemon makes (constructors,
 * adds, serialisation): the n-th such call from now fails, the rest go to
 * the real json-c.
 */
namespace fakejson {

//! Fail the n-th call from now; 0 disarms. Restarts the count either way.
void failAt(long n);

//! Calls seen since the last failAt().
long count();

} // namespace fakejson

#endif // FAKEJSON_H
