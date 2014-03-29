#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "buffer.h"

static void test1() {
    struct Buffer *buffer;
    char input[] = "This is a test.";
    char output[sizeof(input)];
    int len, i;

    buffer = new_buffer(200);
    assert(buffer != NULL);

    len = buffer_push(buffer, input, sizeof(input));
    assert(len == sizeof(input));


    len = buffer_peek(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    /* second peek to ensure the first didn't permute the state of the buffer */
    len = buffer_peek(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    /* test pop */
    len = buffer_pop(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    len = buffer_pop(buffer, output, sizeof(output));
    assert(len == 0);

    free_buffer(buffer);
}

static void test2() {
    struct Buffer *buffer;
    char input[] = "Testing wrap around behaviour.";
    char output[sizeof(input)];
    int len, i = 0;

    buffer = new_buffer(200);
    assert(buffer != NULL);

    while (i < 180) {
        len = buffer_push(buffer, input, sizeof(input));
        assert(len == sizeof(input));

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

static void test3() {
    struct Buffer *buffer;
    char input[] = "Test buffer resizing.";
    char output[sizeof(input)];
    int len, i;

    buffer = new_buffer(200);
    assert(buffer != NULL);

    len = buffer_push(buffer, input, sizeof(input));
    assert(len == sizeof(input));

    /* Test resizing to too small of a buffer size */
    len = buffer_resize(buffer, 5);
    assert(len == -1);


    buffer_resize(buffer, 40);

    len = buffer_peek(buffer, output, sizeof(output));
    assert(len == sizeof(input));

    for (i = 0; i < len; i++)
        assert(input[i] == output[i]);

    free_buffer(buffer);
}

static void test4() {
    struct Buffer *buffer;
    int read_fd, write_fd;

    buffer = new_buffer(4096);

    read_fd = open("/dev/zero", O_RDONLY);
    if (read_fd < 0) {
        perror("open:");
        exit(1);
    }

    write_fd = open("/dev/null", O_WRONLY);
    if (write_fd < 0) {
        perror("open:");
        exit(1);
    }

    while (buffer->tx_bytes < 65536) {
        buffer_read(buffer, read_fd);
        buffer_write(buffer, write_fd);
    }
}

int main() {
    test1();

    test2();

    test3();

    test4();
}
