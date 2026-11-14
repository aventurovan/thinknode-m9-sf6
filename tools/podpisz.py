#!/usr/bin/env python3
"""Tworzy podpisany plik wersja.txt dla aktualizacji przez WiFi.

    podpisz.py <wersja> <plik-aktualizacja.bin> <OtaKey.h> <wyjście wersja.txt>

Wiersz: „wersja nazwa_pliku rozmiar md5 sha256 podpis”. Podpis Ed25519 obejmuje pięć pierwszych pól
(dokładnie ten tekst, pola rozdzielone jedną spacją). Klucz prywatny pochodzi ze zmiennej OTA_SIGNING_KEY
(PKCS#8 w base64, sekret repozytorium), publiczny jest wpisany w m9_sf6/ui/OtaKey.h.
Bez klucza powstaje plik bez podpisu — urządzenia go nie przyjmą."""
import base64, hashlib, os, re, sys

ver, path, keyh, out = sys.argv[1:5]
data = open(path, "rb").read()
msg = "%s %s %d %s %s" % (ver, os.path.basename(path), len(data), hashlib.md5(data).hexdigest(), hashlib.sha256(data).hexdigest())
pub_hex = re.search(r'M9_OTA_PUBKEY_HEX\s+"([0-9a-fA-F]{64})"', open(keyh, encoding="utf-8").read()).group(1).lower()
key_b64 = os.environ.get("OTA_SIGNING_KEY", "").strip()

if pub_hex == "0" * 64:
    print("::warning::W OtaKey.h nie ma jeszcze klucza publicznego — wersja.txt bez podpisu.")
    key_b64 = ""
if not key_b64:
    print("::warning::Brak sekretu OTA_SIGNING_KEY — wersja.txt bez podpisu, aktualizacja przez WiFi nie zadziała.")
    open(out, "w").write(msg + "\n")
    sys.exit(0)

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

key = serialization.load_der_private_key(base64.b64decode(key_b64), password=None)
my_pub = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()
if my_pub != pub_hex:
    sys.exit("BŁĄD: klucz z sekretu OTA_SIGNING_KEY nie pasuje do klucza publicznego w OtaKey.h — urządzenia odrzuciłyby aktualizację.")
sig = key.sign(msg.encode())
Ed25519PublicKey.from_public_bytes(bytes.fromhex(pub_hex)).verify(sig, msg.encode())
open(out, "w").write("%s %s\n" % (msg, sig.hex()))
print("wersja.txt podpisany:", msg)
