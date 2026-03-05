#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h> 

int sig_catched = -1;

void sig_handler(int signum) {
    sig_catched = signum;
}

int main(int argc, char *argv[]) {

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);    
    bool deamonize = false;

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    if (argc > 2) {
        syslog(LOG_ERR, "Usage: %s [-d]\n", argv[0]);
        return 1;
    }
    if (argc == 2)
    {
        if (strcmp(argv[1], "-d") != 0) {
            syslog(LOG_ERR, "Usage: %s [-d]\n", argv[0]);
            return 1;
        }
        deamonize = true;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);  

    server_fd = socket(AF_INET, SOCK_STREAM, /*SO_REUSEADDR*/0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Error %s creating socket", strerror(errno));
        return 1;
    }   
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
        syslog(LOG_ERR, "Error %s setting socket options", strerror(errno));
        return 1;
    }   

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(9000); 

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Error %s binding socket", strerror(errno));
        return 1;
    }

    if (deamonize) {
        pid_t pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "Error %s daemonizing process", strerror(errno));
            return 1;
        }
        if (pid > 0) {
            return 0;
        }
        if (setsid() == -1) {
            syslog(LOG_ERR, "Error %s creating new session", strerror(errno));
            return 1;
        }
    }

    if (listen(server_fd, 5) == -1) {
        syslog(LOG_ERR, "Error %s listening on socket", strerror(errno));
        return 1;
    }

    FILE *file = fopen("/var/tmp/aesdsocketdata", "w+");
    if (file == NULL) {
        syslog(LOG_ERR, "Error %s opening file", strerror(errno));
        return 1;
    }
    fseek(file, 0, SEEK_END);

    do
    {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (sig_catched != -1) {
                break;
            }
            syslog(LOG_ERR, "Error %s accepting connection", strerror(errno));
            return 1;
        }   
        syslog(LOG_DEBUG, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        size_t buf_size = 1024;
        char *recvbuf = malloc(buf_size+1);
        size_t total_len = 0;
        ssize_t recv_len;
        while ((recv_len = recv(client_fd, recvbuf+total_len, buf_size-total_len, 0)) > 0) {
            total_len += recv_len;
            recvbuf[total_len] = '\0';
            if (recvbuf[total_len-1] == '\n') {
                break;
            }
            if (total_len == buf_size) {
                buf_size *= 2;
                char *new_buf = realloc(recvbuf, buf_size+1);
                if (new_buf == NULL) {
                    syslog(LOG_ERR, "Error %s reallocating buffer", strerror(errno));
                    free(recvbuf);
                    return 1;   
                }
                recvbuf = new_buf;  
            }               
        }
        syslog(LOG_DEBUG, "Received data: %s", recvbuf);
        fputs(recvbuf, file);
        free(recvbuf);

        char sendbuf[1024];
        fseek(file, 0, SEEK_SET);
        while (fgets(sendbuf, sizeof(sendbuf), file) != NULL) {
            syslog(LOG_DEBUG, "Sending data: %s", sendbuf);
            ssize_t result = send(client_fd, sendbuf, strlen(sendbuf), 0);
            if (result == -1) {
                syslog(LOG_ERR, "Error %s sending data", strerror(errno));
                return 1;
            }
            if (result != (ssize_t) strlen(sendbuf)) {
                syslog(LOG_ERR, "Error sending all data");
                return 1;
            }
            syslog(LOG_DEBUG, "Data sent %ld", result);
        }

        close(client_fd);   

        syslog(LOG_DEBUG, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));

    } while (sig_catched == -1);

    syslog(LOG_DEBUG, "Caught signal %d, exiting", sig_catched);

    fclose(file);
    close(server_fd);

    unlink("/var/tmp/aesdsocketdata");

    return 0;    
}