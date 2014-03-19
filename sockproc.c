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
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#if __APPLE__
#undef daemon
extern int daemon(int, int);
#endif

#define PIPE_READ 0
#define PIPE_WRITE 1

#define BUFFER_CHAIN_LINK_SIZE 16384 
#define SHELL_BIN "/bin/bash"
#define SHELL_ARG "-c"

struct buffer_chain_t {
    char bytes[BUFFER_CHAIN_LINK_SIZE];
    size_t len;
    struct buffer_chain_t* next;
};

struct sockaddr_un addr;

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


int create_child(int fd, const char* cmd, char* const argv[], char* const env[], const char* input) 
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

        /* pipe input to child, if provided */
        if (NULL != input) {
          write(stdin_pipe[PIPE_WRITE], input, strlen(input));
        }

        /* read output */
        out_buffers = read_pipe(stdout_pipe[PIPE_READ]);

        /* read error */
        err_buffers = read_pipe(stderr_pipe[PIPE_READ]);

        /* wait and get the exit code */
        waitpid(child_pid, &child_exit_code, 0);

        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "status:%d\n", child_exit_code);
        write(fd, buf, strlen(buf));

        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "%zu\n", total_bytes(out_buffers));
        write(fd, buf, strlen(buf));
        curr = out_buffers;
        while (curr) {
            write(fd, curr->bytes, curr->len);
            curr = curr->next;
        }

        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "%zu\n", total_bytes(err_buffers));
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
        close(stdin_pipe[PIPE_WRITE]);
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


int main(int argc, char *argv[])
{
    int fd, cl, rc;
    char buf[512];
    char *p, *end;
    int count, has_cmd;
    char *child_argv[4];
    char *socket_path;

    if (argc < 2) {
        printf("Usage: %s <socket-path>\n", argv[0]);
        exit(0);
    }

    daemon(0, 0);

    socket_path = strdup(argv[1]);
    unlink(socket_path);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));

    if (listen(fd, 32) == -1) {
      perror("listen error");
      exit(-1);
    }

    signal(SIGCHLD, SIG_IGN);

    while (1) {
        if ( (cl = accept(fd, NULL, NULL)) == -1) {
          perror("accept error");
        }

        if (fork()==0) {
            /* child */
            memset(buf, 0, sizeof(buf));
            p = buf; has_cmd = 0; count = sizeof(buf)-1;
            while (count > 0) {
                rc = read(cl, p, count);
                if (rc > 0) {
                    if ((end = strstr(buf, "\r\n"))) {
                        end[0] = '\0';
                        has_cmd = 1;
                        break;
                    }
                    p += rc;
                    count -= rc;
                }
            }
                    
            /* execute command */
            child_argv[0] = SHELL_BIN;
            child_argv[1] = SHELL_ARG;
            child_argv[2] = buf;
            child_argv[3] = 0;
            create_child(cl, child_argv[0], child_argv, NULL, NULL);
            close(cl);

            exit(0);
        }
        else {
            /* parent */
            close(cl);
        }
    }
}

