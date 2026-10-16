// Not built. Every rule in this directory must match something here;
// static-analysis.sh checks that, so a rule that stops matching (a
// Coccinelle upgrade, a typo) is noticed instead of passing quietly.
#include <sys/stat.h>
#include <glib.h>
#include <json.h>

void addUnchecked(json_object* o, json_object* a, json_object* v)
{
    json_object_object_add(o, "key", v);
    json_object_array_add(a, v);
}

bool followsLinks(const char* path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return g_file_test(path, G_FILE_TEST_IS_REGULAR);
}
