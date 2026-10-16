// json_object_object_add() and json_object_array_add() return -1 when they
// cannot allocate, and leave the value with the caller. Discarding that
// result leaks the value and sends a reply that is missing a member - for
// preBackup, a "complete" backup that silently lacks files.
@@
expression O, K, V;
@@
(
* json_object_object_add(O, K, V);
|
* json_object_array_add(O, V);
)
