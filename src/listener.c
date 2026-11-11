#include "include/listener.h"
#include "blocking_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include "signal_shutdown.h"
#include "protocol_parser.h"
#include "coroutine_pool.h"
#include "workers_pool.h"

Client* clients = nullptr;
int num_of_clients = 0;

cor_mutex_t clients_lock;
const int BACKLOG = 10;


void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void start_listener(int port) {
    int server_fd;
    struct sockaddr_in addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        LOG(LOG_FATAL, "server socket is -1");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        LOG(LOG_FATAL, "couldn't bind server socket: %s", strerror(errno));
        exit(1);
    }

    if (listen(server_fd, BACKLOG) == -1) {
        LOG(LOG_FATAL, "failed to start listening: %s", strerror(errno));
        exit(1);
    }

    set_nonblocking(server_fd);

    struct pollfd fds[2];
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;
    fds[1].fd = listener_efd;
    fds[1].events = POLLIN;

    cor_mutex_init(&clients_lock);

    LOG(LOG_INFO, "listening started on port %d", port);

    struct timespec last_event_time;
    clock_gettime(CLOCK_MONOTONIC, &last_event_time);

    while (!stop) {
        int ready = poll(fds, 2, 0);
        if (ready < 0) {
            if (errno == EINTR) continue;
            LOG(LOG_ERROR, "poll error: %s", strerror(errno));
            break;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - last_event_time.tv_sec) * 1000 +
                          (now.tv_nsec - last_event_time.tv_nsec) / 1000000;

        if (ready == 0) {
            if (elapsed_ms >= 1000) {
                cor_sleep_ms(100);
                LOG(LOG_INFO, "listener slept");
            }
            cor_yield();
            continue;
        }

        clock_gettime(CLOCK_MONOTONIC, &last_event_time);

        if (fds[1].revents & POLLIN) {
            uint64_t tmp;
            read(listener_efd, &tmp, sizeof(tmp));
            break;
        }

        if (fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                LOG(LOG_ERROR, "the client cannot connect: %s", strerror(errno));
                continue;
            }

            Client* client = xmalloc(sizeof(Client));
            memset(client, 0, sizeof(Client));
            client->client_fd = client_fd;
            client->client_commands = xmalloc(sizeof(BlockingQueue_t));
            bq_init(client->client_commands, 100);
            cor_mutex_init(&client->mutex);
            client->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            client->is_first_command = 1;
            client->is_deleting = 0;
            client->is_executing = 0;
            client->prev_client = nullptr;
            client->next_client = nullptr;
            client->is_joined = 0;
            client->cond = (cor_wait_token_t)xmalloc(1);

            coro_create(sched, (coro_fn_t) client_thread, client, 128 * 1024);

            cor_mutex_lock(&clients_lock);
            client->next_client = clients;
            client->prev_client = nullptr;
            if (clients) {
                clients->prev_client = client;
            }
            clients = client;
            cor_mutex_unlock(&clients_lock);
        }
    }

    close(server_fd);
    close(listener_efd);

    LOG(LOG_INFO, "listener shutting down: notifying clients");

    cor_mutex_lock(&clients_lock);
    for (Client* t = clients; t; t = t->next_client) num_of_clients++;

    if (num_of_clients > 0) {
        for (Client* t = clients; t; t = t->next_client) {
            t->is_joined = 1;
            uint64_t two = 2;
            if (t->event_fd != -1) write(t->event_fd, &two, sizeof(two));
        }
    }
    cor_mutex_unlock(&clients_lock);

    /*while(1) {
        pthread_mutex_lock(&clients_lock);
        if (clients == NULL) {
            pthread_mutex_unlock(&clients_lock);
            break;
        }

        for (Client* t = clients; t; t = t->next_client) {
            uint64_t two = 2;
            if (t->event_fd != -1) write(t->event_fd, &two, sizeof(two));
        }
        pthread_mutex_unlock(&clients_lock);
        usleep(100000); // 100ms
    }*/

    cor_notify_one(bq_commands->not_empty);

    LOG(LOG_INFO, "shutdown listener");
    cor_exit();
}

void send_answer(void* arg) {
    if (!arg) {
        LOG(LOG_ERROR, "send_answer: response is NULL");
        return;
    }

    Response *response = (Response *) arg;
    Client *client = response->client;
    if (!client) {
        free(response->answer);
        free(response);
        return;
    }

    size_t len = response->answer ? strlen(response->answer) : 0;
    cor_mutex_lock(&client->mutex);
    if (client->is_deleting) {
        free(response->answer);
        free(response);
        client->is_executing=0;
        LOG(LOG_INFO, "EXECUTE FINISH");
        cor_mutex_unlock(&client->mutex);
        return;
    }

    ssize_t sent = write(client->client_fd, response->answer, len);
    if (sent < 0) {
        LOG(LOG_ERROR, "send_answer: write() failed: %s", strerror(errno));
    } else if ((size_t)sent < len) {
        LOG(LOG_ERROR, "send_answer: partial send (%zd/%zu bytes)", sent, len);
    }

    if (bq_get_size(client->client_commands) == 0) {
        client->is_first_command = 1;
    } else {
        uint64_t one = 1;
        if (client->event_fd != -1) write(client->event_fd, &one, sizeof(one));
    }
    cor_notify_one(client->cond);
    client->is_executing=0;
    cor_mutex_unlock(&client->mutex);
    free(response->answer);
    free(response);
}


