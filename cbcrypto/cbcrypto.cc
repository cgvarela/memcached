/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include "config.h"
#include <cbcrypto/cbcrypto.h>

#include <iomanip>
#include <memory>
#include <phosphor/phosphor.h>
#include <platform/base64.h>
#include <sstream>
#include <stdexcept>

#ifdef _MSC_VER

#include <bcrypt.h>
#include <windows.h>

namespace cb {
namespace crypto {

struct HeapAllocDeleter {
    void operator()(PBYTE bytes) {
        HeapFree(GetProcessHeap(), 0, bytes);
    }
};

using uniqueHeapPtr = std::unique_ptr<BYTE, HeapAllocDeleter>;

static inline std::vector<uint8_t> hash(const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& data,
                                        LPCWSTR algorithm,
                                        int flags) {
    BCRYPT_ALG_HANDLE hAlg;
    NTSTATUS status =
            BCryptOpenAlgorithmProvider(&hAlg, algorithm, nullptr, flags);
    if (status < 0) {
        throw std::runtime_error(
                "digest: BCryptOpenAlgorithmProvider return: " +
                std::to_string(status));
    }

    DWORD pcbResult = 0;
    DWORD cbHashObject = 0;

    // calculate the size of the buffer to hold the hash object
    status = BCryptGetProperty(hAlg,
                               BCRYPT_OBJECT_LENGTH,
                               (PBYTE)&cbHashObject,
                               sizeof(DWORD),
                               &pcbResult,
                               0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("digest: BCryptGetProperty return: " +
                                 std::to_string(status));
    }

    // calculate the length of the hash
    DWORD cbHash = 0;
    status = BCryptGetProperty(hAlg,
                               BCRYPT_HASH_LENGTH,
                               (PBYTE)&cbHash,
                               sizeof(DWORD),
                               &pcbResult,
                               0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("digest: BCryptGetProperty return: " +
                                 std::to_string(status));
    }

    // allocate the hash object on the heap
    uniqueHeapPtr pbHashObject(
            (PBYTE)HeapAlloc(GetProcessHeap(), 0, cbHashObject));
    if (!pbHashObject) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::bad_alloc();
    }

    std::vector<uint8_t> ret(cbHash);

    // create the hash
    BCRYPT_HASH_HANDLE hHash;
    status = BCryptCreateHash(hAlg,
                              &hHash,
                              pbHashObject.get(),
                              cbHashObject,
                              (PUCHAR)key.data(),
                              ULONG(key.size()),
                              0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("digest: BCryptCreateHash return: " +
                                 std::to_string(status));
    }

    status = BCryptHashData(hHash, (PBYTE)data.data(), ULONG(data.size()), 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        BCryptDestroyHash(hHash);
        throw std::runtime_error("digest: BCryptHashData return: " +
                                 std::to_string(status));
    }

    status = BCryptFinishHash(hHash, ret.data(), cbHash, 0);

    // Release resources
    BCryptCloseAlgorithmProvider(hAlg, 0);
    BCryptDestroyHash(hHash);

    if (status < 0) {
        throw std::runtime_error("digest: BCryptFinishHash return: " +
                                 std::to_string(status));
    }

