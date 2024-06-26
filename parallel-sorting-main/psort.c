#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define RECORD_SIZE 100

typedef struct _kvpair {
    int key;
    char* value;
} kvpair_t;

typedef struct _thread_data_t {
    int tid;
    int low;
    int high;
    int num_lines;
    kvpair_t** lines;
} thread_data_t;


typedef struct _chunk {
    int low;
    int high;
    struct _chunk* next;
} chunk_t;

typedef struct _merge_data_t {
    int low;
    int mid;
    int high;
    kvpair_t** lines;
    int num_lines;
} merge_data_t;

int keycmp(kvpair_t* a, kvpair_t* b) {
    return a->key - b->key;
}


void merging(int low, int mid, int high, kvpair_t** lines, int num_lines) {
    kvpair_t** lines_ph2 = malloc((high - low + 1) * sizeof(kvpair_t*));

    int l1, l2, i;

    for(l1 = low, l2 = mid + 1, i = 0; l1 <= mid && l2 <= high; i++) {
        if(keycmp(lines[l1], lines[l2]) <= 0)
            lines_ph2[i] = lines[l1++];
        else
            lines_ph2[i] = lines[l2++];
    }

    while(l1 <= mid)
        lines_ph2[i++] = lines[l1++];

    while(l2 <= high)
        lines_ph2[i++] = lines[l2++];

    for(i = low; i <= high; i++)
        lines[i] = lines_ph2[i - low];

    free(lines_ph2);
}

// low, high inclusive
void sort(int low, int high, kvpair_t** lines, int num_lines) {
   int mid;

   if(low < high) {
      mid = (low + high) / 2;
      sort(low, mid, lines, num_lines);
      sort(mid+1, high, lines, num_lines);
      merging(low, mid, high, lines, num_lines);
   } else {
      return;
   }
}

void *sort_thread(void* thr_data) {
    thread_data_t *data = (thread_data_t*) thr_data;

    sort(data->low, data->high, data->lines, data->num_lines);

    return NULL;
}

void *merge_thread(void* arg) {
    merge_data_t *data = (merge_data_t*) arg;
    //printf("[merge_thread] %d:%d:%d\n", data->low, data->mid, data->high);
    merging(data->low, data->mid, data->high, data->lines, data->num_lines);
    return NULL;
}

int parallel_sort(kvpair_t **lines, int total_lines, int num_threads){
    pthread_t threads[num_threads];
    thread_data_t thr_data[num_threads];

    if (lines == NULL) {
        return 1;
    }

    int rc;

    int chunk_size = 0;

    if(num_threads == 0 || (chunk_size = total_lines/num_threads) == 0){
        sort(0, total_lines - 1, lines, total_lines);
    } else {
        /* create threads */
        for (int i = 0; i < num_threads; ++i) {
            thr_data[i].tid = i;
            thr_data[i].lines = lines;
            thr_data[i].low = i * chunk_size;
            if (i == num_threads - 1) {
                thr_data[i].high = total_lines - 1; // high index inclusive
            } else {
                thr_data[i].high = (i + 1) * chunk_size - 1; // one below start of next chunk
            }
            thr_data[i].num_lines = total_lines;

            if ((rc = pthread_create(&threads[i], NULL, sort_thread, &thr_data[i]))) {
                fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
                return -1;
            }
        }

        /* block until all threads complete */
        for (int i = 0; i < num_threads; ++i) {
             pthread_join(threads[i], NULL);
        }

        chunk_t* chunk_head = NULL;
        for (int c = num_threads - 1; c >= 0; c--) {
            chunk_t* new_node = malloc(sizeof(chunk_t));
            new_node->low = thr_data[c].low;
            new_node->high = thr_data[c].high;
            new_node->next = chunk_head;
            chunk_head = new_node;
        }

        int num_chunks = num_threads;
        pthread_t merge_threads[num_threads];
        merge_data_t mdata[num_threads];
        int k;

        while (num_chunks > 1) {
            // [0 1 2 3 4]
            // chunks = 5
            // 0+1; 2+3; 4
            // chunks = 3
            // 01+23; 4
            // chunks = 2
            // 0123+4
            // chunks = 1
            // 01234 -> done!

            k = 0;
            //printf("chunkmerge iteration\n");
            for(chunk_t* curr = chunk_head; curr->next != NULL; curr = curr->next) {
                int low = curr->low;
                int mid = curr->next->low - 1; // one below start of next chunk
                int high = curr->next->high; // top of next chunk

                mdata[k] = (merge_data_t) { low, mid, high, lines, total_lines };

                if (pthread_create(&merge_threads[k], NULL, merge_thread, &mdata[k])) {
                    perror("pthread_create mergethread");
                    return -1;
                }
                
                // coalesce chunks
                curr->high = high;
                chunk_t* old = curr->next;
                curr->next = curr->next->next;
                free(old); // free unused chunk data

                num_chunks--;
                k++;
                if(curr->next == NULL){
                    break;
                }

            }
            //printf("k = %d\n", k);
            
            // wait for all merge threads to complete in current iteration
            for (int j = 0; j < k; j++) {
                //printf("waiting on thread %d\n", j);
                pthread_join(merge_threads[j], NULL);
            }
        }
    }
    return 0;
}

