# ADAM & MPA Konsensüs Güvenlik Denetim Raporu

Bu rapor, KRISTA kod tabanında uygulanan **ADAM (A Decentralized Approach Model)** ve **MPA (Multi-Proof Algorithm)** işbirlikçi konsensüs mekanizmalarının güvenlik duruşunu, mantığını ve olası çalışma zamanı (runtime) açıklarını, yeni entegre edilen çoklu algoritmalı bulmaca madenciliği (multi-algorithm puzzle mining) ve yedek quorum ölçeklendirmesini (fallback quorum scaling) içerecek şekilde değerlendirmektedir.

---

## 1. Tehdit Modellemesi: Yaygın Saldırı Vektörlerine Karşı Güvenlik

Bu bölüm, hibrit ADAM/MPA konsensüs modelinin blokzincir ağlarındaki en yaygın ve kritik saldırı vektörlerini nasıl azalttığını analiz etmektedir.

### 1.1. %51 Hash Gücü (Hashpower) Saldırısı / Kartel Tekelleşmesi
* **Geleneksel Güvenlik Açığı**: Standart PoW'da, ağın hash gücünün %51'ini kontrol eden bir aktör çifte harcama (double-spend) yapabilir, blokları yeniden düzenleyebilir (reorganize blocks) ve işlemleri sansürleyebilir.
* **ADAM/MPA Önlemi**: 
  - Yalnızca hash gücü (hashpower) yeterli değildir. Blok üretimine katılmak için bir düğümün (node) 11 madenciden biri veya koordinatör (coordinator) olarak seçilmesi gerekir.
  - Seçim havuzu, kilitli teminat (4.200 KRISTA) ile güvence altına alınan Masternode ağıdır.
  - Bir bloku tehlikeye atmak için, bir kartelin belirli bir turda seçilen 11 madenciden en az 7'sini (threshold = 7) kontrol etmesi gerekir. Bu, aktif masternode ağının yaklaşık %63'ünden fazlasına sahip olmayı gerektirir. Bu durum, saldırıyı ekonomik olarak mantıksız kılan devasa bir finansal engel teşkil eder.

### 1.2. Bencil Madencilik (Selfish Mining) ve Blok Saklama (Block Withholding)
* **Geleneksel Güvenlik Açığı**: Bir madenci kazdığı blokları gizler ve dürüst madencilerin bloklarını yetim bırakmak (orphan) için bunları seçici olarak yayınlayarak blok ödüllerinden (block rewards) haksız bir pay elde eder.
* **ADAM/MPA Önlemi**: 
  - Blok üretimi işbirlikçidir. Seçilen bir madenci yalnızca hafif bir bulmaca (`vAdamSolutions`) çözer ve bunu koordinatöre gönderir.
  - Bir madenci, Koordinatörün imzasını (`vAdamCoordinatorSig`) veya diğer 10 madencinin imzalarını üretemediği için özel bir çatallanmada (private fork) "bencil madencilik" (selfish mine) yapamaz.
  - Bir madenci kendi çözümünü saklasa bile, en az 7 diğer seçilmiş madenci çözümlerini sunduğu sürece koordinatör bloku yayınlayabilir. Bencil madencilik (selfish mining) tamamen etkisiz hale getirilmiştir.