    return ret;
}

static std::vector<uint8_t> HMAC_MD5(const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& data) {
    return hash(key, data, BCRYPT_MD5_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
}

static std::vector<uint8_t> HMAC_SHA1(const std::vector<uint8_t>& key,
                                      const std::vector<uint8_t>& data) {
    return hash(key, data, BCRYPT_SHA1_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
}

static std::vector<uint8_t> HMAC_SHA256(const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& data) {
    return hash(
            key, data, BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
}

static std::vector<uint8_t> HMAC_SHA512(const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& data) {
    return hash(
            key, data, BCRYPT_SHA512_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
}

static inline std::vector<uint8_t> PBKDF2(const std::string& pass,
                                          const std::vector<uint8_t>& salt,
                                          unsigned int iterationCount,
                                          LPCWSTR algorithm) {
    // open an algorithm handle
    BCRYPT_ALG_HANDLE hAlg;
    NTSTATUS status;

    status = BCryptOpenAlgorithmProvider(
            &hAlg, algorithm, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status < 0) {
        throw std::runtime_error(
                "digest: BCryptOpenAlgorithmProvider return: " +
                std::to_string(status));
    }

    DWORD pcbResult = 0;
    DWORD cbHashObject = 0;

    // calculate the length of the hash
    DWORD cbHash = 0;
    status = BCryptGetProperty(hAlg,
                               BCRYPT_HASH_LENGTH,
                               (PBYTE)&cbHash,
                               sizeof(DWORD),
                               &pcbResult,
                               0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("digest: BCryptGetProperty return: " +
                                 std::to_string(status));
    }

    std::vector<uint8_t> ret(cbHash);

    status = BCryptDeriveKeyPBKDF2(hAlg,
                                   (PUCHAR)pass.data(),
                                   ULONG(pass.size()),
                                   (PUCHAR)salt.data(),
                                   ULONG(salt.size()),
                                   iterationCount,
                                   (PUCHAR)ret.data(),
                                   ULONG(ret.size()),
                                   0);

    // Release resources
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (status < 0) {
        throw std::runtime_error("digest: BCryptDeriveKeyPBKDF2 return: " +
                                 std::to_string(status));
    }

    return ret;
}

static std::vector<uint8_t> PBKDF2_HMAC_SHA1(const std::string& pass,
                                             const std::vector<uint8_t>& salt,
                                             unsigned int iterationCount) {
    return PBKDF2(pass, salt, iterationCount, BCRYPT_SHA1_ALGORITHM);
}

static std::vector<uint8_t> PBKDF2_HMAC_SHA256(const std::string& pass,
                                               const std::vector<uint8_t>& salt,
                                               unsigned int iterationCount) {
    return PBKDF2(pass, salt, iterationCount, BCRYPT_SHA256_ALGORITHM);
}

static std::vector<uint8_t> PBKDF2_HMAC_SHA512(const std::string& pass,
                                               const std::vector<uint8_t>& salt,
                                               unsigned int iterationCount) {
    return PBKDF2(pass, salt, iterationCount, BCRYPT_SHA512_ALGORITHM);
}

static std::vector<uint8_t> digest_md5(const std::vector<uint8_t>& data) {
    return hash({}, data, BCRYPT_MD5_ALGORITHM, 0);
}

static std::vector<uint8_t> digest_sha1(const std::vector<uint8_t>& data) {
    return hash({}, data, BCRYPT_SHA1_ALGORITHM, 0);
}

static std::vector<uint8_t> digest_sha256(const std::vector<uint8_t>& data) {
    return hash({}, data, BCRYPT_SHA256_ALGORITHM, 0);
}

static std::vector<uint8_t> digest_sha512(const std::vector<uint8_t>& data) {
    return hash({}, data, BCRYPT_SHA512_ALGORITHM, 0);
}

std::vector<uint8_t> AES_256_cbc(bool encrypt,
                                 const std::vector<uint8_t>& key,
                                 const std::vector<uint8_t>& iv,
                                 const uint8_t* data,
                                 size_t length) {
    BCRYPT_ALG_HANDLE hAlg;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
            &hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);

    if (status < 0) {
        throw std::runtime_error(
                "encrypt: BCryptOpenAlgorithmProvider() return: " +
                std::to_string(status));
    }

    DWORD cbData = 0;
    DWORD cbKeyObject = 0;

    // Calculate the size of the buffer to hold the KeyObject.
    status = BCryptGetProperty(hAlg,
                               BCRYPT_OBJECT_LENGTH,
                               (PBYTE)&cbKeyObject,
                               sizeof(DWORD),
                               &cbData,
                               0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("encrypt: BCryptGetProperty() return: " +
                                 std::to_string(status));
    }

    // Allocate the key object on the heap.
    uniqueHeapPtr pbKeyObject(
            (PBYTE)HeapAlloc(GetProcessHeap(), 0, cbKeyObject));
    if (!pbKeyObject) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::bad_alloc();
    }

    status = BCryptSetProperty(hAlg,
                               BCRYPT_CHAINING_MODE,
                               (PBYTE)BCRYPT_CHAIN_MODE_CBC,
                               sizeof(BCRYPT_CHAIN_MODE_CBC),
                               0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("encrypt: BCryptSetProperty() return: " +
                                 std::to_string(status));
    }

    // Generate the key from supplied input key bytes.
    BCRYPT_KEY_HANDLE hKey;
    status = BCryptGenerateSymmetricKey(hAlg,
                                        &hKey,
                                        pbKeyObject.get(),
                                        cbKeyObject,
                                        (PBYTE)key.data(),
                                        ULONG(key.size()),
                                        0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error(
                "encrypt: BCryptGenerateSymmetricKey() return: " +
                std::to_string(status));
    }

    // For some reason the API will modify the input vector.. just create a
    // copy.. it's small anyway
    std::vector<uint8_t> civ{iv};

    std::vector<uint8_t> ret(length + iv.size());
    if (encrypt) {
        status = BCryptEncrypt(hKey,
                               (PUCHAR)data,
                               ULONG(length),
                               NULL,
                               (PBYTE)civ.data(),
                               ULONG(civ.size()),
                               ret.data(),
                               ULONG(ret.size()),
                               &cbData,
                               BCRYPT_BLOCK_PADDING);
    } else {
        status = BCryptDecrypt(hKey,
                               (PUCHAR)data,
                               ULONG(length),
                               nullptr,
                               (PBYTE)civ.data(),
                               ULONG(civ.size()),
                               ret.data(),
                               ULONG(ret.size()),
                               &cbData,
                               BCRYPT_BLOCK_PADDING);
    }

    BCryptCloseAlgorithmProvider(hAlg, 0);
    BCryptDestroyKey(hKey);

    if (status < 0) {
        throw std::runtime_error("encrypt: BCryptEncrypt() return: " +
                                 std::to_string(status));
    }

    ret.resize(cbData);

    return ret;
}

std::vector<uint8_t> encrypt(const Cipher& cipher,
                             const std::vector<uint8_t>& key,
                             const std::vector<uint8_t>& iv,
                             const uint8_t* data,
                             size_t length) {
    return AES_256_cbc(true, key, iv, data, length);
}

std::vector<uint8_t> decrypt(const Cipher& cipher,
                             const std::vector<uint8_t>& key,
                             const std::vector<uint8_t>& iv,
                             const uint8_t* data,
                             size_t length) {
    return AES_256_cbc(false, key, iv, data, length);
}
} // namespace crypto
} // namespace cb

#elif defined(__APPLE__)

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonKeyDerivation.h>

static std::vector<uint8_t> HMAC_MD5(const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::MD5_DIGEST_SIZE);
    CCHmac(kCCHmacAlgMD5, key.data(), key.size(), data.data(), data.size(),
           ret.data());
    return ret;
}

