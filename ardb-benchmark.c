#include <stdio.h>
#include <stdlib.h>
#include <hiredis.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "openssl/sha.h"

#define CHUNKSIZE     16 * 1024   // 16 KB
#define CHUNKS        128 * 1024  // 4096 chunks of 16 KB per client

#define SHA256LEN     (size_t) SHA256_DIGEST_LENGTH * 2

typedef struct benchmark_pass_t {
    unsigned int success;        // upload success
    struct timespec time_begin;  // init time
    struct timespec time_end;    // end time

} benchmark_pass_t;

typedef struct benchmark_t {
    unsigned int id;          // benchmark unique id
    redisContext *redis;      // redis context
    pthread_t pthread;        // thread context

    size_t chunksize;         // chunk size
    size_t chunks;            // chunks length
    unsigned char **buffers;  // chunks buffers
    unsigned char **hashes;   // chunks hashes

    struct benchmark_pass_t read;
    struct benchmark_pass_t write;

} benchmark_t;

void diep(char *str) {
    perror(str);
    exit(EXIT_FAILURE);
}

//
// hashing
//
static unsigned char *sha256hex(unsigned char *hash) {
    unsigned char *buffer = calloc((SHA256_DIGEST_LENGTH * 2) + 1, sizeof(char));

    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf((char *) buffer + (i * 2), "%02x", hash[i]);

    return buffer;
}

static unsigned char *sha256(const unsigned char *buffer, size_t length) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;

    SHA256_Init(&sha256);
    SHA256_Update(&sha256, buffer, length);
    SHA256_Final(hash, &sha256);

    return sha256hex(hash);
}

//
// redis
//
benchmark_t *benchmark_init(const char *host, int port) {
    struct timeval timeout = {5, 0};
    benchmark_t *bench;
    redisReply *reply;

    if(!(bench = calloc(sizeof(benchmark_t), 1)))
        diep("malloc");

    bench->redis = redisConnectWithTimeout(host, port, timeout);
    if(bench->redis == NULL || bench->redis->err) {
        printf("[-] redis: %s\n", (bench->redis->err) ? bench->redis->errstr : "memory error.");
        return NULL;
    }

    // ping redis to ensure connection
    reply = redisCommand(bench->redis, "PING");
    if(strcmp(reply->str, "PONG"))
        fprintf(stderr, "[-] warning, invalid redis PING response: %s\n", reply->str);

    freeReplyObject(reply);

    return bench;
}

//
// benchmark passes
//
static void *benchmark_pass_write(void *data) {
    benchmark_t *b = (benchmark_t *) data;
    redisReply *reply;

    clock_gettime(CLOCK_MONOTONIC_RAW, &b->write.time_begin);

    for(unsigned int i = 0; i < b->chunks; i++) {
        reply = redisCommand(b->redis, "SET %b %b", b->hashes[i], SHA256LEN, b->buffers[i], b->chunksize);

        // reply should be "OK" (length: 2)
        if(reply->len != 2) {
            fprintf(stderr, "[-] %s: %s\n", b->hashes[i], reply->str);
            exit(EXIT_FAILURE);
        }

        freeReplyObject(reply);

        b->write.success += 1;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &b->write.time_end);

    return NULL;
}

static void *benchmark_pass_read(void *data) {
    benchmark_t *b = (benchmark_t *) data;
    redisReply *reply;

    clock_gettime(CLOCK_MONOTONIC_RAW, &b->read.time_begin);

    for(unsigned int i = 0; i < b->chunks; i++) {
        reply = redisCommand(b->redis, "GET %b", b->hashes[i], SHA256LEN);

        if(reply->len != (int) b->chunksize) {
            fprintf(stderr, "[-] %s: invalid size: %d\n", b->hashes[i], reply->len);
            exit(EXIT_FAILURE);
        }

        freeReplyObject(reply);

        b->read.success += 1;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &b->read.time_end);

    return NULL;
}

static void *benchmark_pass_read_secure(void *data) {
    benchmark_t *b = (benchmark_t *) data;
    redisReply *reply;

    for(unsigned int i = 0; i < b->chunks; i++) {
        reply = redisCommand(b->redis, "GET %b", b->hashes[i], SHA256LEN);
        // printf("[+] downloaded: %s\n", bench->hashes[i]);

        unsigned char *hash = sha256((unsigned char *) reply->str, reply->len);

        // compare hashes
        if(strcmp((const char *) hash, (const char *) b->hashes[i])) {
            fprintf(stderr, "[-] hash mismatch: %s (size: %u)\n", hash, reply->len);
            fprintf(stderr, "[-] hash expected: %s\n", b->hashes[i]);
            // exit(EXIT_FAILURE);
        }

        freeReplyObject(reply);
    }

    return NULL;
}


