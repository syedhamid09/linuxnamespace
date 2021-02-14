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
    if (signo == SIGINT || signo == SIGTERM)
        if (mq_close(mq) == -1)
            perror("Sighandler: mq_close");

    exit(EXIT_SUCCESS);
}


int main(void)
{
    struct mq_attr   attr;
    char             *msg = NULL;
    size_t           len = 0;
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
    if ((mq = mq_open("/ipc_namespace", O_WRONLY)) == -1) {
        perror("Main: mq_open");
        exit(EXIT_FAILURE);
    }

    // Send messages
    setbuf(stdout, NULL);
    if (mq_getattr(mq, &attr) != -1) {
        printf("\nReady for sending messages (MAX 60 chars) ....\n\n");
        while(1) {
            printf("SND: ");
            if ((getline(&msg, &len, stdin)) != -1) {
                msg[strcspn(msg, "\n")] = '\0';
                if (strlen(msg) > attr.mq_msgsize - 1)
                    msg[attr.mq_msgsize - 1] = '\0';
                if (mq_send(mq, msg, strlen(msg) + 1, 0) == -1) {
                    perror("Main: mq_send");
                    exit(EXIT_FAILURE);
                }
            }
            else
                perror("Main: getline");
        }
    }
    else
        perror("Main: mq_getattr");
}
