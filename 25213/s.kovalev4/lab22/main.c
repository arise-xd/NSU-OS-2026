#define _FILE_OFFSET_BITS 64

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <poll.h>

struct source 
{
    int fd;
    const char *path;
    char *buffer;
    size_t begin;
    size_t end;
    size_t capacity;
};

enum 
{ 
    TIME_OUT = 5 
};

static volatile sig_atomic_t timeout_expired = 0;
static sigset_t alarm_set;
static sigset_t wait_mask;

static void handle_alarm(int signo)
{
    (void)signo;
    timeout_expired = 1;
}

static int initialize_signals(void)
{
    struct sigaction action = {0};

    action.sa_handler = handle_alarm;

    if (sigemptyset(&action.sa_mask) == -1) {
        perror("sigemptyset");
        return -1;
    }

    if (sigaction(SIGALRM, &action, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    action.sa_handler = SIG_IGN;

    if (sigaction(SIGPIPE, &action, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    if (sigemptyset(&alarm_set) == -1) {
        perror("sigemptyset");
        return -1;
    }

    if (sigaddset(&alarm_set, SIGALRM) == -1) {
        perror("sigaddset");
        return -1;
    }

    if (sigprocmask(SIG_BLOCK, &alarm_set, &wait_mask) == -1) {
        perror("sigprocmask");
        return -1;
    }

    if (sigdelset(&wait_mask, SIGALRM) == -1) {
        perror("sigdelset");
        return -1;
    }

    return 0;
}

static int cancel_alarm(void)
{
    const struct timespec no_wait = {0, 0};

    (void)alarm(0);

    for (;;) {
        int result = sigtimedwait(&alarm_set, NULL, &no_wait);

        if (result == SIGALRM) {
            continue;
        }

        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN) {
                return 0;
            }

            perror("sigtimedwait");
            return -1;
        }
    }
}

static int read_with_timeout(struct source *source, char *block, size_t capacity, ssize_t *bytes_read)
{
    struct pollfd descriptor = {
        .fd = source->fd,
        .events = POLLIN,
        .revents = 0
    };

    int status = -1;
    int readiness_reported = 0;

    timeout_expired = 0;
    (void)alarm(TIME_OUT);

    for (;;) {
        if (timeout_expired) {
            status = 1;
            break;
        }

        *bytes_read = read(source->fd, block, capacity);

        if (*bytes_read >= 0) {
            status = 0;
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror(source->path);
            break;
        }

        if (readiness_reported) {
            perror(source->path);
            break;
        }

        descriptor.revents = 0;

        int result = ppoll(&descriptor, 1, NULL, &wait_mask);

        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("ppoll");
            break;
        }

        if (descriptor.revents & POLLNVAL) {
            (void)fprintf(stderr, "%s: invalid file descriptor\n", source->path);
            break;
        }

        if (result > 0 && !(descriptor.revents & (POLLIN | POLLHUP | POLLERR))) {
            (void)fprintf(stderr, "%s: unexpected poll event\n", source->path);
            break;
        }

        if (result > 0) {
            readiness_reported = 1;
        }
    }

    if (cancel_alarm() == -1) {
        return -1;
    }

    return status;
}

static int destroy_sources(struct source *sources, size_t count)
{
    int result = 0;
    for (size_t i = 0; i < count; i++) {
        if (sources[i].fd >= 0) {
            if (close(sources[i].fd) == -1) {
                perror(sources[i].path);
                result = -1;
            }
        }
        free(sources[i].buffer);
    }

    return result;
}

static int initialize_sources(struct source *sources, size_t count, char *const paths[])
{
    for (size_t i = 0; i < count; i++) {
        sources[i].fd = -1;
        sources[i].path = paths[i];
        sources[i].buffer = NULL;
        sources[i].begin = 0;
        sources[i].end = 0;
        sources[i].capacity = 0;

        sources[i].fd = open(sources[i].path, O_RDONLY | O_NONBLOCK | O_NOCTTY);
        if (sources[i].fd == -1) {
            perror(sources[i].path);
            (void)destroy_sources(sources, i + 1);
            return -1;
        }
    }

    return 0;
}

static int reserve_buffer(struct source *source, size_t required)
{
    if (required <= source->capacity) {
        return 0;
    }

    size_t new_capacity = source->capacity;

    if (new_capacity == 0) {
        new_capacity = BUFSIZ;
    }

    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2;
    }

    char *new_buffer = realloc(source->buffer, new_capacity);

    if (new_buffer == NULL) {
        perror("realloc");
        return -1;
    }

    source->buffer = new_buffer;
    source->capacity = new_capacity;
    return 0;
}

