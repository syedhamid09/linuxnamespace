/*
   Used to experiment with Linux namespaces, and can be used in conjunction
   with a set of tutorials to be found at https://www.windsock.io.

   Needs to be compiled with the POSIX real-time extensions library, librt, to
   make use of the message queue API.
*/

#define _GNU_SOURCE
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <mqueue.h>
#include <time.h>


// Define size of space for stack used by child process
#define STACK_SIZE (64 * 1024)

// Define message queue name
const char *mq_name = "/ipc_namespace";

struct arguments {
    int  verbose;
    int  flags;
    int  ipc;
    int  jail;
    char *path;
    char *hostname;
    char **command;
};


// Function to print command usage
static void usage(char *prog)
{
    fprintf(stderr, "\nUsage: %s [options] [cmd [arg...]]\n", prog);
    fprintf(stderr, "Options can be:\n");
    fprintf(stderr, "    -h           display this help message\n");
    fprintf(stderr, "    -v           display verbose messages\n");
    fprintf(stderr, "    -p           new PID namespace\n");
    fprintf(stderr, "    -m           new MNT namespace\n");
    fprintf(stderr, "    -u hostname  new UTS namespace with associated hostname\n");
    fprintf(stderr, "    -n           new NET namespace\n");
    fprintf(stderr, "    -i no|yes    create message queue in new IPC namespace (yes), or\n                 default namespace (no)\n");
    fprintf(stderr, "    -c dir       jail process in specified directory\n");
}


// Prepare message queue
mqd_t prepareMQ(void *child_args)
{
    struct arguments *args = child_args;
    mqd_t            mq;
    int              oflags = O_CREAT | O_RDWR;
    struct mq_attr   attr;

    attr.mq_flags   = 0;
    attr.mq_maxmsg  = 10;
    attr.mq_msgsize = 81;
    attr.mq_curmsgs = 0;

    mq = mq_open(mq_name, oflags, 0644, &attr);
    if (mq != -1) {
        if (args->verbose)
            printf("Parent: opening message queue %s\n", mq_name);
    }
    else
        perror("Parent: prepareMQ: mq_open");

    return mq;
}


// Function passed to the clone system call
int childFunction(void *child_args)
{
    struct arguments *args = child_args;
    mqd_t            mq;
    int              oflags = O_WRONLY;
    struct mq_attr   attr;
    char             *msg;

    if (args->verbose)
        printf(" Child: PID of child is %d\n", getpid());

    // Send message to parent if -i option provided
    if (args->ipc) {
        if (args->verbose)
            printf(" Child: opening message queue %s\n", mq_name);
        mq = mq_open(mq_name, oflags);
        if (mq != -1) {
            if (mq_getattr(mq, &attr) != -1) {
                msg = malloc(attr.mq_msgsize);
                printf("\n     Child: enter a message to send to the parent process (MAX 80 chars)\n     >> ");
                if (fgets(msg, attr.mq_msgsize, stdin) != NULL) {
                    msg[strcspn(msg, "\n")] = '\0';
                    printf("\n");
                    if (args->verbose)
                        printf(" Child: sending message to parent\n");
                    if (mq_send(mq, msg, attr.mq_msgsize,0) == -1)
                        perror(" Child: childFunction: mq_send");
                }
                else
                    perror(" Child: childFunction: fgets");
                if (args->verbose)
                    printf(" Child: closing message queue %s\n", mq_name);
                if (mq_close(mq) == -1)
                    perror(" Child: childFunction: mq_close");
                free(msg);
            }
            else
                perror(" Child: childFunction: mq_getattr");
        }
        else
            perror(" Child: childFunction: mq_open");
    }

    // If specified, place process in chroot jail
    if (args->jail) {
        if (args->verbose)
            printf(" Child: creating chroot jail\n");
        if (chroot(args->path) == -1) {
            perror(" Child: childFunction: chroot");
            exit(EXIT_FAILURE);
        }
        else {
            if (args->verbose)
                printf(" Child: changing directory into chroot jail\n");
            if (chdir("/") == -1) {
                perror(" Child: childFunction: chdir");
                exit(EXIT_FAILURE);
            }
            if (access("/proc", F_OK) != 0)
                if (mkdir("/proc", 0555) == -1) {
                    perror(" Child: childFunction: mkdir");
                    exit(EXIT_FAILURE);
                }
        }
    }

    // Mount new proc instance in new mount namespace if and only if
    // the child exists in both a new PID and MNT namespace
    if ((args->flags & CLONE_NEWPID) && (args->flags & CLONE_NEWNS)) {
        if (!args->jail)
            if (mount("none", "/proc", "", MS_REC|MS_PRIVATE, NULL) == -1)
                perror(" Child: childFunction: mount");
        if (mount("proc", "/proc", "proc", 0, NULL) == -1)
            perror(" Child: childFunction: mount");
    }

    // Set new hostname in UTS namespace if applicable
    if (args->flags & CLONE_NEWUTS)
        if (sethostname(args->hostname, strlen(args->hostname)) == -1)
            perror(" Child: childFunction: sethostname");

    // Execute command if given
    if (args->command != NULL) {
        if (clearenv() != 0)
            fprintf(stderr, " Child: childFunction: couldn't clear environment\n");
        if (args->verbose)
            printf(" Child: executing command %s ...\n", args->command[0]);
        execvp(args->command[0], &args->command[0]);
    }
    else
        exit(EXIT_SUCCESS);

    perror(" Child: childFunction: execvp");
    exit(EXIT_FAILURE);
}



