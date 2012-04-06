#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h> /* close() */
#include <stdlib.h> /* exit() */
#include <assert.h>
#include <time.h>
#include "buffer.h"

void copy(const char *src, const char *dst) {
    struct Buffer *buffer;
    int in_fd, out_fd;
    int bytes;
    int bytes_read = 0;
    int bytes_written = 0;;

    printf("Copying %s to %s\n", src, dst);

    buffer = new_buffer();

    in_fd = open(src, O_RDONLY);
    if (in_fd < 0)
        perror("open():");

    out_fd = open(dst, O_WRONLY|O_CREAT, S_IRWXU);
    if (out_fd < 0)
        perror("open():");

    while ((bytes = buffer_read(buffer, in_fd)) > 0) {
        if (bytes < 0) {
            perror("readv():");
            exit(1);
        }
        bytes_read += bytes;

        bytes = buffer_write(buffer, out_fd);
        if (bytes < 0) {
            perror("writev():");
            exit(1);
        }
        bytes_written += bytes;
    }

    while (bytes_read > bytes_written) {
        bytes = buffer_write(buffer, out_fd);
        if (bytes < 0) {
            perror("writev():");
            exit(1);
        }
        bytes_written += bytes;
    }

    close(in_fd);
    close(out_fd);
    
    free_buffer(buffer);

    printf("copied %d bytes\n", bytes_written);
}

int random_writer(long size) {
    int pipe_fd[2];
    int fd;
    int result;
    pid_t pid;
    int len;
    char buffer[1024];
    struct timespec ts;

    result = pipe(pipe_fd);
    if (result < 0) {
        perror("pipe");
        exit(1);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) { /* child */
        close (pipe_fd[0]);

        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            perror("open");
            exit(1);
        }
        
        while (size > 0) {
            len = (rand() + 1) % sizeof(buffer);
            
            len = read(fd, buffer, len);
            if (len == 0) {
                /* Reached end of /dev/urandom, wait for entropy pool */
                sleep(1);
                continue;
            } else if (len < 0) {
                perror("read");
                exit(1);
            }
        
            size -= len;

            len = write(pipe_fd[1], buffer, len);
            if (len < 0) {
                perror("write");
                exit(1);
            }

            ts.tv_sec = 0;
            ts.tv_nsec = 5000;
            nanosleep(&ts, NULL);
        }
        exit(0);
    }
    /* parent */
    close(pipe_fd[1]);

    return pipe_fd[0]; /* read end */
}

void test3() {
    struct Buffer *buffer;
    char filename[TMP_MAX];
    int in_fd, out_fd;
    int len;
    
    buffer = new_buffer();

    tmpnam(filename);

    in_fd = random_writer(64 * 1024);
    if (in_fd < 0)
        perror("open():");

    out_fd = open(filename, O_WRONLY | O_CREAT, S_IRWXU);
    if (out_fd < 0)
        perror("open():");

    do {
        len = buffer_read(buffer, in_fd);
        len = buffer_write(buffer, out_fd);
    } while (len);

    close(out_fd);

    
}

void test1() {
    struct Buffer *buffer;
    char input[] = "This is a test.";
    char output[sizeof(input)];
    int len, i;

    buffer = new_buffer();

    len = buffer_push(buffer, input, sizeof(input));
    assert(len == sizeof(input));


    len = buffer_peek(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    len = buffer_peek(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    len = buffer_pop(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    len = buffer_pop(buffer, output, sizeof(output));
    assert(len == 0);
    
    free_buffer(buffer);
}

void test2() {
    struct Buffer *buffer;
    char input[] = "Testing wrap around behaviour.";
    char output[sizeof(input)];
    int len, i = 0;

    buffer = new_buffer();

    while (i < 4080) {
        len = buffer_push(buffer, input, sizeof(input));
        assert(len  == sizeof(input));

        i += len;
    }
    
    while (len) {
        len = buffer_pop(buffer, output, sizeof(output));
    }

    len = buffer_push(buffer, input, sizeof(input));
    assert(len == sizeof(input));


    len = buffer_peek(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    len = buffer_pop(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    len = buffer_push(buffer, input, sizeof(input));
    assert(len == sizeof(input));


    len = buffer_peek(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    free_buffer(buffer);
}

int main() {
    test1();

    test2();

    test3();
}


