#include "fw/CertUtil.hpp"
#include "fw/Logger.hpp"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/asn1.h>

#include <cstdio>
#include <cstring>
#ifdef _WIN32
#else
  #include <sys/stat.h>
#endif

namespace alkaidlab {
namespace fw {

bool CertUtil::generateSelfSigned(const std::string& certPath,
                                  const std::string& keyPath,
                                  int days,
                                  const std::string& cn) {
    EVP_PKEY* pkey = nullptr;
    X509* x509 = nullptr;
    FILE* fp = nullptr;
    bool ok = false;

    /* 生成 RSA-2048 密钥对 */
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) {
        goto cleanup;
    }
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        goto cleanup;
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
        goto cleanup;
    }
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        goto cleanup;
    }
    EVP_PKEY_CTX_free(ctx);
    ctx = nullptr;

    /* 创建 X509 证书 */
    x509 = X509_new();
    if (!x509) {
        goto cleanup;
    }

    /* 设置序列号和版本 */
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_set_version(x509, 2); /* V3 */

    /* 有效期 */
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), static_cast<long>(days) * 86400);

    /* 设置公钥 */
    X509_set_pubkey(x509, pkey);

    /* Subject = Issuer（自签名） */
    {
        X509_NAME* name = X509_get_subject_name(x509);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
        X509_NAME_add_entry_by_txt(name, "O", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("AlkaidLab"), -1, -1, 0);
        X509_set_issuer_name(x509, name);
    }

    /* 添加 SAN (Subject Alternative Name) 扩展 */
    {
        X509V3_CTX v3ctx;
        X509V3_set_ctx_nodb(&v3ctx);
        X509V3_set_ctx(&v3ctx, x509, x509, nullptr, nullptr, 0);

        /* 组装 SAN：localhost + 回环 IP，如果 CN 像域名则一并加入 */
        std::string san = "DNS:localhost,IP:127.0.0.1,IP:::1";
        if (!cn.empty() && cn != "localhost") {
            bool hasDot = (cn.find('.') != std::string::npos);
            bool hasColon = (cn.find(':') != std::string::npos);
            if (hasColon) {
                /* IPv6 地址 */
                san = "IP:" + cn + "," + san;
            } else if (hasDot) {
                /* 域名或 IPv4 */
                /* 简单判断：全是数字和点 → IP，否则域名 */
                bool isIp = true;
                for (std::string::size_type i = 0; i < cn.size(); ++i) {
                    char ch = cn[i];
                    if (ch != '.' && (ch < '0' || ch > '9')) { isIp = false; break; }
                }
                san = (isIp ? "IP:" : "DNS:") + cn + "," + san;
            }
        }

        X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &v3ctx, NID_subject_alt_name,
                                                   const_cast<char*>(san.c_str()));
        if (ext) {
            X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
        }
    }

    /* 签名 */
    if (X509_sign(x509, pkey, EVP_sha256()) == 0) {
        goto cleanup;
    }

    /* 写入私钥文件（权限 0600） */
    fp = std::fopen(keyPath.c_str(), "wb");
    if (!fp) {
        LOG_ERROR("[cert] failed to open key file for writing: " + keyPath);
        goto cleanup;
    }
    if (!PEM_write_PrivateKey(fp, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
        std::fclose(fp);
        goto cleanup;
    }
    std::fclose(fp);
#ifndef _WIN32
    chmod(keyPath.c_str(), 0600);
#endif

    /* 写入证书文件 */
    fp = std::fopen(certPath.c_str(), "wb");
    if (!fp) {
        LOG_ERROR("[cert] failed to open cert file for writing: " + certPath);
        goto cleanup;
    }
    if (!PEM_write_X509(fp, x509)) {
        std::fclose(fp);
        goto cleanup;
    }
    std::fclose(fp);
    fp = nullptr;

    ok = true;
    LOG_INFO("[cert] self-signed certificate generated: " + certPath + " + " + keyPath + " (valid " + std::to_string(days) + " days)");

cleanup:
    if (!ok) {
        unsigned long err = ERR_get_error();
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        LOG_ERROR("[cert] self-signed certificate generation failed: " + std::string(buf));
    }
    if (ctx) {
        EVP_PKEY_CTX_free(ctx);
    }
    if (x509) {
        X509_free(x509);
    }
    if (pkey) {
        EVP_PKEY_free(pkey);
    }
    return ok;
}

/* ---------- ASN1_TIME → ISO 8601 字符串 ---------- */
static std::string asn1TimeToISO(const ASN1_TIME* t) {
    if (!t) {
        return "";
    }
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return "";
    }
    ASN1_TIME_print(bio, t);          /* "Mon DD HH:MM:SS YYYY GMT" */
    char buf[64];
    int len = BIO_read(bio, buf, sizeof(buf) - 1);
    BIO_free(bio);
    if (len <= 0) {
        return "";
    }
    buf[len] = '\0';
    return std::string(buf);
}

bool CertUtil::readCertInfo(const std::string& certPath, CertInfo& info) {
    FILE* fp = std::fopen(certPath.c_str(), "rb");
    if (!fp) {
        return false;
    }
    X509* x509 = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    std::fclose(fp);
    if (!x509) {
        return false;
    }

    /* Subject CN */
    X509_NAME* subj = X509_get_subject_name(x509);
    if (subj) {
        char cn[256] = {0};
        X509_NAME_get_text_by_NID(subj, NID_commonName, cn, sizeof(cn));
        info.subject = cn;
    }

    /* 自签名判断：Issuer == Subject */
    X509_NAME* issuer = X509_get_issuer_name(x509);
    info.selfSigned = (issuer && subj && X509_NAME_cmp(issuer, subj) == 0);

    /* 有效期 */
    info.notBefore = asn1TimeToISO(X509_get0_notBefore(x509));
    info.notAfter  = asn1TimeToISO(X509_get0_notAfter(x509));

    /* SAN (Subject Alternative Name) DNS 列表 */
    info.domains.clear();
    GENERAL_NAMES* sans = static_cast<GENERAL_NAMES*>(
        X509_get_ext_d2i(x509, NID_subject_alt_name, nullptr, nullptr));
    if (sans) {
        int count = sk_GENERAL_NAME_num(sans);
        for (int i = 0; i < count; ++i) {
            GENERAL_NAME* entry = sk_GENERAL_NAME_value(sans, i);
            if (entry->type == GEN_DNS) {
                const char* dns = reinterpret_cast<const char*>(
                    ASN1_STRING_get0_data(entry->d.dNSName));
                if (dns) {
                    info.domains.push_back(dns);
                }
            }
        }
        GENERAL_NAMES_free(sans);
    }

    X509_free(x509);
    return true;
}

} // namespace fw
} // namespace alkaidlab