void* client_thread(void* arg1) {

    Client* self = (Client*) arg1;
    ssize_t bytes_read;

    int event_fd = self->event_fd;

    struct pollfd fds[2];
    fds[0].fd = self->client_fd;
    fds[0].events = POLLIN;
    fds[1].fd = event_fd;
    fds[1].events = POLLIN;

    self->buffer_len = 0;
    ParseResult res = {nullptr};
    struct timespec last_event_time;
    clock_gettime(CLOCK_MONOTONIC, &last_event_time);
    while (1) {
        int ret = poll(fds, 2, 0);
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG(LOG_ERROR, "poll error in client_thread: %s", strerror(errno));
            break;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - last_event_time.tv_sec) * 1000 +
                          (now.tv_nsec - last_event_time.tv_nsec) / 1000000;

        if (ret == 0) {
            if (elapsed_ms >= 1000) {
                cor_sleep_ms(10);
            } else {
                cor_yield();
            }
            continue;
        }

        clock_gettime(CLOCK_MONOTONIC, &last_event_time);

        cor_mutex_lock(&self->mutex);
        if (fds[1].revents & POLLIN) {
            uint64_t tmp;
            ssize_t r = read(event_fd, &tmp, sizeof(tmp));
            (void)r;
            if (tmp == 1) {
                RespObject* command = bq_dequeue(self->client_commands);
                if (command == NULL) {
                    cor_mutex_unlock(&self->mutex);
                    continue;
                }
                add_command(command);
                cor_mutex_unlock(&self->mutex);
                continue;
            } else {
                cor_mutex_unlock(&self->mutex);
                break;
            }
        }
        cor_mutex_unlock(&self->mutex);

        if (fds[0].revents & POLLIN) {
            bytes_read = read(self->client_fd,
                              self->buffer + self->buffer_len,
                              sizeof(self->buffer) - self->buffer_len - 1);

            if (bytes_read <= 0) {
                LOG(LOG_INFO, "Client disconnected (fd=%d)", self->client_fd);
                break;
            }

            self->buffer_len += bytes_read;
            self->buffer[self->buffer_len] = '\0';

            res = parse_resp_stream(self->buffer, self->buffer_len);
            LOG(LOG_DEBUG, "Parsed %zu full commands, used %zu bytes", res.count, res.used_bytes);

            for (size_t i = 0; i < res.count; i++) {

                RespObject *cmd = res.cmds[i];
                cmd->client = self;
                add_callback(cmd, send_answer);

                if (self->is_first_command) {
                    self->is_first_command = 0;
                    add_command(cmd);
                } else {
                    bq_enqueue(self->client_commands, cmd);
                }
            }

            if (res.used_bytes < self->buffer_len) {
                memmove(self->buffer,
                        self->buffer + res.used_bytes,
                        self->buffer_len - res.used_bytes);
                self->buffer_len -= res.used_bytes;
            } else {
                self->buffer_len = 0;
            }

            free(res.cmds);
            res.cmds = nullptr;
            res.count = 0;
            res.used_bytes = 0;
        }
    }
    LOG(LOG_INFO, "Client thread cleanup start (fd=%d)", self->client_fd);

    cor_mutex_lock(&self->mutex);
    if (!self->is_first_command) {
        while (!workers_pool_shutdown && !self->is_first_command) {
            cor_cond_wait(self->cond, &self->mutex);
        }
    }
    cor_mutex_unlock(&self->mutex);



    cor_mutex_lock(&clients_lock);
    if (self->prev_client) {
        self->prev_client->next_client = self->next_client;
    } else {
        clients = self->next_client;
    }
    if (self->next_client) {
        self->next_client->prev_client = self->prev_client;
    }
    cor_mutex_unlock(&clients_lock);

    cor_mutex_lock(&self->mutex);
    self->is_deleting = 1;
    cor_mutex_unlock(&self->mutex);
    while (self->is_executing) {
        LOG(LOG_INFO, "WAIT");
        sleep(1);
    }

    RespObject* pending = nullptr;
    while (bq_get_size(self->client_commands)!=0) {
        free_resp_object(bq_dequeue(self->client_commands));
    }

    close(self->client_fd);
    if (self->event_fd != -1) close(self->event_fd);

    cor_mutex_destroy(&self->mutex);
    bq_destroy(self->client_commands);
    free(self->client_commands);

    free_parse_result(&res);
    free(self);

    LOG(LOG_INFO, "Client thread exited and cleaned up");
    //sleep(100);
    cor_exit();
}
