#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s file\n", argv[0]);
        return EXIT_FAILURE;
    }

    int fd = open(argv[1], O_RDWR);

    if (fd == -1) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    struct flock lock = {0};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;

    if (fcntl(fd, F_SETLK, &lock) == -1) {
        perror("fcntl(F_SETLK)");
        close(fd);
        return EXIT_FAILURE;
    }

    if (setenv("LAB8_FILE", argv[1], 1) == -1) {
        perror("setenv");
        close(fd);
        return EXIT_FAILURE;
    }

    int status = system("/usr/bin/vi -- \"$LAB8_FILE\"");

    if (status == -1) {
        perror("system");
        close(fd);
        return EXIT_FAILURE;
    }

    close(fd);

    if (status == 0) {
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}
