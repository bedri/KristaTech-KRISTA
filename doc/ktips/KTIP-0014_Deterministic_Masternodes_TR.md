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

- **Tip `1` — `ProRegTx` (`CProRegTx`, Masternode Kaydı)**:
  - `nVersion` (`UPGRADE_HARDENING_V3` Yükseklik `1` altında `uint16_t = 2` zorunludur; eski `nVersion = 1` yalnızca V3 öncesindedir), `collateralOutpoint` (`COutPoint`), `addr` (`CService`), `pubKeyOwner` (`CPubKey`, V3 altında 33 baytlık sıkıştırılmış açık anahtar zorunludur), `blsPubKeyOperator` (`CBLSPubKey`, 48 bayt BLS açık anahtarı), **`blsPoP`** (`CBLSSignature`, `nVersion = 2` yükü ve `GetSignHash()` içine dahil edilen `BLSDomain::PROTX_POP` DST etiketli `blsPubKeyOperator` üzerinde 96 baytlık Sahiplik Kanıtı / Proof-of-Possession), `keyIDVoting` (`CKeyID`), `nOperatorReward` (`uint16_t`, `0..10000` bps), `scriptPayout` (`CScript`), `inputsHash` (`uint256`), `vchSig` (`std::vector<unsigned char>`, `pubKeyOwner` ile doğrulanan sıkıştırılmış ECDSA imzası; tüm ECDSA geri dönüşlerinde katı DER/low-S zorunludur). Kayıt sırasında `nOperatorKeyHeight` değeri `nRegisteredHeight` ile birlikte başlatılır.
  - Teminat en az `15` onay almış, harcanmamış `4.200 KRISTA` çıktısı olmalıdır; `ConnectBlock` DIP3 doğrulamasını (`fJustCheck=true`) `UpdateCoins` öncesinde hazırlar, `IsBlockValueValid()` sonrasında atomik olarak işler, teminat sahip anahtarının `pubKeyOwner` ile eşleşmesini zorunlu kılar (`bad-protx-collateral-owner`) ve aynı blok içinde teminatın harcanmasını (`bad-protx-collateral-spent-in-block`) reddeder. `UPGRADE_HARDENING_V3` kapsamında coinbase, coinstake veya ZeroCoinPoW işlemlerindeki `KTDIP3` yükleri kesinlikle reddedilir (`bad-protx-container`).
  - **Rule 8 (Tek IP Çoklu MN)**: Birden fazla Masternode aynı IP adresini paylaşabilir; tekillik `proTxHash`, `collateralOutpoint` ve `blsPubKeyOperator` üzerinden denetlenir.
- **Tip `2` — `ProUpServTx` (`CProUpServTx`, Servis Güncelleme / PoSe Canlandırma)**:
  - `nVersion`, `proTxHash`, `addr`, `scriptOperatorPayout`, `inputsHash`, `blsSig` (`CBLSSignature`, `BLSDomain::PROTX_UPSERV` DST etiketiyle `blsPubKeyOperator` BLS imzası).
  - V3 altında `nOperatorReward == 0` iken boş olmayan `scriptOperatorPayout` alanını reddeder (`bad-protx-op-payout-not-allowed`).
  - Operatör tarafından geçerli şekilde imzalandığında **`nPoseReviveCooldownBlocks` blok geçtikten sonra** (`Mainnet` üzerinde `120` blok, `Testnet/Regtest` üzerinde `12` blok; daha erken denemeler `bad-protx-pose-cooldown` ile reddedilir) `PoSeBanned` durumunu kaldırır ve `nPoSeRevivedHeight` değerini kaydeder. `UPGRADE_HARDENING_V4` (Yükseklik `1`) altında canlandırma işlemi, KTDIP3 `PoM` staking uptime başlangıcını katı biçimde `std::max(nRegisteredHeight, nPoSeRevivedHeight)` değerine sıfırlar.
