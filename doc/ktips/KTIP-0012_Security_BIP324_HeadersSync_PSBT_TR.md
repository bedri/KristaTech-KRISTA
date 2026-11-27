```
KTIP: 0012
Başlık: Taproot/CSV/BIP68 Konsensüs Zorunluluğu, P2P/DoS Sıkılaştırması, BIP324 v2 Şifreli Taşıma, HeadersSyncState ve PSBT
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Aktif
Tür: Standart Takip (Konsensüs / Ağ / Cüzdan)
Oluşturulma Tarihi: 2026-11-16
```

## Özet
Bu öneri, KRISTA Core (`v1.5.9.0`, `PROTOCOL_VERSION = 70983`, `MIN_PEER_PROTO_VERSION = 70983`) için kapsamlı bir güvenlik ve protokol modernizasyonu paketi tanımlar: (1) **Konsensüs Betik ve Göreceli Zaman Kilidi Zorunluluğu** (`SCRIPT_VERIFY_TAPROOT`, `SCRIPT_VERIFY_TAPROOT_BIP341`, `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY`, temel `SCRIPT_VERIFY_NULLFAIL`, BIP-341 `ComputeTapBranchHash` `memcmp`, Tapscript içinde ECDSA BIP-341 sighash, Schnorr sigcache ve BIP68 `CheckSequenceLocks`), (2) **P2P ve RPC/HTTP DoS Sıkılaştırması** (`GETDATA` kuyruk sınırı, eş-bazlı `CMPCTBLOCK`/`BLOCKTXN` izolasyonu ve boş `shorttxids` koruması, mükerrer `VERACK` koruması, `CBlockHeader` ADAM serileştirme sınırları, `ADDR` token-bucket hız sınırlaması, BIP133 `FEEFILTER`, `-blocksonly`, tuzlanmış `-rpcauth` ve HTTP `Host` DNS-rebinding `403` koruması), (3) **`HeadersSyncState`** düşük zorluklu başlık ön-senkronizasyon tamponu (`8 MiB` bayt sınırı ve sıfır olmayan `txcount` reddi), (4) **BIP324 v2 Şifreli P2P Taşıma** (otomatik v1 geri dönüşü ve decoy taşması bağlantı kesme korumalı `ElligatorSwift` + `FSChaCha20Poly1305`) ve (5) **Sıkılaştırılmış BIP174 PSBT** (`4.096` girdi/çıktı sınırı, UTXO/üst-veri birleştirme çakışması reddi, Taproot ve `P2SH` multisig imzalama/sonuçlandırma).

## Motivasyon
1. **Konsensüs-Politika Farkının Kapatılması**: Daha önce `SCRIPT_VERIFY_TAPROOT` ve `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` yalnızca mempool politikasında (`STANDARD_SCRIPT_VERIFY_FLAGS`) uygulanıyor, `ConnectBlock()` içinde zorunlu tutulmuyor ve kazılan bloklarda zaman kilidi atlama / harcanabilirlik riski oluşturuyordu.
2. **P2P Bellek Tükenmesi ve Zehirleme Koruması**: Sınırsız `vRecvGetData` kuyrukları (CVE-2018-17145 deseni), global `mapPartiallyDownloadedBlocks` durumu (CVE-2023-39910 / CVE-2024-35202 alanı), hız sınırsız `ADDR` taşmaları (CVE-2021-3401) ve ön-doğrulamasız düşük zorluklu `HEADERS` saldırıları düğümleri bellek tükenmesine açık bırakıyordu.
3. **Ağ Gizliliği ve Soğuk İmzalama Uyumluluğu**: Şifresiz P2P trafiği pasif dinleyicilere işlem kaynağını açığa çıkarırken, donanım/çevrimdışı cüzdan akışları standart BIP174 PSBT desteğine ihtiyaç duyar.

## Teknik Özellikler