//
// random generator
//
// warning: this is a very trivial random generator
// this is really unsafe, it's just made to provide
// a really fast way to produce random unique buffer
// this function is not thread-safe
//
static uint64_t __internal_random = 0;

static size_t randomize(unsigned char *buffer, size_t length) {
    // initialize our random init point
    if(__internal_random == 0) {
        srand(time(NULL));
        __internal_random = rand();
    }

    // fill the buffer with our fake-random integer
    for(size_t i = 0; i < length; i += sizeof(__internal_random)) {
        memcpy(buffer + i, &__internal_random, sizeof(__internal_random));
        __internal_random++;
    }

    return length;
}

//
// buffers
//
static unsigned char *benchmark_buffer_generate(benchmark_t *bench, size_t buffer) {
    if(!(bench->buffers[buffer] = (unsigned char *) malloc(sizeof(char) * bench->chunksize)))
        diep("malloc: buffer");

    if(randomize(bench->buffers[buffer], bench->chunksize) != bench->chunksize) {
        fprintf(stderr, "[-] not enought random data\n");
        exit(EXIT_FAILURE);
    }

    bench->hashes[buffer] = sha256(bench->buffers[buffer], bench->chunksize);
    // printf("[+] client %u, buffer %lu: %s\n", bench->id, buffer, bench->hashes[buffer]);

    return bench->hashes[buffer];
}

static void progress(unsigned int current, size_t total, char *message) {
    double pc = ((double) current / total) * 100;
    printf("\033[?25l");
    printf("\r[+] %s: %04.1f%%", message, pc);
}

static benchmark_t *benchmark_generate(benchmark_t *bench) {
    double size = ((double) bench->chunks * bench->chunksize) / (1024 * 1024);
    printf("[+] allocating buffers [client #%02u]: %.1f MB\n", bench->id, size);

    // allocating memory for hashes
    if(!(bench->hashes = (unsigned char **) malloc(sizeof(char *) * bench->chunks)))
        diep("malloc: hashes");

    // allocating memory for buffers
    if(!(bench->buffers = (unsigned char **) malloc(sizeof(char *) * bench->chunks)))
        diep("malloc: buffers");

    // generating buffers
    for(unsigned int buffer = 0; buffer < bench->chunks; buffer++) {
        if(buffer % 32 == 0)
            progress(buffer, bench->chunks, "generating random data");

        benchmark_buffer_generate(bench, buffer);
    }

    printf("\r\033[2K");

    return bench;
}

double benchmark_deltatime(benchmark_pass_t *pass) {
    int secs = (pass->time_end.tv_sec - pass->time_begin.tv_sec);
    double usecs = (pass->time_end.tv_nsec - pass->time_begin.tv_nsec) / 1000000000.0;

    return secs + usecs;
}

void benchmark_statistics(benchmark_t *bench) {
    double wtime = benchmark_deltatime(&bench->write);
    double rtime = benchmark_deltatime(&bench->read);

    float chunkskb = bench->chunksize / 1024.0;
    float wspeed = ((bench->chunksize * bench->chunks) / wtime) / (1024 * 1024);
    float rspeed = ((bench->chunksize * bench->chunks) / rtime) / (1024 * 1024);

    printf("[+] --- client %u ---\n", bench->id);
    printf("[+] write: %u keys of %.2f KB uploaded in %.2f seconds\n", bench->write.success, chunkskb, wtime);
    printf("[+] read : %u keys of %.2f KB uploaded in %.2f seconds\n", bench->read.success, chunkskb, rtime);

    printf("[+] write: client speed: %.2f MB/s\n", wspeed);
    printf("[+] read : client speed: %.2f MB/s\n", rspeed);
}

void benchmark_statistics_summary(benchmark_t **benchs, size_t length) {
    double wspeed = 0;
    double rspeed = 0;
    unsigned int rkeys = 0;
    unsigned int wkeys = 0;
    float chunksmb = 0;

    for(unsigned int i = 0; i < length; i++) {
        benchmark_t *bench = benchs[i];

        double wtime = benchmark_deltatime(&bench->write);
        double rtime = benchmark_deltatime(&bench->read);

        chunksmb += (bench->chunksize / 1024 / 1024.0) * bench->chunks;
        wspeed += ((bench->chunksize * bench->chunks) / wtime) / (1024 * 1024);
        rspeed += ((bench->chunksize * bench->chunks) / rtime) / (1024 * 1024);
        wkeys += bench->write.success;
        rkeys += bench->read.success;
    }

    printf("[+] === SUMMARY ===\n");
    printf("[+] write: clients speed: %.2f MB/s (%.2f MB)\n", wspeed, chunksmb);
    printf("[+] read : clients speed: %.2f MB/s (%.2f MB)\n", rspeed, chunksmb);
    printf("[+] ===============\n");
}

