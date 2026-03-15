#include "cachelab.h"
#include <stdio.h>   // printf
#include <unistd.h>  // getopt（核心头文件）
#include <stdlib.h>
#include <getopt.h>  // 必须显式包含这个头文件
#include <limits.h>
typedef struct {
    int valid;
    unsigned long tag;
    int lru_counter;
} cache_line_t;

int global_counter = 0;

void usage(char* argv[]) {
    printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n\n"); 
    printf("Examples:\n");
    printf("  linux>  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n", argv[0]);
    printf("  linux>  %s -v -s 8 -E 2 -b 4 -t traces/yi.trace\n", argv[0]);
}

int main(int argc,char* argv[])
{
    const char *optstring = "hvs:E:b:t:";
    int verbose = 0;
    int s = 0,E = 0,b = 0;
    char *tracefile = NULL;
    int opt;
    while((opt = getopt(argc,argv,optstring)) != -1){
        switch(opt){
            case 'h':
                usage(argv);
                exit(0);
            case 'v':
                verbose = 1;
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                tracefile = optarg;
                break;
            default:
                printf("Missing required command line argument\n");
                usage(argv);
                exit(1);
                    
        }
    }

    // 检查必要参数是否已输入 (s, E, b 通常应大于 0)
    if (s <= 0 || E <= 0 || b <= 0 || tracefile == NULL) {
        printf("%s: Missing required command line argument\n", argv[0]);
        usage(argv);
        exit(1);
    }

    int S = 1 << s; 
    cache_line_t **cache = (cache_line_t **)malloc(S * sizeof(cache_line_t *));
    for (int i = 0; i < S; i++) {
        cache[i] = (cache_line_t *)calloc(E,sizeof(cache_line_t));
    }

    FILE *fp = fopen(tracefile, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: Could not open file %s\n", tracefile);
        exit(1);
    }
    
    char operation;
    unsigned long address;
    int size;
    int hits = 0,misses = 0,evictions = 0;
    while(fscanf(fp," %c %lx,%d",&operation,&address,&size) != EOF){
        if(operation == 'I'){
            continue;
        }
        int index = (address >> b) & (S-1);
        unsigned long tag = address >> (s+b);

        int hit = 0,miss = 0,eviction = 0;
        int empty_slot = -1;

        int eviction_index = 0;
        int lru_min = INT_MAX;
        cache_line_t *line = cache[index];
        for(int i=0;i<E;i++){
            if(line[i].valid){
                if(line[i].tag == tag){
                    hit = 1;
                    line[i].lru_counter = ++global_counter;
                    break;
                }
                if(line[i].lru_counter < lru_min){
                    lru_min =line[i].lru_counter;
                    eviction_index = i;
                }
            }else{
                // 记录第一个空闲位置
                if (empty_slot == -1) empty_slot = i;
            }
        }
        if(!hit){
            miss = 1;
            int new_line_index;
            if(empty_slot != -1){
                new_line_index = empty_slot;
            }else{
                eviction = 1;
                new_line_index = eviction_index;
            }
            line[new_line_index].valid = 1;
            line[new_line_index].tag = tag;
            line[new_line_index].lru_counter = ++global_counter;

        }
        hits += hit;
        misses += miss;
        evictions += eviction;
        if(operation == 'M'){
            hits += 1;
        }
        if(verbose){
            printf("%c %lx,%d ", operation, address, size);
            if(hit) printf(" hit");
            if(miss) printf(" miss");
            if(eviction) printf(" eviction");
            if(operation == 'M') printf(" hit");
            printf("\n");
        }
    }
    printSummary(hits,misses,evictions);

    // 释放内存
    for (int i = 0; i < S; i++){
        free(cache[i]);
    }
    free(cache);
    fclose(fp);
    
    return 0;
}
