#pragma once
// TLS for the RPC port, using Windows Schannel (no third-party library).
//
// Session is transport-agnostic: the caller moves bytes between it and its own
// socket. Feed() takes whatever arrived from the peer and returns both the bytes
// to send back (handshake replies) and any decrypted application data; Encrypt()
// turns application data into records for the wire. The service drives it from
// its event-driven sockets (RpcProto), the UI from a plain socket (RPCConnection).
//
// Clients do not trust certificate authorities here: the UI pins the server's
// certificate by its SHA-256 fingerprint (Fingerprint()), which the server's
// administrator reads with "rpmgui --show-management-key". So client credentials
// use manual validation, and the caller compares PeerFingerprint() itself.
//
// Protocol versions follow Windows: every version the system allows except those
// below TLS 1.2, so TLS 1.3 where Windows has it (Windows 11, Server 2022) and
// whatever later Windows adds, while an admin who disables a version in the
// registry disables it for RPM too. That needs SCH_CREDENTIALS (Windows 10 1809
// and later); older Windows falls back to SCHANNEL_CRED with TLS 1.2 only.
// Schannel picks the cipher suites.
//
// Header-only, like RpmSecrets.h, so the service and the UI share it.
//
// rpmcli: copied unchanged (apart from this note) from shared/RpmTls.h in the RPM
// source, which is the original; take fixes from there.

#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
// SCH_CREDENTIALS is declared only with SCHANNEL_USE_BLACKLISTS, which needs
// UNICODE_STRING (subauth.h) first.
#ifndef SCHANNEL_USE_BLACKLISTS
#define SCHANNEL_USE_BLACKLISTS
#endif
#include <windows.h>
#include <subauth.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <security.h>
#include <schannel.h>
#ifndef SCH_CRED_MAX_SUPPORTED_PARAMETERS
#error "schannel.h was included before RpmTls.h without SCHANNEL_USE_BLACKLISTS; include RpmTls.h first"
#endif
#include <string>
#include <memory>
#include <algorithm>
#include <cstdio>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

namespace RpmTls {

inline std::string StatusText(const char* what, SECURITY_STATUS status)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "%s failed (0x%08lX)", what, (unsigned long)status);
    std::string text = buf;
    char* msg = nullptr;
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, (DWORD)status, 0, (LPSTR)&msg, 0, nullptr) && msg) {
        std::string m = msg;
        LocalFree(msg);
        while (!m.empty() && (m.back() == '\r' || m.back() == '\n' || m.back() == '.'))
            m.pop_back();
        text += ": " + m;
    }
    return text;
}

// SHA-256 of the certificate as "AB:CD:...", the form openssl and browsers show.
inline std::string Fingerprint(PCCERT_CONTEXT cert)
{
    if (!cert)
        return std::string();
    BYTE hash[32];
    DWORD size = sizeof(hash);
    if (!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, cert->pbCertEncoded, cert->cbCertEncoded, hash, &size)
        || size != sizeof(hash))
        return std::string();
    static const char hex[] = "0123456789ABCDEF";
    std::string text;
    for (DWORD i = 0; i != size; ++i) {
        if (i)
            text.push_back(':');
        text.push_back(hex[hash[i] >> 4]);
        text.push_back(hex[hash[i] & 15]);
    }
    return text;
}

// The 64 hex digits of a fingerprint in upper case, whatever separators or case
// it was typed or pasted with; "" when it is not a SHA-256 fingerprint.
inline std::string NormalizeFingerprint(const std::string& text)
{
    std::string hex;
    for (char c : text) {
        if (c >= '0' && c <= '9')
            hex.push_back(c);
        else if (c >= 'a' && c <= 'f')
            hex.push_back((char)(c - 'a' + 'A'));
        else if (c >= 'A' && c <= 'F')
            hex.push_back(c);
        else if (c == ':' || c == ' ' || c == '-' || c == '\t')
            continue;
        else
            return std::string();
    }
    return hex.size() == 64 ? hex : std::string();
}

