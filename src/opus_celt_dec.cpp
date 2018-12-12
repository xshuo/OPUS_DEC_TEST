#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include <malloc.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
    #include "avccelt_api.h"
}//:(:(:(:(
#endif

static bool XDEBUG = false;
static bool XXDEBUG = false;

static int kSamplesPerFrame = 320;//20ms 16K
static int kChannels = 2;
static int kBitsPerSample = 16;
static int kOutputBufferSize = kSamplesPerFrame * kBitsPerSample / 8 * kChannels; //1280
static int kInputBufferSize = 160; //base encoder

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

int main(int argc, char *argv[]) {
    char* inputFilePath = NULL;
    char* outputFilePath = NULL;
    char* logFiltPath = NULL;

    uint32_t retVal = 0;
    FILE* fpInput = NULL;
    FILE* fpOutput = NULL;
    OpusCustomDecoder* opus_custom_dec = NULL;
    unsigned char* inputBuf = NULL;
    short* outputBuf = NULL;
    int resInitDec = 0;

    int dec_frame_cnt_debug = 0;
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
        } else if (!(strcmp(*argv, "-XD"))) {
            XXDEBUG = true;
        }
        if (*argv) argv++;
    }

    fprintf(stdout,
            "InputFilePath: %s, OutputFilePath: %s, LogFilePath: %s, Channels: %d\n",
            inputFilePath,
            outputFilePath,
            logFiltPath,
            kChannels
            );

    if(logFiltPath != NULL) {
        FILE* fpLog = fopen(logFiltPath, "a");
        if (!fpLog) {
            fprintf(stderr, "log file open failed %s\n", logFiltPath);
            //do nothing
        } else {
            dup2(fileno(fpLog), STDERR_FILENO);
            fclose(fpLog);
            XDEBUG = true;
        }
    }

    // Open the input file
    fpInput = fopen(inputFilePath, "rb");
    if (!fpInput) {
        fprintf(stderr, "Could not open input file %s\n", inputFilePath);
        retVal = 1;
        goto job_finish;
    }

    // Open the output file
    fpOutput = fopen(outputFilePath, "wb");
    if (!fpOutput) {
        fprintf(stderr, "Could not open output file %s\n", outputFilePath);
        retVal = 1;
        goto job_finish;
    }

    resInitDec = AVC_DEC_CM4_16K_C1_F320_init(kChannels, &opus_custom_dec);
    if (resInitDec) {
        fprintf(stderr, "init avc decoder fail\n");
        retVal = 1;
        goto job_finish;
    }

    //Allocate input buffer
    inputBuf = (unsigned char*)malloc(kInputBufferSize);
    assert(inputBuf != NULL);

    //Allocate output buffer
    outputBuf = (short*)malloc(kOutputBufferSize);
    assert(outputBuf != NULL);

    if (XDEBUG)
        gettimeofday(&d_dec_start, NULL);
    fprintf(stderr, "start decode %s\n", inputFilePath);
    // Decode loop
    while (true) {
        int bytesRead =
            fread(inputBuf, sizeof(char), kInputBufferSize, fpInput);
        //fread do not get any byte, then feof return true;
        if (bytesRead == 0)//:(:(:( end of the file
            break;
        if (bytesRead < kInputBufferSize) {
            if (ferror(fpInput)) {
                if (errno == EINTR) {
                    //rewrite th position and retry
                    fprintf(stderr, "blocking read get EINTR! read bytes: %d\n", bytesRead);
                    fseek(fpInput, -bytesRead, SEEK_CUR);
                    clearerr(fpInput);
                    continue;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    //will not happen in this case
                    retVal = 1;
                    goto job_finish;
                } else {
                    retVal = 1;
                    goto job_finish;
                }
            } else if (feof(fpInput)) {//not happen
                fprintf(stderr, "input file length is not right, but pass(%d)\n", bytesRead);
                retVal = 0;
                goto job_finish;
            } else {
                fprintf(stderr, "read error error error\n");
                retVal = 1;
                goto job_finish;
            }
        }

        int decRet = AVC_DEC_CM4_16K_C1_F320_proc(
                inputBuf,
                outputBuf,
                kInputBufferSize,
                opus_custom_dec);
        fwrite(outputBuf, sizeof(short), decRet * kChannels, fpOutput);
        if (XXDEBUG)
            fprintf(stdout, "decode frame %d: %d %d\n", ++dec_frame_cnt_debug, bytesRead, decRet * kChannels);

    }
    fprintf(stderr, "end decode %s output file is %s\n", inputFilePath, outputFilePath);
    if (XDEBUG) {
        gettimeofday(&d_dec_end, NULL);
        //fprintf(stdout, "%ld %ld %ld %ld \n", d_dec_start.tv_sec, d_dec_start.tv_usec, d_dec_end.tv_sec, d_dec_end.tv_usec);
        d_cost_time = d_dec_end.tv_sec - d_dec_start.tv_sec + (d_dec_end.tv_usec - d_dec_start.tv_usec) / 1000000.0;
        fprintf(stderr, "cost time: %lf seconds\n", d_cost_time);
    }
    fprintf(stderr, "--------------------------------------------\n");

job_finish:
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

    return retVal;
}