static std::vector<uint8_t> HMAC_SHA1(const std::vector<uint8_t>& key,
                                      const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA1_DIGEST_SIZE);
    CCHmac(kCCHmacAlgSHA1, key.data(), key.size(), data.data(), data.size(),
           ret.data());
    return ret;
}

static std::vector<uint8_t> HMAC_SHA256(const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA256_DIGEST_SIZE);
    CCHmac(kCCHmacAlgSHA256, key.data(), key.size(), data.data(), data.size(),
           ret.data());
    return ret;
}

static std::vector<uint8_t> HMAC_SHA512(const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA512_DIGEST_SIZE);
    CCHmac(kCCHmacAlgSHA512, key.data(), key.size(), data.data(), data.size(),
           ret.data());
    return ret;
}

static std::vector<uint8_t> PBKDF2_HMAC_SHA1(const std::string& pass,
                                             const std::vector<uint8_t>& salt,
                                             unsigned int iterationCount) {
    std::vector<uint8_t> ret(cb::crypto::SHA1_DIGEST_SIZE);
    auto err = CCKeyDerivationPBKDF(kCCPBKDF2,
                                    pass.data(), pass.size(),
                                    salt.data(), salt.size(),
                                    kCCPRFHmacAlgSHA1, iterationCount,
                                    ret.data(), ret.size());

    if (err != 0) {
        throw std::runtime_error(
            "cb::crypto::PBKDF2_HMAC(SHA1): CCKeyDerivationPBKDF failed: " +
            std::to_string(err));
    }
    return ret;
}

static std::vector<uint8_t> PBKDF2_HMAC_SHA256(const std::string& pass,
                                               const std::vector<uint8_t>& salt,
                                               unsigned int iterationCount) {
    std::vector<uint8_t> ret(cb::crypto::SHA256_DIGEST_SIZE);
    auto err = CCKeyDerivationPBKDF(kCCPBKDF2,
                                    pass.data(), pass.size(),
                                    salt.data(), salt.size(),
                                    kCCPRFHmacAlgSHA256, iterationCount,
                                    ret.data(), ret.size());
    if (err != 0) {
        throw std::runtime_error(
            "cb::crypto::PBKDF2_HMAC(SHA256): CCKeyDerivationPBKDF failed: " +
            std::to_string(err));
    }
    return ret;
}

static std::vector<uint8_t> PBKDF2_HMAC_SHA512(const std::string& pass,
                                               const std::vector<uint8_t>& salt,
                                               unsigned int iterationCount) {
    std::vector<uint8_t> ret(cb::crypto::SHA512_DIGEST_SIZE);
    auto err = CCKeyDerivationPBKDF(kCCPBKDF2,
                                    pass.data(), pass.size(),
                                    salt.data(), salt.size(),
                                    kCCPRFHmacAlgSHA512, iterationCount,
                                    ret.data(), ret.size());
    if (err != 0) {
        throw std::runtime_error(
            "cb::crypto::PBKDF2_HMAC(SHA512): CCKeyDerivationPBKDF failed: " +
            std::to_string(err));
    }
    return ret;
}

