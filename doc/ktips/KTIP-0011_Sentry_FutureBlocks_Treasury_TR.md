```
KTIP: 0011
Başlık: Sentry Düğüm DDoS Kalkanı, Sınırlı Gelecek Blok Kuyruğu ve LLMQ Yönetişimli Geliştirici Hazinesi
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Active
Tür: Standard Track (Network / Governance)
Oluşturulma Tarihi: 2026-11-09
```

## Özet
Bu öneri, KRISTA Core içerisindeki üç altyapı ve yönetişim sıkılaştırma mekanizmasını tanımlar: (1) Masternode ve Doğrulayıcı düğümlerini yüksek erişilebilirlikli DDoS korumalı aktarım düğümleri arkasında izole eden **Sentry Düğüm Mimarisi (`-sentrynode=1`)**, (2) küçük saat kaymalarını bellek tükenmesine yol açmadan yöneten **Kriptografik Ön-Doğrulamalı ve Sınırlı Gelecek Blok Kuyruğu (`mapFutureBlocks`, `SEC-04`)** ve (3) zincir üstü Geliştirici Fonu (`DeveloperFundAddress()`) harcamaları için **LLMQ 5'te 3 Çoğunluklu Eşik Yönetişimi (`SEC-09`)**.

## Motivasyon
1. **Doğrulayıcı IP Koruması**: Aktif Masternode ve Doğrulayıcı düğümleri konsensüs imzalama anahtarlarını (`activemasternode.conf`) taşır. Bu düğümlerin genel IP adreslerinin doğrudan güvenilmeyen eşlere açık olması, onları hacimsel DDoS saldırılarına karşı hedef haline getirir.
2. **Gelecek Blok Kuyruğu Doldurma (Queue Stuffing) DoS Saldırısı (`SEC-04`)**: Düğümlerin küçük NTP saat farklarını tolere edebilmesi için yerel ayarlanmış saatten biraz ileride olan (`time-too-new`) blokları geçici kuyruğa alması gerekir. Kriptografik ön doğrulama ve boyut sınırı olmadığında bir saldırgan `mapFutureBlocks` kuyruğunu sahte uzak-gelecek bloklarıyla doldurarak meşru blokların kuyruğa alınmasını engelleyebilir.
3. **Güven Gerektirmeyen (Trustless) Geliştirici Hazinesi Harcaması (`SEC-09`)**: %7,0 Geliştirici Hazinesi protokol geliştirme fonlarını zincir üstünde biriktirir. `DeveloperFundAddress()` adresinden yapılan harcamalar (çok girdili işlemler dahil) tek taraflı bir anahtar yerine aktif LLMQ kurulunun kriptografik onayını gerektirmelidir.

## Teknik Özellikler

### 1. Sentry Düğüm Modu (`-sentrynode=1`)
Düğüm `-sentrynode=1` (`src/init.cpp` ve `src/net.cpp` içindeki `fSentryNode`) parametresiyle başlatıldığında:
* **Yüksek Gelen Bağlantı Kapasitesi**: Yüksek kapasiteli bir P2P aktarım merkezi olarak çalışması için varsayılan `-maxconnections` sınırı otomatik olarak `256` değerine yükseltilir.
* **Cüzdansız ve İmzasız İzolasyon**: Sentry düğümleri staking, madencilik veya Masternode özel anahtarları olmadan çalışır; genel ağ eşleri ile `-addnode` / `-connect` üzerinden bağlı özel doğrulayıcı düğümler arasında `CMPCTBLOCK`, `ADAMSOL`, `PROPOSEBLOCK` ve `QUORUMSIGSHARE` mesajlarını aktarır.

---