static int append_data(struct source *source, const char *data, size_t count)
{
    if (count == 0) {
        return 0;
    }

    if (source->begin > 0) {
        size_t remaining = source->end - source->begin;

        if (remaining > 0) {
            memmove(source->buffer, source->buffer + source->begin, remaining);
        }

        source->begin = 0;
        source->end = remaining;
    }

    if (count > SIZE_MAX - source->end) {
        (void)fprintf(stderr, "Required buffer size is too large\n");
        return -1;
    }

    if (reserve_buffer(source, source->end + count) == -1) {
        return -1;
    }

    memcpy(source->buffer + source->end, data, count);
    source->end += count;

    return 0;
}

static size_t line_length(const struct source *source)
{   
    if (source->begin == source->end) {
        return 0;
    }

    const char *newline = memchr(source->buffer + source->begin, '\n', source->end - source->begin);

    if (newline == NULL) {
        return 0;
    }

    return (size_t)(newline - (source->buffer + source->begin)) + 1;
}

static int write_all(const char *data, size_t count)
{
    size_t written = 0;
    while (written < count) {
        size_t remaining = count - written;
        size_t chunk = (BUFSIZ >= remaining ? remaining : BUFSIZ);
        ssize_t write_result = write(STDOUT_FILENO, data + written, chunk);
        if (write_result == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("write");
            return -1;
        }

        if (write_result == 0) {
            (void)fprintf(stderr, "write: no progress\n");
            return -1;
        }

        written += (size_t)write_result;
    }

    return 0;
}

static int output_data(struct source *source, size_t count)
{
    if (count == 0) {
        return 0;
    }

    if (write_all(source->buffer + source->begin, count) == -1) {
        return -1;
    }

    source->begin += count;
    if (source->begin == source->end) {
        source->begin = 0;
        source->end = 0;
    }
    return 0;
}

static int process_source(struct source *source)
{
    char block[BUFSIZ];

    for (;;) {
        size_t line_len = line_length(source);

        if (line_len != 0) {
            return output_data(source, line_len);
        }

        ssize_t bytes_read;

        int read_status = read_with_timeout(source, block, sizeof block, &bytes_read);

        if (read_status == -1) {
            return -1;
        }

        if (read_status == 1) {
            return 0;
        }

        if (bytes_read > 0) {
            if (append_data(source, block, (size_t)bytes_read) == -1) {
                return -1;
            }

            continue;
        }

        if (output_data(source, source->end - source->begin) == -1) {
            return -1;
        }

        int close_result = close(source->fd);
        source->fd = -1;

        if (close_result == -1) {
            perror(source->path);
            return -1;
        }

        return 0;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: %s file...\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (initialize_signals() == -1) {
        return EXIT_FAILURE;
    }

    size_t source_count = (size_t)(argc - 1);
    struct source sources[source_count];
    if (initialize_sources(sources, source_count, argv + 1) == -1) {
        return EXIT_FAILURE;
    }

    size_t active_count = source_count;

    while (active_count > 0) {
        for (size_t i = 0; i < source_count; i++) {
            if (sources[i].fd == -1) {
                continue;
            }
            if (process_source(&sources[i]) == -1) {
                (void)destroy_sources(sources, source_count);
                return EXIT_FAILURE;
            }
            if (sources[i].fd == -1) {
                active_count -= 1;
            }
        }
    }

    if (destroy_sources(sources, source_count) == -1) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
