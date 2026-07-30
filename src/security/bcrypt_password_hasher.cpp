#include "rtc/security/bcrypt_password_hasher.hpp"

#include <bcrypt.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

#include "rtc/errors/exceptions.hpp"

// ---------------------------------------------------------------------------
// Why this file does not call bcrypt::generateHash
//
// Bcrypt.cpp's generateHash() derives its 16-byte salt like this:
//
//     arc4random_init();          // -> srand(time(NULL))
//     arc4random_buf(seed, 16);   // -> rand() % 256, sixteen times
//
// On OpenBSD those map to the real arc4random. Everywhere else — Linux, macOS,
// Windows — the library substitutes the shim above, and the consequences are
// serious:
//
//   * The salt is fully determined by the current wall-clock *second*. srand() is
//     re-seeded on every call, so entropy never accumulates.
//   * Two passwords hashed in the same second receive the identical salt. Two
//     users who register in the same second with the same password therefore get
//     byte-identical hashes — which is precisely the outcome salting exists to
//     prevent.
//   * An attacker who can estimate a registration timestamp can reconstruct the
//     salt and precompute against it.
//
// The fix keeps the output format byte-compatible with the library's own — the
// same $2b$ bcrypt hash, verifiable by any bcrypt implementation and by every
// password already stored — by supplying the salt ourselves from a CSPRNG and
// calling the library's two lower-level primitives directly. bcrypt_gensalt()
// merely base64-encodes the caller's seed into the "$2b$NN$...." prefix; it adds
// no entropy of its own, which is exactly why passing a good seed is sufficient.
//
// Verification is unaffected and still goes through bcrypt::validatePassword():
// the salt is embedded in the stored hash, so verifying never needs randomness.
// That is what makes this change fully backward compatible with existing hashes.
// ---------------------------------------------------------------------------

// Declared rather than #included: these live in the dependency's private
// src/node_blf.h, which is not on our include path. The symbols have external
// linkage in the static library, and the signatures below match that header
// exactly (its u_int8_t is a typedef for unsigned char), so the C++ mangling
// resolves. A mismatch would fail at link time, not silently at run time.
void bcrypt_gensalt(char minor, unsigned char log_rounds, unsigned char* seed, char* gsalt);
void node_bcrypt(const char* key, std::size_t key_len, const char* salt, char* encrypted);

namespace rtc::security {
namespace {

// bcrypt's salt is 128 bits.
constexpr std::size_t kSeedBytes = 16;
// _SALT_LEN in the library; the encoded "$2b$NN$<22 chars>" needs far less.
constexpr std::size_t kSaltBufferBytes = 64;
// A bcrypt hash is 60 characters; the library writes a trailing NUL.
constexpr std::size_t kHashBufferBytes = 61;
constexpr std::size_t kHashLength = 60;

}  // namespace

BcryptPasswordHasher::BcryptPasswordHasher(int cost)
    : cost_(std::clamp(cost, kMinCost, kMaxCost)) {}

std::string BcryptPasswordHasher::hash(std::string_view plaintext) const {
    std::array<unsigned char, kSeedBytes> seed{};
    // RAND_bytes is OpenSSL's CSPRNG — already a dependency of this project for
    // JWT signing. A return value other than 1 means the generator could not
    // produce secure output; continuing with a degraded salt would be far worse
    // than failing the request, so this throws.
    if (RAND_bytes(seed.data(), static_cast<int>(seed.size())) != 1) {
        throw rtc::errors::InternalException(
            "Password hashing failed: no secure randomness available for the salt");
    }

    try {
        std::array<char, kSaltBufferBytes> salt{};
        bcrypt_gensalt('b', static_cast<unsigned char>(cost_), seed.data(), salt.data());

        std::string hashed(kHashBufferBytes, '\0');
        node_bcrypt(plaintext.data(), plaintext.size(), salt.data(), hashed.data());
        hashed.resize(kHashLength);

        // A truncated or empty result means the library rejected the input; never
        // return something that would later be mistaken for a valid hash.
        if (hashed.size() != kHashLength || hashed.front() != '$') {
            throw rtc::errors::InternalException("Password hashing produced a malformed hash");
        }
        return hashed;
    } catch (const rtc::errors::AppException&) {
        throw;
    } catch (const std::exception& ex) {
        throw rtc::errors::InternalException("Password hashing failed", ex.what());
    }
}

bool BcryptPasswordHasher::verify(std::string_view plaintext, std::string_view hash) const {
    if (hash.empty()) {
        return false;
    }
    try {
        // Verification reads the salt out of the stored hash, so it works for every
        // hash this service has ever written, including those produced before the
        // salt-entropy fix above.
        return bcrypt::validatePassword(std::string(plaintext), std::string(hash));
    } catch (const std::exception&) {
        // A malformed stored hash must never crash auth; treat it as a mismatch.
        return false;
    }
}

}  // namespace rtc::security