### 1.3. Block Grinding / Tohum (Seed) Manipülasyonu
* **Geleneksel Güvenlik Açığı**: PoS zincirlerinde, doğrulayıcılar (validators) bir sonraki blokun hash değerini manipüle etmek için blok içeriğini (nonce'lar, işlemler) değiştirir ve gelecekteki slotlarda kendilerini seçtirmek üzere sözde rastgele tohumu (pseudo-random seed) kendi lehine saptırmaya çalışır.
* **ADAM/MPA Önlemi**: 
  - Bir sonraki seçim slotu için rolling seed, Koordinatörün bir önceki tohumun belirlenimci VRF imzasından türetilir:

    $$\text{Seed}_H = \text{Hash}\left(\text{Seed}_{H-1} \mathbin{\Vert} \text{VRFProof}_{H-1}\right)$$

  - Hibrit bir BLS12-381 + ECDSA fallback imza mekanizması (`SignBLSWithECDSAFallback`) kullandığımız için, Koordinatörün belirli bir tohum için tam olarak bir geçerli imzası vardır. BLS imzası, uzun vadeli ECDSA anahtarından belirlenimci olarak türetilir ve BLS açık anahtarı (public key), onu yetkilendirmek için ECDSA kullanılarak imzalanır. Bu, imza değerini değiştirmek veya manipüle etmek (grind) için herhangi bir serbestlik derecesini ortadan kaldırarak bir sonraki blokun seçim tohumunu (election seed) %100 kurcalamaya karşı korumalı (tamper-proof) hale getirir.

### 1.4. Nothing-at-Stake Saldırısı
* **Geleneksel Güvenlik Açığı**: PoS sistemlerinde, doğrulayıcılar sıfır maliyetle aynı anda birden fazla rakip çatallanmada (fork) blok başlıklarını imzalayabilir ve bu da çatallanmanın çözülmesini engeller.
* **ADAM/MPA Önlemi**: 
  - Herhangi bir çatallanmadaki bir bloku doğrulamak için, blokun o çatallanmanın tohumu (seed) için seçilen madencilerin anahtarlarıyla eşleşen en az 7 geçerli PoW bulmaca çözümü içermesi gerekir.
  - Bu bulmacaları çözmek, gerçek C++ hashleme döngülerinin çalıştırılmasını gerektirir (desteklenen 18 algoritmanın 3 aşamalı permütasyonu kullanılarak).
  - Madenciler, rakip her bir çatallanma için bulmacaları çözmek adına gerçek fiziksel işlemci gücü (CPU/GPU döngüleri) harcamak zorunda olduklarından, birden fazla çatallanmada stake etmenin maliyeti sıfır değildir. Birden fazla çatallanmada stake etmek hesaplama açısından maliyetlidir ve bu durum nothing-at-stake açığını çözer.

### 1.5. Düğüm Seçiminde Sybil Saldırıları ve Çift Kulvarlı (Dual-Track) Kota İzolasyonu
* **Geleneksel Güvenlik Açığı**: Bir saldırgan, akran (peer) listesini domine etmek ve tüm lider seçimlerini kazanmak için binlerce sanal düğüm (node) oluşturur.
* **ADAM/MPA Önlemi**: 
  - **Çift Kulvarlı Kota Seçimi (`ElectDualTrackValidators`, `nAdamDualTrackHeight = UPGRADE_ADAM` yüksekliğinde aktif)** kapsamında, 11 doğrulayıcı koltuğu **1. Kulvar (Teminatlı Havuz — 8 Koltuk: 7 Aktif Masternode [`4.200 KRISTA`] + 1 Coin-Lock [`250 KRISTA`], Kulvar-1 içi taşma destekli)** ve **2. Kulvar (Açık PoW Havuzu — 3 Koltuk)** olarak ikiye ayrılmıştır. `ADAM_MASTERNODE_TRACK_QUOTA = 7` hem derleme anında (`static_assert`) hem de `chainparams.cpp` içinde çalışma anında (`assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold)`) zorunlu tutulduğundan, Masternode olmayan 4 koltuğun tamamı çözüm saklasa dahi Aktif Masternode'lar bootstrap sonrası kuorum eşiğini (`7`) tek başına karşılar; 5 veya daha fazla koltuğun aynı anda susması durumunda ise **Yedek Doğrulayıcı Koltuk Devri (`60 sn`)** ve **Kademeli Eşik Gevşetme (`120 sn`)** bloğun kurtarılmasını garanti eder.
  - Bir saldırgan yüzlerce Açık PoW anahtarı kaydetse dahi, Teminatlı Kulvar dolu olduğu sürece Açık PoW havuzunun komitedeki temsili **11 koltukta en fazla 3 (%27,3)** ile sınırlandırılmıştır; bu oran konsensüs eşiğinin (`GetAdamThreshold`: bootstrap döneminde `5`, sonrasında `7`) çok altındadır.
  - Ayrıca fonlu `PoW-Lock` kayıtları en az 100 onaylı (`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100`) UTXO girdileriyle fonlanan **10 KRISTA (`POW_LOCK_REGISTRATION_AMOUNT = 10 * COIN`)** kilitlenmesini şart koşar ve blok başına en fazla 3 `PoW-Lock` işlemiyle (`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 3`) sınırlandırılmıştır. 10 KRISTA altındaki fonlu `PoW-Lock` çıktıları ve 250 KRISTA altındaki `Coin-Lock` çıktıları hem mempool politikası (`AcceptToMemoryPoolWorker`) hem de blok doğrulaması (`CheckBlock`) tarafından reddedilir.

### 1.6. İmza Bükülebilirliği (Signature Malleability) ve Relay DoS
* **Geleneksel Güvenlik Açığı**: Bir saldırgan, geçerliliği değiştirmeden blok verisini (payload) değiştirmek için iletilen bloklardaki ECDSA imza baytlarını manipüle eder (mutate) ve bu durum önbellek kirliliğine (cache pollution) ve P2P engelleme tetikleyicilerine (ban triggers) neden olur.
* **ADAM/MPA Önlemi**: 
  - Koordinatör imzası blok hash değerinden (`GetHash()`) hariç tutulur, bu nedenle imzanın manipüle edilmesi blokun hash kimliğini etkilemez.
  - Sıkı DER biçimlendirme kontrolleri uyguluyoruz. Değiştirilmiş imzaya sahip herhangi bir blok, blok dizinine (block index) girmeden önce P2P katmanında hemen reddedilir.

---

## 2. Teknik Bulgular ve Kod Güvenliği

### 2.1. Çoklu Algoritma Dizi Sınır Güvenliği
Bulmaca hashleme algoritmaları şu şekilde seçilir:

$$\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{MinerPubKey}_i)$$

