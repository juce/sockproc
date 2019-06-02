#if __APPLE__
/* In Mac OS X 10.5 and later trying to use the daemon function gives a “‘daemon’ is deprecated”
** error, which prevents compilation because we build with "-Werror".
** Since this is supposed to be portable cross-platform code, we don't care that daemon is
** deprecated on Mac OS X 10.5, so we use this preprocessor trick to eliminate the error message.
*/
#define daemon yes_we_know_that_daemon_is_deprecated_in_os_x_10_5_thankyou
#endif

#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#if __APPLE__
#undef daemon
extern int daemon(int, int);
#endif

#define PIPE_READ 0
#define PIPE_WRITE 1

#define BUFFER_CHAIN_LINK_SIZE 16384
#define SHELL_BIN "/bin/sh"
#define SHELL_ARG "-c"

struct buffer_chain_t {
    char bytes[BUFFER_CHAIN_LINK_SIZE];
    size_t len;
    struct buffer_chain_t* next;
};

struct sockaddr_un addr;
struct sockaddr_in addr_in;

char *socket_path;
char *pid_file;

void proc_exit() {}

struct buffer_chain_t* read_pipe(int fd)
{
    struct buffer_chain_t *buffers;
    struct buffer_chain_t *curr, *next;
    size_t count;
    ssize_t n, space;
    char *p;

    buffers = (struct buffer_chain_t*)malloc(sizeof(struct buffer_chain_t));
    if (!buffers) {
        perror("buffer allocation error");
        exit(-1);
    }
    memset(buffers, 0, sizeof(struct buffer_chain_t));
    curr = buffers;
    count = 0;
    space = BUFFER_CHAIN_LINK_SIZE;
    p = curr->bytes;
    while ((n = read(fd, p, space)) > 0) {
        p += n; count += n; space -= n;
        if (space == 0) {
            /* allocate new chain link, reset count */
            next = (struct buffer_chain_t*)malloc(sizeof(struct buffer_chain_t));
            if (!next) {
                perror("buffer allocation error");
                exit(-1);
            }
            memset(next, 0, sizeof(struct buffer_chain_t));
            curr->len = count;
            curr->next = next;
            curr = next;
            count = 0;
            space = BUFFER_CHAIN_LINK_SIZE;
            p = curr->bytes;
        }
    }
    curr->len = count;
    return buffers;
}


size_t total_bytes(struct buffer_chain_t* buffers)
{
    struct buffer_chain_t* curr;
    size_t count;

    curr = buffers;
    count = 0;

    while (curr) {
        count += curr->len;
        curr = curr->next;
    }
    return count;
}


void free_buffer_chain(struct buffer_chain_t* buffers)
{
    struct buffer_chain_t *curr, *next;

    curr = buffers;

    while (curr) {
        next = curr->next;
        free(curr);
        curr = next;
    }
}


int create_child(int fd, const char* cmd, char* const argv[], char* const env[], int fd_in, size_t in_byte_count)
{
    int stdin_pipe[2];
    int stdout_pipe[2];
    int stderr_pipe[2];
    int fork_result;
    int result;
    pid_t child_pid;
    int child_exit_code;
    char buf[32];
    struct buffer_chain_t *out_buffers;
    struct buffer_chain_t *err_buffers;
    struct buffer_chain_t *curr;
    ssize_t rc, count;
    char input_buf[2048];

    if (pipe(stdin_pipe) < 0) {
        perror("allocating pipe for child input redirect");
        return -1;
    }
    if (pipe(stdout_pipe) < 0) {
        close(stdin_pipe[PIPE_READ]);
        close(stdin_pipe[PIPE_WRITE]);
        perror("allocating pipe for child output redirect");
        return -1;
    }
    if (pipe(stderr_pipe) < 0) {
        close(stdin_pipe[PIPE_READ]);
        close(stdin_pipe[PIPE_WRITE]);
        close(stdout_pipe[PIPE_READ]);
        close(stdout_pipe[PIPE_WRITE]);
        perror("allocating pipe for child error redirect");
        return -1;
    }

    signal(SIGCHLD, proc_exit);

    fork_result = fork();

    if (fork_result == 0) {
        /* child continues here */

        /* unused duplicate of socket fd */
        close(fd);

        if (dup2(stdin_pipe[PIPE_READ], STDIN_FILENO) == -1) {
            perror("dup2: stdin");
            return -1;
        }
        if (dup2(stdout_pipe[PIPE_WRITE], STDOUT_FILENO) == -1) {
            perror("dup2: stdout");
            return -1;
        }
        if (dup2(stderr_pipe[PIPE_WRITE], STDERR_FILENO) == -1) {
            perror("dup2: stderr");
            return -1;
        }

        /* all these are for use by parent only */
        close(stdin_pipe[PIPE_READ]);
        close(stdin_pipe[PIPE_WRITE]);
        close(stdout_pipe[PIPE_READ]);
        close(stdout_pipe[PIPE_WRITE]);
        close(stderr_pipe[PIPE_READ]);
        close(stderr_pipe[PIPE_WRITE]);

        /* run child process image */
        result = execve(cmd, argv, env);

        /* if we got here, then an error had occurred */
        perror("should not get here");
        exit(result);

    } else if (fork_result > 0) {
        /* parent continues here */
        child_pid = fork_result;

        /* close unused file descriptors, these are for child only */
        close(stdin_pipe[PIPE_READ]);
        close(stdout_pipe[PIPE_WRITE]);
        close(stderr_pipe[PIPE_WRITE]);

        /* write input to child, if provided */
        if (fd_in != -1) {
            count = in_byte_count;
            while (count > 0) {
                rc = read(fd_in, input_buf, sizeof(input_buf));
                if (rc == 0) {
                    break;
                }
                write(stdin_pipe[PIPE_WRITE], input_buf, rc);
                count -= rc;
            }
        }
        close(stdin_pipe[PIPE_WRITE]);

        /* read output */
        out_buffers = read_pipe(stdout_pipe[PIPE_READ]);

        /* read error */
        err_buffers = read_pipe(stderr_pipe[PIPE_READ]);

        /* wait and get the exit code */
        waitpid(child_pid, &child_exit_code, 0);

        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "status:%d\r\n", child_exit_code);
        write(fd, buf, strlen(buf));

        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "%zu\r\n", total_bytes(out_buffers));
        write(fd, buf, strlen(buf));
        curr = out_buffers;
        while (curr) {
            write(fd, curr->bytes, curr->len);
            curr = curr->next;
        }

        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "%zu\r\n", total_bytes(err_buffers));
        write(fd, buf, strlen(buf));
        curr = err_buffers;
        while (curr) {
            write(fd, curr->bytes, curr->len);
            curr = curr->next;
        }

        /* free memory */
        free_buffer_chain(out_buffers);
        free_buffer_chain(err_buffers);

        /* close file descriptors */
        close(stdout_pipe[PIPE_READ]);
        close(stderr_pipe[PIPE_READ]);

    } else {
        /* failed to create child. Not much to do at this point
         * since we don't log anything */
        close(stdin_pipe[PIPE_READ]);
        close(stdin_pipe[PIPE_WRITE]);
        close(stdout_pipe[PIPE_READ]);
        close(stdout_pipe[PIPE_WRITE]);
        close(stderr_pipe[PIPE_READ]);
        close(stderr_pipe[PIPE_WRITE]);
    }

    return fork_result;
}