static std::vector<uint8_t> digest_md5(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::MD5_DIGEST_SIZE);
    CC_MD5(data.data(), data.size(), ret.data());
    return ret;
}

static std::vector<uint8_t> digest_sha1(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA1_DIGEST_SIZE);
    CC_SHA1(data.data(), data.size(), ret.data());
    return ret;
}

static std::vector<uint8_t> digest_sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA256_DIGEST_SIZE);
    CC_SHA256(data.data(), data.size(), ret.data());
    return ret;
}

static std::vector<uint8_t> digest_sha512(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA512_DIGEST_SIZE);
    CC_SHA512(data.data(), data.size(), ret.data());
    return ret;
}

namespace cb {
namespace crypto {

/**
 * Validate that the input parameters for the encryption cipher specified
 * is supported and contains the right buffers.
 *
 * Currently only AES_256_cbc is supported
 */
static void validateEncryptionCipher(const Cipher& cipher,
                                     const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& iv) {
    switch (cipher) {
    case Cipher::AES_256_cbc:
        if (key.size() != kCCKeySizeAES256) {
            throw std::invalid_argument(
                    "cb::crypto::validateEncryptionCipher: Cipher requires a "
                    "key "
                    "length of " +
                    std::to_string(kCCKeySizeAES256) +
                    " provided key with length " + std::to_string(key.size()));
        }

        if (iv.size() != 16) {
            throw std::invalid_argument(
                    "cb::crypto::validateEncryptionCipher: Cipher requires a "
                    "iv "
                    "length of 16 provided iv with length " +
                    std::to_string(iv.size()));
        }
        return;
    }

    throw std::invalid_argument("cb::crypto::validateEncryptionCipher: Unknown Cipher " +
                                std::to_string(int(cipher)));
}

std::vector<uint8_t> encrypt(const Cipher& cipher,
                             const std::vector<uint8_t>& key,
                             const std::vector<uint8_t>& iv,
                             const uint8_t* data,
                             size_t length) {
    TRACE_EVENT2("cbcrypto", "encrypt", "cipher", int(cipher), "size", length);
    size_t outputsize = 0;
    std::vector<uint8_t> ret(length + kCCBlockSizeAES128);

    validateEncryptionCipher(cipher, key, iv);

    auto status = CCCrypt(kCCEncrypt,
                          kCCAlgorithmAES128,
                          kCCOptionPKCS7Padding,
                          key.data(),
                          kCCKeySizeAES256,
                          iv.data(),
                          data,
                          length,
                          ret.data(),
                          ret.size(),
                          &outputsize);

    if (status != kCCSuccess) {
        throw std::runtime_error("cb::crypto::encrypt: CCCrypt failed: " +
                                 std::to_string(status));
    }

    ret.resize(outputsize);
    return ret;
}

std::vector<uint8_t> decrypt(const Cipher& cipher,
                             const std::vector<uint8_t>& key,
                             const std::vector<uint8_t>& iv,
                             const uint8_t* data,
                             size_t length) {
    TRACE_EVENT2("cbcrypto", "decrypt", "cipher", int(cipher), "size", length);
    size_t outputsize = 0;
    std::vector<uint8_t> ret(length);

    validateEncryptionCipher(cipher, key, iv);

    auto status = CCCrypt(kCCDecrypt,
                          kCCAlgorithmAES128,
                          kCCOptionPKCS7Padding,
                          key.data(),
                          kCCKeySizeAES256,
                          iv.data(),
                          data,
                          length,
                          ret.data(),
                          ret.size(),
                          &outputsize);

    if (status != kCCSuccess) {
        throw std::runtime_error("cb::crypto::decrypt: CCCrypt failed: " +
                                 std::to_string(status));
    }

    ret.resize(outputsize);
    return ret;
}

} // namespace crypto
} // namespace cb

#else

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

// OpenSSL

static std::vector<uint8_t> HMAC_MD5(const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::MD5_DIGEST_SIZE);
    if (HMAC(EVP_md5(), key.data(), key.size(), data.data(), data.size(),
             ret.data(), nullptr) == nullptr) {
        throw std::runtime_error("cb::crypto::HMAC(MD5): HMAC failed");
    }
    return ret;
}

