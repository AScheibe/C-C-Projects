#include "types.h"
#include "stat.h"
#include "user.h"

// Test that pages are copied on write and that the parent values are not affected by the child

int main(void) {
    int sz = 1000;
	int* arr = (int*)malloc(sz * sizeof(int));
    int pid = getpid();

    int init_free = getFreePagesCount();
    
    for(int i = 0; i < sz; i++){
        arr[i] = i * pid;
    }

    int pid2 = fork();

    if(pid2 == 0) {
        int mypid = getpid();
        for(int i = 0; i < sz; i++){
            arr[i] = i * mypid;
        }
    } else {
        wait();

        // Check that values in parent are not changed
        for(int i = 0; i < sz; i++){
            if(arr[i] != i * pid){
                printf(1, "XV6_COW\t FAILED\n");
                exit();
            }
        }

        int final_free = getFreePagesCount();
        
        // After child exits, parent should have same number of free pages
        if(final_free != init_free){
            printf(1, "XV6_COW\t FAILED\n");
            exit();
        }

        printf(1, "XV6_COW\t SUCCESS\n");
    }
 	exit();
}