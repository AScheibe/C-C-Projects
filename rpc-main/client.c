#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

#include "client.h"
#include "rpc.h"
#include "udp.h"

#define RPC_TIMEOUT 1 // second

// initializes the RPC connection to the server
struct rpc_connection RPC_init(int src_port, int dst_port, char dst_addr[]) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_usec);
    struct rpc_connection new_con;
    new_con.client_id = rand();
    new_con.seq_number = 1;
    new_con.recv_socket = init_socket(src_port);

    struct sockaddr_storage sa_storage;
    populate_sockaddr(RPC_AF, dst_port, dst_addr, &sa_storage, &new_con.dst_len);
    new_con.dst_addr = *((struct sockaddr*) &sa_storage);

    return new_con;
}

void send_message(struct rpc_connection *rpc, struct rpc_request *msg) {
    send_packet(
        rpc->recv_socket,
        rpc->dst_addr,
        rpc->dst_len,
        (char *) msg,
        sizeof(struct rpc_request)
    );
}

int RPC_call(struct rpc_connection *rpc, call_type_t call_type, int arg1, int arg2) {
    // printf("RPC_CALL: type = %s -----\n", CALL_STR[call_type]);

    struct rpc_request req = {
        .call_type = call_type,
        .seq_number = rpc->seq_number,
        .client_id = rpc->client_id,
        .arg1 = arg1,
        .arg2 = arg2
    };

    struct packet_info packet;
    struct rpc_response* res;

    int attempts = 1;
    do {
        if(attempts > 5){
            fprintf(stderr, "RPC ERROR: No response after 5 attempts\n");
            exit(EXIT_FAILURE);
        }

        // printf("Trying: send message\n");
        send_message(rpc, &req);
        // receive packet (will block thread)
        packet = receive_packet_timeout(rpc->recv_socket, RPC_TIMEOUT);

        // if ack, we delay retry for 1 second
        if (packet.recv_len > 0) {
            // printf("received response! %s\n", packet.buf);
            res = (struct rpc_response*) packet.buf;
            if (res->response_type == RESPONSE_ACK) {
                // printf("Received ack!\n");
                sleep(1);
                attempts = 0;
            }
        }

        attempts++;
        // if timeout (recv_len < 0), retry - go back to beginning of loop
    } while (packet.recv_len <= 0 || res->response_type == RESPONSE_ACK);

    // packet received
    if (packet.recv_len != sizeof(struct rpc_response)) {
        fprintf(stderr, "RPC ERROR: recv_len != sizeof(response), recv_len = %d, buf = %s\n", packet.recv_len, packet.buf);
        rpc->seq_number++;
        return -1;
    }
    res = (struct rpc_response*) packet.buf;
    // printf("Response received\n\t"
    //                 "response_type = %s\n\t"
    //                 "call type = %s\n\t"
    //                 "seq number = %d\n\t"
    //                 "client_id = %d\n\t"
    //                 "value = %d\n\n",
    //                 RES_STR[res->response_type],
    //                 CALL_STR[res->call_type],
    //                 res->seq_number,
    //                 res->client_id,
    //                 res->value);


    if (res->response_type != RESPONSE_VALUE ||
        res->client_id != rpc->client_id ||
        res->seq_number != rpc->seq_number ||
        res->call_type != req.call_type
    ) {
        fprintf(stderr, "RPC ERROR: Response params did not match request\n");
        rpc->seq_number++;

        return -1;
    }

    rpc->seq_number++;
    // printf("PUT SEQ: %d\n", rpc->seq_number);

    return res->value;
}

void RPC_idle(struct rpc_connection *rpc, int time) {
    RPC_call(rpc, CALL_IDLE, 0, 0);
}

int RPC_get(struct rpc_connection *rpc, int key) {
    return RPC_call(rpc, CALL_GET, key, 0);
}

int RPC_put(struct rpc_connection *rpc, int key, int value) {
    return RPC_call(rpc, CALL_PUT, key, value);
}

void RPC_close(struct rpc_connection *rpc) {
    close_socket(rpc->recv_socket);
}
