#include "hash.h"

#include <openssl/evp.h>
#include <stdio.h>

int mygit_sha1(const unsigned char *data, size_t len,
               unsigned char out[20])
{
    if (data == NULL || out == NULL) {
        return -1;
    }

    unsigned int digest_len = 0;
    if (EVP_Digest(data, len, out, &digest_len, EVP_sha1(), NULL) != 1) {
        return -1;
    }

    return 0;
}

void mygit_hex(const unsigned char hash[20], char out[41])
{
    for (int i = 0; i < 20; i++) {
        snprintf(out + i * 2, 3, "%02x", hash[i]);
    }
    out[40] = '\0';
}
