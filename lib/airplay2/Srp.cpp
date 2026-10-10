#include "airplay2/Srp.h"

#include <openssl/bn.h>
#include <stdexcept>
#include <cstring>

using namespace std;

namespace AirPlay2
{
    namespace
    {
        struct BnDeleter { void operator()(BIGNUM* p) const { BN_clear_free(p); } };
        struct CtxDeleter { void operator()(BN_CTX* p) const { BN_CTX_free(p); } };
        using Bn = unique_ptr<BIGNUM, BnDeleter>;
        using BnCtx = unique_ptr<BN_CTX, CtxDeleter>;

        Bn NewBn()
        {
            Bn r(BN_new());
            if (!r)
            {
                throw bad_alloc();
            }
            return r;
        }

        Bn FromBytes(const Bytes& b)
        {
            Bn r(BN_bin2bn(b.data(), static_cast<int>(b.size()), nullptr));
            if (!r)
            {
                throw bad_alloc();
            }
            return r;
        }

        Bytes ToBytes(const BIGNUM* n)
        {
            Bytes r(BN_num_bytes(n));
            BN_bn2bin(n, r.data());
            return r;
        }

        Bytes ToBytesPadded(const BIGNUM* n, size_t len)
        {
            Bytes r(len);
            BN_bn2binpad(n, r.data(), static_cast<int>(len));
            return r;
        }

        // RFC 5054 3072-bit group (identical to the RFC 3526 MODP prime), generator 5
        struct Group
        {
            Bn N;
            Bn g;
            size_t len;

            Group()
            {
                N.reset(BN_get_rfc3526_prime_3072(nullptr));
                g = NewBn();
                BN_set_word(g.get(), 5);
                len = BN_num_bytes(N.get());
            }
        };

        const Group& GetGroup()
        {
            static const Group group;
            return group;
        }

        // k = H(N | PAD(g))
        Bn ComputeK(const Group& grp)
        {
            const Bytes n = ToBytes(grp.N.get());
            const Bytes g = ToBytesPadded(grp.g.get(), grp.len);
            return FromBytes(Crypto::Sha512({ &n, &g }));
        }

        // x = H(s | H(I | ":" | P))
        Bn ComputeX(const string& user, const string& password, const Bytes& salt)
        {
            const string up = user + ":" + password;
            const Bytes inner = Crypto::Sha512(up.data(), up.size());
            return FromBytes(Crypto::Sha512({ &salt, &inner }));
        }

        // u = H(PAD(A) | PAD(B))
        Bn ComputeU(const Group& grp, const Bytes& A, const Bytes& B)
        {
            Bytes a(grp.len - min(grp.len, A.size()), 0);
            a.insert(a.end(), A.begin(), A.end());
            Bytes b(grp.len - min(grp.len, B.size()), 0);
            b.insert(b.end(), B.begin(), B.end());
            return FromBytes(Crypto::Sha512({ &a, &b }));
        }

        // M = H(H(N) xor H(g) | H(I) | s | A | B | K)
        Bytes ComputeM(const Group& grp, const string& user, const Bytes& salt, const Bytes& A, const Bytes& B, const Bytes& K)
        {
            const Bytes n = ToBytes(grp.N.get());
            const Bytes g = ToBytes(grp.g.get());
            Bytes hn = Crypto::Sha512(n.data(), n.size());
            const Bytes hg = Crypto::Sha512(g.data(), g.size());

            for (size_t i = 0; i < hn.size(); ++i)
            {
                hn[i] ^= hg[i];
            }
            const Bytes hi = Crypto::Sha512(user.data(), user.size());
            return Crypto::Sha512({ &hn, &hi, &salt, &A, &B, &K });
        }

        bool ConstTimeEqual(const Bytes& a, const Bytes& b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            uint8_t r = 0;
            for (size_t i = 0; i < a.size(); ++i)
            {
                r |= a[i] ^ b[i];
            }
            return r == 0;
        }
    }

    struct SrpServer::Impl
    {
        Bn b;
        Bn v;
    };

    SrpServer::SrpServer(const string& username, const string& password, const Bytes& salt, const Bytes& privateKey)
        : m_impl(make_unique<Impl>())
        , m_username(username)
        , m_salt(salt.empty() ? Crypto::RandomBytes(16) : salt)
    {
        const Group& grp = GetGroup();
        BnCtx ctx(BN_CTX_new());

        const Bn x = ComputeX(username, password, m_salt);
        m_impl->v = NewBn();
        BN_mod_exp(m_impl->v.get(), grp.g.get(), x.get(), grp.N.get(), ctx.get());

        m_impl->b = FromBytes(privateKey.empty() ? Crypto::RandomBytes(32) : privateKey);

        // B = (k*v + g^b) % N
        const Bn k = ComputeK(grp);
        Bn kv = NewBn();
        Bn gb = NewBn();
        Bn B = NewBn();
        BN_mod_mul(kv.get(), k.get(), m_impl->v.get(), grp.N.get(), ctx.get());
        BN_mod_exp(gb.get(), grp.g.get(), m_impl->b.get(), grp.N.get(), ctx.get());
        BN_mod_add(B.get(), kv.get(), gb.get(), grp.N.get(), ctx.get());
        m_B = ToBytes(B.get());
    }

