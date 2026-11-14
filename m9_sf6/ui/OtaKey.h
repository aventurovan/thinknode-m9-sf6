#pragma once
// Klucz publiczny (Ed25519), którym urządzenie sprawdza podpis pliku wersja.txt przed aktualizacją przez WiFi.
// Klucz prywatny jest tylko w sekretach repozytorium (OTA_SIGNING_KEY) i podpisuje każdy build w GitHub Actions.
// Same zera oznaczałyby brak klucza: aktualizacja przez WiFi jest wtedy wyłączona.
#define M9_OTA_PUBKEY_HEX "91a6556730ac02775fe6343176cfec5e3858b1bb07de80050fcfa1df5f9306b2"