### 1. Konsensüs Zorunluluğu: Taproot (`BIP-341`), CSV, `NULLFAIL` ve BIP68 (`ConnectBlock` / `AcceptToMemoryPoolWorker`)
* `ConnectBlock()` ve `AcceptToMemoryPoolWorker()` (`src/main.cpp`), tüm coinbase/coinstake dışı işlemlerde `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY | SCRIPT_VERIFY_TAPROOT | SCRIPT_VERIFY_TAPROOT_BIP341` (ve BIP65 aktifken `SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY`) bayraklarını zorunlu kılar.
* **V4 BIP-341 Betik ve İmza Sıkılaştırması (`UPGRADE_HARDENING_V4`, Yükseklik `1`)**:
  - **Sözlük Sıralı `ComputeTapBranchHash` (`src/script/interpreter.cpp`)**: Taproot dal alt özetlerini BIP-341 uyumlu bayt düzeyinde `std::memcmp(left.begin(), right.begin(), 32) < 0` ile sıralar (V4 öncesi little-endian `uint256::operator<` yerine).
  - **Tapscript İçinde ECDSA BIP-341 Sighash**: `SCRIPT_VERIFY_TAPROOT_BIP341` aktifken `SIGVERSION_TAPSCRIPT` içindeki 33/65 baytlık ECDSA anahtarları, `tapleaf_hash` ve `codesep_pos` değerlerini taahhüt eden kanonik `SignatureHashTaproot(..., SIGVERSION_TAPSCRIPT, ...)` özetini hesaplar (V4 öncesi `SIGVERSION_BASE` DER geri dönüşü yerine).
  - **Schnorr SigCache ve Temel `SCRIPT_VERIFY_NULLFAIL`**: `CachingTransactionSignatureChecker::VerifySchnorrSignature` (`src/script/sigcache.cpp`), BIP-340 Schnorr imzalarını `'S'` alan etiketi altında önbelleğe alır (`TapscriptSignatureChecker` üzerinden iletilir) ve `EvalScript` başarısız boş-olmayan imzaları hem `SIGVERSION_TAPSCRIPT` içinde hem de temel `OP_CHECKSIG` betiklerinde `SCRIPT_VERIFY_NULLFAIL` aktifken `SCRIPT_ERR_SIG_NULLFAIL` ile reddeder.
* **BIP68 Göreceli Zaman Kilitleri (`CheckSequenceLocks`)**: Hem `AcceptToMemoryPoolWorker` hem de `ConnectBlock`, `tx.nVersion >= 2` olan ve `CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG` taşımayan girdilerde blok yüksekliği ve medyan zaman (MTP) tabanlı göreceli zaman kilitlerini doğrular.

### 2. Faz 1 P2P, HTTP ve RPC DoS Sıkılaştırması
* **`GETDATA` Kuyruk Sınırı**: `ProcessMessage(NetMsgType::GETDATA)` toplam kuyruk boyutunu `MAX_INV_SZ` (`50.000`) ile sınırlar; aşım durumunda `Misbehaving(pfrom->GetId(), 20)` uygular.
* **Eş-Bazlı Compact Blocks (`CMPCTBLOCK` / `BLOCKTXN`) ve Boş-ShortTxIDs Koruması**: `mapPartiallyDownloadedBlocks` artık `std::pair<NodeId, uint256>` ile eş-bazlı tutulur ve `FinalizeNode()` içinde temizlenir. Yalnızca compact block'u başlatan eş `BLOCKTXN` kaydını tamamlayabilir. Ayrıca (`src/net_processing.cpp`), `CMPCTBLOCK` eş-bazlı kayan pencere hız sınırı uygular ve `cmpct.shorttxids.empty()` olduğunda `mempool.mapTx` taramasını tamamen atlar (boş değilken yalnızca istenen kısa kimlikleri tarar).
* **`CBlockHeader` ADAM Serileştirme Sınırları (`src/primitives/block.h`)**: `SerializeAdamMpaFields` açma (unserialization) sırasında katı `CompactSize` tavanları zorunlu kılar (`<= 32` `vAdamMiners`, `<= 32` `vAdamSolutions`, çözüm / `vAdamVRFProof` / `vAdamCoordinatorSig` başına `<= 512` bayt, `vQuorumSig` için `<= 4096` bayt).
* **Mükerrer `VERACK` Koruması (`src/net_processing.cpp`)**: `pfrom->fSuccessfullyConnected` zaten `true` olduğunda mükerrer `NetMsgType::VERACK` mesajlarını yok sayar ve başlangıç `ADAMSOL` eşitlemesini atomik `fSentInitialAdamSolSync` ile korur.
* **`ADDR` Token-Bucket Hız Sınırlaması**: `CNode` üzerinde `nAddrTokenBucket` (kapasite `1.000,0`, dolum `0,1` adres/sn) ile beyaz listede olmayan eşlerin aşırı `ADDR` akışı düşürülür.
* **BIP133 `FEEFILTER` ve `-blocksonly`**: `NetMsgType::FEEFILTER` periyodik olarak yayınlanır; `SendMessages` içinde `pto->minFeeFilter` altındaki veya `-blocksonly` modundaki `MSG_TX` duyuruları filtrelenir.
* **Tuzlanmış `-rpcauth` ve HTTP `Host` DNS-Rebinding `403` Koruması**: `src/httprpc.cpp` üzerinde `<KULLANICI>:<TUZ>$<ÖZET>` biçimindeki kimlik bilgileri `CHMAC_SHA256` ve `TimingResistantEqual` ile doğrulanır. `http_request_cb` (`src/httpserver.cpp`), tarayıcı DNS-rebinding saldırılarını önlemek için gelen HTTP `Host` başlığını `IsAllowedHTTPRequestHost` ile denetler; `localhost`, sayısal IP değişmezleri (`fAllowLookup=false`) ve açıkça yapılandırılmış `-rpcbind` adresleri dışındaki güvenilmeyen `Host` başlıklarını `403 Forbidden` (`HTTP_FORBIDDEN`) ile reddeder.