    SrpServer::~SrpServer() = default;

    bool SrpServer::VerifyClient(const Bytes& A, const Bytes& M1, Bytes& serverProof)
    {
        const Group& grp = GetGroup();
        BnCtx ctx(BN_CTX_new());

        const Bn a = FromBytes(A);
        Bn check = NewBn();

        // safeguard: A % N must not be zero
        BN_mod(check.get(), a.get(), grp.N.get(), ctx.get());
        if (BN_is_zero(check.get()))
        {
            return false;
        }
        const Bn u = ComputeU(grp, A, m_B);

        if (BN_is_zero(u.get()))
        {
            return false;
        }

        // S = (A * v^u) ^ b % N
        Bn vu = NewBn();
        Bn avu = NewBn();
        Bn S = NewBn();
        BN_mod_exp(vu.get(), m_impl->v.get(), u.get(), grp.N.get(), ctx.get());
        BN_mod_mul(avu.get(), a.get(), vu.get(), grp.N.get(), ctx.get());
        BN_mod_exp(S.get(), avu.get(), m_impl->b.get(), grp.N.get(), ctx.get());

        const Bytes s = ToBytes(S.get());
        const Bytes K = Crypto::Sha512(s.data(), s.size());
        const Bytes M = ComputeM(grp, m_username, m_salt, A, m_B, K);

        if (!ConstTimeEqual(M, M1))
        {
            return false;
        }
        m_K = K;
        serverProof = Crypto::Sha512({ &A, &M, &K });
        return true;
    }

    struct SrpClient::Impl
    {
        Bn a;
    };

    SrpClient::SrpClient(const string& username, const string& password, const Bytes& privateKey)
        : m_impl(make_unique<Impl>())
        , m_username(username)
        , m_password(password)
    {
        const Group& grp = GetGroup();
        BnCtx ctx(BN_CTX_new());

        m_impl->a = FromBytes(privateKey.empty() ? Crypto::RandomBytes(32) : privateKey);
        Bn A = NewBn();
        BN_mod_exp(A.get(), grp.g.get(), m_impl->a.get(), grp.N.get(), ctx.get());
        m_A = ToBytes(A.get());
    }

    SrpClient::~SrpClient() = default;

    bool SrpClient::ProcessChallenge(const Bytes& salt, const Bytes& B, Bytes& M1)
    {
        const Group& grp = GetGroup();
        BnCtx ctx(BN_CTX_new());

        const Bn b = FromBytes(B);
        Bn check = NewBn();
        BN_mod(check.get(), b.get(), grp.N.get(), ctx.get());

        if (BN_is_zero(check.get()))
        {
            return false;
        }
        const Bn u = ComputeU(grp, m_A, B);
        const Bn x = ComputeX(m_username, m_password, salt);
        const Bn k = ComputeK(grp);

        // S = (B - k * g^x) ^ (a + u * x) % N
        Bn gx = NewBn();
        Bn kgx = NewBn();
        Bn base = NewBn();
        Bn ux = NewBn();
        Bn exp = NewBn();
        Bn S = NewBn();
        BN_mod_exp(gx.get(), grp.g.get(), x.get(), grp.N.get(), ctx.get());
        BN_mod_mul(kgx.get(), k.get(), gx.get(), grp.N.get(), ctx.get());
        BN_mod_sub(base.get(), b.get(), kgx.get(), grp.N.get(), ctx.get());
        BN_mul(ux.get(), u.get(), x.get(), ctx.get());
        BN_add(exp.get(), m_impl->a.get(), ux.get());
        BN_mod_exp(S.get(), base.get(), exp.get(), grp.N.get(), ctx.get());

        const Bytes s = ToBytes(S.get());
        m_K = Crypto::Sha512(s.data(), s.size());
        m_M1 = ComputeM(grp, m_username, salt, m_A, B, m_K);
        M1 = m_M1;
        return true;
    }

    bool SrpClient::VerifyServer(const Bytes& serverProof) const
    {
        const Bytes expected = Crypto::Sha512({ &m_A, &m_M1, &m_K });
        return ConstTimeEqual(expected, serverProof);
    }
}
