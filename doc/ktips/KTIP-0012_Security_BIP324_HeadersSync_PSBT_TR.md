```
KTIP: 0012
Başlık: Taproot/CSV/BIP68 Konsensüs Zorunluluğu, P2P/DoS Sıkılaştırması, BIP324 v2 Şifreli Taşıma, HeadersSyncState ve PSBT
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Aktif
Tür: Standart Takip (Konsensüs / Ağ / Cüzdan)
Oluşturulma Tarihi: 2026-11-16
```

## Özet
Bu öneri, KRISTA Core için kapsamlı bir güvenlik ve protokol modernizasyonu paketi tanımlar: (1) **Konsensüs Betik ve Göreceli Zaman Kilidi Zorunluluğu** (`ConnectBlock` içinde `SCRIPT_VERIFY_TAPROOT`, `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` ve BIP68 `CheckSequenceLocks`), (2) **P2P ve RPC DoS Sıkılaştırması** (`GETDATA` kuyruk sınırı, eş-bazlı `CMPCTBLOCK`/`BLOCKTXN` izolasyonu, `ADDR` token-bucket hız sınırlaması, BIP133 `FEEFILTER`, `-blocksonly` ve tuzlanmış HMAC-SHA256 `-rpcauth`), (3) **`HeadersSyncState`** düşük zorluklu başlık ön-senkronizasyon tamponu, (4) **BIP324 v2 Şifreli P2P Taşıma** (otomatik v1 geri dönüşlü `ElligatorSwift` + `FSChaCha20Poly1305`) ve (5) **BIP174 Kısmen İmzalanmış KristaTech İşlemleri (PSBT)**.

## Motivasyon
1. **Konsensüs-Politika Farkının Kapatılması**: Daha önce `SCRIPT_VERIFY_TAPROOT` ve `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` yalnızca mempool politikasında (`STANDARD_SCRIPT_VERIFY_FLAGS`) uygulanıyor, `ConnectBlock()` içinde zorunlu tutulmuyor ve kazılan bloklarda zaman kilidi atlama / harcanabilirlik riski oluşturuyordu.
2. **P2P Bellek Tükenmesi ve Zehirleme Koruması**: Sınırsız `vRecvGetData` kuyrukları (CVE-2018-17145 deseni), global `mapPartiallyDownloadedBlocks` durumu (CVE-2023-39910 / CVE-2024-35202 alanı), hız sınırsız `ADDR` taşmaları (CVE-2021-3401) ve ön-doğrulamasız düşük zorluklu `HEADERS` saldırıları düğümleri bellek tükenmesine açık bırakıyordu.
3. **Ağ Gizliliği ve Soğuk İmzalama Uyumluluğu**: Şifresiz P2P trafiği pasif dinleyicilere işlem kaynağını açığa çıkarırken, donanım/çevrimdışı cüzdan akışları standart BIP174 PSBT desteğine ihtiyaç duyar.

## Teknik Özellikler

### 1. Konsensüs Zorunluluğu: Taproot, CSV ve BIP68 (`ConnectBlock` / `AcceptToMemoryPoolWorker`)
* `ConnectBlock()` (`src/main.cpp`) artık tüm coinbase/coinstake dışı işlemlerde `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY | SCRIPT_VERIFY_TAPROOT` (ve BIP65 aktifken `SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY`) bayraklarını zorunlu kılar.
* **BIP68 Göreceli Zaman Kilitleri (`CheckSequenceLocks`)**: Hem `AcceptToMemoryPoolWorker` hem de `ConnectBlock`, `tx.nVersion >= 2` olan ve `CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG` taşımayan girdilerde blok yüksekliği ve medyan zaman (MTP) tabanlı göreceli zaman kilitlerini doğrular.

### 2. P2P ve RPC DoS Sıkılaştırması
* **`GETDATA` Kuyruk Sınırı**: `ProcessMessage(NetMsgType::GETDATA)` toplam kuyruk boyutunu `MAX_INV_SZ` (`50.000`) ile sınırlar; aşım durumunda `Misbehaving(pfrom->GetId(), 20)` uygular.
* **Eş-Bazlı Compact Blocks (`CMPCTBLOCK` / `BLOCKTXN`)**: `mapPartiallyDownloadedBlocks` artık `std::pair<NodeId, uint256>` ile eş-bazlı tutulur ve `FinalizeNode()` içinde temizlenir. Böylece bir eşin başlattığı compact block başka bir eşin `BLOCKTXN` mesajıyla bozulamaz.
* **`ADDR` Token-Bucket Hız Sınırlaması**: `CNode` üzerinde `nAddrTokenBucket` (kapasite `1.000,0`, dolum `0,1` adres/sn) ile beyaz listede olmayan eşlerin aşırı `ADDR` akışı düşürülür.
* **BIP133 `FEEFILTER` ve `-blocksonly`**: `NetMsgType::FEEFILTER` periyodik olarak yayınlanır; `SendMessages` içinde `pto->minFeeFilter` altındaki veya `-blocksonly` modundaki `MSG_TX` duyuruları filtrelenir.
* **Tuzlanmış `-rpcauth`**: `src/httprpc.cpp` üzerinde `<KULLANICI>:<TUZ>$<ÖZET>` biçimindeki kimlik bilgileri `CHMAC_SHA256` ve `TimingResistantEqual` ile doğrulanır.

