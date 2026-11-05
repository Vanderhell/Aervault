# Optional crypto adapters

The adapters are opt-in and use C99 caller-owned buffers. Configure with the
microcrypt CMake package installed (`microcrypt::microcrypt`):

```sh
cmake -S . -B build-crypto -DAV_ENABLE_AUTH=ON -DAV_ENABLE_SEAL=ON -DAV_ENABLE_PAIR=ON
cmake --build build-crypto
```

`av_auth_tag` can be assigned directly to `av_config.tag`; it is the first 16
bytes of HMAC-SHA256 over the same AV01 bytes and so retains AV01 encoding.
Example setup: `av_auth_key key = { key_bytes, sizeof key_bytes }; cfg.tag = av_auth_tag; cfg.tag_ctx = &key;`.

For storage, explicitly provision a counter object and pass it as `counter_id`
to `av_seal_init`. `av_seal_encrypt` commits one counter value before encryption;
any reservation error returns without encrypting. Nonce = 4-byte configured
prefix followed by the 64-bit reserved value in big endian. Use a distinct,
stable prefix for each key domain and never restore an older counter snapshot.
Store the resulting nonce, ciphertext and tag in an explicitly selected object;
this helper does not inspect or migrate AV01 records. Example: initialize once,
then call `av_seal_encrypt` before `av_put`; retrieve with `av_get`, then pass
the envelope to `av_seal_decrypt`. On authentication error the plaintext buffer
is cleared.

For pairing, authenticate the peer identity and derive a checked shared secret
with microdh (or use a pre-shared pair key), then pass that secret to
`av_pair_derive` with `peer_verified=1`. Distinct transmit/receive keys are
HKDF-SHA256 derived and bound to ordered identities and direction. The app owns
identity trust, handshake transcript, transport, replay protection and message
protocol. Current adapter accepts the shared secret; X25519 primitive invocation
remains an application call to microdh.
