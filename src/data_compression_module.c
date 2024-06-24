#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// Data Compression Module
// This component handles the compression and decompression of data to save storage space.

void data_compression_module(const char *input, char *output, size_t input_size, size_t *output_size) {
    // Compress the input data using zlib
    int ret;
    z_stream strm;
    unsigned char out[1024];

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        printf("Failed to initialize compression.\n");
        return;
    }

    strm.avail_in = input_size;
    strm.next_in = (unsigned char *)input;
    strm.avail_out = sizeof(out);
    strm.next_out = out;

    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        printf("Failed to compress data.\n");
        deflateEnd(&strm);
        return;
    }

    *output_size = sizeof(out) - strm.avail_out;
    memcpy(output, out, *output_size);

    deflateEnd(&strm);
    printf("Data compressed successfully.\n");
}

int main() {
    // Test the data_compression_module function
    const char *data = "Sample data to compress.";
    char compressed_data[1024];
    size_t compressed_size;

    data_compression_module(data, compressed_data, strlen(data), &compressed_size);
    printf("Compressed data size: %zu bytes\n", compressed_size);

    return 0;
}
