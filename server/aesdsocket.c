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
#include <pthread.h> 
#include "queue.h"

int sig_catched = -1;

void sig_handler(int signum) {
    sig_catched = signum;
}

struct client_data {
    int client_fd;
    pthread_t thread_id;
    bool active;
    bool terminate;
    SLIST_ENTRY(client_data) entries;
};

FILE *output_file;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; 

void* client_thread(void *arg) {
    struct client_data* data = (struct client_data *)arg;

    do
    {
        size_t buf_size = 1024;
        char *recvbuf = malloc(buf_size+1);
        size_t total_len = 0;
        ssize_t recv_len;
        while ((recv_len = recv(data->client_fd, recvbuf+total_len, buf_size-total_len, 0)) > 0) {
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
                    exit(1);   
                }
                recvbuf = new_buf;  
            }               
        }
        if (recv_len == -1) {
            syslog(LOG_ERR, "Error %s receiving data", strerror(errno));
            free(recvbuf);
            data->active = false;
        }
        else if (recv_len == 0) {
            syslog(LOG_DEBUG, "Received no data");
            free(recvbuf);
            data->active = false;
        }
        else {    
            syslog(LOG_DEBUG, "Received data: %s", recvbuf);
            pthread_mutex_lock(&file_mutex);
            fputs(recvbuf, output_file);
            fflush(output_file);
            free(recvbuf);

            char sendbuf[1024];
            fseek(output_file, 0, SEEK_SET);
            while (fgets(sendbuf, sizeof(sendbuf), output_file) != NULL) {
                syslog(LOG_DEBUG, "Sending data: %s", sendbuf);
                ssize_t result = send(data->client_fd, sendbuf, strlen(sendbuf), 0);
                if (result == -1) {
                    syslog(LOG_ERR, "Error %s sending data", strerror(errno));
                    data->active = false;
                    break;
                }
                if (result != (ssize_t) strlen(sendbuf)) {
                    syslog(LOG_ERR, "Error sending all data");
                    data->active = false;
                    break;
                }
                syslog(LOG_DEBUG, "Data sent %ld", result);
            }
            pthread_mutex_unlock(&file_mutex);
        }    
    } while (data->active && !data->terminate);

    syslog(LOG_DEBUG, "Closing connection for socket handle %d", data->client_fd);
    close(data->client_fd);
    data->active = false;   
    return NULL;
}

void* timer_thread(void *) {
    while (sig_catched == -1) {
        sleep(10);
        time_t now = time(NULL);
        char time_str[26];
        ctime_r(&now, time_str);    
        pthread_mutex_lock(&file_mutex);
        fprintf(output_file, "timestamp:%s", time_str);
        fflush(output_file);
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);    
    bool deamonize = false;
    SLIST_HEAD(client_list, client_data) clients = SLIST_HEAD_INITIALIZER(clients);
    pthread_t timer_thread_id;

    openlog("aesdsocket", LOG_PID | LOG_PERROR, LOG_USER);

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

    output_file = fopen("/var/tmp/aesdsocketdata", "w+");
    if (output_file == NULL) {
        syslog(LOG_ERR, "Error %s opening file", strerror(errno));
        return 1;
    }
    fseek(output_file, 0, SEEK_END);

    if (pthread_create(&timer_thread_id, NULL, timer_thread, NULL) != 0) {
        syslog(LOG_ERR, "Error %s creating timer thread", strerror(errno));
        return 1;
    }

    do
    {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (sig_catched != -1) {
                break;
            }
            syslog(LOG_ERR, "Error %s accepting connection", strerror(errno));
            continue;
        }   
        syslog(LOG_DEBUG, "Accepted connection from %s with socket handle %d", inet_ntoa(client_addr.sin_addr), client_fd);

        struct client_data *data = malloc(sizeof(struct client_data));
        if (data == NULL) {
            syslog(LOG_ERR, "Error %s allocating memory for client data", strerror(errno));
            close(client_fd);
            continue;
        }
        data->client_fd = client_fd;
        data->active = true;
        data->terminate = false;
        SLIST_INSERT_HEAD(&clients, data, entries);
        if (pthread_create(&data->thread_id, NULL, client_thread, data) != 0) {
            syslog(LOG_ERR, "Error %s creating thread for client", strerror(errno));
            SLIST_REMOVE(&clients, data, client_data, entries);
            free(data);
            close(client_fd);
            continue;
        }
        syslog(LOG_DEBUG, "Created client thread %ld", data->thread_id);

        struct client_data *temp;
        SLIST_FOREACH_SAFE(data, &clients, entries, temp) {
            if (!data->active) {
                syslog(LOG_DEBUG, "Terminating client thread %ld", data->thread_id);
                pthread_join(data->thread_id, NULL);
                SLIST_REMOVE(&clients, data, client_data, entries);
                free(data);
            }    
        }    
    } while (sig_catched == -1);

    syslog(LOG_DEBUG, "Caught signal %d, exiting", sig_catched);

    struct client_data *data, *temp;
    SLIST_FOREACH_SAFE(data, &clients, entries, temp) {
        syslog(LOG_DEBUG, "Terminating client thread %ld", data->thread_id);
        data->terminate = true;
        pthread_kill(data->thread_id, SIGINT);
        pthread_join(data->thread_id, NULL);
        SLIST_REMOVE(&clients, data, client_data, entries);
        free(data);
    }
    pthread_kill(timer_thread_id, SIGINT);
    pthread_join(timer_thread_id, NULL);

    syslog(LOG_DEBUG, "Done, terminating");

    fclose(output_file);
    close(server_fd);

    //unlink("/var/tmp/aesdsocketdata");

    return 0;    
}