- **Tip `3` — `ProUpRegTx` (`CProUpRegTx`, Kayıt Güncelleme)**:
  - `nVersion` (`UPGRADE_HARDENING_V3` Yükseklik `1` altında `uint16_t = 2` zorunludur), `proTxHash`, `blsPubKeyOperator`, **`blsPoP`** (yeni `blsPubKeyOperator` üzerinde 96 baytlık PoP), `keyIDVoting`, `scriptPayout`, `inputsHash`, `vchSig` (`pubKeyOwner` ECDSA imzası). Anahtar rotasyonu sonrasında eski operatörün ödeme almaya devam etmemesi için `scriptOperatorPayout` alanını atomik olarak temizler.
  - **V4 Operatör Anahtar Rotasyonu Bekleme Süresi (`nProUpRegCooldownBlocks = 100`)**: `UPGRADE_HARDENING_V4` (Yükseklik `1`) kapsamında `blsPubKeyOperator` alanının farklı bir operatör anahtarına değiştirilebilmesi için son operatör anahtarı aktivasyonundan (`nOperatorKeyHeight >= 0 ? nOperatorKeyHeight : nRegisteredHeight`) bu yana en az **`consensus.nProUpRegCooldownBlocks = 100` blok** geçmiş olmalıdır (aksi halde `bad-proupreg-cooldown` ile reddedilir); rotasyon tamamlandığında `nOperatorKeyHeight = nHeight` olarak güncellenir.
- **Tip `4` — `ProUpRevTx` (`CProUpRevTx`, Operatör İptali)**:
  - `nVersion`, `proTxHash`, `nReason`, `inputsHash`, `blsSig` (`BLSDomain::PROTX_UPREV` DST etiketiyle `blsPubKeyOperator` BLS imzası). İptal sırasında `scriptOperatorPayout` alanını atomik olarak temizler.

### 2. Deterministik Durum, Sıcak/Soğuk BLS ve Hizmet Kanıtı (PoSe)

