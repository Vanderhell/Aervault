# Optional crypto adapters

The adapters are opt-in and use C99 caller-owned buffers. CMake fetches these
unchanged upstream revisions only when a consuming adapter is selected:

- microcrypt `a492e7ea36d77bb7ab89ee9b4c1dfc8c4d017124`
- microdh `e2b16dee4cdc6888608b6146a67cdf43d36f65ab` (only with X25519)

Configure all three adapters, including X25519:

```sh
cmake -S . -B build-crypto -DAV_TESTS=ON -DAV_ENABLE_AUTH=ON -DAV_ENABLE_SEAL=ON -DAV_ENABLE_PAIR=ON -DAV_PAIR_X25519=ON
cmake --build build-crypto
ctest --test-dir build-crypto --output-on-failure
```

`av_auth_tag` can be assigned directly to `av_config.tag`; it is the first 16
bytes of HMAC-SHA256 over the same AV01 bytes and so retains AV01 encoding.
Example setup: `av_auth_key key = { key_bytes, sizeof key_bytes }; cfg.tag = av_auth_tag; cfg.tag_ctx = &key;`.

For storage, explicitly provision a counter object and pass it as `counter_id`
to `av_seal_init`. All uses of the same AES key must share that one durable
counter object and stable 4-byte prefix. `av_seal_encrypt` commits one counter
value before encryption; any reservation error returns without encrypting.
Nonce = prefix followed by the 64-bit reserved value in big endian. Never
restore an older counter snapshot or use the same key with a separate counter.
Store the resulting nonce, ciphertext and tag in an explicitly selected object;
this helper does not inspect or migrate AV01 records. Example: initialize once,
then call `av_seal_encrypt` before `av_put`; retrieve with `av_get`, then pass
the envelope to `av_seal_decrypt`. On authentication error the plaintext buffer
is cleared.

For PSK pairing, call `av_pair_derive` with the pair key. For X25519, call
`av_pair_x25519` with the local private key. Both require an application
`verify_peer` callback that authenticates the exact identity and pinned peer
public key; X25519 then uses `mdh_shared_secret_checked`. Distinct transmit and
receive keys are HKDF-SHA256 derived and bound to ordered identities and
direction. The app owns trust policy, transcript, transport and replay defense.
In ESP-IDF, PSK builds use `aervault_pair`; select `aervault_pair_x25519` only
when the firmware needs X25519. The latter component requires microdh.

The linker smoke programs are `aervault_link_core`, `aervault_link_auth`,
`aervault_link_seal`, and `aervault_link_pair`. With them built, symbol
separation is reproducible using `tests/check_crypto_linking.py` and the
toolchain `nm` executable.