### 3. `HeadersSyncState` Düşük Zorluklu Başlık Ön-Senkronizasyonu
* `mapHeadersSyncState` (`src/main.cpp`, `FinalizeNode()` ile temizlenir), ebeveyn zincir işi `chainActive.Tip()->nChainWork / 2` altında kaldığında gelen `HEADERS` mesajlarını `MAX_HEADERS_PRESYNC_BUFFER` (`16.384` başlık) sınırına kadar geçici tamponda biriktirir.
* Başlıklar süreklilik ve kümülatif `GetBlockProof()` işi açısından doğrulanıp yeterli işe ulaştığında `AcceptBlockHeader()` üzerinden `mapBlockIndex`'e aktarılır. Yeterli iş olmadan tampon dolarsa `Misbehaving(20)` uygulanır ve tampon atılır.

### 4. BIP324 v2 Şifreli P2P Taşıma (`NODE_P2P_V2`)
* `-v2transport` aktifken (varsayılan: `1`) düğümler `NODE_P2P_V2 = (1 << 11)` servis bitini duyurur.
* `BIP324Cipher` (`src/bip324.cpp`, `src/crypto/chacha20poly1305.cpp`), `secp256k1_ellswift_create` ve `secp256k1_ellswift_xdh` (`secp256k1_ellswift_xdh_hash_function_bip324`) ile paylaşılan gizli anahtarı üretir; HKDF-SHA256 (`"bitcoin_v2_shared_secret" || magic`) üzerinden `FSChaCha20` (uzunluk şifreleme) ve her `224` pakette anahtar yenileyen `FSChaCha20Poly1305` (AEAD) anahtarlarını türetir.
* **Otomatik v1 Geri Dönüşü**: Gelen bağlantılar ilk `4` bayta bakar (`fV2AutoDetect`); `Params().MessageStart()` ile eşleşirse sorunsuz biçimde v1 düz metin akışına geçer, giden bağlantılar ise karşı eş `NODE_P2P_V2` duyuruyorsa v2 el sıkışmasını başlatır.

### 5. PSBT (BIP174 Kısmen İmzalanmış İşlemler)
* `src/psbt.h` ve `src/psbt.cpp` içinde `PartiallySignedTransaction`, `PSBTInput`, `PSBTOutput`, Base64/ikili serileştirme (`psbt\xff`), `Merge`, `FinalizePSBT` ve `SignPSBTInput` eklendi.
* RPC komutları: `createpsbt`, `converttopsbt`, `decodepsbt`, `combinepsbt`, `utxoupdatepsbt`, `analyzepsbt`, `finalizepsbt` ve `walletprocesspsbt`.

## Geriye Dönük Uyumluluk
* `ConnectBlock` üzerinde `SCRIPT_VERIFY_TAPROOT`, `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` ve `BIP68` zorunluluğu konsensüs kuralı değişikliğidir; tüm Seed düğümlerinde eşzamanlı güncelleme ve temiz Genesis sıfırlaması ile devreye alınmıştır.
* BIP324 v2 taşıma ve BIP133 `FEEFILTER` P2P katmanında `NODE_P2P_V2` ve v1 magic otomatik algılaması sayesinde geriye dönük uyumludur.

## Referans Uygulama
* Konsensüs, `HeadersSyncState`, `GETDATA`/`ADDR`/`CMPCTBLOCK`/`FEEFILTER`: `src/main.cpp`, `src/protocol.h`, `src/protocol.cpp`, `src/init.cpp`.
* BIP324 v2 Taşıma: `src/bip324.h`, `src/bip324.cpp`, `src/crypto/chacha20poly1305.h`, `src/crypto/chacha20poly1305.cpp`, `src/net.h`, `src/net.cpp`.
* RPC Yetkilendirme ve PSBT: `src/httprpc.cpp`, `src/psbt.h`, `src/psbt.cpp`, `src/rpc/rawtransaction.cpp`, `src/wallet/rpcwallet.cpp`, `src/rpc/server.cpp`.
