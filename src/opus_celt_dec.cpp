#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/prctl.h>

#include <malloc.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "CountDownLatch.hpp"

#ifdef __cplusplus
extern "C" {
    #include "avccelt_api.h"
}//:(:(:(:(
#endif

#define MAX_FILE_PATH_LEN 256
#define FRAME_NUM_PER_THREAD 200000

typedef struct job_para {
    int job_id;

    char inputFilePath[MAX_FILE_PATH_LEN];
    long r_pos;
    long r_len;

    char outputFilePath[MAX_FILE_PATH_LEN];
    long w_pos;
    long w_len;

    CountDownLatch* latch;
}job_para, *job_para_p;

static bool XDEBUG = true;
static int kChannels = 2;
static int kSamplesPerFrame = 320;//20ms 16K static int kChannels = 2;
static int kBitsPerSample = 16;
static int kOutputBufferSize = kSamplesPerFrame * kBitsPerSample / 8 * kChannels; //1280
static int kInputBufferSize = 160; //base encoder

static int kFrameBytesPerThread = FRAME_NUM_PER_THREAD * kInputBufferSize;

static void show_help(const char* cmd) {
    fprintf(stderr, "Usage: %s [options]\n", cmd);

    fprintf(stderr, "options include:\n"
            "  -I <input file path>\n"
            "  -O <output file path>\n"
            "  -C [<channels>] 1 or 2, default is 2\n"
            "  -D [<log file>] default is stderr\n");
    fprintf(stderr, "\n\n");
    fprintf(stderr, "Mandatory options are <-I> <-O>\n");
    fprintf(stderr, "full command like this:\n"
            "  C1_OPUS_DEC -I ~/1.avc -O ~/1.pcm\n"
            "full debug command like this: \n"
            "  C1_OPUS_DEC -I ~/1.avc -O ~/1.pcm -D ~/1.log\n");
}

void* decode_thread(void* args) {
    FILE* fpInput = NULL;
    FILE* fpOutput = NULL;
    OpusCustomDecoder* opus_custom_dec = NULL;
    unsigned char* inputBuf = NULL;
    short* outputBuf = NULL;
    int resInitDec = 0;
    char thread_name[30];
    int debug_decode_frame = 0;

    job_para_p p = (job_para_p)args;
    sprintf(thread_name, "avc_decode_thread%d", p->job_id);
    prctl(PR_SET_NAME, thread_name);
    // Open the input file
    fpInput = fopen(p->inputFilePath, "rb");
    if (!fpInput) {
        fprintf(stderr, "Could not open input file %s\n", p->inputFilePath);
        goto job_finish;
    }

    // Open the output file
    fpOutput = fopen(p->outputFilePath, "a");
    if (!fpOutput) {
        fprintf(stderr, "Could not open output file %s\n", p->outputFilePath);
        goto job_finish;
    }

    resInitDec = AVC_DEC_CM4_16K_C1_F320_init(kChannels, &opus_custom_dec);
    if (resInitDec) {
        fprintf(stderr, "init avc decoder fail\n");
        goto job_finish;
    }

    //Allocate input buffer
    inputBuf = (unsigned char*)malloc(kInputBufferSize);
    assert(inputBuf != NULL);

    //Allocate output buffer
    outputBuf = (short*)malloc(kOutputBufferSize);
    assert(outputBuf != NULL);

    fprintf(stderr, "start decode(%s %ld) (%ld, %ld)\n", thread_name, pthread_self(), p->r_pos, p->r_len);
    fseek(fpInput, p->r_pos, SEEK_SET);
    fseek(fpOutput, p->w_pos, SEEK_SET);
    // Decode loop
    while (true) {
        int bytesRead =
            fread(inputBuf, sizeof(char), kInputBufferSize, fpInput);
        //fread do not get any byte, then feof return true;
        if (bytesRead == 0)//:(:(:( end of the file
            break;
        if (bytesRead < kInputBufferSize) {
            if (ferror(fpInput)) {
                fprintf(stderr, "read data is abnormal!:%s", strerror(errno));
                if (errno == EINTR) {
                    //rewrite th position and retry
                    fprintf(stderr, "blocking read get EINTR! read bytes: %d\n", bytesRead);
                    fseek(fpInput, -bytesRead, SEEK_CUR);
                    clearerr(fpInput);
                    continue;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    //will not happen in this case
                    goto job_finish;
                } else {
                    goto job_finish;
                }
            } else if (feof(fpInput)) {//not happen
                fprintf(stderr, "input file length is not right, but pass(%d)\n", bytesRead);
                goto job_finish;
            } else {
                fprintf(stderr, "read error error error\n");
                goto job_finish;
            }
        }

        int decRet = AVC_DEC_CM4_16K_C1_F320_proc(
                inputBuf,
                outputBuf,
                bytesRead,
                opus_custom_dec);
        fwrite(outputBuf, sizeof(short), decRet * kChannels, fpOutput);
        debug_decode_frame++;

        if (XDEBUG) {
            fprintf(stdout, "%s:decode frame(%d): %d->%d\n",
                    thread_name, debug_decode_frame, bytesRead, decRet * kChannels * 2);
        }

        p->r_len -= bytesRead;
        if (!p->r_len) break;
    }
    fprintf(stderr, "end decode(%s)\n", thread_name);

job_finish:
    p->latch->countDown();
    if (fpInput)
        fclose(fpInput);
    if (fpOutput)
        fclose(fpOutput);
    if (opus_custom_dec)
        AVC_DEC_CM4_uninit(opus_custom_dec);
    if (inputBuf)
        free(inputBuf);
    if (outputBuf)
        free(outputBuf);
    return (void*)112233;
}

