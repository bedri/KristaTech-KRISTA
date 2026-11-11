```
KTIP: 0011
Başlık: Sentry Düğüm DDoS Kalkanı, Sınırlı Gelecek Blok Kuyruğu ve LLMQ Yönetişimli Geliştirici Hazinesi
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Aktif
Tür: Standart Takip (Ağ / Yönetişim)
Oluşturulma Tarihi: 2026-11-09
```

## Özet
Bu öneri, KRISTA Core içerisindeki üç altyapı ve yönetişim sıkılaştırma mekanizmasını tanımlar: (1) Masternode ve Doğrulayıcı düğümlerini yüksek erişilebilirlikli DDoS korumalı aktarım düğümleri arkasında izole eden **Sentry Aktarım Düğümü Mimarisi**, (2) küçük saat kaymalarını bellek tükenmesine yol açmadan yöneten **Yapısal Ön-Doğrulamalı ve Sınırlı Gelecek Blok Kuyruğu (`mapFutureBlocks`, `SEC-04`)** ve (3) zincir üstü Geliştirici Fonu (`Params().DeveloperFundAddress()`) harcamaları için **LLMQ 5'te 3 Çoğunluklu Eşik Yönetişimi (`SEC-09`)**.

## Motivasyon
1. **Doğrulayıcı IP Koruması**: Aktif Masternode ve Doğrulayıcı düğümleri konsensüs imzalama anahtarlarını (`activemasternode.conf`) taşır. Bu düğümlerin genel IP adreslerinin doğrudan güvenilmeyen eşlere açık olması, onları hacimsel DDoS saldırılarına karşı hedef haline getirir.
2. **Gelecek Blok Kuyruğu Doldurma (Queue Stuffing) DoS Saldırısı (`SEC-04`)**: Düğümlerin küçük NTP saat farklarını tolere edebilmesi için yerel ayarlanmış saatten biraz ileride olan (`time-too-new`) blokları geçici kuyruğa alması gerekir. Yapısal başlık ön-doğrulaması, `+300 sn` üst sınırı ve boyut sınırı olmadığında bir saldırgan `mapFutureBlocks` kuyruğunu sahte uzak-gelecek bloklarıyla doldurarak meşru blokların kuyruğa alınmasını engelleyebilir.
3. **Güven Gerektirmeyen (Trustless) Geliştirici Hazinesi Harcaması (`SEC-09`)**: %7,0 Geliştirici Hazinesi protokol geliştirme fonlarını zincir üstünde biriktirir. `Params().DeveloperFundAddress()` adresinden yapılan harcamalar (çok girdili işlemler dahil) tek taraflı bir anahtar yerine hem Spork ECDSA yetkisini hem de aktif LLMQ kurulunun 5'te 3 kriptografik onayını gerektirmelidir.

## Teknik Özellikler

### 1. Sentry Aktarım Düğümü Mimarisi
Sentry Düğümleri, standart KRISTA Core P2P ve izolasyon parametreleriyle (`listen=1`, `maxconnections=256`, `masternode=0`, `staking=0`, `gen=0`, `disablewallet=1`; bkz. `doc/Sentry_Node_Setup_TR.md`) yapılandırılan genel ağ aktarım düğümleridir; özel doğrulayıcı düğümler ise yalnızca kendi Sentry eşlerine bağlanır (`listen=0`, `connect=<sentry_ip>:27999`):
* **Yüksek Gelen Bağlantı Kapasitesi**: `maxconnections=256` ayarı, her Sentry Düğümünün yüksek kapasiteli bir P2P aktarım merkezi olarak çalışmasını sağlar.
* **Cüzdansız ve İmzasız İzolasyon**: Sentry düğümleri staking, madencilik veya Masternode özel anahtarları olmadan çalışır; genel ağ eşleri ile özel doğrulayıcı düğümler arasında `CMPCTBLOCK`, `ADAMSOL`, `PROPOSEBLOCK`, `QUORUMSIGSHARE`, `POSCOORDREQ` ve `POSCOORDSIG` mesajlarını aktarır.

---

### 2. Sınırlı ve Kriptografik Ön-Doğrulamalı Gelecek Blok Kuyruğu (`mapFutureBlocks` — `SEC-04` / `R42-F1`)
`ProcessMessage` (`src/main.cpp` içindeki `"headers"` veya `"block"`), zaman damgası `GetAdjustedTime() + 300` saniye içinde olan (hem `HEADERS` hem de `BLOCK` mesajlarında uygulanır) ve `"time-too-new"` hatası veren bir başlık/blokla karşılaştığında:
1. **Kriptografik Ön-Doğrulama (`IsFutureBlockPreVerified(header, pindexPrev)`)**: Blok `mapFutureBlocks` kuyruğuna eklenmeden önce `mapBlockIndex` üzerinde bilinen bir ebeveyn `pindexPrev` (`header.GetBlockTime() > pindexPrev->MinPastBlockTime()`) zorunlu tutulur ve şu kriptografik denetimler yürütülür:
   - `header.nVersion >= 11` için: `vAdamCoordinatorSig` ve `vAdamVRFProof` alanlarının `1..512` bayt aralığında olması, `GetAdamSeed(pindexPrev)` ve `header.GetHash()` üzerinde `VerifySelfContainedBLSSignedData` (`vrfBlsPk == coordBlsPk`) doğrulaması ve `GetAdamMinerPool(nCandidateHeight - 1, ...)` içindeki yetkili bir doğrulayıcıya karşı secp256k1 sahiplik doğrulaması (`VerifyAdamVRFProof` + `VerifyAdamCoordinatorSig`).
   - `header.nVersion >= 12` için: `IsModelDActive(nCandidateHeight)` aktifken `vQuorumSig` alanının `1..4096` bayt aralığında olması ve aktif LLMQ kurulu üzerinde 5'te 3 `CQuorumSignature::Verify` (`qsig.blockHash == header.GetHash()`) doğrulaması (Model D öncesinde `<= 4096` bayt).