int main(int argc, char *argv[])
{
    char             *child_stack;
    int              i;
    int              option;
    int              flags = 0;
    pid_t            child;
    struct arguments args;
    mqd_t            mq = 0;
    struct mq_attr   attr;
    char             *msg;
    struct timespec  timeout;

    args.verbose = 0;
    args.flags = 0;
    args.ipc = 0;
    args.jail = 0;
    args.path = NULL;
    args.hostname = NULL;
    args.command = NULL;

    // Parse command line options and construct arguments
    // to be passed to childFunction
    while ((option = getopt(argc, argv, "+hvpmu:ni:c:")) != -1) {
        switch (option) {
            case 'c':
                args.jail = 1;
                args.path = malloc(sizeof(char) * (strlen(optarg) + 1));
                strcpy(args.path, optarg);
                break;
            case 'i':
                if (strcmp("no", optarg) != 0 && strcmp("yes", optarg) != 0) {
                    fprintf(stderr, "%s: option requires valid argument -- 'i'\n", argv[0]);
                    usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                else
                    if (strcmp("yes", optarg) == 0)
                        flags |= CLONE_NEWIPC;
                args.ipc = 1;
                break;
            case 'n':
                flags |= CLONE_NEWNET;
                break;
            case 'u':
                flags |= CLONE_NEWUTS;
                args.hostname = malloc(sizeof(char) * (strlen(optarg) + 1));
                strcpy(args.hostname, optarg);
                break;
            case 'm':
                flags |= CLONE_NEWNS;
                break;
            case 'p':
                flags |= CLONE_NEWPID;
                break;
            case 'v':
                args.verbose = 1;
                break;
            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // childFunc needs to know which namespaces have been created
    args.flags = flags;

    // Assemble command to be executed in namespace
    if(optind != argc) {
        args.command = malloc(sizeof(char *) * (argc - optind + 1));
        for (i = optind; i < argc; i++) {
            args.command[i - optind] = malloc(strlen(argv[i]) + 1);
            strcpy(args.command[i - optind], argv[i]);
        }
        args.command[argc - optind] = NULL;
    }

    if (args.verbose)
        printf("Parent: PID of parent is %d\n", getpid());

    // Prepare message queue
    if (args.ipc) {
        mq = prepareMQ(&args);
        if (mq == -1)
            exit(EXIT_FAILURE);
    }

    // Allocate heap for child's stack
    child_stack = malloc(STACK_SIZE);
    if (child_stack == NULL) {
        perror("Parent: main: malloc");
        exit(EXIT_FAILURE);
    }

    // Clone child process
    child = clone(childFunction, child_stack + STACK_SIZE, flags | SIGCHLD, &args);
    if (child == -1) {
        perror("Parent: main: clone");
        exit(EXIT_FAILURE);
    }

    if (args.verbose)
        printf("Parent: PID of child is %d\n", child);

    // Read message from child on message queue
    if (args.ipc) {
        if (clock_gettime(CLOCK_REALTIME, &timeout) == 0) {
            timeout.tv_sec += 60;
            if (mq_getattr(mq, &attr) != -1) {
                msg = malloc(attr.mq_msgsize);
                if (mq_timedreceive(mq, msg, attr.mq_msgsize, NULL, &timeout) != -1) {
                    if (args.verbose)
                        printf("Parent: received message from child\n");
                    printf("\n    Parent: the following message was received from the child\n     >> %s\n\n", msg);
                }
                else
                    perror("Parent: main: mq_timedreceive");
                free(msg);
            }
            else
                perror("Parent: main: mq_getattr");
        }
        else
            perror("Parent: main: clock_gettime");
    }

    // Wait for child to finish
    if (waitpid(child, NULL, 0) == -1) {
        perror("Parent: main: waitpid");
        exit(EXIT_FAILURE);
    }

    // Remove message queue
    if (args.ipc) {
        if (args.verbose)
            printf("Parent: closing message queue %s\n", mq_name);
        if (mq_close(mq) == -1)
            perror("Parent: main: mq_close");
        if (args.verbose)
            printf("Parent: removing message queue %s\n", mq_name);
        if (mq_unlink(mq_name) == -1)
            perror("Parent: main: mq_unlink");
    }

    if (args.verbose)
        printf("Parent: %s - Finishing up\n", argv[0]);

    exit(EXIT_SUCCESS);
}