### 3. `HeadersSyncState` Düşük Zorluklu Başlık Ön-Senkronizasyonu
* `mapHeadersSyncState` (`src/net_processing.cpp`, `FinalizeNode()` ile temizlenir), ebeveyn zincir işi `chainActive.Tip()->nChainWork / 2` altında kaldığında gelen `HEADERS` mesajlarını `max(16.384, chainActive.Height() + 2000)` başlık ve **`8 MiB` (`MAX_PRESYNC_BYTES = 8 * 1024 * 1024`)** serileştirilmiş bayt tavanına kadar geçici tamponda biriktirir.
* `ProcessHeadersMessage`, sıfır olmayan `nTxCount != 0` taşıyan herhangi bir başlık kaydını anında reddeder (`Misbehaving(20)`). Tampondaki başlıklar süreklilik ve kümülatif `GetBlockProof()` işi açısından doğrulanıp yeterli işe ulaştığında `AcceptBlockHeader()` üzerinden `mapBlockIndex`'e aktarılır. Yeterli iş olmadan başlık sayısı veya `8 MiB` bayt tavanı aşılırsa `Misbehaving(20)` uygulanır ve tampon atılır.

### 4. BIP324 v2 Şifreli P2P Taşıma (`NODE_P2P_V2`)
* `-v2transport` aktifken (varsayılan: `1`) düğümler `NODE_P2P_V2 = (1 << 11)` servis bitini duyurur.
* `BIP324Cipher` (`src/bip324.cpp`, `src/crypto/chacha20poly1305.cpp`), `secp256k1_ellswift_create` ve `secp256k1_ellswift_xdh` (`secp256k1_ellswift_xdh_hash_function_bip324`) ile paylaşılan gizli anahtarı üretir; HKDF-SHA256 (`"bitcoin_v2_shared_secret" || magic`) üzerinden `FSChaCha20` (uzunluk şifreleme) ve her `224` pakette anahtar yenileyen `FSChaCha20Poly1305` (AEAD) anahtarlarını türetir.
* **Otomatik v1 Geri Dönüşü ve Decoy Taşması Bağlantı Kesme (`src/net.cpp`)**: Gelen bağlantılar ilk `4` bayta bakar (`fV2AutoDetect`); `Params().MessageStart()` ile eşleşirse sorunsuz biçimde v1 düz metin akışına geçer, giden bağlantılar ise karşı eş `NODE_P2P_V2` duyuruyorsa v2 el sıkışmasını başlatır. v2 çerçeveleme sırasında `ReceiveMsgBytes`, el sıkışma (`VERSION_PACKET`) aşamasında `> 64` decoy paketi gönderen veya uygulama (`APPLICATION`) aşamasında art arda `> 64` decoy (`v2ConsecutiveDecoys`) ya da `60 sn` penceresinde `> 256` decoy paketi / `> 4 MiB` decoy verisi gönderen eşlerin bağlantısını (`fDisconnect = true`) derhal keser.

