# KTIP-0014: Deterministik Masternode'lar (KRISTA-DIP3 ve Sıcak/Soğuk BLS)

- **KTIP**: 0014
- **Başlık**: Zincir Üstü Deterministik Masternode'lar (KRISTA-DIP3 ve Sıcak/Soğuk BLS)
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

- **Tip `1` — `ProRegTx` (`CProRegTx`, Kayıt)**:
  - `nVersion` (`uint16_t = 1`), `collateralOutpoint` (`COutPoint`), `addr` (`CService`), `pubKeyOwner` (`CPubKey`), `blsPubKeyOperator` (`CBLSPubKey`, 48-byte BLS açık anahtarı), `keyIDVoting` (`CKeyID`), `nOperatorReward` (`uint16_t`, `0..10000` bps), `scriptPayout` (`CScript`), `inputsHash` (`uint256`), `vchSig` (`pubKeyOwner` sıkıştırılmış ECDSA imzası).
  - Teminat en az `15` onay almış, harcanmamış `4.200 KRISTA` çıktısı olmalıdır; `ConnectBlock` teminat sahip anahtarının `pubKeyOwner` ile eşleşmesini zorunlu kılar (`bad-protx-collateral-owner`) ve aynı blok içinde teminatın harcanmasını (`bad-protx-collateral-spent-in-block`) reddeder.
  - **Rule 8 (Tek IP Çoklu MN)**: Birden fazla Masternode aynı IP adresini paylaşabilir; tekillik `proTxHash`, `collateralOutpoint` ve `blsPubKeyOperator` üzerinden denetlenir.
- **Tip `2` — `ProUpServTx` (`CProUpServTx`, Servis Güncelleme / PoSe Canlandırma)**:
  - `nVersion`, `proTxHash`, `addr`, `scriptOperatorPayout`, `inputsHash`, `blsSig` (`blsPubKeyOperator` BLS imzası).
  - Geçerli operatör imzasıyla `PoSeBanned` durumunu kaldırır.
- **Tip `3` — `ProUpRegTx` (`CProUpRegTx`, Kayıt Güncelleme)**:
  - `nVersion`, `proTxHash`, `blsPubKeyOperator`, `keyIDVoting`, `scriptPayout`, `inputsHash`, `vchSig` (`pubKeyOwner` ECDSA imzası).
- **Tip `4` — `ProUpRevTx` (`CProUpRevTx`, Operatör İptali)**:
  - `nVersion`, `proTxHash`, `nReason`, `inputsHash`, `blsSig` (`blsPubKeyOperator` BLS imzası).

### 2. Deterministik Durum, Sıcak/Soğuk BLS ve Hizmet Kanıtı (PoSe)

- **Teminat Harcaması**: Kayıtlı `collateralOutpoint` harcandığında ilgili Masternode o blokta listeden otomatik çıkarılır.
- **PoSe Puanlaması**: ADAM komitelerine veya PoBLS koordinasyonuna seçilip geçerli çözüm/imza sunmayan Masternode'lar PoSe cezası alır; eşiği aşanlar `ProUpServTx` gelene kadar `PoSeBanned` olur.
- **ADAM ve Dinamik BFT LLMQ Entegrasyonu**: `UPGRADE_DIP3` aktifken `GetAdamMinerPool` aktif Masternode listesini doğrudan `CDeterministicMNManager::GetListForBlock(pindex)` üzerinden okur ve LLMQ üyeliği/doğrulaması aktif havuza göre dinamik olarak (`GetTargetQuorumSize`: `5 -> 9 -> 11 -> 15`) ölçeklenir.
- **KTDIP3 Sıcak/Soğuk BLS Ayrımı (`UPGRADE_DIP3 = 1` Mainnet & Testnet; `NO_ACTIVATION_HEIGHT` Regtest)**:
  - `UPGRADE_DIP3` aktifken deterministik Masternode'lar koordinatör VRF kanıtlarını, blok başlıklarını ve ADAM çözümlerini yalnızca kayıtlı `blsPubKeyOperator` anahtarlarıyla imzalar (`SignBLSOperatorOnly`, `PinBLSPubKey` olmadan boş `ecdsaSig`); doğrulama `VerifyAdamOrDip3Signature` ve `GetMNByOperator` üzerinden tekdüze yapılır.
  - Masternode ödülleri `AppendDip3OrDirectPayment` aracılığıyla doğrudan `scriptPayout` adresine (ve tanımlıysa `nOperatorReward` baz puanı oranında `scriptOperatorPayout` adresine) yönlendirilir.

### 3. RPC Arayüzü

`protx` RPC komutu şu alt komutları sunar:
- `protx bls_generate` — yeni bir BLS12-381 operatör `(secret, public)` anahtar çifti üretir
- `protx register`
- `protx update_service`
- `protx update_registrar`
- `protx revoke`
- `protx list`
- `protx info`

Ek olarak `UPGRADE_DIP3` aktifken `listmasternodes` ve `getmasternodecount` komutları da KTDIP3 deterministik Masternode'larını kapsar; `getmasternodestatus` ise aktif deterministik kayıtlara `" (KTDIP3)"` ekini döner.

### 4. Soğuk Cüzdan GUI Entegrasyonu ve Mempool Koruması

- **Cüzdan İçinden Tek Tıkla Kayıt**: `MasterNodeWizardDialog` 3. Adımda kullanıcı **Operator BLS Public Key (optional)** alanını boş bırakabilir (`DeriveBLSFromCKey(mnKey)` ile deterministik türetilip `activemasternode.conf` için `<alias> <privkey> <bls_privkey_hex>` üretilir) ya da ayrı sıcak düğümden aldığı 48 bayt hex `blsPubKeyOperator` anahtarını girebilir (`operatorPubKey=<hex>` çıktısı verilir). Teminat `>= 15` onaya ulaştığında **MASTERNODES** sekmesinde **Start** / **Start All** / **Start Inactive/s** butonuna basılması (veya olgunlaşmış teminatla sihirbazın tamamlanması) RPC Debug Console / CLI gerekmeden `RegisterDip3MasternodeFromWallet` üzerinden doğrudan `ProRegTx` işlemini imzalayıp yayınlar.
- **Mempool ve Blok Şablonu Çakışma Koruması**: `AcceptToMemoryPool`, aynı `collateralOutpoint` veya `blsPubKeyOperator` için mempool'da bekleyen mükerrer `ProRegTx` işlemlerini `bad-protx-mempool-conflict` ile reddeder; `CreateNewBlock` ise aynı blok içine çakışan `ProRegTx` / `ProUp*` işlemlerini almaz.
- **Açılışta Otomatik Teminat Kilidi ve Canlı Görünüm**: `AppInit2` cüzdana ait tüm zincir üstü KTDIP3 teminatlarını açılışta otomatik kilitler; `WalletModel::getMNCollateralCandidate` kayıtlı DIP3 teminatlarını atlar; `MNModel` mempool'da bekleyen `ProRegTx` için `PRE_ENABLED` gösterir ve `TopBar` / `StatsWidget` / `SettingsConsoleWidget` / `ClientModel` sayaçları KTDIP3 verilerini yansıtır.
