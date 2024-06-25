#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>

// Data Encryption Module
// This component handles the encryption and decryption of data to ensure data security.

void data_encryption_module(const char *input, char *output, int encrypt) {
    EVP_CIPHER_CTX *ctx;
    int len;
    int ciphertext_len;
    unsigned char key[EVP_MAX_KEY_LENGTH] = "0123456789abcdef"; // Example key
    unsigned char iv[EVP_MAX_IV_LENGTH] = "0123456789abcdef"; // Example IV

    if (!(ctx = EVP_CIPHER_CTX_new())) {
        printf("Failed to create context.\n");
        return;
    }

    if (encrypt) {
        if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)) {
            printf("Failed to initialize encryption.\n");
            EVP_CIPHER_CTX_free(ctx);
            return;
        }

        if (1 != EVP_EncryptUpdate(ctx, (unsigned char *)output, &len, (unsigned char *)input, strlen(input))) {
            printf("Failed to encrypt data.\n");
            EVP_CIPHER_CTX_free(ctx);
            return;
        }
        ciphertext_len = len;

        if (1 != EVP_EncryptFinal_ex(ctx, (unsigned char *)output + len, &len)) {
            printf("Failed to finalize encryption.\n");
            EVP_CIPHER_CTX_free(ctx);
            return;
        }
        ciphertext_len += len;
        output[ciphertext_len] = '\0';
    } else {
        if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)) {
            printf("Failed to initialize decryption.\n");
            EVP_CIPHER_CTX_free(ctx);
            return;
        }

        if (1 != EVP_DecryptUpdate(ctx, (unsigned char *)output, &len, (unsigned char *)input, strlen(input))) {
            printf("Failed to decrypt data.\n");
            EVP_CIPHER_CTX_free(ctx);
            return;
        }
        ciphertext_len = len;

        if (1 != EVP_DecryptFinal_ex(ctx, (unsigned char *)output + len, &len)) {
            printf("Failed to finalize decryption.\n");
            EVP_CIPHER_CTX_free(ctx);
            return;
        }
        ciphertext_len += len;
        output[ciphertext_len] = '\0';
    }

    EVP_CIPHER_CTX_free(ctx);
    printf("Data %s successfully.\n", encrypt ? "encrypted" : "decrypted");
}
