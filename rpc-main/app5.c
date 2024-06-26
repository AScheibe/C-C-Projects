// Test parallel client handling.
#include <stdio.h>
#include <stdlib.h>
#include<unistd.h>
#include <pthread.h>
#include "client.h"

void *sendPut(void *vargp)
{
    int* client_id = (int *) vargp;
    int val = (*client_id * 10000) + 1234;
    printf("client id %d, val = %d\n", *client_id, val);
    struct rpc_connection rpc = RPC_init(*client_id + 8000, 8888, "127.0.0.1");
    rpc.client_id = *client_id;
    RPC_put(&rpc, *client_id, val);
    sleep(1);
    int retval = RPC_get(&rpc, *client_id);
    char* passfail = (val == retval) ? "PASS" : "FAIL";
    printf("client %d put get value: %d | %s\n", *client_id, retval, passfail);

    RPC_close(&rpc);

    return NULL;
}


int main(){
    int num_threads = 100;
    pthread_t thread_id[num_threads];
    for(int i = 0; i < num_threads; i++) {
        int *a = (int*) malloc(sizeof(int));
        *a = i;
        pthread_create(&thread_id[i], NULL, sendPut, a);
    }

    for(int j = 0; j < num_threads; j++) {
        pthread_join(thread_id[j], NULL);
    }
}

