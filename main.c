#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

static int err_code;

// Counts number of files when -n is specified
static int count = 0;

/*
 * here are some function signatures and macros that may be helpful.
 */

void handle_error(char* fullname, char* action);
bool test_file(char* pathandname);
bool is_dir(char* pathandname);
const char* ftype_to_str(mode_t mode);
void list_file(char* pathandname, char* name, bool list_long, bool list_count);
void list_dir(char* dirname, bool list_long, bool list_all, bool recursive, bool list_count);

/*
 * You can use the NOT_YET_IMPLEMENTED macro to error out when you reach parts
 * of the code you have not yet finished implementing.
 */
#define NOT_YET_IMPLEMENTED(msg)                  \
    do {                                          \
        printf("Not yet implemented: " msg "\n"); \
        exit(255);                                \
    } while (0)

/*
 * PRINT_ERROR: This can be used to print the cause of an error returned by a
 * system call. It can help with debugging and reporting error causes to
 * the user. Example usage:
 *     if ( error_condition ) {
 *        PRINT_ERROR();
 *     }
 */
#define PRINT_ERROR(progname, what_happened, pathandname)               \
    do {                                                                \
        printf("%s: %s %s: %s\n", progname, what_happened, pathandname, \
               strerror(errno));                                        \
    } while (0)

/* PRINT_PERM_CHAR:
 *
 * This will be useful for -l permission printing.  It prints the given
 * 'ch' if the permission exists, or "-" otherwise.
 * Example usage:
 *     PRINT_PERM_CHAR(sb.st_mode, S_IRUSR, "r");
 */
#define PRINT_PERM_CHAR(mode, mask, ch) printf("%s", (mode & mask) ? ch : "-");

/*
 * Get username for uid. Return 1 on failure, 0 otherwise.
 */
static int uname_for_uid(uid_t uid, char* buf, size_t buflen) {
    struct passwd* p = getpwuid(uid);
    if (p == NULL) {
        return 1;
    }
    strncpy(buf, p->pw_name, buflen);
    return 0;
}

/*
 * Get group name for gid. Return 1 on failure, 0 otherwise.
 */
static int group_for_gid(gid_t gid, char* buf, size_t buflen) {
    struct group* g = getgrgid(gid);
    if (g == NULL) {
        return 1;
    }
    strncpy(buf, g->gr_name, buflen);
    return 0;
}

/*
 * Format the supplied `struct timespec` in `ts` (e.g., from `stat.st_mtime`) as a
 * string in `char *out`. Returns the length of the formatted string (see, `man
 * 3 strftime`).
 */
static size_t date_string(struct timespec* ts, char* out, size_t len) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    struct tm* t = localtime(&ts->tv_sec);
    if (now.tv_sec < ts->tv_sec) {
        // Future time, treat with care.
        return strftime(out, len, "%b %e %Y", t);
    } else {
        time_t difference = now.tv_sec - ts->tv_sec;
        if (difference < 31556952ull) {
            return strftime(out, len, "%b %e %H:%M", t);
        } else {
            return strftime(out, len, "%b %e %Y", t);
        }
    }
}

/*
 * Print help message and exit.
 */
static void help() {
    /* TODO: add to this */
    printf("ls: List files\n");
    printf("\t--help: Print this help\n");
    exit(0);
}

/*
 * call this when there's been an error.
 * The function should:
 * - print a suitable error message (this is already implemented)
 * - set appropriate bits in err_code
 */
void handle_error(char* what_happened, char* fullname) {
    PRINT_ERROR("ls", what_happened, fullname);

    // General error
    err_code |= 64; 

    // cannot find file
    if (errno == ENOENT) {
        err_code |= 8; 
    } else if (errno == EACCES) {
        // cannot access
        err_code |= 16; 
    } else {
        // other errors
        err_code |= 32; 
    }
    return;
}

/*
 * test_file():
 * test whether stat() returns successfully and if not, handle error.
 * Use this to test for whether a file or dir exists
 */