static std::vector<uint8_t> HMAC_SHA1(const std::vector<uint8_t>& key,
                                      const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA1_DIGEST_SIZE);
    if (HMAC(EVP_sha1(), key.data(), key.size(), data.data(), data.size(),
             ret.data(), nullptr) == nullptr) {
        throw std::runtime_error("cb::crypto::HMAC(SHA1): HMAC failed");
    }
    return ret;
}

static std::vector<uint8_t> HMAC_SHA256(const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA256_DIGEST_SIZE);
    if (HMAC(EVP_sha256(), key.data(), key.size(), data.data(), data.size(),
             ret.data(), nullptr) == nullptr) {
        throw std::runtime_error(
            "cb::crypto::HMAC(SHA256): HMAC failed");
    }
    return ret;
}

static std::vector<uint8_t> HMAC_SHA512(const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA512_DIGEST_SIZE);
    if (HMAC(EVP_sha512(), key.data(), key.size(), data.data(), data.size(),
             ret.data(), nullptr) == nullptr) {
        throw std::runtime_error(
            "cb::crypto::HMAC(SHA512): HMAC failed");
    }
    return ret;
}

static std::vector<uint8_t> PBKDF2_HMAC_SHA1(const std::string& pass,
                                             const std::vector<uint8_t>& salt,
                                             unsigned int iterationCount) {
    std::vector<uint8_t> ret(cb::crypto::SHA1_DIGEST_SIZE);
#if defined(HAVE_PKCS5_PBKDF2_HMAC)
    auto err = PKCS5_PBKDF2_HMAC(pass.data(), int(pass.size()),
                                 salt.data(), int(salt.size()),
                                 iterationCount,
                                 EVP_sha1(),
                                 cb::crypto::SHA1_DIGEST_SIZE,
                                 ret.data());

    if (err != 1) {
        throw std::runtime_error(
            "cb::crypto::PBKDF2_HMAC(SHA1): PKCS5_PBKDF2_HMAC_SHA1 failed: " +
            std::to_string(err));
    }
#elif defined(HAVE_PKCS5_PBKDF2_HMAC_SHA1)
    auto err = PKCS5_PBKDF2_HMAC_SHA1(pass.data(), int(pass.size()),
                                      salt.data(), int(salt.size()),
                                      iterationCount,
                                      cb::crypto::SHA1_DIGEST_SIZE,
                                      ret.data());
    if (err != 1) {
        throw std::runtime_error(
            "cb::crypto::PBKDF2_HMAC(SHA1): PKCS5_PBKDF2_HMAC_SHA1 failed" +
            std::to_string(err));
    }
#else
    throw std::runtime_error("cb::crypto::PBKDF2_HMAC(SHA1): Not supported");
#endif

    return ret;
}

static std::vector<uint8_t> PBKDF2_HMAC_SHA256(const std::string& pass,
                                               const std::vector<uint8_t>& salt,
                                               unsigned int iterationCount) {
    std::vector<uint8_t> ret(cb::crypto::SHA256_DIGEST_SIZE);
#if defined(HAVE_PKCS5_PBKDF2_HMAC)
    auto err = PKCS5_PBKDF2_HMAC(pass.data(), int(pass.size()),
                                 salt.data(), int(salt.size()),
                                 iterationCount,
                                 EVP_sha256(),
                                 cb::crypto::SHA256_DIGEST_SIZE,
                                 ret.data());
    if (err != 1) {
        throw std::runtime_error(
            "cb::crypto::PBKDF2_HMAC(SHA256): PKCS5_PBKDF2_HMAC failed" +
            std::to_string(err));
    }
#else
    throw std::runtime_error(
        "cb::crypto::PBKDF2_HMAC(SHA256): Not supported");
#endif
    return ret;
}

static std::vector<uint8_t> PBKDF2_HMAC_SHA512(const std::string& pass,
                                               const std::vector<uint8_t>& salt,
                                               unsigned int iterationCount) {
    std::vector<uint8_t> ret(cb::crypto::SHA512_DIGEST_SIZE);
#if defined(HAVE_PKCS5_PBKDF2_HMAC)
    auto err = PKCS5_PBKDF2_HMAC(pass.data(), int(pass.size()),
                                 salt.data(), int(salt.size()),
                                 iterationCount,
                                 EVP_sha512(),
                                 cb::crypto::SHA512_DIGEST_SIZE,
                                 ret.data());
     if (err != 1) {
        throw std::runtime_error(
            "cb::crypto::PBKDF2_HMAC(SHA512): PKCS5_PBKDF2_HMAC failed" +
            std::to_string(err));
    }
#else
    throw std::runtime_error(
        "cb::crypto::PBKDF2_HMAC(SHA512): Not supported");
#endif
    return ret;
}

