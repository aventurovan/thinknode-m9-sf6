#pragma once
// Zapamiętane parowania Bluetooth w urządzeniu (diagnostyka i czyszczenie).
int  m9bleBondCount();      // ile telefonów jest zapamiętanych; -1 = nie da się odczytać
int  m9bleClearBonds();     // usuwa wszystkie; zwraca liczbę usuniętych