inline bool SameFingerprint(const std::string& a, const std::string& b)
{
    std::string x = NormalizeFingerprint(a), y = NormalizeFingerprint(b);
    return !x.empty() && x == y;
}

// An Schannel credentials handle: the server's certificate, or a client with none.
class Credentials {
public:
    Credentials() { SecInvalidateHandle(&handle_); }
    ~Credentials() { if (SecIsValidHandle(&handle_)) FreeCredentialsHandle(&handle_); }
    Credentials(const Credentials&) = delete;
    Credentials& operator=(const Credentials&) = delete;

    bool AcquireServer(PCCERT_CONTEXT cert, std::string* err = nullptr)
    {
        return acquire(SECPKG_CRED_INBOUND, &cert, SCH_USE_STRONG_CRYPTO,
                       SP_PROT_SSL2_SERVER | SP_PROT_SSL3_SERVER | SP_PROT_TLS1_0_SERVER | SP_PROT_TLS1_1_SERVER,
                       SP_PROT_TLS1_2_SERVER, err);
    }

    bool AcquireClient(std::string* err = nullptr)
    {
        // The caller checks the server's certificate against a pinned fingerprint.
        return acquire(SECPKG_CRED_OUTBOUND, nullptr,
                       SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO,
                       SP_PROT_SSL2_CLIENT | SP_PROT_SSL3_CLIENT | SP_PROT_TLS1_0_CLIENT | SP_PROT_TLS1_1_CLIENT,
                       SP_PROT_TLS1_2_CLIENT, err);
    }

    // Which structure the credentials were acquired with ("SCH_CREDENTIALS", or
    // "SCHANNEL_CRED" on Windows without it), for diagnostics.
    const char* Kind() const { return kind_; }

    CredHandle* get() { return &handle_; }
    bool valid() const { return SecIsValidHandle(&handle_); }

private:
    CredHandle handle_;
    const char* kind_ = "";

    // SCH_CREDENTIALS with the old versions disabled (the system's defaults apply
    // to the rest); if Windows rejects that structure, SCHANNEL_CRED with only
    // the fallback version enabled.
    bool acquire(unsigned long use, PCCERT_CONTEXT* cert, DWORD flags, DWORD disabled, DWORD fallback,
                 std::string* err)
    {
        TLS_PARAMETERS tp = {};
        tp.grbitDisabledProtocols = disabled;
        SCH_CREDENTIALS sc = {};
        sc.dwVersion = SCH_CREDENTIALS_VERSION;
        sc.cCreds = cert ? 1 : 0;
        sc.paCred = cert;
        sc.dwFlags = flags;
        sc.cTlsParameters = 1;
        sc.pTlsParameters = &tp;
        TimeStamp expiry;
        SECURITY_STATUS status = AcquireCredentialsHandleW(nullptr, const_cast<LPWSTR>(UNISP_NAME_W), use,
                                                           nullptr, &sc, nullptr, nullptr, &handle_, &expiry);
        if (status == SEC_E_OK) {
            kind_ = "SCH_CREDENTIALS";
            return true;
        }

        SCHANNEL_CRED old = {};
        old.dwVersion = SCHANNEL_CRED_VERSION;
        old.cCreds = cert ? 1 : 0;
        old.paCred = cert;
        old.grbitEnabledProtocols = fallback;
        old.dwFlags = flags;
        status = AcquireCredentialsHandleW(nullptr, const_cast<LPWSTR>(UNISP_NAME_W), use,
                                           nullptr, &old, nullptr, nullptr, &handle_, &expiry);
        if (status != SEC_E_OK) {
            SecInvalidateHandle(&handle_);
            if (err)
                *err = StatusText("AcquireCredentialsHandle", status);
            return false;
        }
        kind_ = "SCHANNEL_CRED";
        return true;
    }
};

