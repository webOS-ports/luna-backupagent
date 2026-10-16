// stat() and g_file_test() follow symbolic links. A path vetted with either
// and then reported to the backup service gets written back through the
// link on restore, as root. Vet with lstat() (G_FILE_TEST_IS_SYMLINK is the
// one g_file_test() flag that does not follow).
@@
expression P, B, F;
@@
(
* stat(P, B)
|
* g_file_test(P, F)
)
