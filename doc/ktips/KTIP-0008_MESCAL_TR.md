```
KTIP: 0008
Başlık: MESCAL Bildirimsel Akıllı Sözleşmeleri ve Taproot (P2TR / BIP-350 Bech32m) Entegrasyonu
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Aktif
Tür: Standart Takip (Konsensüs / Betik)
Oluşturulma Tarihi: 2026-07-10
```

## Özet
Bu öneri, durumsuz (stateless) ve döngüsüz yığın tabanlı `CScript` bayt koduna ve Taproot (`P2TR`, Witness v1, BIP-350 Bech32m) script-path harcama yapraklarına deterministik olarak derlenen JSON tabanlı bildirimsel akıllı sözleşme dili **MESCAL (Minimalistically Envisioned Smart Contract Assembling Language)** standardını tanımlar. Küresel durum bozulmalarını ve yetkisiz depolama açıklarını ortadan kaldırmak amacıyla, eski durum bilgili (stateful) opcode'lar (`OP_MLOAD`, `OP_MSTORE`, `OP_SLOAD`, `OP_SSTORE`, `OP_PUBLISH`, `OP_RUN`, `OP_UPDATE_STATUS`) betik yorumlayıcı katmanında (`SCRIPT_ERR_DISABLED_OPCODE`) kalıcı olarak devre dışı bırakılmıştır.

## Motivasyon
Geleneksel sanal makine tabanlı akıllı sözleşme platformları; sınırsız yürütüm döngüleri, yeniden giriş (re-entrancy) saldırı vektörleri, karmaşık gaz hesaplamaları ve zincir yeniden düzenlemeleri (reorg) sırasında ağır durum görüntüleri olmadan güvenle geri alınamayan değiştirilebilir küresel depolama ağaçları barındırır.

MESCAL bu sorunları dört mimari garantiyle çözer:
1. **Döngüsüz Doğrusal Sonlanma ($O(k)$)**: MESCAL dil bilgisi geriye doğru dallanmaları ve döngüleri yasaklar; kontrol akış grafiği, en kötü durum çalışma maliyeti tanımlanan eylem sayısı $k$ ile doğrusal olarak sınırlı olan bir Yönlü Asiklik Grafiktir (DAG).
2. **Gazsız UTXO Determinizmi**: Sözleşme durum geçişleri doğrudan UTXO kilit betiklerine (`P2SH` veya `P2TR`) kodlanır ve çalışma zamanı gaz sayaçları yerine standart işlem ücretleriyle doğrulanır.
3. **Taproot Script-Path Gizliliği (`P2TR` / BIP-350 Bech32m)**: MESCAL sözleşmelerinin Taproot betik ağaçlarına derlenmesi, zincir üstünde yalnızca yürütülen dalın (`leafScript` + 33 baytlık `controlBlock`) açığa çıkmasını sağlarken kullanılmayan dalları gizli tutar.
4. **Eski Durum Bilgili Opcode'ların Kalıcı Olarak Kapatılması (`SEC-10`)**: Kimlik doğrulaması veya reorg geri alma (undo) kaydı olmadan doğrudan `pScriptDB` veritabanına yazan eski DECENOMY/SWMP opcode'ları kalıcı olarak devre dışı bırakılmıştır.

## Teknik Özellikler

### 1. JSON Yapısı ve Bildirimsel Roller
Bir MESCAL sözleşme belgesi dört üst düzey anahtardan oluşur: `basic`, `condition`, `contract` ve `active_contract`.

