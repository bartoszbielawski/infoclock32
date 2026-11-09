# TLS root certificates

`x509_crt_bundle.bin` is embedded in the firmware (`board_build.embed_files`)
and used by `HttpUtils::httpGet` to verify HTTPS servers when `tls_verify=1`.

| | |
|---|---|
| Source | Mozilla CA list via https://curl.se/ca/cacert.pem |
| Mozilla data as of | Thu Nov  5 04:12:01 2026 GMT |
| cacert.pem SHA-256 | `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285` |
| Certificates | 118 |
| Bundle SHA-256 | `8ca330673e7ecd07610eecfaffd23aba8b9ceca10a381d3878f268348edf5aec` |

Format: the ESP-IDF 4.4 layout that the Arduino-ESP32 2.0.x core reads (see
`tools/gen_ca_bundle.py`). `tests/host/ca_bundle_check.cpp` checks the file
parses the way the device parses it.

To refresh (needs Python with `cryptography`):

```bash
curl -sSO https://curl.se/ca/cacert.pem
python3 tools/gen_ca_bundle.py cacert.pem certs/x509_crt_bundle.bin
```