bool test_file(char* pathandname) {
    struct stat sb;
    if (stat(pathandname, &sb)) {
        handle_error("cannot access", pathandname);
        return false;
    }
    return true;
}

/*
 * is_dir(): tests whether the argument refers to a directory.
 * precondition: test_file() returns true. that is, call this function
 * only if test_file(pathandname) returned true.
 */
bool is_dir(char* pathandname) {
    struct stat dir_stat;

    if (stat(pathandname, &dir_stat) == -1) {
        return false;
    }
    
    if (S_ISDIR(dir_stat.st_mode)) {
        return true;
    }

    return false;
}

/* convert the mode field in a struct stat to a file type, for -l printing */
const char* ftype_to_str(mode_t mode) {
    static char perms[11]; 

    // file type
    if (S_ISDIR(mode)) perms[0] = 'd';
    else if (S_ISREG(mode)) perms[0] = '-'; 
    else perms[0] = '?';

    // User
    perms[1] = (mode & S_IRUSR) ? 'r' : '-';
    perms[2] = (mode & S_IWUSR) ? 'w' : '-';
    perms[3] = (mode & S_IXUSR) ? 'x' : '-';

    // Group
    perms[4] = (mode & S_IRGRP) ? 'r' : '-';
    perms[5] = (mode & S_IWGRP) ? 'w' : '-';
    perms[6] = (mode & S_IXGRP) ? 'x' : '-';

    // Others
    perms[7] = (mode & S_IROTH) ? 'r' : '-';
    perms[8] = (mode & S_IWOTH) ? 'w' : '-';
    perms[9] = (mode & S_IXOTH) ? 'x' : '-';

    perms[10] = '\0';

    return perms;
}

/* list_file():
 * implement the logic for listing a single file.
 * This function takes:
 *   - pathandname: the directory name plus the file name.
 *   - name: just the name "component".
 *   - list_long: a flag indicated whether the printout should be in
 *   long mode.
 *
 *   The reason for this signature is convenience: some of the file-outputting
 *   logic requires the full pathandname (specifically, testing for a directory
 *   so you can print a '/' and outputting in long mode), and some of it
 *   requires only the 'name' part. So we pass in both. An alternative
 *   implementation would pass in pathandname and parse out 'name'.
 */