class Session {
public:
    enum class Role { Server, Client };

    // target is the server's host name (clients only; sent as SNI).
    Session(std::shared_ptr<Credentials> creds, Role role, const std::wstring& target = std::wstring())
        : creds_(std::move(creds)), role_(role), target_(target)
    {
        SecInvalidateHandle(&ctx_);
    }
    ~Session() { if (SecIsValidHandle(&ctx_)) DeleteSecurityContext(&ctx_); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Client: the opening handshake message, to send before anything else.
    bool Start(std::string& out)
    {
        std::string plain;
        return handshake(out, plain);
    }

    // Bytes received from the peer. out receives what must be sent back (handshake
    // messages); plain receives decrypted application data. False on a fatal error.
    bool Feed(const char* data, size_t size, std::string& out, std::string& plain)
    {
        if (!error_.empty())
            return false;
        in_.append(data, size);
        if (!open_)
            return handshake(out, plain);
        return decrypt(out, plain);
    }

    // Application data as TLS records. Only once Open().
    bool Encrypt(const std::string& plain, std::string& out)
    {
        if (!open_ || !error_.empty()) {
            if (error_.empty())
                error_ = "TLS: encrypt before the handshake finished";
            return false;
        }
        size_t pos = 0;
        while (pos < plain.size()) {
            size_t len = (std::min)(plain.size() - pos, (size_t)sizes_.cbMaximumMessage);
            std::string record(sizes_.cbHeader + len + sizes_.cbTrailer, '\0');
            memcpy(&record[sizes_.cbHeader], plain.data() + pos, len);
            SecBuffer b[4] = {
                { sizes_.cbHeader,  SECBUFFER_STREAM_HEADER,  &record[0] },
                { (ULONG)len,       SECBUFFER_DATA,           &record[sizes_.cbHeader] },
                { sizes_.cbTrailer, SECBUFFER_STREAM_TRAILER, &record[sizes_.cbHeader + len] },
                { 0,                SECBUFFER_EMPTY,          nullptr },
            };
            SecBufferDesc desc = { SECBUFFER_VERSION, 4, b };
            SECURITY_STATUS status = EncryptMessage(&ctx_, 0, &desc, 0);
            if (status != SEC_E_OK) {
                error_ = StatusText("EncryptMessage", status);
                return false;
            }
            out.append(record.data(), b[0].cbBuffer + b[1].cbBuffer + b[2].cbBuffer);
            pos += len;
        }
        return true;
    }

    // A close_notify alert for the peer, before closing the socket.
    void Shutdown(std::string& out)
    {
        if (!SecIsValidHandle(&ctx_) || !open_)
            return;
        DWORD type = SCHANNEL_SHUTDOWN;
        SecBuffer tb = { sizeof(type), SECBUFFER_TOKEN, &type };
        SecBufferDesc td = { SECBUFFER_VERSION, 1, &tb };
        if (ApplyControlToken(&ctx_, &td) != SEC_E_OK)
            return;
        SecBuffer ob = { 0, SECBUFFER_TOKEN, nullptr };
        SecBufferDesc od = { SECBUFFER_VERSION, 1, &ob };
        ULONG attrs = 0;
        TimeStamp expiry;
        if (role_ == Role::Server)
            AcceptSecurityContext(creds_->get(), &ctx_, nullptr, serverFlags(), 0, nullptr, &od, &attrs, &expiry);
        else
            InitializeSecurityContextW(creds_->get(), &ctx_, nullptr, clientFlags(), 0, 0, nullptr, 0, &ctx_, &od, &attrs, &expiry);
        if (ob.pvBuffer) {
            out.append((const char*)ob.pvBuffer, ob.cbBuffer);
            FreeContextBuffer(ob.pvBuffer);
        }
        open_ = false;
    }

    // The peer's certificate fingerprint (see Fingerprint()); "" before the handshake.
    std::string PeerFingerprint()
    {
        PCCERT_CONTEXT cert = nullptr;
        if (!SecIsValidHandle(&ctx_)
            || QueryContextAttributesW(&ctx_, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert) != SEC_E_OK || !cert)
            return std::string();
        std::string fp = Fingerprint(cert);
        CertFreeCertificateContext(cert);
        return fp;
    }

    // The negotiated protocol, such as "TLS 1.3"; "" before the handshake.
    std::string Protocol()
    {
        SecPkgContext_ConnectionInfo info = {};
        if (!SecIsValidHandle(&ctx_)
            || QueryContextAttributesW(&ctx_, SECPKG_ATTR_CONNECTION_INFO, &info) != SEC_E_OK)
            return std::string();
        if (info.dwProtocol & SP_PROT_TLS1_3)
            return "TLS 1.3";
        if (info.dwProtocol & SP_PROT_TLS1_2)
            return "TLS 1.2";
        char buf[32];
        snprintf(buf, sizeof(buf), "protocol 0x%lx", info.dwProtocol);
        return buf;
    }

    // The negotiated cipher suite, such as "TLS_AES_256_GCM_SHA384"; "" if unknown.
    std::string Cipher()
    {
        SecPkgContext_CipherInfo info = {};
        info.dwVersion = SECPKGCONTEXT_CIPHERINFO_V1;
        if (!SecIsValidHandle(&ctx_)
            || QueryContextAttributesW(&ctx_, SECPKG_ATTR_CIPHER_INFO, &info) != SEC_E_OK)
            return std::string();
        std::string name;
        for (const wchar_t* p = info.szCipherSuite; *p && p < info.szCipherSuite + SZ_ALG_MAX_SIZE; ++p)
            name.push_back(static_cast<char>(*p));      // ASCII names
        return name;
    }

    bool Open() const { return open_; }
    bool PeerClosed() const { return peerClosed_; }
    const std::string& Error() const { return error_; }

private:
    std::shared_ptr<Credentials> creds_;
    Role role_;
    std::wstring target_;
    CtxtHandle ctx_;
    bool open_ = false, peerClosed_ = false;
    std::string in_, error_;
    SecPkgContext_StreamSizes sizes_ = {};

    static ULONG serverFlags()
    {
        return ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY
             | ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM;
    }
    static ULONG clientFlags()
    {
        return ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY
             | ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM
             | ISC_REQ_MANUAL_CRED_VALIDATION;
    }

    // Runs the handshake as far as the bytes in in_ allow. Also used when the peer
    // asks to renegotiate (DecryptMessage returns SEC_I_RENEGOTIATE).
    bool handshake(std::string& out, std::string& plain)
    {
        bool retried = false;
        for (;;) {
            SecBuffer ib[2] = {
                { (ULONG)in_.size(), SECBUFFER_TOKEN, in_.empty() ? nullptr : &in_[0] },
                { 0,                 SECBUFFER_EMPTY, nullptr },
            };
            SecBufferDesc id = { SECBUFFER_VERSION, 2, ib };
            SecBuffer ob[2] = {
                { 0, SECBUFFER_TOKEN, nullptr },
                { 0, SECBUFFER_ALERT, nullptr },
            };
            SecBufferDesc od = { SECBUFFER_VERSION, 2, ob };
            ULONG attrs = 0;
            TimeStamp expiry;
            const bool first = !SecIsValidHandle(&ctx_);
            SECURITY_STATUS status;
            if (role_ == Role::Server) {
                if (in_.empty())
                    return true;            // nothing from the client yet
                status = AcceptSecurityContext(creds_->get(), first ? nullptr : &ctx_, &id, serverFlags(), 0,
                                               first ? &ctx_ : nullptr, &od, &attrs, &expiry);
            }
            else {
                if (!first && in_.empty())
                    return true;            // waiting for the server
                status = InitializeSecurityContextW(creds_->get(), first ? nullptr : &ctx_,
                                                    target_.empty() ? nullptr : const_cast<wchar_t*>(target_.c_str()),
                                                    clientFlags(), 0, 0, first ? nullptr : &id, 0,
                                                    &ctx_, &od, &attrs, &expiry);
            }

            // Send whatever Schannel produced, an alert included, before judging the status.
            for (SecBuffer& b : ob) {
                if (b.pvBuffer) {
                    if (b.cbBuffer)
                        out.append((const char*)b.pvBuffer, b.cbBuffer);
                    FreeContextBuffer(b.pvBuffer);
                }
            }

            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                if (first)
                    SecInvalidateHandle(&ctx_);     // start over once the record is whole
                return true;                // keep in_ and wait for the rest of the record
            }

            if (status == SEC_I_INCOMPLETE_CREDENTIALS && !retried) {
                // The server asked for a client certificate; carry on without one.
                retried = true;
                continue;
            }

            if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED) {
                if (first)
                    SecInvalidateHandle(&ctx_);     // no context was created
                error_ = StatusText(role_ == Role::Server ? "TLS handshake (AcceptSecurityContext)"
                                                          : "TLS handshake (InitializeSecurityContext)", status);
                return false;
            }

            // Keep only what Schannel did not consume.
            if (ib[1].BufferType == SECBUFFER_EXTRA && ib[1].cbBuffer && ib[1].cbBuffer <= in_.size())
                in_ = in_.substr(in_.size() - ib[1].cbBuffer);
            else if (role_ == Role::Server || !first)
                in_.clear();

            if (status == SEC_E_OK) {
                SECURITY_STATUS q = QueryContextAttributesW(&ctx_, SECPKG_ATTR_STREAM_SIZES, &sizes_);
                if (q != SEC_E_OK) {
                    error_ = StatusText("QueryContextAttributes(STREAM_SIZES)", q);
                    return false;
                }
                open_ = true;
                // Application data may have arrived on the heels of the last handshake message.
                return in_.empty() ? true : decrypt(out, plain);
            }
            // SEC_I_CONTINUE_NEEDED: go round again only if there is more input to use.
            if (in_.empty() || (role_ == Role::Client && first))
                return true;
        }
    }

    bool decrypt(std::string& out, std::string& plain)
    {
        while (!in_.empty()) {
            SecBuffer b[4] = {
                { (ULONG)in_.size(), SECBUFFER_DATA,  &in_[0] },
                { 0,                 SECBUFFER_EMPTY, nullptr },
                { 0,                 SECBUFFER_EMPTY, nullptr },
                { 0,                 SECBUFFER_EMPTY, nullptr },
            };
            SecBufferDesc desc = { SECBUFFER_VERSION, 4, b };
            SECURITY_STATUS status = DecryptMessage(&ctx_, &desc, 0, nullptr);
            if (status == SEC_E_INCOMPLETE_MESSAGE)
                return true;
            if (status == SEC_I_CONTEXT_EXPIRED) {
                // close_notify: the peer will send nothing more.
                peerClosed_ = true;
                open_ = false;
                in_.clear();
                return true;
            }
            if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE) {
                error_ = StatusText("DecryptMessage", status);
                return false;
            }
            // The buffers point into in_, so take everything out before changing it.
            std::string extra;
            for (const SecBuffer& x : b) {
                if (x.BufferType == SECBUFFER_DATA && x.cbBuffer)
                    plain.append((const char*)x.pvBuffer, x.cbBuffer);
                else if (x.BufferType == SECBUFFER_EXTRA && x.cbBuffer)
                    extra.assign((const char*)x.pvBuffer, x.cbBuffer);
            }
            in_.swap(extra);
            if (status == SEC_I_RENEGOTIATE) {
                open_ = false;
                return handshake(out, plain);
            }
        }
        return true;
    }
};

} // namespace RpmTls
