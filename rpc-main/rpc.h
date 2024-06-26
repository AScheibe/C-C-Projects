#ifndef RPC_H
#define RPC_H

typedef unsigned int call_type_t;
typedef unsigned short int response_type_t;

#define CALL_IDLE 1
#define CALL_PUT 2
#define CALL_GET 3

static char CALL_STR[4][8] __attribute_maybe_unused__ = { "UNDEF", "IDLE", "PUT", "GET" };

#define RESPONSE_VALUE 0
#define RESPONSE_ACK 1
#define RESPONSE_ERROR 2

static char RES_STR[3][8] __attribute_maybe_unused__ = { "VALUE", "ACK", "ERROR" };

#define RPC_AF AF_INET

struct rpc_request
{
    call_type_t call_type;
    int seq_number;
    int client_id;
    int arg1;
    int arg2;
};

struct rpc_response
{
    response_type_t response_type;
    call_type_t call_type;
    int seq_number;
    int client_id;
    int value;
};

#endif