bu işlem, girdiyi $[0, 17]$ aralığındaki 3 farklı algoritma dizinine belirlenimci olarak eşler.

* **Güvenlik Kontrolü**: `CalculateAdamPuzzleHash()` `switch` ifadesi, desteklenen 18 algoritmanın tümünü temsil eden `0` ile `17` arasındaki durumları (cases) işler. `default` dalı (branch) Double-SHA256 algoritmasına geri döner (falls back) ve olası herhangi bir sınır dışı dizi indeksi (index out-of-bounds) veya tanımlanmamış davranışlara karşı koruma sağlar.

### 2.2. Masternode Fallback ve Yerel Anahtar Havuzları
* **Güvenlik Kontrolü**: Özel ağlarda (Testnet ve Regtest), aktif masternode sayısı düşükse ağ, yerel izole ortamlarda konsensüs kilitlenmelerini önlemek amacıyla belirlenimci (deterministik) bir anahtar havuzuna geri döner (falls back). Mainnet, Testnet veya Regtest üzerinde kod içinde sabitlenmiş (hardcoded) herhangi bir anahtar kimliği filtrelemesi veya yerel cüzdan kısıtlaması uygulanmaz, bu da tamamen merkeziyetsiz ve güven gerektirmeyen (trustless) bir test ortamı sağlar.
* **Üretim Önerisi**: Canlı halka açık Mainnet üzerinde bu fallback özelliğinin devre dışı bırakıldığından veya kilitlendiğinden emin olun. Mainnet üzerinde masternode sayısı 15'ten azsa, fallback mantığında public seed'lerden türetilebilen gizli anahtarların (private keys) ifşa edilmesi yerine, zincir durmalı veya seçim yapamamalıdır. Mainnet üzerinde hiçbir yerel anahtar filtrelemesine veya sabit kodlanmış fallback'e (hardcoded fallback) izin verilmemelidir.

### 2.3. CPU Hizmet Dışı Bırakma (Doğrulama Üzerinde DoS)
* **DoS Riski**: Blokları ileten düğümlerin (relaying nodes), blok başına 11 kısmi bulmaca imzasını ve bir koordinatör imzasını doğrulaması gerekir ve bu işlem yoğun CPU kullanımı gerektirir.
* **Önlem**: Blokları ileten düğümler, maliyetli imza doğrulamalarını gerçekleştirmeden *önce* blokun zorluk hedefini (difficulty target), işlem yapısını ve seçilen madenci listesini doğrular ve geçersiz spam bloklarını erkenden reddeder.