void benchmark_passes(benchmark_t **benchs, unsigned int length) {
    //
    // starting write pass
    // during this pass, buffers are written to the backend
    //
    printf("[+] starting pass: write\n");
    for(unsigned int i = 0; i < length; i++)
        if(pthread_create(&benchs[i]->pthread, NULL, benchmark_pass_write, benchs[i]))
            diep("pthread_create");

    for(unsigned int i = 0; i < length; i++)
        pthread_join(benchs[i]->pthread, NULL);

    //
    // starting read pass
    // during this pass, we get hashes keys but we don't do anything with them
    //
    printf("[+] starting pass: read, simple\n");
    for(unsigned int i = 0; i < length; i++)
        if(pthread_create(&benchs[i]->pthread, NULL, benchmark_pass_read, benchs[i]))
            diep("pthread_create");

    for(unsigned int i = 0; i < length; i++)
        pthread_join(benchs[i]->pthread, NULL);

    //
    // starting read secure pass
    // during this pass, we get hashes keys from backend and data hashes of
    // data are compared to keys to check data consistancy, we don't mesure time
    // of this pass because hashing time is not related to backend read/write
    //
    printf("[+] starting pass: read, secure\n");
    for(unsigned int i = 0; i < length; i++)
        if(pthread_create(&benchs[i]->pthread, NULL, benchmark_pass_read_secure, benchs[i]))
            diep("pthread_create");

    for(unsigned int i = 0; i < length; i++)
        pthread_join(benchs[i]->pthread, NULL);
}

int benchmark(benchmark_t **benchs, unsigned int length) {
    size_t psize = (benchs[0]->chunks * benchs[0]->chunksize);
    int size = (psize * length) / (1024 * 1024);

    //
    // allocating and fill buffers with random data
    // computing hash of buffers, which will be used as keys
    //
    printf("[+] generating random buffers: %d MB\n", size);
    for(unsigned int i = 0; i < length; i++)
        benchmark_generate(benchs[i]);

    //
    // running benchmark's differents passes
    //
    printf("[+] running passes\n");
    benchmark_passes(benchs, length);

    //
    // collecting and computing statistics per client (speed, ...)
    //
    printf("[+] collecting statistics\n");
    for(unsigned int i = 0; i < length; i++)
        benchmark_statistics(benchs[i]);

    benchmark_statistics_summary(benchs, length);

    return 0;
}

int main(int argc, char *argv[]) {
    benchmark_t **remotes;

    //
    // settings
    //
    if(argc < 2) {
        fprintf(stderr, "[-] missing ardb hosts: %s host:port [host:port [...]]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //
    // initializing
    //
    printf("[+] initializing %d clients\n", argc - 1);
    if(!(remotes = (benchmark_t **) malloc(sizeof(benchmark_t *) * (argc - 1))))
        diep("malloc");

    //
    // connecting clients
    //
    for(int i = 1; i < argc; i++) {
        char *arg = argv[i];
        int idx = i - 1;

        char *host;
        int port;

        if(!(host = strchr(arg, ':'))) {
            fprintf(stderr, "[-] %s: invalid host format, expected <host:port>\n", arg);
            exit(EXIT_FAILURE);
        }

        if(!(port = atoi(host + 1))) {
            fprintf(stderr, "[-] %s: invalid host format, expected <host:port>\n", arg);
            exit(EXIT_FAILURE);
        }

        if(!(host = strndup(arg, host - arg)))
            diep("strndup");

        printf("[+] connecting host: %s, port: %d\n", host, port);

        if(!(remotes[idx] = benchmark_init(host, port))) {
            fprintf(stderr, "[-] cannot allocate benchmark\n");
            exit(EXIT_FAILURE);
        }

        // cleaning
        free(host);

        remotes[idx]->id = (unsigned int) i;
        remotes[idx]->chunksize = CHUNKSIZE;
        remotes[idx]->chunks = CHUNKS;
    }

    //
    // starting benchmarks process
    //
    return benchmark(remotes, argc - 1);
}
