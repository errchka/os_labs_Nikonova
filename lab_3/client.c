#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <errno.h>
#define MAX_LINE 8192

struct slot {
    size_t len;
    int eof;
    char data[MAX_LINE];
};
static void must_sem_wait(sem_t *s)
{
    while (sem_wait(s) < 0) {
        if (errno == EINTR) continue;
        write(STDERR_FILENO, "sem_wait failed\n", 16);
        _exit(1);
    }
}

static void must_sem_post(sem_t *s)
{
    if (sem_post(s) < 0) {
        write(STDERR_FILENO, "sem_post failed\n", 16);
        _exit(1);
    }
}
int main(int argc, char **argv)
{
    if (argc < 7) {
        write(STDERR_FILENO, "bad args\n", 9);
        return 1;
    }

    const char *shm1_name = argv[1];
    const char *shm2_name = argv[2];
    const char *s1r_name  = argv[3];
    const char *s1f_name  = argv[4];
    const char *s2r_name  = argv[5];
    const char *s2f_name  = argv[6];
    int shm1_fd = shm_open(shm1_name, O_RDWR, 0600);
    int shm2_fd = shm_open(shm2_name, O_RDWR, 0600);
    if (shm1_fd < 0 || shm2_fd < 0) return 1;

    struct slot *to_client = mmap(NULL, sizeof(struct slot), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, shm1_fd, 0);
    struct slot *to_server = mmap(NULL, sizeof(struct slot), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, shm2_fd, 0);
    if (to_client == MAP_FAILED || to_server == MAP_FAILED) return 1;
    close(shm1_fd);
    close(shm2_fd);

    sem_t *s1_ready = sem_open(s1r_name, 0);
    sem_t *s1_free  = sem_open(s1f_name, 0);
    sem_t *s2_ready = sem_open(s2r_name, 0);
    sem_t *s2_free  = sem_open(s2f_name, 0);
    if (s1_ready == SEM_FAILED || s1_free == SEM_FAILED ||
        s2_ready == SEM_FAILED || s2_free == SEM_FAILED) return 1;

    for (;;) {
        must_sem_wait(s1_ready);        
        size_t len = to_client->len;
        int eof = to_client->eof;

        if (eof) {
            must_sem_post(s1_free);
            must_sem_wait(s2_free);
            to_server->len = 0;
            to_server->eof = 1;
            must_sem_post(s2_ready);
            break;
        }
        char *line = malloc(len + 1);
        if (!line) {
            must_sem_post(s1_free);
            must_sem_wait(s2_free);
            to_server->len = 0;
            to_server->eof = 1;
            must_sem_post(s2_ready);
            break;
        }
        memcpy(line, to_client->data, len);
        line[len] = '\0';
        must_sem_post(s1_free);           /* слот свободен, server может писать */

        size_t check_len = len;
        if (check_len > 0 && line[check_len - 1] == '\n') check_len--;

        int valid = 0;
        if (check_len > 0) {
            char last = line[check_len - 1];
            if (last == '.' || last == ';') valid = 1;
        }

        if (valid) {
            if (write(STDOUT_FILENO, line, len) != (ssize_t)len) {
                write(STDERR_FILENO, "write failed\n", 13);
                _exit(1);
            }
        } else {
            const char *pfx = "INVALID: ";
            size_t plen = strlen(pfx);
            int need_nl = (len == 0 || line[len - 1] != '\n');

            must_sem_wait(s2_free);
            size_t off = 0;
            if (plen > MAX_LINE) plen = MAX_LINE;
            memcpy(to_server->data, pfx, plen); off += plen;
            size_t copy = len;
            if (off + copy > MAX_LINE) copy = MAX_LINE - off;
            memcpy(to_server->data + off, line, copy); off += copy;
            if (need_nl && off < MAX_LINE) to_server->data[off++] = '\n';
            to_server->len = off;
            to_server->eof = 0;
            must_sem_post(s2_ready);
        }
        free(line);
    }

    munmap(to_client, sizeof(struct slot));
    munmap(to_server, sizeof(struct slot));
    sem_close(s1_ready); sem_close(s1_free);
    sem_close(s2_ready); sem_close(s2_free);
    return 0;
}