`CMescal::Compile` (`src/script/mescal.cpp` içindeki `CompileBasicBlock`) tarafından derlenen `basic` rolleri:
* **`hash160`**: `<input> OP_HASH160` (ham girdi verisi `MAX_SCRIPT_ELEMENT_SIZE = 520` bayt ile katı biçimde sınırlıdır; veya `"mode": "htlc-equal"` verildiğinde `OP_HASH160 <20-bayt-hash> OP_EQUAL`, ya da satır içi hash-kilidi Pattern 4b doğrulamaları için `"mode": "hash-equalverify"` verildiğinde `OP_HASH160 <20-bayt-hash> OP_EQUALVERIFY`)
* **`check-signature-verification`**: `<pubkey> OP_CHECKSIGVERIFY` (veya `"verify": false` verildiğinde `<pubkey> OP_CHECKSIG`)
* **`equalverify-checksig`**: `OP_DUP OP_HASH160 <pubkeyhash> OP_EQUALVERIFY OP_CHECKSIG`
* **`multi-signature`**: `<m> <pubkeys...> <n> OP_CHECKMULTISIG`
* **`lock-time`**: `<locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP` (işaretsiz 32-bit `0..4294967295` sınırları içinde katı biçimde doğrulanır)
* **`number`, `add`, `sub`, `bool-and`, `bool-or`, `num-equal`, `greater-than`, `drop`**: Yığın aritmetik ve mantıksal işlemleri (`<int64_t>`, `OP_ADD`, `OP_SUB`, `OP_BOOLAND`, `OP_BOOLOR`, `OP_NUMEQUAL`, `OP_GREATERTHAN`, `OP_DROP`).
* **`coin-lock-miner`**: `<minerPubKey(33B)> OP_DROP <lockHeight> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <pubKeyHash> OP_EQUALVERIFY OP_CHECKSIG` (`33` bayt sıkıştırılmış `minerPubKey` zorunludur; `UPGRADE_HARDENING_V4` Yükseklik `1` altında zincir üstü Coin-Lock kayıtları `<blsPubKey(48B)> <blsPoP(96B)> OP_2DROP` öneki ile başlar)
* **`pow-miner`**: `<nonce(1..520B)> <powChallenge(32B)> <minerPubKey(33B)> OP_DROP OP_DROP OP_DROP <lockHeight> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <pubKeyHash> OP_EQUALVERIFY OP_CHECKSIG` (çift-uzunluklu `1..520` bayt hex `nonce`, `32` bayt `64`-karakter hex `powChallenge` ve `33` bayt sıkıştırılmış `minerPubKey` zorunludur; `UPGRADE_HARDENING_V4` Yükseklik `1` altında zincir üstü PoW-Lock / Zero-Coin kayıtları `<blsPubKey(48B)> <blsPoP(96B)> OP_2DROP` öneki ile başlar)

Desteklenen `condition` rolü:
* **`if-condition`**: `expressions` dizisini (tanık mantıksal ifadeleri için `stack-input` dahil; geri derlemede `OP_NOTIF` ve mükerrer `OP_ELSE` reddedilir) ve ardından `OP_IF <true_actions> OP_ELSE <false_actions> OP_ENDIF` bloğunu derler.

**Derleyici Güvenlik Sınırları (`src/script/mescal.cpp`)**: Girdi sözleşmeleri `1.000.000` bayt JSON (`1 MB`), en fazla `MAX_MESCAL_CONDITION_DEPTH = 32` iç içe koşul derinliği ve `2048` derlenmiş yürütüm adımı ile sınırlandırılmıştır.

---

### 2. Taproot (`P2TR`) Derlemesi ve BIP-350 Bech32m Kodlaması
`compilemescaltotaproot` RPC komutu (`src/rpc/rawtransaction.cpp`, `CMescal::Compile` çağırır), bir MESCAL JSON tanımını 32 baytlık x-only `internal_pubkey` $P$ ile birleştirerek bir Witness v1 Taproot çıktısına derler:

1. **Etiketli Yaprak Özeti (Tagged Leaf Hash)**:

   $$
   \text{leafHash} = \text{TaggedHash}_{\text{TapLeaf}}\left(0\text{xc0} \mathbin{\Vert} \text{CompactSize}(|\text{leafScript}|) \mathbin{\Vert} \text{leafScript}\right)
   $$

2. **Tweak ve Çıktı Anahtarı**:

   $$
   t = \text{TaggedHash}_{\text{TapTweak}}\left(P \mathbin{\Vert} \text{leafHash}\right)
   $$

   $$
   Q = P + t \cdot G
   $$

3. **Kilit Betiği (`TX_WITNESS_V1_TAPROOT`)**:

   $$
   \text{scriptPubKey} = \text{OP}\underline{\hspace{0.4em}}\text{1}\hspace{0.3em}\langle 32\text{-bayt } x(Q) \rangle
   $$