### 2.4. Eşit İş Çatal Yakınsaması ve LLMQ Çifte İmza Koruması (v1.4.5.9)
* **Eşit İş (Equal-Work) Çatal Çözümü (`CBlockIndexWorkComparator`)**: Aynı yükseklikteki rakip zincir uçları eşit kümülatif zincir işine (`nChainWork`) sahip olduğunda, düğümler sayısal olarak daha küçük blok özetine (`pa->GetBlockHash() < pb->GetBlockHash()`) sahip zincir ucunu deterministik olarak tercih eder; bu sayede ağ bölünmeleri sonrasında kalıcı zincir ayrışmaları önlenir.
* **LLMQ Slot Bazlı Çifte İmza Önleme (`mapQuorumSignedSlot`)**: Kurul üyeleri imzaladıkları her `(nHeight, rotationIndex)` slotunu kaydeder ve koordinatör rotasyon indeksi ilerlemediği sürece aynı slot için ikinci bir rakip `PROPOSEBLOCK` önerisini imzalamayı reddeder; böylece her slotta en fazla tek bir blok 5'te 3 korum sertifikası (`vQuorumSig`) alabilir.
* **Katı Hot/Cold Masternode İmza İzolasyonu**: Soğuk kontrolcü cüzdanlar (`masternode.conf`) konsensüs imzalama süreçlerinden tamamen izole edilmiştir; yalnızca aktif Sıcak Düğümler (`masternode=1` + `activemasternode.conf`) LLMQ paylarını, blok önerilerini ve zincir üstü heartbeat ping işlemlerini imzalar.

---

## 3. Kapsamlı 3 Katmanlı Güvenlik Denetimi ve Sıkılaştırmaları (`v1.4.5.9` — `SEC-01` – `SEC-13`)