static std::vector<uint8_t> digest_md5(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::MD5_DIGEST_SIZE);
    MD5(data.data(), data.size(), ret.data());
    return ret;
}

static std::vector<uint8_t> digest_sha1(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA1_DIGEST_SIZE);
    SHA1(data.data(), data.size(), ret.data());
    return ret;
}

static std::vector<uint8_t> digest_sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA256_DIGEST_SIZE);
    SHA256(data.data(), data.size(), ret.data());
    return ret;
}

static std::vector<uint8_t> digest_sha512(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ret(cb::crypto::SHA512_DIGEST_SIZE);
    SHA512(data.data(), data.size(), ret.data());
    return ret;
}

struct EVP_CIPHER_CTX_Deleter {
    void operator()(EVP_CIPHER_CTX* ctx) {
        if (ctx != nullptr) {
            EVP_CIPHER_CTX_free(ctx);
        }
    }
};

using unique_EVP_CIPHER_CTX_ptr =
        std::unique_ptr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_Deleter>;

namespace cb {
namespace crypto {

/**
 * Get the OpenSSL Cipher to use for the encryption, and validate
 * the input key and iv sizes
 */
static const EVP_CIPHER* getCipher(const Cipher& cipher,
                                   const std::vector<uint8_t>& key,
                                   const std::vector<uint8_t>& iv) {
    const EVP_CIPHER* cip = nullptr;

    switch (cipher) {
    case cb::crypto::Cipher::AES_256_cbc:
        cip = EVP_aes_256_cbc();
        break;
    }

    if (cip == nullptr) {
        throw std::invalid_argument("cb::crypto::getCipher: Unknown Cipher " +
                                    std::to_string(int(cipher)));
    }

    if (int(key.size()) != EVP_CIPHER_key_length(cip)) {
        throw std::invalid_argument(
                "cb::crypto::getCipher: Cipher requires a key "
                "length of " +
                std::to_string(EVP_CIPHER_key_length(cip)) +
                " provided key with length " + std::to_string(key.size()));
    }

    if (int(iv.size()) != EVP_CIPHER_iv_length(cip)) {
        throw std::invalid_argument(
                "cb::crypto::getCipher: Cipher requires a iv "
                "length of " +
                std::to_string(EVP_CIPHER_iv_length(cip)) +
                " provided iv with length " + std::to_string(iv.size()));
    }

    return cip;
}

std::vector<uint8_t> encrypt(const Cipher& cipher,
                             const std::vector<uint8_t>& key,
                             const std::vector<uint8_t>& iv,
                             const uint8_t* data,
                             size_t length) {
    TRACE_EVENT2("cbcrypto", "encrypt", "cipher", int(cipher), "size", length);
    unique_EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new());

    auto* cip = getCipher(cipher, key, iv);
    if (EVP_EncryptInit_ex(ctx.get(), cip, nullptr, key.data(), iv.data()) !=
        1) {
        throw std::runtime_error(
                "cb::crypto::encrypt: EVP_EncryptInit_ex failed");
    }

    std::vector<uint8_t> ret(length + EVP_CIPHER_block_size(cip));
    int len1 = int(ret.size());

    if (EVP_EncryptUpdate(ctx.get(), ret.data(), &len1, data, int(length)) !=
        1) {
        throw std::runtime_error(
                "cb::crypto::encrypt: EVP_EncryptUpdate failed");
    }

    int len2 = int(ret.size()) - len1;
    if (EVP_EncryptFinal_ex(ctx.get(), ret.data() + len1, &len2) != 1) {
        throw std::runtime_error(
                "cb::crypto::encrypt: EVP_EncryptFinal_ex failed");
    }

    // Resize the destination to the sum of the two length fields
    ret.resize(size_t(len1) + size_t(len2));
    return ret;
}

std::vector<uint8_t> decrypt(const Cipher& cipher,
                             const std::vector<uint8_t>& key,
                             const std::vector<uint8_t>& iv,
                             const uint8_t* data,
                             size_t length) {
    TRACE_EVENT2("cbcrypto", "decrypt", "cipher", int(cipher), "size", length);
    unique_EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new());
    auto* cip = getCipher(cipher, key, iv);

    if (EVP_DecryptInit_ex(ctx.get(), cip, nullptr, key.data(), iv.data()) !=
        1) {
        throw std::runtime_error(
                "cb::crypto::decrypt: EVP_DecryptInit_ex failed");
    }