- **Teminat Harcaması**: Kayıtlı `collateralOutpoint` harcandığında ilgili Masternode o blokta listeden otomatik çıkarılır.
- **PoSe Puanlaması ve `M-4` PoSe Adaleti**: ADAM komitelerine (kaçırılan birincil koordinatör slotları için `+5` ceza; ayrıca `UPGRADE_HARDENING_V4` `M-4` kapsamında yedek ikamesiyle yeri doldurulan sessiz birincil ADAM komitesi üyeleri için `+5` ceza) veya yedek koordinasyona seçilip geçerli çözüm/imza sunmayan Masternode'lar PoSe cezası alır; LLMQ kurul imzalaması `-5` PoSe iyileşmesi sağlarken `UPGRADE_HARDENING_V4` (Yükseklik `1`) altında LLMQ imzalamama artık `+5` PoSe cezası doğurmaz (V4 öncesi `-5/+5` LLMQ imzasızlık cezası kaldırılmıştır). Ban eşiğini aşanlar, `nPoseReviveCooldownBlocks` (`Mainnet` `120`, `Testnet/Regtest` `12`) blok sonrasında `ProUpServTx` ile canlandırılana kadar `PoSeBanned` olur. `UPGRADE_HARDENING_V3` kapsamında banlı veya iptal edilmiş DIP3 Masternode'lar `VerifyAdamOrDip3Signature` içinde kesin başarısız sayılır (fail-closed).
- **V4 `PoM` Uptime Sıfırlaması (`src/kernel.cpp`)**: `UPGRADE_HARDENING_V4` (Yükseklik `1`) altında `CalculateMPAWeight()` içindeki KTDIP3 Proof of Masternode (`PoM`) staking uptime hesabı katı biçimde **`std::max(nRegisteredHeight, nPoSeRevivedHeight)`** yüksekliğinden başlar (V4 öncesi `std::min(nRegisteredHeight, nCoinHeight)` kuralının yerini alır).
- **ADAM ve Dinamik `2/3` BFT LLMQ Entegrasyonu**: `GetAdamMinerPool` aktif Masternode listesini doğrudan `CDeterministicMNManager::GetListForBlock(pindex)` üzerinden okur (`UPGRADE_DIP3` + `UPGRADE_HARDENING_V3` aktifleştiğinde yalnızca ping tabanlı eski MN'leri keser). `UPGRADE_HARDENING_V4` (Yükseklik `1`) kapsamında **`ADAM_VRF_KEY_MATURITY = 100` blok** olgunlaşması `(nOperatorKeyHeight >= 0 ? nOperatorKeyHeight : nRegisteredHeight)` üzerinden takip edilir; `SelectAdamNodes` ve `RankValidatorsByPoBLSDistance` en az `100` blok olgunlaşmış anahtarlara yeni kayıtlı veya yeni döndürülmüş anahtarların önünde öncelik verir. `ElectQuorumMembers` içinde `UPGRADE_HARDENING_V3`, `pindexPrev` ile `seedHeight - 1` `evodb` tohum-öncesi anlık görüntüsünü `proTxHash` sıralamasıyla kesiştirerek tohum sonrası manipülasyonu önler ve `<5` aktif MN yedeğini korur; kurul büyüklüğü dinamik olarak ölçeklenir (`GetTargetQuorumSize`: `5 -> 9 -> 11 -> 15`) ve `UPGRADE_HARDENING_V4` altında `llmq::GetQuorumThreshold(N, h) = floor(2N/3) + 1` ile `2/3` BFT nitelikli çoğunluğu (`5'te 4`, `9'da 7`, `11'de 8`, `15'te 11`; V4 öncesi `5'te 3`..`15'te 8`) ile `llmq::QUORUM_EQUIVOCATION_GUARD_SECS = 45` (V4 öncesi `30 sn`) zorunlu tutulur.
- **KTDIP3 Sıcak/Soğuk BLS Ayrımı (`UPGRADE_DIP3 = 1` Mainnet ve Testnet, `UPGRADE_HARDENING_V3 = 1` ve `UPGRADE_HARDENING_V4 = 1` Tüm Ağlar)**:
  - `UPGRADE_DIP3` aktifken deterministik Masternode'lar koordinatör VRF kanıtlarını, blok başlıklarını ve ADAM çözümlerini yalnızca kayıtlı `blsPubKeyOperator` anahtarlarıyla imzalar (`SignBLSOperatorOnly`, V3 altında `PinBLSPubKey` olmadan kesin boş `ecdsaSig` zorunludur); doğrulama etki alanıyla ayrılmış BLS DST'leri üzerinden `VerifyAdamOrDip3Signature` ve `GetMNByOperator` ile tekdüze yapılır.
  - Soğuk/Sıcak ayrımı `blsPoP` ile sorunsuz çalışır: Sıcak operatör `protx bls_generate` / `protx bls_pop` aracılığıyla `(opPub, opPoP)` (`<opPubHex(96)>:<opPoPHex(192)>` veya 288-hex) üretir ve operatör BLS gizli anahtarını ya da sahip ECDSA özel anahtarını paylaşmadan bu açık değerleri Soğuk sahip cüzdanına (`MasterNodeWizardDialog` / `protx register`) iletir.
  - Masternode ödülleri `AppendDip3OrDirectPayment` aracılığıyla doğrudan `scriptPayout` adresine (ve tanımlıysa `nOperatorReward` baz puanı oranında `scriptOperatorPayout` adresine) yönlendirilir.

### 3. RPC Arayüzü

`protx` RPC komutu şu alt komutları sunar:
- `protx bls_generate` — yeni bir BLS12-381 operatör `(secret, public, pop)` anahtar çifti üretir
- `protx bls_pop <op_pubkey_or_secret>` — bir operatör BLS gizli anahtarından veya aktif Sıcak MN'den 96 baytlık `PROTX_POP` imzası hesaplar (`masterNodeBLS` GUI konsollarından gizlenir)
- `protx register` (isteğe bağlı argüman veya `<opPub>:<opPoP>` / 288-hex girdisi ile v2 `blsPoP` destekli)
- `protx update_service`
- `protx update_registrar` (v2 `blsPoP` destekli; V4 altında operatör anahtarı değişikliklerinde `nProUpRegCooldownBlocks = 100` zorunludur)
- `protx revoke`
- `protx list`
- `protx info`
- `protx wallet_list` — `wallet.dat` (`mnmeta`) içinde saklanan soğuk kontrolcü takma adlarını, 15 onay bekleyen taslakları ve cüzdana ait zincir üstü Masternode'ları listeler
- `protx set_alias <outpoint_or_protxhash> <alias>` — `wallet.dat` içinde yerel kontrolcü takma adı atar veya günceller
- `protx remove_meta <outpoint_or_protxhash_or_alias> (unlock_collateral)` — yerel takma ad/taslak kaydını `wallet.dat`'tan siler (zincir üstünde aktif teminatların kilidini açmaz)

Ek olarak `UPGRADE_DIP3` aktifken `listmasternodes` ve `getmasternodecount` komutları da KTDIP3 deterministik Masternode'larını kapsar; `getmasternodestatus` ise aktif deterministik kayıtlara `" (KTDIP3)"` ekini döner. Eski `masternode.conf` yönetimi (`startmasternode`, `listmasternodeconf`, `createmasternodebroadcast`, `-mnconf`) kaldırılmıştır ve varsa açılışta bir kez otomatik olarak `wallet.dat` (`mnmeta`) içine aktarılır; `masternode.conf` dosyası ise düz metin WIF özel anahtarları sansürlenerek (`***REDACTED***`) `masternode.conf.migrated` (`chmod 0600`) olarak arşivlenir (sembolik bağlantılar ve normal olmayan dosyalar reddedilir).

### 4. Soğuk Cüzdan GUI Entegrasyonu ve Mempool Koruması

- **Gezinme Konumu ve Cüzdan İçinden Tek Tıkla Kayıt (`wallet.dat` Kalıcılığı)**: Üst gezinme çubuğunu sade tutmak amacıyla `MasterNodesWidget` varsayılan olarak **`MORE ▾ -> Masternodes`** (`Masternodes (4,200 KRISTA)`) menüsü altındadır; **Settings -> Display & Units** altındaki **`Pin advanced tabs to top bar`** seçeneğiyle `MASTERNODES` sekmesi anında üst çubuğa sabitlenebilir. `MasterNodeWizardDialog` 3. Adımda kullanıcı **Operator BLS Public Key (optional)** alanını boş bırakabilir (şifreli operatör ECDSA+BLS anahtar çifti `wallet.dat` içinde saklanır ve `activemasternode.conf` için `<alias> <privkey> <bls_privkey_hex>` üretilir) ya da ayrı sıcak düğümden aldığı 48 bayt hex `blsPubKeyOperator` anahtarını girebilir (`operatorPubKey=<hex>` çıktısı verilir). Taslaklar ve takma adlar doğrudan `wallet.dat` (`mnmeta`) içinde tutulur; `masternode.conf` dosyasına artık ihtiyaç yoktur.
- **Teminat Onay İlerleme Rozeti (`"Confirming (X/15)"`) ve Doğrudan UTXO Birleştirme**: `MNRow` / `MNModel`, `4.200 KRISTA` teminatı `15` onaya ulaşana kadar **`"Confirming (X/15)"`** durum rozetini gösterir; teminat olgunlaştığında `MasterNodesWidget` içinde **Start** / **Start All** / **Start Inactive** butonuna basılması (veya olgunlaşmış teminatla sihirbazın tamamlanması) RPC Debug Console / CLI gerekmeden `RegisterDip3MasternodeFromWallet` üzerinden doğrudan `ProRegTx` işlemini imzalayıp yayınlar. Para gönderme (`Send`) veya Masternode işlemlerinde parçalanmış UTXO'lar işlem boyut sınırını aşarsa `src/qt/kristatech/guitransactionsutils.cpp` doğrudan pencere içinde **UTXO Birleştirme (`Consolidate Fragmented UTXOs`)** sunar (`-spendzeroconfchange=1` iken `<= 90 KB` için işlemi anında otomatik devam ettirir, daha büyükse 1 blok onayı beklemeyi yönlendirir).
- **Mempool ve Blok Şablonu Çakışma Koruması**: `AcceptToMemoryPool`, aynı `collateralOutpoint` veya `blsPubKeyOperator` için mempool'da bekleyen mükerrer `ProRegTx` işlemlerini `bad-protx-mempool-conflict` ile reddeder; `CreateNewBlock` ise aynı blok içine çakışan `ProRegTx` / `ProUp*` işlemlerini almaz.
- **Açılışta Otomatik Teminat Kilidi ve Canlı Görünüm**: `AppInit2` cüzdana ait tüm zincir üstü KTDIP3 teminatlarını açılışta otomatik kilitler; `WalletModel::getMNCollateralCandidate` kayıtlı DIP3 teminatlarını atlar; `MNModel` mempool'da bekleyen `ProRegTx` için `PRE_ENABLED` gösterir ve `TopBar` / `StatsWidget` / `SettingsConsoleWidget` / `ClientModel` sayaçları KTDIP3 verilerini yansıtır.