### 2. Sınırlı ve Ön-Doğrulamalı Gelecek Blok Kuyruğu (`mapFutureBlocks` — `SEC-04`)
`AcceptBlock` veya `AcceptBlockHeader` (`src/main.cpp`), zaman damgası `GetAdjustedTime() + 300` saniye içinde olan ve `"time-too-new"` hatası veren bir blokla karşılaştığında:
1. **Kriptografik Ön-Doğrulama**: Blok `mapFutureBlocks` kuyruğuna eklenmeden önce şu kontroller zorunlu tutulur:
   - `block.nVersion >= 11` için: `!block.vAdamCoordinatorSig.empty() && !block.vAdamVRFProof.empty()`.
   - `block.nVersion >= 12` (`UPGRADE_MODELD` aktifken) için: `!block.vQuorumSig.empty() && block.vQuorumSig.size() <= 4096`.
2. **Sınırlı Tahliye (`MAX_FUTURE_BLOCKS = 64`)**: `mapFutureBlocks.size() >= 64` olduğunda, yeni gelen bloğun zaman damgası daha erkense kuyruktaki en uzak zaman damgalı (`max_element`) kayıt tahliye edilir; aksi halde gelen blok reddedilir.
3. **Periyodik İşleme**: Zamanlayıcı (`ProcessFutureBlocks`), `GetAdjustedTime()` bloğun zaman damgasına ulaştığında kuyruktaki blokları yeniden işler.

---

### 3. LLMQ 5'te 3 Çoğunluklu Geliştirici Hazinesi Yönetişimi (`SEC-09`)
`Params().GetConsensus().DeveloperFundAddress()` (Mainnet üzerinde `KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP`, Testnet üzerinde `ktEooARF3HdV8w59LUc5jaHrS7GBL7974Uy`) adresine ait bir veya daha fazla UTXO'yu harcayan her işlem `CheckInputs` ve `ConnectBlock` (`src/main.cpp`) tarafından şu şekilde doğrulanır:

1. **Çok Girdili `txTmp.GetHash()` Normalizasyonu (`SEC-09`)**:
   - `DeveloperFundAddress()` harcayan her girdi, `scriptSig` alanında üç yığın (stack) verisi taşır: `<ecdsaSig> <pubKey> <qsig>`.
   - LLMQ kurulunun imzaladığı kanonik işlem özetini (`txHash`) hesaplamak için doğrulayıcı, `DeveloperFundAddress()` harcayan **tüm** girdilerden 3. push (`<qsig>`) verisinin çıkarıldığı (yalnızca `<ecdsaSig> <pubKey>` bırakıldığı) normalize edilmiş bir `txTmp` kopyası oluşturur ve `txHash = txTmp.GetHash()` değerini hesaplar.
2. **LLMQ 5'te 3 Kurul İmzası Doğrulaması**:
   - `<qsig>` bayt dizisi `CQuorumSignature` (`qsig.blockHash == txHash`) nesnesine deserialize edilir.
   - `qsig.Verify(quorum)` fonksiyonu, mevcut blok yüksekliğindeki aktif LLMQ kurulunun en az **5'te 3** üyesinin deterministik BLS12-381 anahtarlarıyla `txHash` özetini imzaladığını (`CBLSSignature::Aggregate` eşleme kontrolüyle) doğrular.
   - Herhangi bir hazine girdisinde `qsig.Verify(quorum)` başarısız olursa işlem `bad-dev-fund-quorum-sig` (`Misbehaving(100)`) ile reddedilir.

## Geriye Dönük Uyumluluk
* Sentry Düğüm modu (`-sentrynode=1`) tüm P2P eşleriyle uyumlu, isteğe bağlı bir çalışma zamanı yapılandırma bayrağıdır.
* `SEC-04` ve `SEC-09` kuralları Mainnet, Testnet ve Regtest üzerinde `v1.4.5.9` sürümünde aktiftir.

## Referans Uygulama
* Sentry Düğüm yapılandırması: `src/init.cpp` ve `src/net.cpp`.
* Gelecek Blok Kuyruğu (`mapFutureBlocks`): `src/main.cpp`.
* Geliştirici Hazinesi LLMQ doğrulaması ve çok girdili normalizasyon: `src/main.cpp` içindeki `CheckInputs` ve `ConnectBlock` ile `src/llmq.cpp` içindeki `CQuorumSignature::Verify`.