4. **BIP-350 Bech32m Adres Kodlaması (`SEC-11`)**:
   Witness v1 adresleri (Mainnet'te `kt1p...`, Testnet'te `tk1p...`, Regtest'te `ert1p...`) standart BIP-350 Bech32m polymod sağlama toplamı sabiti olan **`0x2bc830a3`** (`src/bech32.cpp`) değerini kullanır.
5. **BIP-341 TapBranch, Tapscript ECDSA Sighash, Schnorr SigCache ve `NULLFAIL` (`UPGRADE_HARDENING_V4`)**:
   * **Sözlük Sıralı `ComputeTapBranchHash`**: Çok yapraklı Taproot dal düğümleri, alt yaprak özetlerini BIP-341 ile uyumlu olarak bayt düzeyinde `std::memcmp(left.begin(), right.begin(), 32) < 0` karşılaştırmasıyla sıralar (V4 öncesi little-endian `uint256::operator<` hatasını düzeltir).
   * **`SIGVERSION_TAPSCRIPT` İçinde ECDSA**: `SCRIPT_VERIFY_TAPROOT_BIP341` aktifken `SIGVERSION_TAPSCRIPT` içinde yürütülen 33/65 baytlık ECDSA açık anahtarları, eski `SIGVERSION_BASE` DER ECDSA geri dönüşü yerine `tapleaf_hash` ve `codesep_pos` taahhüt eden kanonik BIP-341 Tapscript sighash (`SignatureHashTaproot(..., SIGVERSION_TAPSCRIPT, ...)`) değerini hesaplar.
   * **Schnorr SigCache ve Temel `SCRIPT_VERIFY_NULLFAIL`**: `CachingTransactionSignatureChecker::VerifySchnorrSignature` (`src/script/sigcache.cpp`), BIP-340 Schnorr imzalarını `'S'` alan etiketi altında önbelleğe alır (`TapscriptSignatureChecker` üzerinden devredilir) ve `EvalScript`, hem `SIGVERSION_TAPSCRIPT` içinde hem de temel `OP_CHECKSIG` betiklerinde `SCRIPT_VERIFY_NULLFAIL` aktif olduğunda başarısız olan boş-olmayan imzaları `SCRIPT_ERR_SIG_NULLFAIL` ile reddeder.

---

### 3. Cüzdan Salt-İzleme (Watch-Only) Takibi ve Coin-Selection İzolasyonu (`SEC-13`)
`IsMine` (`src/script/ismine.cpp`) içerisinde, yerel anahtar deposunda (`HaveCScript`) kayıtlı `TX_WITNESS_V1_TAPROOT` ve genel `TX_MESCAL_CONTRACT` betikleri `ISMINE_SPENDABLE` yerine `ISMINE_WATCH_ONLY` döndürür (ve `src/script/standard.cpp` içindeki `ExtractDestinations`, `TX_MESCAL_CONTRACT` ile eski `TX_CONTRACT_*` betiklerini harcanabilir adres olmayan çıktı olarak reddeder). Bu sayede:
* Çekirdek cüzdan, MESCAL/Taproot sözleşme çıktılarının bakiyesini ve işlem geçmişini takip eder.
* Otomatik UTXO seçimi (`AvailableCoins` / `SelectCoinsMinConf`), standart P2PKH/P2SH `SignStep` imzalayıcısının sözleşmeye özel tanık (witness) argümanları olmadan harcayamayacağı özel sözleşme çıktılarını yanlışlıkla seçmez.

---

### 4. Eski Durum Bilgili Opcode'ların Kalıcı Olarak Kapatılması (`SEC-10`)
`EvalScript` (`src/script/interpreter.cpp`) içerisinde `OP_MLOAD`, `OP_MSTORE`, `OP_SLOAD`, `OP_SSTORE`, `OP_PUBLISH`, `OP_RUN` ve `OP_UPDATE_STATUS` opcode'ları betik yürütümünü anında `SCRIPT_ERR_DISABLED_OPCODE` hatasıyla sonlandırır. `src/script/script.cpp` içindeki tüm `CScriptContract` yardımcı fonksiyonları `pScriptDB`'ye erişmeden doğrudan `false` döndürür.

## Geriye Dönük Uyumluluk
* MESCAL sözleşmeleri mevcut betik doğrulama kurallarıyla uyumlu standart `CScript` (`P2SH` veya `P2TR`) çıktılarına derlenir.
* Eski durum bilgili SWMP opcode'larının kapatılması (`SEC-10`) bir konsensüs güvenlik sıkılaştırmasıdır; hiçbir geçerli KRISTA bloğu bu eski opcode'ları kullanmaz.

## Referans Uygulama
* Derleyici ve Taproot sentezi: `src/script/mescal.h`, `src/script/mescal.cpp` ve `src/rpc/rawtransaction.cpp`.
* BIP-350 Bech32m kodlaması: `src/bech32.cpp`.
* Yorumlayıcı sıkılaştırması, imza önbelleği ve cüzdan izolasyonu: `src/script/interpreter.cpp`, `src/script/sigcache.cpp`, `src/script/script.cpp`, `src/script/standard.cpp` ve `src/script/ismine.cpp`.