int main(int argc, char const *argv[])
{
    struct stat st;
    struct timeval start_time, end_time;

    if (argc != 4) {
        perror("Invalid args!\n");
        exit(EXIT_FAILURE);
    }

    // read in args
    FILE *fp_in = fopen(argv[1], "r");
    FILE *fp_out = fopen(argv[2], "w+");

    if (fp_in == NULL || fp_out == NULL) {
        perror("file failed to open");
        exit(EXIT_FAILURE);
    }

    int num_threads = atoi(argv[3]);

    // get number of keys/lines in file
    int num_lines = 0;

    // get num_lines in O(1) -- file size always multiple of 100
    stat(argv[1], &st);
    num_lines = st.st_size / RECORD_SIZE;
    printf("Read %ld bytes from %s\n", st.st_size, argv[1]);
    printf("Running psort with num_lines = %d\n", num_lines);

    // mmap file

    int i = 0;
    char *data;

    data = (char*) mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fileno(fp_in), 0);
    if (data == MAP_FAILED) {
        perror("mapping failed");
        exit(EXIT_FAILURE);
    }

    // allocate array of kvpair entries

    kvpair_t **entries = (kvpair_t**)malloc(num_lines * sizeof(kvpair_t*));
    for (char *line = data; line < data + st.st_size; line += 100) {
        // printf("mallocing entry: key = %c %c %c %c, line = %p\n", line[0], line[1], line[2], line[3], line);
        kvpair_t* new_pair = (kvpair_t*)malloc(sizeof(kvpair_t));
        new_pair->key = *((int*)line);
        new_pair->value = line;
        entries[i] = new_pair;
        i++;
    }

    int retval = 1;

    gettimeofday(&start_time, NULL);
    int psort_rc = parallel_sort(entries, num_lines, num_threads);
    gettimeofday(&end_time, NULL);
    long seconds = end_time.tv_sec - start_time.tv_sec;
    long microseconds = end_time.tv_usec - start_time.tv_usec;
    double elapsed_time = seconds + (microseconds / 1000000.0);


    if (psort_rc == 0) {
        printf("Elapsed time: %f seconds, num_lines = %d\n", elapsed_time, num_lines);

        // print to output file
        for (int i = 0; i < num_lines; i++){
            fwrite(entries[i]->value, 1, RECORD_SIZE, fp_out);
        }
        fflush(fp_out);
        fsync(fileno(fp_out));
        retval = 0;
    }


    struct stat st_out;
    stat(argv[2], &st_out);
    printf("Wrote %ld bytes to %s\n", st_out.st_size, argv[2]);

    fclose(fp_in);
    fclose(fp_out);

    return retval;
}
