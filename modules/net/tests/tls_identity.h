// A self-signed certificate and its key, made with OpenSSL at test time: a TLS server to test against without
// a file in the repository that expires.
#pragma once

#include <memory>
#include <string>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace test {
    struct Identity { std::string certificatePem, privateKeyPem; };

    inline Identity MakeSelfSignedIdentity(const std::string& name) {
        EVP_PKEY* key = EVP_RSA_gen(2048);
        X509* cert = X509_new();
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
        X509_gmtime_adj(X509_getm_notAfter(cert), 3600L * 24);
        X509_set_pubkey(cert, key);
        X509_NAME* subject = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(name.c_str()), -1, -1, 0);
        X509_set_issuer_name(cert, subject);
        X509V3_CTX ctx;
        X509V3_set_ctx_nodb(&ctx);
        X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
        for (const auto& [nid, value] : {std::pair{NID_subject_alt_name, std::string("DNS:") + name}, std::pair{NID_basic_constraints, std::string("critical,CA:TRUE")}}) {
            X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value.c_str());
            X509_add_ext(cert, ext, -1);
            X509_EXTENSION_free(ext);
        }
        X509_sign(cert, key, EVP_sha256());

        auto toPem = [](auto write) {
            BIO* bio = BIO_new(BIO_s_mem());
            write(bio);
            char* data = nullptr;
            const long size = BIO_get_mem_data(bio, &data);
            std::string pem(data, static_cast<std::size_t>(size));
            BIO_free(bio);
            return pem;
        };
        Identity identity{toPem([&](BIO* b) { PEM_write_bio_X509(b, cert); }),
                          toPem([&](BIO* b) { PEM_write_bio_PrivateKey(b, key, nullptr, nullptr, 0, nullptr, nullptr); })};
        X509_free(cert);
        EVP_PKEY_free(key);
        return identity;
    }
}
