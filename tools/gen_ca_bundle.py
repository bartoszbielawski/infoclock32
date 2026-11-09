#!/usr/bin/env python3
"""Build certs/x509_crt_bundle.bin for WiFiClientSecure::setCACertBundle().

The Arduino-ESP32 2.0.x core reads the ESP-IDF 4.4 bundle layout:

    uint16 BE  number of certificates
    per certificate, sorted by subject DER (the device binary-searches it):
        uint16 BE  subject length
        uint16 BE  public key length
        subject     DER Name
        public key  DER SubjectPublicKeyInfo

Newer ESP-IDF generators (5.x) write a different layout (little-endian
lengths and an offset table) that this core cannot read, hence this script.

Usage:  gen_ca_bundle.py cacert.pem certs/x509_crt_bundle.bin
        (cacert.pem = Mozilla's list from https://curl.se/ca/cacert.pem;
         needs the 'cryptography' package)
"""
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives import serialization


def build(pem_bytes):
    certs = x509.load_pem_x509_certificates(pem_bytes)
    entries = {}
    for c in certs:
        name = c.subject.public_bytes()
        key = c.public_key().public_bytes(
            serialization.Encoding.DER,
            serialization.PublicFormat.SubjectPublicKeyInfo)
        entries.setdefault(name, key)          # same subject twice: keep first
    names = sorted(entries)                    # bytewise = the device's memcmp
    out = bytearray(struct.pack('>H', len(names)))
    for name in names:
        key = entries[name]
        out += struct.pack('>HH', len(name), len(key)) + name + key
    return bytes(out), len(certs), len(names)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], 'rb') as f:
        data, parsed, kept = build(f.read())
    with open(sys.argv[2], 'wb') as f:
        f.write(data)
    print(f'{parsed} certificates read, {kept} written, {len(data)} bytes -> {sys.argv[2]}')


if __name__ == '__main__':
    main()
