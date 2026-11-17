# KTIP-0014: Deterministik Masternode'lar (KRISTA-DIP3)

- **KTIP**: 0014
- **Başlık**: Zincir Üstü Deterministik Masternode'lar (KRISTA-DIP3)
- **Yazar**: KristaTech Core Developers
- **Durum**: Aktif / Uygulandı
- **Tür**: Konsensüs ve Protokol Yükseltmesi (`UPGRADE_DIP3`)
- **Oluşturulma**: 2026-11-17

---

## Özet

KTIP-0014, KristaTech'in eski hibrit Masternode takip yapısını (`mnodeman` P2P yayınları + `MasternodePingState` bellek/LevelDB ping penceresi + `fBlockOfflineSync` atlaması) **%100 zincir üstü deterministik Masternode listesi (`CDeterministicMNManager` / `evodb`)** ile değiştirir. Tasarım Dash DIP3 modelini esas alır ve KristaTech'in **Rule 8 (tek IP'de çoklu MN)** ile **ADAM Dual-Track** kurallarına göre uyarlanmıştır.

## Motivasyon

Eski hibrit yapıda yeniden başlatma, önbellek farkı veya İlk Blok İndirme (IBD) sırasında düğümler farklı aktif Masternode sayıları görebiliyor ve geçerli blokları `bad-cb-payee` / `Invalid standby validator substitution` ile reddedebiliyordu. Masternode listesini yalnızca zincir üstü özel işlemlerden (`ProRegTx`, `ProUpServTx`, `ProUpRegTx`, `ProUpRevTx`) ve UTXO harcamalarından blok-blok geri alınabilir (`undoable`) şekilde türeterek:

1. Aynı yükseklik `H`'deki her düğüm **bit-bit aynı** Masternode listesini hesaplar.
2. Zincir geri almalarında (`DisconnectBlock`) Masternode durumu da blokla birlikte hatasız geri alınır.
3. Tarihi bloklar IBD sırasında `fBlockOfflineSync` istisnası olmadan deterministik doğrulanır.

## Teknik Şartname

### 1. Özel İşlem Kodlaması (`KTDIP3`)

Deterministik Masternode işlemleri, standart bir `OP_RETURN` çıktısı içinde 6 baytlık `KTDIP3` etiketi, ardından `uint8_t nType` ve serileştirilmiş veri ile taşınır:

- **Tip `1` — `ProRegTx` (Kayıt)**:
  - `nVersion` (`uint16_t = 1`), `collateralOutpoint` (`COutPoint`), `addr` (`CService`), `pubKeyOwner` (`CPubKey`), `vchOperatorPubKey` (48-byte BLS açık anahtarı), `keyIDVoting` (`CKeyID`), `nOperatorReward` (`uint16_t`, `0..10000` bps), `scriptPayout` (`CScript`), `inputsHash` (`uint256`), `vchSig` (`pubKeyOwner` ECDSA imzası).
  - Teminat en az `15` onay almış, harcanmamış `4.200 KRISTA` çıktısı olmalıdır.
  - **Rule 8 (Tek IP Çoklu MN)**: Birden fazla Masternode aynı IP adresini paylaşabilir; tekillik `proTxHash`, `collateralOutpoint` ve `vchOperatorPubKey` üzerinden denetlenir.
- **Tip `2` — `ProUpServTx` (Servis Güncelleme / PoSe Canlandırma)**:
  - `nVersion`, `proTxHash`, `addr`, `scriptOperatorPayout`, `inputsHash`, `vchSig` (`vchOperatorPubKey` BLS imzası).
  - Geçerli operatör imzasıyla `PoSeBanned` durumunu kaldırır.
- **Tip `3` — `ProUpRegTx` (Kayıt Güncelleme)**:
  - `nVersion`, `proTxHash`, `vchOperatorPubKey`, `keyIDVoting`, `scriptPayout`, `inputsHash`, `vchSig` (`pubKeyOwner` ECDSA imzası).
- **Tip `4` — `ProUpRevTx` (Operatör İptali)**:
  - `nVersion`, `proTxHash`, `nReason`, `inputsHash`, `vchSig` (`vchOperatorPubKey` BLS imzası).

### 2. Deterministik Durum ve Hizmet Kanıtı (PoSe)

- **Teminat Harcaması**: Kayıtlı `collateralOutpoint` harcandığında ilgili Masternode o blokta listeden otomatik çıkarılır.
- **PoSe Puanlaması**: ADAM komitelerine veya PoBLS koordinasyonuna seçilip geçerli çözüm/imza sunmayan Masternode'lar PoSe cezası alır; eşiği aşanlar `ProUpServTx` gelene kadar `PoSeBanned` olur.
- **ADAM Entegrasyonu**: `UPGRADE_DIP3` aktifken `GetAdamMinerPool` aktif Masternode listesini doğrudan `CDeterministicMNManager::GetListForBlock(pindex)` üzerinden okur.

### 3. RPC Arayüzü

`protx` RPC komutu şu alt komutları sunar:
- `protx register`
- `protx update_service`
- `protx update_registrar`
- `protx revoke`
- `protx list`
- `protx info`
