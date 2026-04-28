#include "talteen_crypto.h"

#include <QFile>

#include <openssl/evp.h>

bool deriveKeyPbkdf2(const QString &password, const QByteArray &salt, int iterations, QByteArray *outKey)
{
    outKey->resize(32);
    const QByteArray pass = password.toUtf8();
    return PKCS5_PBKDF2_HMAC(pass.constData(), pass.size(),
                             reinterpret_cast<const unsigned char *>(salt.constData()), salt.size(),
                             iterations, EVP_sha256(), outKey->size(),
                             reinterpret_cast<unsigned char *>(outKey->data())) == 1;
}

EVP_CIPHER_CTX *createAesGcmEncryptContext(const QByteArray &key, const QByteArray &iv,
                                           const QByteArray &aad, QString *error)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        *error = QObject::tr("Failed to initialize encryption");
        return nullptr;
    }

    bool ok = true;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        ok = false;
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1)
        ok = false;
    if (ok && EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                                 reinterpret_cast<const unsigned char *>(key.constData()),
                                 reinterpret_cast<const unsigned char *>(iv.constData())) != 1)
        ok = false;
    int aadLen = 0;
    if (ok && !aad.isEmpty()
        && EVP_EncryptUpdate(ctx, nullptr, &aadLen,
                             reinterpret_cast<const unsigned char *>(aad.constData()), aad.size()) != 1)
        ok = false;

    if (!ok)
    {
        EVP_CIPHER_CTX_free(ctx);
        *error = QObject::tr("Failed to initialize encryption");
        return nullptr;
    }
    return ctx;
}

bool encryptAesGcmChunk(EVP_CIPHER_CTX *ctx, const QByteArray &inChunk, QByteArray *outChunk, QString *error)
{
    outChunk->resize(inChunk.size() + EVP_CIPHER_block_size(EVP_aes_256_gcm()));
    int outLen = 0;
    if (EVP_EncryptUpdate(ctx,
                          reinterpret_cast<unsigned char *>(outChunk->data()), &outLen,
                          reinterpret_cast<const unsigned char *>(inChunk.constData()),
                          inChunk.size()) != 1)
    {
        *error = QObject::tr("Encryption failed");
        return false;
    }
    outChunk->resize(outLen);
    return true;
}

bool finalizeAesGcmEncrypt(EVP_CIPHER_CTX *ctx, QByteArray *finalChunk, QByteArray *tag, QString *error)
{
    finalChunk->resize(64);
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(finalChunk->data()), &finalLen) != 1)
    {
        *error = QObject::tr("Encryption failed");
        return false;
    }
    finalChunk->resize(finalLen);
    tag->resize(16);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag->size(), tag->data()) != 1)
    {
        *error = QObject::tr("Encryption failed");
        return false;
    }
    return true;
}

EVP_CIPHER_CTX *createAesGcmDecryptContext(const QByteArray &key, const QByteArray &iv,
                                           const QByteArray &aad, const QByteArray &tag, QString *error)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        *error = QObject::tr("Failed to initialize decryption");
        return nullptr;
    }

    bool ok = true;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        ok = false;
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1)
        ok = false;
    if (ok && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                                 reinterpret_cast<const unsigned char *>(key.constData()),
                                 reinterpret_cast<const unsigned char *>(iv.constData())) != 1)
        ok = false;
    int aadLen = 0;
    if (ok && !aad.isEmpty()
        && EVP_DecryptUpdate(ctx, nullptr, &aadLen,
                             reinterpret_cast<const unsigned char *>(aad.constData()), aad.size()) != 1)
        ok = false;
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag.size(),
                                  reinterpret_cast<unsigned char *>(const_cast<char *>(tag.constData()))) != 1)
        ok = false;

    if (!ok)
    {
        EVP_CIPHER_CTX_free(ctx);
        *error = QObject::tr("Failed to initialize decryption");
        return nullptr;
    }
    return ctx;
}

bool decryptAesGcmChunk(EVP_CIPHER_CTX *ctx, const QByteArray &inChunk, QByteArray *outChunk, QString *error)
{
    outChunk->resize(inChunk.size() + EVP_CIPHER_block_size(EVP_aes_256_gcm()));
    int outLen = 0;
    if (EVP_DecryptUpdate(ctx,
                          reinterpret_cast<unsigned char *>(outChunk->data()), &outLen,
                          reinterpret_cast<const unsigned char *>(inChunk.constData()),
                          inChunk.size()) != 1)
    {
        *error = QObject::tr("Decryption failed");
        return false;
    }
    outChunk->resize(outLen);
    return true;
}

bool finalizeAesGcmDecrypt(EVP_CIPHER_CTX *ctx, QByteArray *finalChunk, QString *error)
{
    finalChunk->resize(64);
    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(finalChunk->data()), &finalLen) != 1)
    {
        *error = QObject::tr("Authentication failed");
        return false;
    }
    finalChunk->resize(finalLen);
    return true;
}

void freeCipherContext(EVP_CIPHER_CTX *ctx)
{
    if (ctx)
        EVP_CIPHER_CTX_free(ctx);
}

bool decryptFileAesGcm(const QString &inPath, const QString &outPath,
                       const QByteArray &key, const QByteArray &iv, const QByteArray &aad,
                       const QByteArray &tag, QString *error)
{
    QFile inFile(inPath);
    QFile outFile(outPath);
    if (!inFile.open(QIODevice::ReadOnly))
    {
        *error = QObject::tr("Unable to read encrypted payload");
        return false;
    }
    if (!outFile.open(QIODevice::WriteOnly))
    {
        *error = QObject::tr("Unable to write decrypted payload");
        return false;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        *error = QObject::tr("Failed to initialize decryption");
        return false;
    }

    bool ok = true;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        ok = false;
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1)
        ok = false;
    if (ok && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                                 reinterpret_cast<const unsigned char *>(key.constData()),
                                 reinterpret_cast<const unsigned char *>(iv.constData())) != 1)
        ok = false;

    int len = 0;
    if (ok && !aad.isEmpty()
        && EVP_DecryptUpdate(ctx, nullptr, &len,
                             reinterpret_cast<const unsigned char *>(aad.constData()), aad.size()) != 1)
        ok = false;

    QByteArray inChunk;
    QByteArray outChunk(65536 + EVP_CIPHER_block_size(EVP_aes_256_gcm()), 0);
    while (ok && !(inChunk = inFile.read(65536)).isEmpty())
    {
        if (EVP_DecryptUpdate(ctx,
                              reinterpret_cast<unsigned char *>(outChunk.data()), &len,
                              reinterpret_cast<const unsigned char *>(inChunk.constData()),
                              inChunk.size()) != 1)
        {
            ok = false;
            break;
        }
        if (outFile.write(outChunk.constData(), len) != len)
        {
            ok = false;
            break;
        }
    }
    if (ok && inFile.error() != QFile::NoError)
        ok = false;

    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag.size(),
                                  reinterpret_cast<unsigned char *>(const_cast<char *>(tag.constData()))) != 1)
        ok = false;

    int finalLen = 0;
    if (ok && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(outChunk.data()), &finalLen) != 1)
        ok = false;
    if (ok && finalLen > 0 && outFile.write(outChunk.constData(), finalLen) != finalLen)
        ok = false;

    EVP_CIPHER_CTX_free(ctx);

    if (!ok)
    {
        if (error->isEmpty())
            *error = QObject::tr("Authentication failed");
        outFile.close();
        QFile::remove(outPath);
        return false;
    }
    return true;
}