### 5. Sıkılaştırılmış PSBT (BIP174 Kısmen İmzalanmış İşlemler)
* `src/psbt.h` ve `src/psbt.cpp` içinde `PartiallySignedTransaction`, `PSBTInput`, `PSBTOutput`, Base64/ikili serileştirme (`psbt\xff`), `Merge`, `FinalizePSBT` ve `SignPSBTInput` eklendi ve sıkılaştırıldı:
  - **Kaynak ve Alan Sınırları**: Deserialize ve `IsSane()` aşamalarında **`4.096` girdi veya `4.096` çıktı** sınırını aşan PSBT'leri reddeder; `PSBT_IN_PARTIAL_SIG` (`<= 73` bayt), `PSBT_IN_REDEEMSCRIPT` / `SCRIPTSIG` (`<= MAX_SCRIPT_SIZE`) ve `PSBT_IN_TAP_KEY_SIG` (`64` veya `65` bayt) boyutlarını denetler.
  - **UTXO ve Üst-Veri Birleştirme Çakışması Reddi**: `PSBTInput::Merge` artık `bool` döndürür; `non_witness_utxo` ile `witness_utxo` karıştırılmasını veya çelişkili UTXO özet/çıktı, `redeem_script`, `sighash_type` ya da `tap_internal_key` alanlarını reddeder.
  - **Taproot ve P2SH Multisig İmzalama/Sonuçlandırma**: `SignPSBTInput` ve `BuildScriptSigFromPSBTInput`, `TX_WITNESS_V1_TAPROOT` anahtar-yolu Schnorr imzalama/sonuçlandırmasını (`BuildSpentOutputsPrecom` + `ComputeTaprootSighash`) ve `TX_SCRIPTHASH` (`P2SH`) / `TX_MULTISIG` imzalama ve `scriptSig` oluşturmayı destekler.
* RPC komutları: `createpsbt`, `converttopsbt`, `decodepsbt`, `combinepsbt`, `utxoupdatepsbt`, `analyzepsbt`, `finalizepsbt` ve `walletprocesspsbt`.

## Geriye Dönük Uyumluluk
* `ConnectBlock` üzerinde `SCRIPT_VERIFY_TAPROOT`, `SCRIPT_VERIFY_TAPROOT_BIP341`, `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` ve `BIP68` zorunluluğu (`v1.5.9.0`, `PROTOCOL_VERSION = 70983`, `MIN_PEER_PROTO_VERSION = 70983` ile tüm ağlarda Yükseklik `1`'de devreye giren `UPGRADE_HARDENING_V4` dahil) tüm senkronize Seed düğümlerinde uygulanmaktadır.
* BIP324 v2 taşıma ve BIP133 `FEEFILTER` P2P katmanında `NODE_P2P_V2` ve v1 magic otomatik algılaması sayesinde geriye dönük uyumludur.

## Referans Uygulama
* Konsensüs, `HeadersSyncState`, `GETDATA`/`ADDR`/`CMPCTBLOCK`/`VERACK`/`FEEFILTER`: `src/main.cpp`, `src/net_processing.cpp`, `src/primitives/block.h`, `src/protocol.h`, `src/protocol.cpp`, `src/init.cpp`.
* BIP341 / Tapscript / SigCache / İmzalama: `src/script/interpreter.h`, `src/script/interpreter.cpp`, `src/script/sigcache.cpp`, `src/script/sign.cpp`.
* BIP324 v2 Taşıma ve Decoy Koruması: `src/bip324.h`, `src/bip324.cpp`, `src/crypto/chacha20poly1305.h`, `src/crypto/chacha20poly1305.cpp`, `src/net.h`, `src/net.cpp`.
* HTTP Host Koruması, RPC Yetkilendirme ve PSBT: `src/httpserver.cpp`, `src/httprpc.cpp`, `src/psbt.h`, `src/psbt.cpp`, `src/rpc/rawtransaction.cpp`, `src/wallet/rpcwallet.cpp`, `src/rpc/server.cpp`.
