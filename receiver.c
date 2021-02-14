#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#define QUEUE_NAME "/ipc_namespace"

mqd_t mq;

void sighandler(int signo)
{
    // Make sure we can respond to an interrupt, but also docker stop
    if (signo == SIGINT || signo == SIGTERM) {
        if (mq_close(mq) == -1)
            perror("Sighandler: mq_close");
        if (mq_unlink(QUEUE_NAME) == -1)
            perror("Sighandler: mq_unlink");
    }

    exit(EXIT_SUCCESS);
}


int main(void)
{
    struct mq_attr   attr = {0, 10, 61, 0};
    char             *msg = NULL;
    struct sigaction sa;

    // Create handler for SIGINT and SIGTERM
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &sighandler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Main: sigaction");
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Main: sigaction");
    }

    // Open message queue
    if ((mq = mq_open(QUEUE_NAME, O_CREAT | O_RDONLY, 0644, &attr)) == -1) {
        perror("Main: mq_open");
        exit(EXIT_FAILURE);
    }

    // Wait to receive messages from clients
    msg = malloc(attr.mq_msgsize);
    setbuf(stdout, NULL);
    printf("\nReady to receive messages ....\n\n");
    while(1) {
        printf("RCV: ");
        if ((mq_receive(mq, msg, attr.mq_msgsize, NULL)) == -1) {
            perror("Main: mq_receive");
            exit(EXIT_FAILURE);
        }
        else
            printf("%s\n", msg);
    }
}