2. **Sınırlı Tahliye (`64` Kayıt), Tam Blok Koruması ve Ertelenmiş `AddInventoryKnown`**: `mapFutureBlocks` en fazla `64` kayıt tutar. Gelen `HEADERS` mesajları kuyrukta bekleyen tam bir `CBlock` (`!vtx.empty()`) kaydını ezmez; gelen tam bir `BLOCK` mesajı ise mevcut başlık kaydını yerinde tam bloğa yükseltir. Yeni bir hash için `mapFutureBlocks.size() >= 64` olduğunda, zaman damgası daha erkense kuyruktaki en uzak zaman damgalı kayıt tahliye edilir; `AlreadyHave(MSG_BLOCK)` tam blokları mevcut bildirirken `pfrom->AddInventoryKnown(inv)` çağrısı `ProcessFutureBlocks` içinde `ProcessNewBlock` çalışana kadar ertelenir.
3. **Periyodik İşleme ve Tam Konsensüs Doğrulaması (`ProcessFutureBlocks`)**: Zamanlayıcı, ebeveyni bilinmeyen `3.600` saniyeden eski yetim kayıtları temizler (`GetAdjustedTime() - block.nTime > 3600`) ve zamanı gelen blokları (`block.nTime <= pindexPrev->MaxFutureBlockTime()`) tam kriptografik konsensüs doğrulamasından (`ProcessNewBlockHeaders` / `ProcessNewBlock`) geçirir.

---

### 3. LLMQ 5'te 3 Çoğunluklu Geliştirici Hazinesi Yönetişimi (`SEC-09`)
`consensus.nTreasuryGovernanceStartHeight` yüksekliğinden (`CMainParams` / Mainnet üzerinde `2.880`, `CTestNetParams` / Testnet üzerinde `200`, Regtest üzerinde `100`; bu yüksekliğin altındaki harcamalar `bad-txns-spend-devfund-early` ile reddedilir) itibaren `Params().DeveloperFundAddress()` (Mainnet üzerinde `KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP`, Testnet üzerinde `ktEooARF3HdV8w59LUc5jaHrS7GBL7974Uy`) adresine ait bir veya daha fazla UTXO'yu harcayan her işlem `CheckInputs` ve `ConnectBlock` (`src/main.cpp`) tarafından şu şekilde doğrulanır:

1. **Çok Girdili `txTmp.GetHash()` Normalizasyonu (`SEC-09`)**:
   - `Params().DeveloperFundAddress()` harcayan her girdi, `scriptSig` alanında üç yığın (stack) verisi taşır: `<ecdsaSig> <pubKey> <qsig>` (3'ten az veri `bad-txns-missing-quorum-sig`, bozuk serileştirme ise `bad-txns-malformed-quorum-sig` ile reddedilir).
   - LLMQ kurulunun imzaladığı kanonik işlem özetini (`txHash`) hesaplamak için doğrulayıcı, `Params().DeveloperFundAddress()` harcayan **tüm** girdilerden 3. push (`<qsig>`) verisinin çıkarıldığı (yalnızca `<ecdsaSig> <pubKey>` bırakıldığı) normalize edilmiş bir `txTmp` kopyası oluşturur ve `txHash = txTmp.GetHash()` değerini hesaplar.
2. **LLMQ 5'te 3 Kurul İmzası ve Spork Anahtarı Doğrulaması**:
   - `<qsig>` bayt dizisi `CQuorumSignature` nesnesine deserialize edilir ve `qsig.blockHash = txTmp.GetHash()` atanır.
   - `qsig.Verify(quorum)` fonksiyonu, harcama yüksekliğindeki aktif LLMQ kurulunun en az **5'te 3** üyesinin BLS12-381 anahtarlarıyla `txHash` özetini imzaladığını (`CBLSSignature::Aggregate` eşleme kontrolüyle) doğrular; başarısız olursa işlem `bad-txns-invalid-quorum-sig` (`Misbehaving(100)`) ile reddedilir.
   - Son olarak `<ecdsaSig> <pubKey>` (`scriptSigOverride`), `CScriptCheck` tarafından `Params().GetConsensus().strSporkPubKey` açık anahtarına karşı doğrulanır.

## Geriye Dönük Uyumluluk
* Sentry Düğüm kurulumu tüm P2P eşleriyle uyumlu standart KRISTA Core yapılandırma parametrelerini kullanır.
* `SEC-04` ve `SEC-09` kuralları Mainnet, Testnet ve Regtest üzerinde `v1.4.5.9` sürümünde aktiftir.

## Referans Uygulama
* Sentry Düğüm kurulum profili: `doc/Sentry_Node_Setup_TR.md`, `src/init.cpp` ve `src/net.cpp`.
* Gelecek Blok Kuyruğu (`IsFutureBlockPreVerified`, `mapFutureBlocks`, `ProcessFutureBlocks`): `src/main.cpp`.
* Geliştirici Hazinesi LLMQ doğrulaması ve çok girdili normalizasyon: `src/main.cpp` içindeki `CheckInputs` ve `ConnectBlock` ile `src/llmq.cpp` içindeki `CQuorumSignature::Verify`.