int main(int argc, char *argv[]) {
    char* inputFilePath = NULL;
    char* outputFilePath = NULL;
    char* logFiltPath = NULL;

    uint32_t retVal = 0;
    struct timeval d_dec_start, d_dec_end;
    double d_cost_time = -1;

    if (argc == 2 && 0 == strcmp(argv[1], "--help")) {
        show_help(argv[0]);
        return 0;
    }

    if(argc < 5) {
        show_help(argv[0]);
        return 1;
    }

    argv++;
    while (*argv) {
        if (!strcmp(*argv, "-I")) {
            argv++;
            if (*argv) inputFilePath = *argv;
        } else if (!strcmp(*argv, "-O")) {
            argv++;
            if (*argv) outputFilePath = *argv;
        } else if (!strcmp(*argv, "-C")) {
            argv++;
            if (*argv) kChannels = atoi(*argv);
            //refresh output buffer size
            kOutputBufferSize = kSamplesPerFrame * kBitsPerSample / 8 * kChannels;
        } else if (!strcmp(*argv, "-D")) {
            argv++;
            if (*argv) logFiltPath = *argv;
        } else if (!(strcmp(*argv, "-DD"))) {
            XDEBUG = true;
        }
        if (*argv) argv++;
    }

    fprintf(stderr,
            "InputFilePath: %s, OutputFilePath: %s, LogFilePath: %s, Channels: %d\n",
            inputFilePath,
            outputFilePath,
            logFiltPath,
            kChannels
            );

    if (strlen(inputFilePath) >= MAX_FILE_PATH_LEN
            || strlen(outputFilePath) >= MAX_FILE_PATH_LEN) {
        fprintf(stderr, "file path length need < 256");
        return 1;
    }

    if(logFiltPath != NULL) {
        FILE* fpLog = fopen(logFiltPath, "a");
        if (!fpLog) {
            fprintf(stderr, "log file open failed %s\n", logFiltPath);
            //do nothing
        } else {
            dup2(fileno(fpLog), STDERR_FILENO);
            fclose(fpLog);
        }
    }

    struct stat stat_buf;
    stat(inputFilePath, &stat_buf);
    long file_len = stat_buf.st_size;
    if (file_len % kInputBufferSize) {
        fprintf(stderr, "file length is not frame count");
        return 1;
    }
    int thread_cnt = file_len / kFrameBytesPerThread +
        (file_len % kFrameBytesPerThread > 0 ? 1 : 0);
    fprintf(stderr, "decode thread count: %d\n", thread_cnt);

    gettimeofday(&d_dec_start, NULL);
    fprintf(stderr, "start decode %s:%ld\n", inputFilePath, file_len);
    CountDownLatch latch(thread_cnt);

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    job_para_p thread_args = new job_para[thread_cnt];
    for (int i = 0; i < thread_cnt; i++) {
        thread_args[i].job_id = i;
        strcpy(thread_args[i].inputFilePath, inputFilePath);
        strcpy(thread_args[i].outputFilePath, outputFilePath);
        thread_args[i].r_pos = i * kFrameBytesPerThread;
        thread_args[i].r_len = file_len > kFrameBytesPerThread ? kFrameBytesPerThread : file_len;
        thread_args[i].w_pos = thread_args[i].r_pos * 8;
        thread_args[i].w_len = thread_args[i].r_len * 8;
        thread_args[i].latch = &latch;
        file_len -= thread_args[i].r_len;

        pthread_create(&thread, &attr, decode_thread, &thread_args[i]);
    }

    latch.wait();
    delete [] thread_args;
    fprintf(stderr, "end decode %s output file is %s\n", inputFilePath, outputFilePath);
    gettimeofday(&d_dec_end, NULL);
    //fprintf(stdout, "%ld %ld %ld %ld \n", d_dec_start.tv_sec, d_dec_start.tv_usec, d_dec_end.tv_sec, d_dec_end.tv_usec);
    d_cost_time = d_dec_end.tv_sec - d_dec_start.tv_sec + (d_dec_end.tv_usec - d_dec_start.tv_usec) / 1000000.0;
    fprintf(stderr, "cost time: %lf seconds\n", d_cost_time);
    fprintf(stderr, "--------------------------------------------\n");

    return retVal;
}
