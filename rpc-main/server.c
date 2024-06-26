#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>

#include "rpc.h"
#include "udp.h"
#include "server_functions.h"

#define MAX_CLIENTS 100

static struct socket* sockptr = NULL;
// pthread_mutex_t my_mutex = PTHREAD_MUTEX_INITIALIZER;

struct ct_entry {
    int client_id;
    int seq_number;
    int result;
    int completed;
    pthread_mutex_t lock;
};

struct call_table {
    int first_available;
    struct ct_entry* entries[MAX_CLIENTS];
};

struct thread_data {
    struct socket* sock;
    struct rpc_request* req;
    struct ct_entry* entry;
};

struct ct_entry* ctable_insert(struct call_table* ctable, int client_id) {
    struct ct_entry* new_entry = ctable->entries[ctable->first_available];
    new_entry->client_id = client_id;
    new_entry->seq_number = 0;
    new_entry->result = 0;
    new_entry->completed = 0;
    if (pthread_mutex_init(&new_entry->lock, NULL) != 0) {
        perror("mutex init has failed");
        exit(EXIT_FAILURE);
    }
    ctable->first_available++;
    return new_entry;
}

struct ct_entry* ctable_find(struct call_table* ctable, int client_id) {
    for(int i = 0; i < MAX_CLIENTS;i++){
        if (ctable->entries[i]->client_id == client_id) {
            return ctable->entries[i];
        }
    }
    return NULL;
}

void send_response(struct rpc_request* req,
                    struct socket* sock,
                    struct packet_info *packet,
                    response_type_t response,
                    int result){

    struct rpc_response res;
    res.response_type = response;
    res.client_id = req->client_id;
    res.seq_number = req->seq_number;
    res.call_type = req->call_type;
    res.value = result;

    send_packet(*sock, packet->sock, packet->slen, (char*) &res, sizeof(struct rpc_response));
}

void handle_sigint(int sig) {
    printf("Closing socket!\n");
    close_socket(*sockptr);
    exit(EXIT_SUCCESS);
}

void* thread_start(void* arg) {
    struct thread_data* tdata = (struct thread_data*) arg;
    call_type_t type = tdata->req->call_type;
    int arg1 = tdata->req->arg1;
    int arg2 = tdata->req->arg2;
    int result = 0;

    switch (type)
    {
        case CALL_IDLE:
            idle(arg1);
            break;
        case CALL_GET:
            result = get(arg1);
            break;
        case CALL_PUT:
            result = put(arg1, arg2);
            break;
        default:
            fprintf(stderr, "ERROR: Invalid Call Type, client_id = %d\n", tdata->req->client_id);
            result = -1;
            break;
    }

    // Update call table entry after response
    pthread_mutex_lock(&tdata->entry->lock);
    tdata->entry->result = result;
    tdata->entry->completed = 1;
    pthread_mutex_unlock(&tdata->entry->lock);

    pthread_exit(NULL);
}

void handle_request(struct rpc_request* req,
                    struct socket* sock,
                    struct packet_info* packet,
                    struct call_table* ctable) {
    printf("\nRequest received: [Client %d] SEQ %d %s(%d, %d)\n",
            req->client_id, req->seq_number, CALL_STR[req->call_type], req->arg1, req->arg2);

    // find or create ctable entry
    struct ct_entry* entry = NULL;
    entry = ctable_find(ctable, req->client_id);

    if (entry == NULL){
        entry = ctable_insert(ctable, req->client_id);
    }

    /*
    message arrives with sequence number i:
        i > last: new request - execute RPC and update call table entry
        i = last: duplicate of last RPC or duplicate of in progress RPC. Either resend result or send acknowledgement that RPC is being worked on.
        i < last: old RPC, discard message and do not reply
    */
    if (req->seq_number > entry->seq_number) {
        // new request
        pthread_mutex_lock(&entry->lock);
        entry->completed = 0;
        entry->seq_number = req->seq_number;
        pthread_mutex_unlock(&entry->lock);

        // spin up task thread
        pthread_t thread;
        struct thread_data* tdata = malloc(sizeof(struct thread_data));
        tdata->req = req;
        tdata->sock = sock;
        tdata->entry = entry;
        pthread_create(&thread, NULL, &thread_start, tdata);
        pthread_detach(thread);
        // send ack if value not already sent
        pthread_mutex_lock(&entry->lock);
        int compl = entry->completed;
        int cid = entry->client_id;
        int result = entry->result;
        pthread_mutex_unlock(&entry->lock);

        if (compl == 0) {
            printf("\tNew Request -- In Progress, sending ACK!\n");
            send_response(req, sock, packet, RESPONSE_ACK, 0);
        } else {
            printf("\tNew Request -- Completed, sending VALUE to client %d, result = %d\n", cid, result);
            send_response(req, sock, packet, RESPONSE_VALUE, result);
        }

    } else if (req->seq_number == entry->seq_number) {
        pthread_mutex_lock(&entry->lock);
        int compl = entry->completed;
        int result = entry->result;
        int cid = entry->client_id;
        pthread_mutex_unlock(&entry->lock);
        if (compl) {
            printf("\tExisting Request -- Completed, sending VALUE to client %d, result = %d\n", cid, result);
            send_response(req, sock, packet, RESPONSE_VALUE, result);
        } else {
            printf("\tExisting Request -- In Progress, sending ACK to client %d\n", cid);
            send_response(req, sock, packet, RESPONSE_ACK, 0);
        }
   } // if seq_number is old, ignore
}


int main(int argc, char const *argv[])
{
    // usage: ./server <port>
    if (argc != 2) {
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    printf("Server starting on port %d...\n", port);
    struct socket sock = init_socket(port);
    // sockptr = &sock;
    // atexit(exit_handler);

    // Initialize call table
    struct call_table* ctable = malloc(sizeof(struct call_table));
    ctable->first_available = 0;
    for(int i = 0; i < MAX_CLIENTS; i++) {
        ctable->entries[i] = (struct ct_entry*) malloc(sizeof(struct ct_entry));
    }

    struct packet_info packet;
    struct rpc_request* req;

    // Server loop - handle requests as they come in
    while (1) {
        packet = receive_packet(sock);
        if (packet.recv_len == sizeof(struct rpc_request)) {
            req = (struct rpc_request*) malloc(sizeof(struct rpc_request));
            memcpy(req, packet.buf, sizeof(struct rpc_request));
            handle_request(req, &sock, &packet, ctable);
        }
    }

    return 0;
}
