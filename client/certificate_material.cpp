#include "platform.h"
#include "public_ca.h"
#include <wincrypt.h>

namespace vpn {
bool valid_public_ca(const std::string &raw, std::wstring &error) {
    try {
        for (const auto &block : lansyra::public_ca_blocks(raw))
            if (!valid_public_certificates(block, error)) return false;
        return true;
    } catch (...) {
        error = L"公共 CA 必须是 64 KiB 以内的 PEM 证书。 / Public CA must be PEM certificates within 64 KiB.";
        return false;
    }
}
bool valid_public_certificates(const std::string &raw, std::wstring &error) {
    if (raw.empty() || raw.size() > 1024 * 1024 || raw.find("PRIVATE KEY") != std::string::npos) {
        error = L"CA 文件必须只包含公共证书。 / A CA file must contain only public certificates.";
        return false;
    }
    auto valid_der = [](const BYTE *data, DWORD size) {
        PCCERT_CONTEXT certificate = CertCreateCertificateContext(X509_ASN_ENCODING, data, size);
        if (!certificate)
            return false;
        CertFreeCertificateContext(certificate);
        return true;
    };
    const std::string begin = "-----BEGIN CERTIFICATE-----", end = "-----END CERTIFICATE-----";
    size_t position = 0;
    unsigned count = 0;
    while ((position = raw.find(begin, position)) != std::string::npos) {
        auto close = raw.find(end, position + begin.size());
        if (close == std::string::npos || ++count > 64)
            break;
        DWORD size = 0;
        const char *block = raw.data() + position;
        DWORD length = static_cast<DWORD>(close + end.size() - position);
        if (!CryptStringToBinaryA(block, length, CRYPT_STRING_BASE64HEADER, nullptr, &size, nullptr, nullptr))
            break;
        std::vector<BYTE> der(size);
        if (!CryptStringToBinaryA(block, length, CRYPT_STRING_BASE64HEADER, der.data(), &size, nullptr,
                                  nullptr) ||
            !valid_der(der.data(), size))
            break;
        position = close + end.size();
        if (raw.find(begin, position) == std::string::npos)
            return true;
    }
    if (!count && raw.find("-----BEGIN") == std::string::npos &&
        valid_der(reinterpret_cast<const BYTE *>(raw.data()), static_cast<DWORD>(raw.size())))
        return true;
    error = L"CA 证书内容无效。 / Invalid CA certificate contents.";
    return false;
}
} // namespace vpn