    std::vector<uint8_t> ret(length);
    int len1 = int(ret.size());

    if (EVP_DecryptUpdate(ctx.get(), ret.data(), &len1, data, int(length)) !=
        1) {
        throw std::runtime_error(
                "cb::crypto::decrypt: EVP_DecryptUpdate failed");
    }

    int len2 = int(length) - len1;
    if (EVP_DecryptFinal_ex(ctx.get(), ret.data() + len1, &len2) != 1) {
        throw std::runtime_error(
                "cb::crypto::decrypt: EVP_DecryptFinal_ex failed");
    }

    // Resize the destination to the sum of the two length fields
    ret.resize(size_t(len1) + size_t(len2));
    return ret;
}

} // namespace crypto
} // namespace cb

#endif

std::vector<uint8_t> cb::crypto::HMAC(const Algorithm algorithm,
                                      const std::vector<uint8_t>& key,
                                      const std::vector<uint8_t>& data) {
    TRACE_EVENT1("cbcrypto", "HMAC", "algorithm", int(algorithm));
    switch (algorithm) {
    case Algorithm::MD5:
        return HMAC_MD5(key, data);
    case Algorithm::SHA1:
        return HMAC_SHA1(key, data);
    case Algorithm::SHA256:
        return HMAC_SHA256(key, data);
    case Algorithm::SHA512:
        return HMAC_SHA512(key, data);
    }

    throw std::invalid_argument("cb::crypto::HMAC: Unknown Algorithm: " +
                                std::to_string((int)algorithm));
}

std::vector<uint8_t> cb::crypto::PBKDF2_HMAC(const Algorithm algorithm,
                                             const std::string& pass,
                                             const std::vector<uint8_t>& salt,
                                             unsigned int iterationCount) {
    TRACE_EVENT2("cbcrypto", "PBKDF2_HMAC", "algorithm", int(algorithm),
                 "iteration", iterationCount);
    switch (algorithm) {
    case Algorithm::MD5:
        throw std::invalid_argument(
            "cb::crypto::PBKDF2_HMAC: Can't use MD5");
    case Algorithm::SHA1:
        return PBKDF2_HMAC_SHA1(pass, salt, iterationCount);
    case Algorithm::SHA256:
        return PBKDF2_HMAC_SHA256(pass, salt, iterationCount);
    case Algorithm::SHA512:
        return PBKDF2_HMAC_SHA512(pass, salt, iterationCount);
    }

    throw std::invalid_argument(
        "cb::crypto::PBKDF2_HMAC: Unknown Algorithm: " +
        std::to_string((int)algorithm));
}

static inline void verifyLegalAlgorithm(const cb::crypto::Algorithm al) {
    switch (al) {
    case cb::crypto::Algorithm::MD5:
    case cb::crypto::Algorithm::SHA1:
    case cb::crypto::Algorithm::SHA256:
    case cb::crypto::Algorithm::SHA512:
        return;
    }
    throw std::invalid_argument(
        "verifyLegalAlgorithm: Unknown Algorithm: " + std::to_string((int)al));
}

bool cb::crypto::isSupported(const Algorithm algorithm) {
    verifyLegalAlgorithm(algorithm);

#if defined(__APPLE__) || defined(_MSC_VER) || defined(HAVE_PKCS5_PBKDF2_HMAC)
    return true;
#elif defined(HAVE_PKCS5_PBKDF2_HMAC_SHA1)
    switch (algorithm) {
    case Algorithm::MD5:
    case Algorithm::SHA1:
        return true;
    default:
        return false;
    }
#else
    return algorithm == Algorithm::MD5;
#endif
}

std::vector<uint8_t> cb::crypto::digest(const Algorithm algorithm,
                                        const std::vector<uint8_t>& data) {
    TRACE_EVENT1("cbcrypto", "digest", "algorithm", int(algorithm));
    switch (algorithm) {
    case Algorithm::MD5:
        return digest_md5(data);
    case Algorithm::SHA1:
        return digest_sha1(data);
    case Algorithm::SHA256:
        return digest_sha256(data);
    case Algorithm::SHA512:
        return digest_sha512(data);
    }

    throw std::invalid_argument(
        "cb::crypto::digest: Unknown Algorithm" +
        std::to_string((int)algorithm));
}

namespace cb {
    namespace crypto {


