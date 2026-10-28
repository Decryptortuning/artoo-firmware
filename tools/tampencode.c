#include "../src/tampconfig.h"
#include "../vendor/tamp/compressor.h"
#include <stddef.h>
// Host-only entry point; bounded caller-owned input/output, fresh dictionary.
long artoo_tamp_encode(const unsigned char *input,size_t size,unsigned char *output,size_t capacity,unsigned literal) {
    unsigned char history[256];
    TampCompressor compressor;
    TampConf conf = {.window=8,.literal=literal,.extended=true,.lazy_matching=true};
    if (tamp_compressor_init(&compressor,&conf,history) != TAMP_OK) return -1;
    size_t consumed=0,written=0;
    tamp_res result=tamp_compressor_compress_and_flush(&compressor,output,capacity,&written,input,size,&consumed,false);
    return result==TAMP_OK && consumed==size ? (long)written : -1;
}
