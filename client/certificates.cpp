#include "session.h"
#include <wincrypt.h>
#include <gnutls/gnutls.h>
#include <gnutls/system-keys.h>
#include <cstddef>

namespace bulijie {
namespace {
HCERTSTORE certificate_store(const std::string &raw, DWORD &error) {
    if (raw.empty() || raw.find("PRIVATE KEY") != std::string::npos) {
        error = ERROR_INVALID_DATA;
        return nullptr;
    }
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    if (!store) {
        error = GetLastError();
        return nullptr;
    }
    const std::string begin = "-----BEGIN CERTIFICATE-----", end = "-----END CERTIFICATE-----";
    size_t position = 0;
    unsigned count = 0;
    while ((position = raw.find(begin, position)) != std::string::npos) {
        auto close = raw.find(end, position + begin.size());
        if (close == std::string::npos || ++count > 64) {
            error = ERROR_INVALID_DATA;
            CertCloseStore(store, 0);
            return nullptr;
        }
        DWORD bytes = 0;
        const char *block = raw.data() + position;
        DWORD size = static_cast<DWORD>(close + end.size() - position);
        if (!CryptStringToBinaryA(block, size, CRYPT_STRING_BASE64HEADER, nullptr, &bytes, nullptr,
                                  nullptr)) {
            error = GetLastError();
            CertCloseStore(store, 0);
            return nullptr;
        }
        std::vector<BYTE> der(bytes);
        if (!CryptStringToBinaryA(block, size, CRYPT_STRING_BASE64HEADER, der.data(), &bytes, nullptr,
                                  nullptr) ||
            !CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, der.data(), bytes,
                                              CERT_STORE_ADD_USE_EXISTING, nullptr)) {
            error = GetLastError();
            CertCloseStore(store, 0);
            return nullptr;
        }
        position = close + end.size();
    }
    if (!count && !CertAddEncodedCertificateToStore(
                      store, X509_ASN_ENCODING, reinterpret_cast<const BYTE *>(raw.data()),
                      static_cast<DWORD>(raw.size()), CERT_STORE_ADD_USE_EXISTING, nullptr)) {
        error = GetLastError();
        CertCloseStore(store, 0);
        return nullptr;
    }
    return store;
}
} // namespace
bool certificate_file_info(const std::filesystem::path &path, std::wstring &fingerprint,
                           std::wstring &error) {
    std::string raw;
    if (!read_file(path, raw, error, 1024 * 1024))
        return false;
    if (!valid_public_certificates(raw, error))
        return false;
    DWORD code = 0;
    HCERTSTORE store = certificate_store(raw, code);
    if (!store) {
        error = system_error(code);
        return false;
    }
    PCCERT_CONTEXT cert = CertEnumCertificatesInStore(store, nullptr);
    bool ok = false;
    if (cert) {
        BYTE hash[32]{};
        DWORD size = sizeof(hash);
        if (CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, cert->pbCertEncoded,
                                  cert->cbCertEncoded, hash, &size)) {
            fingerprint = L"SHA256 ";
            const wchar_t alphabet[] = L"0123456789ABCDEF";
            for (DWORD i = 0; i < size; ++i) {
                if (i)
                    fingerprint += L':';
                fingerprint += alphabet[hash[i] >> 4];
                fingerprint += alphabet[hash[i] & 15];
            }
            ok = true;
        }
        CertFreeCertificateContext(cert);
    }
    CertCloseStore(store, 0);
    return ok;
}
bool peer_certificate_in_date(openconnect_info *vpn, DWORD &error) {
    error = ERROR_INVALID_DATA;
    unsigned char *der = nullptr;
    int size = openconnect_get_peer_cert_DER(vpn, &der);
    if (size <= 0 || !der)
        return false;
    PCCERT_CONTEXT certificate =
        CertCreateCertificateContext(X509_ASN_ENCODING, der, static_cast<DWORD>(size));
    openconnect_free_cert_info(vpn, der);
    if (!certificate) {
        error = GetLastError();
        return false;
    }
    bool valid = CertVerifyTimeValidity(nullptr, certificate->pCertInfo) == 0;
    CertFreeCertificateContext(certificate);
    error = valid ? ERROR_SUCCESS : CERT_E_EXPIRED;
    return valid;
}
bool verify_peer_with_windows(openconnect_info *vpn, const std::wstring &hostname,
                              const std::wstring &ca_file, DWORD &error) {
    error = CERT_E_UNTRUSTEDROOT;
    unsigned char *der = nullptr;
    int length = openconnect_get_peer_cert_DER(vpn, &der);
    if (length <= 0 || !der)
        return false;
    PCCERT_CONTEXT leaf = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der,
                                                       static_cast<DWORD>(length));
    openconnect_free_cert_info(vpn, der);
    if (!leaf) {
        error = GetLastError();
        return false;
    }
    HCERTSTORE peers = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    HCERTSTORE roots = nullptr;
    HCERTCHAINENGINE engine = nullptr;
    bool valid = peers != nullptr, accepted = false;
    oc_cert *chain_info = nullptr;
    int count = openconnect_get_peer_cert_chain(vpn, &chain_info);
    valid = valid && count >= 0 && count <= 32;
    if (valid)
        for (int i = 0; i < count; ++i) {
            if (chain_info[i].der_len <= 0 ||
                !CertAddEncodedCertificateToStore(peers, X509_ASN_ENCODING, chain_info[i].der_data,
                                                  static_cast<DWORD>(chain_info[i].der_len),
                                                  CERT_STORE_ADD_USE_EXISTING, nullptr)) {
                valid = false;
                break;
            }
        }
    if (chain_info)
        openconnect_free_peer_cert_chain(vpn, chain_info);
    if (valid && !ca_file.empty()) {
        std::string raw;
        std::wstring message;
        if (!read_file(ca_file, raw, message, 1024 * 1024))
            valid = false;
        else
            roots = certificate_store(raw, error);
        if (!roots)
            valid = false;
        else {
            CERT_CHAIN_ENGINE_CONFIG config{};
            config.cbSize = sizeof(config);
            config.hExclusiveRoot = roots;
            config.dwFlags = CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL;
            if (!CertCreateCertificateChainEngine(&config, &engine)) {
                error = GetLastError();
                valid = false;
            }
        }
    }
    if (valid) {
        CERT_CHAIN_PARA parameters{};
        parameters.cbSize = static_cast<DWORD>(offsetof(CERT_CHAIN_PARA, pStrongSignPara));
        LPSTR usage = const_cast<char *>(szOID_PKIX_KP_SERVER_AUTH);
        parameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        parameters.RequestedUsage.Usage.cUsageIdentifier = 1;
        parameters.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
        parameters.dwUrlRetrievalTimeout = 5000;
        PCCERT_CHAIN_CONTEXT chain = nullptr;
        DWORD flags = ca_file.empty() ? CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT
                                      : CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL;
        if (CertGetCertificateChain(engine, leaf, nullptr, peers, &parameters, flags, nullptr, &chain)) {
            SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
            ssl.cbSize = sizeof(ssl);
            ssl.dwAuthType = AUTHTYPE_SERVER;
            ssl.pwszServerName = const_cast<wchar_t *>(hostname.c_str());
            CERT_CHAIN_POLICY_PARA policy{};
            policy.cbSize = sizeof(policy);
            policy.pvExtraPolicyPara = &ssl;
            CERT_CHAIN_POLICY_STATUS status{};
            status.cbSize = sizeof(status);
            if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status)) {
                error = status.dwError;
                accepted = error == ERROR_SUCCESS;
            } else
                error = GetLastError();
            CertFreeCertificateChain(chain);
        } else
            error = GetLastError();
    }
    if (engine)
        CertFreeCertificateChainEngine(engine);
    if (roots)
        CertCloseStore(roots, 0);
    if (peers)
        CertCloseStore(peers, 0);
    CertFreeCertificateContext(leaf);
    return accepted;
}
std::vector<SystemCertificate> system_certificates() {
    std::vector<SystemCertificate> result;
    gnutls_system_key_iter_t iterator = nullptr;
    while (result.size() < 512) {
        char *certificate = nullptr, *key = nullptr, *label = nullptr;
        int status = gnutls_system_key_iter_get_info(&iterator, GNUTLS_CRT_X509, &certificate, &key, &label,
                                                     nullptr, 0);
        if (status < 0)
            break;
        if (certificate && key)
            result.push_back({wide(label ? label : certificate), wide(certificate), wide(key)});
        if (certificate)
            gnutls_free(certificate);
        if (key)
            gnutls_free(key);
        if (label)
            gnutls_free(label);
    }
    gnutls_system_key_iter_deinit(iterator);
    return result;
}
std::vector<Protocol> supported_protocols() {
    oc_vpn_proto *protocols = nullptr;
    int count = openconnect_get_supported_protocols(&protocols);
    std::vector<Protocol> result;
    if (count > 0 && count < 256)
        for (int i = 0; i < count; ++i)
            if (protocols[i].name)
                result.push_back(
                    {protocols[i].name,
                     wide(protocols[i].pretty_name ? protocols[i].pretty_name : protocols[i].name),
                     wide(protocols[i].description ? protocols[i].description : "")});
    if (protocols)
        openconnect_free_supported_protocols(protocols);
    return result;
}
} // namespace bulijie