        /**
         * decode the META information for the encryption bits.
         *
         * @param meta the input json data (not const to avoid all of the const
         *             casts, and this is a private helper function not visible
         *             outside this file
         * @param cipher the cipher to use (out)
         * @param key the key to use (out)
         * @param iv the iv to use (out)
         */
        static void decodeJsonMeta(cJSON* meta,
                                   Cipher& cipher,
                                   std::vector<uint8_t>& key,
                                   std::vector<uint8_t>& iv) {

            auto* obj = cJSON_GetObjectItem(meta, "cipher");
            if (obj == nullptr) {
                throw std::runtime_error(
                    "cb::crypto::decodeJsonMeta: cipher not specified");
            }

            cipher = crypto::to_cipher(obj->valuestring);
            obj = cJSON_GetObjectItem(meta, "key");
            if (obj == nullptr) {
                throw std::runtime_error(
                    "cb::crypto::decodeJsonMeta: key not specified");
            }

            auto str = Couchbase::Base64::decode(obj->valuestring);
            key.resize(str.size());
            memcpy(key.data(), str.data(), key.size());

            obj = cJSON_GetObjectItem(meta, "iv");
            if (obj == nullptr) {
                throw std::runtime_error(
                    "cb::crypto::decodeJsonMeta: iv not specified");
            }

            str = Couchbase::Base64::decode(obj->valuestring);
            iv.resize(str.size());
            memcpy(iv.data(), str.data(), iv.size());
        }
    }
}

std::vector<uint8_t> cb::crypto::encrypt(const Cipher& cipher,
                                         const std::vector<uint8_t>& key,
                                         const std::vector<uint8_t>& iv,
                                         const std::vector<uint8_t>& data) {
    // We only support a single encryption scheme right now.
    // Verify the input parameters (no need of calling the internal library
    // functions in order to fetch these details)
    if (cipher != Cipher::AES_256_cbc) {
        throw std::invalid_argument(
                "cb::crypto::encrypt(): Unsupported cipher");
    }

    if (key.size() != 32) {
        throw std::invalid_argument(
                "cb::crypto::encrypt(): Invalid key size: " +
                std::to_string(key.size()) + " (expected 32)");
    }

    if (iv.size() != 16) {
        throw std::invalid_argument("cb::crypto::encrypt(): Invalid iv size: " +
                                    std::to_string(iv.size()) +
                                    " (expected 16)");
    }

    return encrypt(cipher, key, iv, data.data(), data.size());
}

std::vector<uint8_t> cb::crypto::encrypt(const cJSON* json,
                                         const uint8_t* data,
                                         size_t length) {
    Cipher cipher;
    std::vector<uint8_t> key;
    std::vector<uint8_t> iv;

    decodeJsonMeta(const_cast<cJSON*>(json), cipher, key, iv);
    return encrypt(cipher, key, iv, data, length);
}

std::vector<uint8_t> cb::crypto::decrypt(const Cipher& cipher,
                                         const std::vector<uint8_t>& key,
                                         const std::vector<uint8_t>& iv,
                                         const std::vector<uint8_t>& data) {
    // We only support a single decryption scheme right now.
    // Verify the input parameters (no need of calling the internal library
    // functions in order to fetch these details)
    if (cipher != Cipher::AES_256_cbc) {
        throw std::invalid_argument(
                "cb::crypto::decrypt(): Unsupported cipher");
    }

    if (key.size() != 32) {
        throw std::invalid_argument(
                "cb::crypto::decrypt(): Invalid key size: " +
                std::to_string(key.size()) + " (expected 32)");
    }

    if (iv.size() != 16) {
        throw std::invalid_argument("cb::crypto::decrypt(): Invalid iv size: " +
                                    std::to_string(iv.size()) +
                                    " (expected 16)");
    }

    return decrypt(cipher, key, iv, data.data(), data.size());
}

cb::crypto::Cipher cb::crypto::to_cipher(const std::string& str) {

    if (str == "AES_256_cbc") {
        return Cipher::AES_256_cbc;
    }

    throw std::invalid_argument("to_cipher: Unknown cipher: " + str);
}

std::vector<uint8_t> cb::crypto::decrypt(const cJSON* json,
                                         const uint8_t* data,
                                         size_t length) {
    Cipher cipher;
    std::vector<uint8_t> key;
    std::vector<uint8_t> iv;

    decodeJsonMeta(const_cast<cJSON*>(json), cipher, key, iv);
    return decrypt(cipher, key, iv, data, length);
}

std::string cb::crypto::digest(const Algorithm algorithm,
                               const std::string& passwd) {
    std::vector<uint8_t> data(passwd.size());
    memcpy(data.data(), passwd.data(), passwd.size());
    auto digest = cb::crypto::digest(algorithm, data);
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (const auto& c : digest) {
        ss << std::setw(2) << uint32_t(c);
    }

    return ss.str();
}
