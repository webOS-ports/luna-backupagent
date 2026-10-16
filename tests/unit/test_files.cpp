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

// filesForBackup(): which candidates are handed to the backup service.

#include "fixture.h"

#include <sys/stat.h>
#include <unistd.h>

#include <list>
#include <vector>

namespace {

class FilesForBackup : public BackupAgentTest
{
protected:
    void SetUp() override
    {
        BackupAgentTest::SetUp();
        GError* error = nullptr;
        char* dir = g_dir_make_tmp("backupagent-test-XXXXXX", &error);
        ASSERT_NE(dir, nullptr) << (error ? error->message : "");
        m_dir = dir;
        g_free(dir);
    }

    void TearDown() override
    {
        for (const std::string& p : m_made)
            (void) remove(p.c_str());
        (void) rmdir(m_dir.c_str());
        BackupAgentTest::TearDown();
    }

    std::string path(const char* name)
    {
        std::string p = m_dir + "/" + name;
        m_made.insert(m_made.begin(), p);   // removed newest first
        return p;
    }

    std::string regular(const char* name)
    {
        std::string p = path(name);
        EXPECT_TRUE(g_file_set_contents(p.c_str(), "x", 1, nullptr));
        return p;
    }

    //! filesForBackup() over these paths, as a vector.
    static std::vector<std::string> run(const std::vector<std::string>& candidates)
    {
        std::vector<const char*> list;
        for (const std::string& c : candidates)
            list.push_back(c.c_str());
        list.push_back(nullptr);
        std::list<std::string> r = BackupAgent::filesForBackup(list.data());
        return std::vector<std::string>(r.begin(), r.end());
    }

    std::string m_dir;
    std::vector<std::string> m_made;
};

TEST_F(FilesForBackup, EmptyListGivesNothing)
{
    EXPECT_TRUE(run({}).empty());
}

TEST_F(FilesForBackup, RegularFilesInListOrder)
{
    std::string b = regular("b");
    std::string a = regular("a");
    std::string c = regular("c");
    EXPECT_EQ(run({ b, a, c }), (std::vector<std::string>{ b, a, c }));
}

TEST_F(FilesForBackup, MissingFilesAreLeftOut)
{
    std::string a = regular("a");
    std::string gone = m_dir + "/never-created";
    EXPECT_EQ(run({ gone, a, gone }), (std::vector<std::string>{ a }));
}

TEST_F(FilesForBackup, DirectoriesAreLeftOut)
{
    // luna-prefs keeps a directory per service; the protocol wants the file
    // inside it, never the directory.
    std::string d = path("dir");
    ASSERT_EQ(mkdir(d.c_str(), 0700), 0);
    std::string a = regular("a");
    EXPECT_EQ(run({ d, a }), (std::vector<std::string>{ a }));
}

TEST_F(FilesForBackup, FifosAreLeftOut)
{
    std::string f = path("fifo");
    ASSERT_EQ(mkfifo(f.c_str(), 0600), 0);
    EXPECT_TRUE(run({ f }).empty());
}

TEST_F(FilesForBackup, SymlinkToRegularFileIsLeftOut)
{
    // Restore writes back to the path reported, as root: a link there would
    // send that write wherever it points.
    std::string target = regular("target");
    std::string link = path("link");
    ASSERT_EQ(symlink(target.c_str(), link.c_str()), 0);
    EXPECT_EQ(run({ link, target }), (std::vector<std::string>{ target }));
}

TEST_F(FilesForBackup, DanglingSymlinkIsLeftOut)
{
    std::string link = path("dangling");
    ASSERT_EQ(symlink((m_dir + "/nowhere").c_str(), link.c_str()), 0);
    EXPECT_TRUE(run({ link }).empty());
}

TEST_F(FilesForBackup, FileUnderSymlinkedDirectoryIsStillListed)
{
    // Only the last component is checked; the directories above it belong
    // to the image and are not ours to second-guess.
    std::string d = path("realdir");
    ASSERT_EQ(mkdir(d.c_str(), 0700), 0);
    std::string f = d + "/prefsDB.sl";
    ASSERT_TRUE(g_file_set_contents(f.c_str(), "x", 1, nullptr));
    m_made.insert(m_made.begin(), f);
    std::string ld = path("linkdir");
    ASSERT_EQ(symlink(d.c_str(), ld.c_str()), 0);
    std::string through = ld + "/prefsDB.sl";
    EXPECT_EQ(run({ through }), (std::vector<std::string>{ through }));
}

} // namespace
