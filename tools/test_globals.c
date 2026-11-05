/* The globals src/main.c defines, for tests linked against the rest of the runtime (see
 * `make test`): main.c itself has main() and stays out. g_log_verbose is left to each test,
 * which defines it next to its other fakes. */
char g_data_root[1024];
char g_root[1024];
char g_user_dir[1024];
char g_obb_path[1024];
