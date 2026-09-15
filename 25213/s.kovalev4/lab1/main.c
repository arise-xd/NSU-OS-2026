#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ulimit.h>
#include <sys/resource.h>
#include <errno.h>   
#include <limits.h>

struct option_data
{
    int option;
    char *argument;
};

int main(int argc, char *argv[])
{
    int ch;

    struct option_data options[argc];
    int options_count = 0;

    while ((ch = getopt(argc, argv, ":ispuU:cC:dvV:")) != -1)
    {   

        if (ch == '?'){
            fprintf(stderr, "Unknown option: -%c\n", optopt);
            return 1;
        }
        if (ch == ':'){
            fprintf(stderr, "Argument for option -%c is missing\n", optopt);
            return 1;
        }
        options[options_count].option = ch;
        options[options_count].argument = optarg;

        options_count++;
    }

    for (int i = options_count - 1; i >= 0; i--)
    {
        ch = options[i].option;

        optarg = options[i].argument;

        switch (ch)
        {
        case 'i':
            printf("Real user ID: %d\n", getuid());
            printf("Effective user ID: %d\n", geteuid());
            printf("Real group ID: %d\n", getgid());
            printf("Effective group ID: %d\n", getegid());

            break;

        case 's':
            if (setpgid(0, getpid()) == -1)
            {
                perror("setpgid");
                return 1;
            }

            printf("Process became a process group leader\n");

            break;

        case 'p':
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

            break;
        }

        case 'u':
        {
            long limit = ulimit(UL_GETFSIZE);

            if (limit == -1)
            {
                perror("ulimit");
                return 1;
            }

            printf("Process limit: %ld\n", limit);

            break;
        }

        case 'U':
        {
            char *endptr;

            errno = 0;

            long new_limit = strtol(optarg, &endptr, 10);

            if (*optarg == '\0' || *endptr != '\0' || errno == ERANGE || new_limit < 0)
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

            break;
        }

        case 'c':
        {
            struct rlimit limit;

            if (getrlimit(RLIMIT_CORE, &limit) == -1)
            {
                perror("getrlimit");
                return 1;
            }

            printf("The largest size core file that may be created - %lld\n",
                   (long long)limit.rlim_cur);

            break;
        }

        case 'C':
        {
            struct rlimit limit;

            if (getrlimit(RLIMIT_CORE, &limit) == -1)
            {
                perror("getrlimit");
                return 1;
            }

            char *endptr;

            errno = 0;

            long new_limit = strtol(optarg, &endptr, 10);

            if (*optarg == '\0' || *endptr != '\0' || errno == ERANGE || new_limit < 0)
            {
                fprintf(stderr, "Invalid value for -C\n");
                return 1;
            }

            limit.rlim_cur = new_limit;

            if (setrlimit(RLIMIT_CORE, &limit) == -1)
            {
                perror("setrlimit");
                return 1;
            }

            printf("New largest size core file that may be created is set\n");

            break;
        }

        case 'd':
        {
            char current_directory[PATH_MAX];

            if (getcwd(current_directory, sizeof(current_directory)) == NULL)
            {
                perror("getcwd");
                return 1;
            }

            printf("Current directory -> %s\n", current_directory);

            break;
        }

        case 'v':
        {
            extern char **environ;

            for (char **env = environ; *env != NULL; env++)
            {
                printf("%s\n", *env);
            }

            break;
        }

        case 'V':
            if (putenv(optarg) != 0)
            {
                perror("putenv");
                return 1;
            }
            break;
        }
    }
    return 0;
}