void terminate(int sig)
{
    /* remove unix-socket-path */
    if (socket_path != NULL) {
        unlink(socket_path);
    }

    /* remove pid_file */
    if (pid_file != NULL) {
        unlink(pid_file);
    }

    /* restore and raise signals */
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(int argc, char *argv[], char *envp[])
{
    int i, fd, cl, rc;
    char buf[2048];
    char *p, *end, *bc;
    int count;
    size_t data_len;
    char *child_argv[4];
    int port;
    FILE* f;
    int daemonize = 1;
    int reuseaddr = 1;

    if (argc < 2 || (argc >= 2 && argv[1][0] == '-')) {
        printf("Usage: %s (<unix-socket-path>|<tcp-port>) {pidfile} [--foreground]\n", argv[0]);
        return 2;
    }

    pid_file = NULL;
    socket_path = strdup(argv[1]);

    if (sscanf(socket_path, "%d", &port) == 1) {
        socket_path = NULL;
        /* tcp socket on localhost interface */
        fd = socket(AF_INET, SOCK_STREAM, 0);
        memset(&addr_in, 0, sizeof(addr_in));
        addr_in.sin_family = AF_INET;
        addr_in.sin_port = htons(port);
        addr_in.sin_addr.s_addr = inet_addr("127.0.0.1");

        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int));

        if (bind(fd, (struct sockaddr*)&addr_in, sizeof(addr_in)) == -1) {
            perror("bind error");
            return errno;
        }
    }
    else {
        if (access(socket_path, X_OK) != -1) {
            errno = EEXIST;
            perror("socket_path error");
            return errno;
        }
        /* unix domain socket */
        unlink(socket_path);
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int));

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("bind error");
            return errno;
        }
    }

    if (listen(fd, 32) == -1) {
        perror("listen error");
        return errno;
    }

    /* check foreground flag */
    for (i=2; i<=argc-1; i++) {
        if (strcmp(argv[i], "--foreground")==0) {
            daemonize = 0;
            break;
        }
    }

    if (daemonize) {
       daemon(0, 0);
    }

    if (argc > 2) {
        if (strcmp(argv[2], "--foreground")==0) {
            if (argc > 3) {
                pid_file = strdup(argv[3]);
            }
        }
        else {
            pid_file = strdup(argv[2]);
        }

        /* write pid to a file, if asked to do so */
        f = fopen(pid_file, "w");
        if (f) {
            fprintf(f, "%d", getpid());
            fclose(f);
        }
    }

    signal(SIGCHLD, SIG_IGN);
    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);

    while (1) {
        if ( (cl = accept(fd, NULL, NULL)) == -1) {
            perror("accept error");
            return errno;
        }

        if (fork()==0) {
            /* child */

            /* unused duplicate of listening socket */
            close(fd);

            memset(buf, 0, sizeof(buf));
            p = bc = buf; count = sizeof(buf)-1;
            while (count > 0) {
                rc = read(cl, p, 1);
                if (rc == 0) {
                    break;
                }
                if ((end = strstr(buf, "\r\n"))) {
                    end[0] = '\0';
                    bc = end + 2;
                    break;
                }
                p += rc;
                count -= rc;
            }

            data_len = 0;
            p = bc;
            do {
                if ((end = strstr(bc, "\r\n"))) {
                    break;
                }
                rc = read(cl, p, 1);
                if (rc == 0) {
                    break;
                }
                p += rc;
                count -= rc;
            }
            while (count > 0);
            sscanf(bc, "%zu", &data_len);

            /* execute command */
            child_argv[0] = SHELL_BIN;
            child_argv[1] = SHELL_ARG;
            child_argv[2] = buf;
            child_argv[3] = 0;
            create_child(cl, child_argv[0], child_argv, envp, cl, data_len);

            exit(0);
        }
        else {
            /* parent */
            close(cl);
        }
    }
}

