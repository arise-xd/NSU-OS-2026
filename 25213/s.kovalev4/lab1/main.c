#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ulimit.h>
#include <sys/resource.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>

struct option_data
{
    int option;
    char *argument;
};

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  -i         Print real and effective user/group IDs\n"
            "  -s         Become process group leader\n"
            "  -p         Print process, parent and process group IDs\n"
            "  -u         Print ulimit\n"
            "  -Uvalue    Change ulimit\n"
            "  -c         Print core file size limit\n"
            "  -Csize     Change core file size limit\n"
            "  -d         Print current working directory\n"
            "  -v         Print environment variables\n"
            "  -Vname=value Add or change environment variable\n",
            program);
}

static int print_ids(void)
{
    printf("Real user ID: %d\n", getuid());
    printf("Effective user ID: %d\n", geteuid());
    printf("Real group ID: %d\n", getgid());
    printf("Effective group ID: %d\n", getegid());

    return 0;
}

static int make_group_leader(void)
{
    if (setpgid(0, getpid()) == -1)
    {
        perror("setpgid");
        return 1;
    }

    printf("Process became a process group leader\n");

    return 0;
}

static int print_process_info(void)
{
    pid_t pgid = getpgid(getpid());

    if (pgid == -1)
    {
        perror("getpgid");
        return 1;
    }

    printf("Process ID: %d\n", getpid());
    printf("Parent process ID: %d\n", getppid());
    printf("Process group ID: %d\n", pgid);

    return 0;
}

static int print_ulimit(void)
{
    long limit = ulimit(UL_GETFSIZE);

    if (limit == -1)
    {
        perror("ulimit");
        return 1;
    }

    printf("Process limit: %ld\n", limit);

    return 0;
}

static int set_ulimit(const char *argument)
{
    char *endptr;

    errno = 0;

    off_t new_limit = (off_t)strtol(argument, &endptr, 10);

    if (*argument == '\0' ||
        *endptr != '\0' ||
        errno == ERANGE ||
        new_limit < 0)
    {
        fprintf(stderr, "Invalid value for -U\n");
        return 1;
    }

    if (ulimit(UL_SETFSIZE, new_limit) == -1)
    {
        perror("ulimit");
        return 1;
    }

    printf("New process limit is set\n");

    return 0;
}

static int print_core_limit(void)
{
    struct rlimit limit;

    if (getrlimit(RLIMIT_CORE, &limit) == -1)
    {
        perror("getrlimit");
        return 1;
    }

    if (limit.rlim_cur == RLIM_INFINITY)
    {
        printf("The core file size limit is unlimited\n");
    }
    else
    {
        printf("The largest size core file that may be created - %" PRIuMAX "\n",
               (uintmax_t)limit.rlim_cur);
    }

    return 0;
}

static int set_core_limit(const char *argument)
{
    struct rlimit limit;

    if (getrlimit(RLIMIT_CORE, &limit) == -1)
    {
        perror("getrlimit");
        return 1;
    }

    char *endptr;

    errno = 0;

    off_t new_limit = (off_t)strtol(argument, &endptr, 10);

    if (*argument == '\0' ||
        *endptr != '\0' ||
        errno == ERANGE ||
        new_limit < 0)
    {
        fprintf(stderr, "Invalid value for -C\n");
        return 1;
    }

    limit.rlim_cur = (rlim_t)new_limit;

    if (setrlimit(RLIMIT_CORE, &limit) == -1)
    {
        perror("setrlimit");
        return 1;
    }

    printf("New largest size core file that may be created is set\n");

    return 0;
}

static int print_current_directory(void)
{
    char current_directory[PATH_MAX];

    if (getcwd(current_directory, sizeof(current_directory)) == NULL)
    {
        perror("getcwd");
        return 1;
    }

    printf("Current directory -> %s\n", current_directory);

    return 0;
}

static int print_environment(void)
{
    extern char **environ;

    for (char **env = environ; *env != NULL; env++)
    {
        printf("%s\n", *env);
    }

    return 0;
}

static int set_environment(const char *argument)
{
    if (putenv((char *)argument) != 0)
    {
        perror("putenv");
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        usage(argv[0]);
        return 1;
    }
    
    int ch;

    struct option_data options[argc];
    int options_count = 0;

    while ((ch = getopt(argc, argv, ":ispuU:cC:dvV:")) != -1)
    {
        if (ch == '?')
        {
            fprintf(stderr, "Unknown option: -%c\n", optopt);
            usage(argv[0]);
            return 1;
        }

        if (ch == ':')
        {
            fprintf(stderr,
                    "Argument for option -%c is missing\n",
                    optopt);
            usage(argv[0]);
            return 1;
        }

        options[options_count].option = ch;
        options[options_count].argument = optarg;

        options_count++;
    }

    for (int i = options_count - 1; i >= 0; i--)
    {
        ch = options[i].option;

        switch (ch)
        {
        case 'i':
            if (print_ids() != 0)
                return 1;
            break;

        case 's':
            if (make_group_leader() != 0)
                return 1;
            break;

        case 'p':
            if (print_process_info() != 0)
                return 1;
            break;

        case 'u':
            if (print_ulimit() != 0)
                return 1;
            break;

        case 'U':
            if (set_ulimit(options[i].argument) != 0)
                return 1;
            break;

        case 'c':
            if (print_core_limit() != 0)
                return 1;
            break;

        case 'C':
            if (set_core_limit(options[i].argument) != 0) 
                return 1;
            break;

        case 'd':
            if (print_current_directory() != 0)
                return 1;
            break;

        case 'v':
            if (print_environment() != 0)
                return 1;
            break;

        case 'V':
            if (set_environment(options[i].argument) != 0)
                return 1;
            break;
        }
    }

    return 0;
}
