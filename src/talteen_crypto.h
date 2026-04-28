#ifndef TALTEEN_CRYPTO_H
#define TALTEEN_CRYPTO_H

#include <QByteArray>
#include <QString>

struct evp_cipher_ctx_st;
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;

bool deriveKeyPbkdf2(const QString &password, const QByteArray &salt, int iterations, QByteArray *outKey);

EVP_CIPHER_CTX *createAesGcmEncryptContext(const QByteArray &key, const QByteArray &iv,
                                           const QByteArray &aad, QString *error);
bool encryptAesGcmChunk(EVP_CIPHER_CTX *ctx, const QByteArray &inChunk, QByteArray *outChunk, QString *error);
bool finalizeAesGcmEncrypt(EVP_CIPHER_CTX *ctx, QByteArray *finalChunk, QByteArray *tag, QString *error);

EVP_CIPHER_CTX *createAesGcmDecryptContext(const QByteArray &key, const QByteArray &iv,
                                           const QByteArray &aad, const QByteArray &tag, QString *error);
bool decryptAesGcmChunk(EVP_CIPHER_CTX *ctx, const QByteArray &inChunk, QByteArray *outChunk, QString *error);
bool finalizeAesGcmDecrypt(EVP_CIPHER_CTX *ctx, QByteArray *finalChunk, QString *error);

void freeCipherContext(EVP_CIPHER_CTX *ctx);

bool decryptFileAesGcm(const QString &inPath, const QString &outPath,
                       const QByteArray &key, const QByteArray &iv, const QByteArray &aad,
                       const QByteArray &tag, QString *error);

#endif
