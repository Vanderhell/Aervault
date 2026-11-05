# Aervault

Samostatné C99 jadro na uchovanie malých stavových záznamov v MCU. RAM dodáva volajúci. Každý objekt potrebuje dva vyhradené sloty a pracovný buffer veľkosti `2 * (52 + payload_limit)` bajtov.

- `AV_RECOVERABLE` načíta platný slot aj pri chýbajúcej alebo poškodenej druhej kópii. Takýto návrat môže obnoviť staršiu konfiguráciu.
- `AV_STRICT` vyžaduje dva platné autentifikované záznamy s po sebe idúcimi revíziami. Volajúci určuje význam a povolené prechody obsahu, napríklad pre stav replay okna.
- `AV_COUNTER` vydá rozsah počítadiel až po úspešnom trvalom zápise. Ak sa výsledok zápisu nedá potvrdiť, aplikácia nedostane žiadne číslo.

Backend musí izolovať sloty aj pri výpadku napájania a pri úspechu potvrdiť trvalosť. Čítanie smie sprístupniť iba stav z trvalého média, aj po neúspešnej operácii. Pri `AV_UNCERTAIN` treba pred ďalšou operáciou zavrieť alebo zahodiť staging backendu, ak jeho kontrakt túto izoláciu nezaručuje. Zápis môže zanechať poškodený cieľový slot. Nikdy nesmie poškodiť druhý slot. Pri NOR si to vyžaduje samostatné mazacie jednotky alebo iný mechanizmus izolácie. Prerušené založenie objektu môže v prísnom režime vyžadovať zásah správcu. Pre bezpečnostný objekt povoľte založenie iba pri preukázateľne novej identite a nových kľúčoch.

Callback `tag` musí dodať deterministický kryptografický MAC (autentifikačný kód) s chráneným kľúčom a vlastnou doménou, napríklad HMAC-SHA-256 skrátený na 16 bajtov. Obsah záznamu je čitateľný z média. Dôvernosť, ochrana pred obnovením celej starej autentifikovanej dvojice, správa kľúčov, transakcie cez viac objektov a pravidlá protokolu patria do integrácie. Na dôverné dáta použite samostatnú autentifikovanú šifrovaciu vrstvu so správou nonce (jednorazovej hodnoty).

Prvé vydanie obsahuje prenosné jadro a simuláciu chýb. ESP-IDF adaptér a dôkaz trvalosti na doske sú samostatné kroky. FORTAVIS môže neskôr mapovať rezervácie počítadiel a replay stav cez vlastný adaptér. Dokončenie prenosu po reštarte musí vyriešiť jeho protokol.

```sh
cmake -S . -B build -DAV_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Inšpirácia: oddelené obnovovacie rozhodnutia z LoxDust, kanonická serializácia z microconf a loxseq, izolácia a geometria média z microflash a pravidlá vydávania počítadiel z FORTAVIS.