void list_file(char* pathandname, char* name, bool list_long, bool list_count) {
    struct stat file_stat;

    if (stat(pathandname, &file_stat) != 0) {
        handle_error("cannot access", pathandname);
        return;
    }

    // If counting filtes, increment count and return
    if (list_count) {
        count++;
        return;
    }

    // add slash if directory and not . or ..
    bool is_directory = S_ISDIR(file_stat.st_mode);
    bool is_special_dot = (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
    bool append_slash = is_directory && !is_special_dot;

    if (list_long) {
        char time_str[128];
        char user_str[32];
        char group_str[32];
        char output[1024];
        const char *file_info = ftype_to_str(file_stat.st_mode);
        
        // Username
        if (uname_for_uid(file_stat.st_uid, user_str, sizeof(user_str)) != 0) {
            snprintf(user_str, sizeof(user_str), "%d", file_stat.st_uid);
            err_code |= 96;
        }

        // Groupname
        if (group_for_gid(file_stat.st_gid, group_str, sizeof(group_str)) != 0) {
            snprintf(group_str, sizeof(group_str), "%d", file_stat.st_gid);
            err_code |= 96;
        }

        // Time
        date_string(&file_stat.st_mtim, time_str, sizeof(time_str));

        snprintf(output, sizeof(output), 
                "%s %4ld %s %s %6ld %s %s%s",
                file_info,
                (long)file_stat.st_nlink,
                user_str,
                group_str,
                (long)file_stat.st_size,
                time_str,
                name,
                append_slash ? "/" : ""); 

        printf("%s\n", output);
    } else {
        if (append_slash) {
            printf("%s/\n", name);
        } else {
            printf("%s\n", name);
        }
    }
}

/* list_dir():
 * implement the logic for listing a directory.
 * This function takes:
 *    - dirname: the name of the directory
 *    - list_long: should the directory be listed in long mode?
 *    - list_all: are we in "-a" mode?
 *    - recursive: are we supposed to list sub-directories?
 *    - list_count: are we supposed to count the number of files in a directory?
 */
void list_dir(char* dirname, bool list_long, bool list_all, bool recursive, bool list_count) {
    /* TODO: fill in
     *   You'll probably want to make use of:
     *       opendir()
     *       readdir()
     *       list_file()
     *       snprintf() [to make the 'pathandname' argument to
     *          list_file(). that requires concatenating 'dirname' and
     *          the 'd_name' portion of the dirents]
     *       closedir()
     *   See the lab description for further hints
     */

    DIR *dir = opendir(dirname);
    
    if (dir == NULL) {
        handle_error("cannot open directory", dirname);
        return; 
    }  

    if (recursive && !list_count) {
        printf("%s:\n", dirname);
    }

    struct dirent *entry;
    char full_path[1024]; 
    // int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files if not in list_all mode
        if (!list_all && entry->d_name[0] == '.') {
            continue;
        }

        // Combine path
        snprintf(full_path, sizeof(full_path), "%s/%s", dirname, entry->d_name);
    
        list_file(full_path, entry->d_name, list_long, list_count);
    }

    if (recursive) {
        // backward to the beginning of the directory
        rewinddir(dir);

        while ((entry = readdir(dir)) != NULL) {
            
            if (!list_all && entry->d_name[0] == '.') {
                continue;
            }

            // skip . and .. to avoid infinite loop
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            snprintf(full_path, sizeof(full_path), "%s/%s", dirname, entry->d_name);

            struct stat sb;
            if (stat(full_path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
                // printf("\n%s:\n", full_path); 
                if (!list_count){
                    printf("\n");
                }
                list_dir(full_path, list_long, list_all, recursive, list_count);
            }
        }
    }

    closedir(dir);
}

int main(int argc, char* argv[]) {
    // This needs to be int since C does not specify whether char is signed or
    // unsigned.
    int opt;
    err_code = 0;
    bool list_long = false, list_all = false, recursive = false, list_count = false;
    // We make use of getopt_long for argument parsing, and this
    // (single-element) array is used as input to that function. The `struct
    // option` helps us parse arguments of the form `--FOO`. Refer to `man 3
    // getopt_long` for more information.
    struct option opts[] = {
        {.name = "help", .has_arg = 0, .flag = NULL, .val = '\a'}};

    // This loop is used for argument parsing. Refer to `man 3 getopt_long` to
    // better understand what is going on here.
    while ((opt = getopt_long(argc, argv, "1anRl", opts, NULL)) != -1) {
        switch (opt) {
            case '\a':
                // Handle the case that the user passed in `--help`. (In the
                // long argument array above, we used '\a' to indicate this
                // case.)
                help();
                break;
            case '1':
                // Safe to ignore since this is default behavior for our version
                // of ls.
                break;
            case 'a':
                list_all = true;
                break;
                // TODO: you will need to add items here to handle the
                // cases that the user enters "-l" or "-R"
            case 'l':
                list_long = true;
                break;
            case 'n':
                list_count = true;
                break;
            case 'R':
                recursive = true;
                break;
            default:
                printf("Unimplemented flag %d\n", opt);
                break;
        }
    }

    // TODO: Replace this.
    if (optind == argc) {
        list_dir(".", list_long, list_all, recursive, list_count);
        exit(err_code);
    }

    for (int i = optind; i < argc; i++) {
        if (!test_file(argv[i])) { 
            continue; 
        }

        if (is_dir(argv[i])) {
            list_dir(argv[i], list_long, list_all, recursive, list_count);
        } else {
            list_file(argv[i], argv[i], list_long, list_count);
        }
    }

    if (list_count) {
        printf("%d\n", count);
    }
    exit(err_code);
}