1. **`SEC-01` (Sunucu Dosya İzinleri ve Çok Kullanıcılı İzolasyon)**: Tüm 5 düğümde (`Seed 1..4` + Yerel) `~/.kristatech` ve `~/.kristatech-explorer` dizinlerine `0700`, `kristatech.conf`, `activemasternode.conf`, `masternode.conf`, `wallet.dat` ve `.env` dosyalarına `0600` izinleri zorunlu kılındı.
2. **`SEC-02` (`MNBROADCAST` / `MNPING` Önbellek Zehirleme ve Bellek DoS Koruması)**: `src/masternodeman.cpp` ve `src/masternode.cpp` içerisinde `mapSeenMasternodeBroadcast` ve `mapSeenMasternodePing` önbellek kayıtları, yalnızca kriptografik imza (`CheckSignature`) ve teminat UTXO doğrulaması başarılı olduktan *sonra* yapılacak şekilde güncellendi.
3. **`SEC-03` (`ADAMSOL` ve `QUORUMSIGSHARE` Boyut Sınırları)**: `src/main.cpp` içerisinde `ADAMSOL` (`msg.vchSolution`) yetim önbelleğine (`mapOrphanAdamSolutions`) alınmadan önce ve `QUORUMSIGSHARE` (`msg.vchSig`) deserialize edilmeden önce katı boyut sınırı (`!empty() && size() <= 512`, `Misbehaving(20)`) eklendi.
4. **`SEC-04` (`mapFutureBlocks` Kuyruk Doldurma Koruması)**: Gelecek zaman damgalı (`time-too-new`) blok/başlıkların `mapFutureBlocks` kuyruğuna alınabilmesi için `vAdamCoordinatorSig`, `vAdamVRFProof` (`nVersion >= 11`) ve `vQuorumSig` (`nVersion >= 12`) alanlarının dolu olması şartı ve kuyruk dolduğunda (`>= 64`) en uzak zaman damgalı kaydı tahliye etme mekanizması eklendi.
5. **`SEC-05` (`CQuorumSignature::Verify` Vektör ve Eleman Sınırları)**: `CQuorumSignature::Verify` ve `CQuorumSignature::AggregateBLSSignatures` (`src/llmq.cpp`) fonksiyonlarında `signatures.size() <= 32` ve `sig.size() <= 512` sınırları zorunlu kılındı; `CheckBlockHeader` (`src/main.cpp`) içerisindeki `block.vQuorumSig.size()` sınırı `4096` bayta indirildi.
6. **`SEC-06` (Web Servisleri Localhost Bağlaması)**: `krista-explorer`, `krista-website` ve `kristalteknoloji.com` Nitro sunucuları Nginx TLS sonlandırması arkasında yalnızca `--host 127.0.0.1` arayüzünü dinleyecek şekilde sınırlandırıldı.
7. **`SEC-07` (Explorer Adres Arama Regex Düzeltmesi)**: 35 karakterli KRISTA adreslerinin (`KT...` / `kt...`) aranabilmesi için `Explorer/server/api/search.ts` içerisindeki regex `{34}` yerine `/^[a-zA-Z0-9]{33,36}$/` olarak güncellendi.
8. **`SEC-08` (Faucet Hız Sınırlaması ve Hazine Portalı Doğrulaması)**: `KristaTech-Faucet/server/api/request.ts` uç noktasına IP ve cüzdan adresi başına 24 saatlik hız sınırlaması (`HTTP 429`) eklendi; `KristaTech-Treasury-Portal` teklif oluşturma doğrulaması (`rawTxHex <= 200000` hex karakter, en fazla 50 bekleyen teklif ve `src/llmq.cpp` ile uyumlu 5'te 3 çoğunluk eşiği) sıkılaştırıldı.
9. **`SEC-09` (Çok Girdili Hazine İşlemlerinde `txTmp.GetHash()` Normalizasyonu)**: `CheckInputs` ve `ConnectBlock` (`src/main.cpp`) içerisinde `DeveloperFundAddress()` harcayan tüm girdilerde 3. push (`qsig`) verisinin `txTmp.GetHash()` hesaplanmadan önce normalize edilerek çıkarılması sağlandı.
10. **`SEC-10` (Eski DECENOMY/SWMP Stateful Opcode'larının ve `CScriptContract` Yardımcılarının Devre Dışı Bırakılması)**: Yetki kontrolü ve reorg geri alma (undo) mekanizması bulunmayan eski `OP_MLOAD`, `OP_MSTORE`, `OP_SLOAD`, `OP_SSTORE`, `OP_PUBLISH`, `OP_RUN` ve `OP_UPDATE_STATUS` opcode'ları `EvalScript` (`src/script/interpreter.cpp`) içerisinde `SCRIPT_ERR_DISABLED_OPCODE` ile tamamen devre dışı bırakıldı; `CScriptContract::RunContractScript`, `SaveContract`, `LoadContract` ve `UpdateContractStatus` (`src/script/script.cpp`) fonksiyonları doğrudan `false` dönecek şekilde kapatıldı.
11. **`SEC-11` (BIP-350 Standart Bech32m Checksum Sabiti)**: `src/bech32.cpp` ve `KristaTech-MESCAL-Web/server/utils/mescal.ts` içerisinde `Encoding::BECH32M` checksum sabiti `0x3fffffff` yerine BIP-350 standardı olan `0x2bc830a3` değerine geçirildi.
12. **`SEC-12` (%100 Zincir Üstü Deterministik Konsensüs Seçimi ve Sync-Bağımsız Doğrulama)**: `IsValidMasternodeCollateralAndAuth`, `RegisterBlockMasternodeActivity`, `InitializeOnChainMasternodePings`, `GetAdamMinerPool`, `SelectAdamNodes` (`src/adam.cpp`), `ElectQuorumMembers` (`src/llmq.cpp`), `IsBlockPayeeValid` / `FillBlockPayee` (`src/masternode-payments.cpp`) ve `CheckBlock` (`src/main.cpp`) içerisinden yerel P2P `mnodeman` / `masternodeSync.IsSynced()` konsensüs bağımlılıkları tamamen kaldırılarak tüm seçim ve doğrulamaların %100 zincir üstü (on-chain) veriye dayanması sağlandı.
13. **`SEC-13` (Çekirdek Cüzdanda `TX_WITNESS_V1_TAPROOT` İzleme ve Coin-Selection İzolasyonu)**: `src/script/ismine.cpp` içerisine `TX_WITNESS_V1_TAPROOT` (`OP_1 <32-bayt XOnlyPubKey>`) ve genel `TX_MESCAL_CONTRACT` (`keystore.HaveCScript`) için `ISMINE_WATCH_ONLY` desteği eklenerek çekirdek cüzdanın (`wallet.dat`) kontrat UTXO çıktılarını izlemesi, ancak otomatik UTXO seçiminin (`AvailableCoins` / `SelectCoinsMinConf`) yalnızca doğrudan imzalanabilen çıktıları (`ISMINE_SPENDABLE`) kullanması sağlandı.

---

## 4. Konsensüs Canlılığı, Yedek Koltuk Devri ve Teşvik Uyumu Sıkılaştırmaları (`v1.4.5.9` — `Rev 30–35`)

1. **`R30-1` / `R31-2` (7 + 1 + 3 Çift Kulvarlı Kotalar ve Çalışma Anı Doğrulaması)**: `ADAM_MASTERNODE_TRACK_QUOTA = 7`, `ADAM_COIN_LOCK_TRACK_QUOTA = 1` ve `ADAM_OPEN_POW_TRACK_QUOTA = 3` kotaları hem derleme anında (`src/adam.h` içinde `static_assert`) hem de çalışma anında (`src/chainparams.cpp` içinde `assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold)`) zorunlu kılınarak Aktif Masternode'ların (`4.200 KRISTA`) bootstrap sonrası çoğunluk eşiğini (`7`) tek başına karşılaması garanti altına alındı.
2. **`R31-1` / `R32-2` / `R32-3` / `R33-3` / `R34-2` ($t \ge 60\text{ sn}$ Aynı Kulvar Yedek Koltuk Devri ve $t \ge 120\text{ sn}$ Zamana Bağlı Eşik Gevşetme)**:
   - `ResolveAdamCommitteeForTime` ve `ValidateAdamCommitteeForTime` (`src/adam.cpp`) fonksiyonları ile `nTimeElapsed >= 60 sn` (`ADAM_STANDBY_ROTATION_START_SECS = 60`) sonrasında sessiz kalan birincil komite koltuklarının yalnızca aynı kulvardaki (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Open PoW -> Open PoW`) sıradaki yedek adaylarla değiştirilmesine izin verildi (reorg tutarlılığı için bloğun kendi `CCoinsViewCache* pView` görünümüne göre doğrulanır).
   - `GetAdamEffectiveThreshold(nHeight, nTimeElapsed, nActiveMasternodes)` fonksiyonu ile $t = 120\text{ sn}$'den itibaren her `60 sn`'de bir çoğunluk eşiğinin `1` düşürülerek `ADAM_MIN_RELAXED_THRESHOLD = 5` değerine (`> 4` MN-dışı koltuk; bootstrap sonrasında veya aktif MN $\ge 7$ iken en az 1 Aktif Masternode katılımını zorunlu kılar; aktif MN $< 7$ olan bootstrap aşamasında ise `3`) kadar gevşetilmesi sağlandı. Başlık doğrulaması (`ContextualCheckBlockHeader`) senkronizasyon/reorg sırasında yanlış banlamaları önlemek için `nActiveMasternodes = 0` geçirirken, tam blok doğrulaması (`CheckFormsOfWork`) `GetActiveMasternodePoolCount(nHeight - 1, pView)` değerini zorunlu kılar.
3. **`R33-4` / `R34-3` / `R35-2` (Eşit Emek Kulvar Payları, Koordinatör İkame Cezası ve Geliştirici Hazinesi Yönlendirmesi)**:
   - Model D (`src/masternode-payments.cpp` içindeki `FillBlockPayee` ve `IsBlockPayeeValid`) kapsamında bulmacasını çözen her komite koltuğu (`!vAdamSolutions[i].empty()`, birincil veya $t \ge 60\text{ sn}$ aynı kulvar yedek doğrulayıcısı) %40'lık Katılımcı Doğrulayıcı havuzundan (`ComputeModelDValidatorSeatPayments`) kendi kulvar payını (`nPartPaymentPerSeat[i]`: 1 Coin-Lock koltuğu için `%8.00` + 3 Açık PoW koltuğu için koltuk başına `%4.00` = `%20` ve 7 Masternode doğrulayıcı koltuğu için koltuk başına `~%2.857` = `%20`) alır.
   - Çözülmeyen/boş koltuklar **%0** alır ve yedek doğrulayıcı ile değiştirilen her koltuk (`vAdamMiners[i] != vExpectedMiners[i]`) için Koordinatörün %15'lik ödülünden ikame edilen koltuğun ödülüne eşit bir ceza (`nCoordSubstitutionPenalty`) düşülür (`+koltukPayı - koltukPayı = 0` net kazanç). Hem sahipsiz paylar hem de ikame cezaları **Geliştirici Hazinesine (`nExpectedTreasury = nTreasurySplit + nUnclaimedPartSplit`)** yönlendirilir.
4. **`R35-1` / `R35-5` (LLMQ `qblockprop` Sansür Karşıtı İkame Vetosu)**: Yalnızca LLMQ üyesinin zincir ucu `msg.block.hashPrevBlock` ile eşleştiğinde (`pProposalView != nullptr`) değerlendirilen bu kontrolde, $t \ge 60\text{ sn}$ sonrasında LLMQ üyeleri yerel önbellekte (`mapAdamSolutionsCache`) geçerli çözümü bulunan birincil bir koltuğu yedekle değiştiren (`vAdamMiners[i] != vExpectedMiners[i]`) blok önerilerini veto eder; buna karşılık boş birincil koltukları (`vAdamMiners[i] == vExpectedMiners[i]`) asla veto etmez, böylece ağda yolda olan çözümlerin normal hızlı blokları geciktirmesi önlenir.
5. **`R32-1` / `R35-4` (Düğüm Genelinde Ebeveyn Başına Tek Blok Koruması)**: `src/miner.cpp` içindeki `ActiveProposalGuard` ve yıkıcı olmayan FIFO/yaş tahliyesi (`PruneParentCoordMap`), bir düğümün tüm blok sürümlerinde (`v11` ve `v12`) aynı ebeveyn blok üzerinde eşzamanlı veya ardışık olarak hem PoW hem de PoS bloğu üretmesini engeller.
6. **`R36` Güvenlik Denetimi Sıkılaştırmaları (`BULGU-1..13`)**:
   - **Yedek LLMQ Alıcı Çözümlemesi ve Boş Kurul Hazine Aktarımı (`BULGU-1`)**: `4.200 KRISTA` teminat UTXO'su bulunmayan yedek LLMQ üyelerine doğrulayıcı P2PKH adresleri üzerinden ödeme yapılır; `vLlmqPayees.empty()` durumunda ise %10 `nLLMQSplitTotal` payı Koordinatöre kalmak yerine Geliştirici Hazinesine aktarılır.
   - **Tam Komite Çözücü Canlılığı (`BULGU-2`)**: `BitcoinMiner` çözücü iş parçacıkları, koordinatör olmayan düğümlerde veya 30 saniyelik blok öneri diliminden önce `cachedCount >= 5` olduğunda erken durmak yerine tüm yerel seçilmiş doğrulayıcı koltuklarını çözmeye devam eder; böylece `Rev 34.1` Eşit Emeğe Eşit Kazanç modelinde erken durma kaynaklı ödül kaybı giderilir.
   - **Çatal-Duyarlı `pindexPrev` Soy Ağacı ve Hazine Kurulu (`BULGU-3`)**: `GetAdamMinerPool` (`src/adam.cpp`), `chainActive[h]` yerine `pView->GetBestBlock()` üzerinden `pindexTip->GetAncestor(h)` soy ağacını tarar; `CheckInputs` (`src/main.cpp`) ise `llmq::GetActiveQuorum` çağrısına `pindexPrevSpend` ve `&inputs` görünümlerini iletir.
   - **Blok İçi BLS Anahtar Tutarlılığı ve VRF Mıhlama (`BULGU-4`)**: `CheckBlock`, `vQuorumSig`, `vAdamSolutions`, `vAdamVRFProof` ve `vAdamCoordinatorSig` alanlarının tamamında `mapBlockBlsKeys` tutarlılığını zorunlu kılar ve `RegisterBlockMasternodeActivity` VRF BLS anahtarını da mıhlar.
   - **P2P DoS Korumaları ve Dosya İzinleri (`BULGU-5, 6, 7, 10, 11, 12`)**: `ADAMSOL` mesajları `chainActive.Height() +/- 2` dışındaki geçmiş/uzak gelecek yükseklikleri reddeder; `mapQuorumBlockSigs` tahliyesi görülmüş blok önerilerini korur; `PROPOSEBLOCK` içindeki `R35-1` vekil ikame vetosu `setSeenProposedBlocks` kaydı ve P2P aktarımından sonra değerlendirilir; tüm yapılandırma ve anahtar dosyası izinleri `0600` olarak kilitlenmiştir (`BULGU-10`).
