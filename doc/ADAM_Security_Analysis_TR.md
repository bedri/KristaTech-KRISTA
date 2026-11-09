# ADAM Konsensüs Quorum Direnci ve Yer Tutucu Güvenlik Analizi

## 1. Yönetici Özeti

Bu belge, KRISTA ağının **ADAM (A Decentralized Approach Model)** konsensüs çerçevesindeki quorum dirençli blok şablonu oluşturma ve doğrulama mekanizmasının güvenlik durumunu ve kriptografik güvenliğini değerlendirmektedir. 

Önceden, kooperatif madencilik döngüsü, bir bloğu başarıyla oluşturmak ve yaymak için seçilen tüm düğümlerden (Fallback Mode'da 11-14 madenci, Standard Mode'da 11 madenci) %100 yanıt oranı gerektiriyordu. Bu modelde, tek bir düğüm bile çevrimdışı olduğunda, çözümünü geciktirdiğinde veya bir ağ bölünmesi yaşadığında, Koordinatör (Coordinator) blok şablonunu oluşturamıyordu. Bu durum zincir donmalarına yol açıyor ve ağın liveness (canlılık) özelliğini tehlikeye atıyordu.

Bunu çözmek için, **Quorum Dirençli Yanıt Verme ve Yer Tutucu Mekanizması** (Quorum-Resilient Responding and Placeholder Mechanism) tanıtılmıştır:
1. Blok şablonu, minimum bir geçerli çözüm quorum'u (`GetAdamThreshold(nHeight)`) karşılandığı sürece nihai hale getirilir ve yayılır:
   - **Bootstrap Aşaması (`nHeight < nAdamThresholdBootstrapLimit`, yani Mainnet'te 50.000, Testnet'te 100.000, Regtest'te 1.000. bloğun altında)**: Seçilen madencilerden en az **5** geçerli çözüm.
   - **Standart Aşama (`nHeight >= nAdamThresholdBootstrapLimit`, Versiyon 11 ve Versiyon 12)**: Seçilmiş madencilerden en az **7** (Mainnet/Regtest üzerinde `nAdamThreshold`) veya **3** (Testnet üzerinde) geçerli çözüm.
2. Eksik çözümler, blok başlığının serileştirme biçimi içinde **boş vektör yer tutucuları** (`std::vector<unsigned char>()`) kullanılarak temsil edilir.
3. Bu analiz; yer tutucu mekanizmasının konsensüs modelinin kriptografik güvenliğini koruduğunu, geriye dönük uyumluluğu sürdürdüğünü ve ağın liveness (canlılık) durumunu önemli ölçüde iyileştirirken temel saldırı vektörlerini (sahtecilik, koordinatör sansürü, ödeme hırsızlığı, kurcalama ve yeniden oynatma saldırıları dahil) azalttığını göstermektedir.

---

## 2. Teknik Mekanizma ve Doğrulama İş Akışı

### 2.1. Blok Şablonu Oluşturma Sırasında Yer Tutucu Ekleme
Koordinatör'ün staking/mining iş parçacığı (`CreateNewBlock()`) bir blok şablonu oluşturmaya çalıştığında:
1. Bloğun tohumu (seed) için aktif olarak seçilen madenciler listesini sorgular (`SelectAdamNodes()`).
2. Ağın konsensüs önbelleğinden bu seçilen madencilerin açık anahtarlarıyla (public keys) eşleşen mevcut kısmi PoW bulmaca çözümlerini getirir.
3. Toplanan geçerli çözümlerin sayısı gerekli quorum eşiğinden ($T$) azsa, blok şablonu üretimi ertelenir.
4. Geçerli çözümlerin sayısı $T$ değerine eşit veya bu değerden büyükse, ancak bazı seçilmiş madenciler eksikse, Koordinatör eksik madenci çözümlerini boş bir bayt vektörü ile değiştirir:

   $$\text{vAdamSolutions}[i] = \text{std::vector<unsigned char>()}$$

5. Bu, `vAdamSolutions` dizisinin, `vAdamMiners` içinde listelenen seçilmiş madencilerle 1:1 konumsal eşlemeyi korumasını sağlar.

### 2.2. Ağ Doğrulama Mantığı
Bir bloğu aldığında, her doğrulayan eş (peer), `CheckBlock()` ve `ContextualCheckBlockHeader()` içindeki konsensüs doğrulama kontrollerini yürütür:
1. **Dizi Sınır Doğrulaması**: `vAdamSolutions` boyutunun beklenen madenci sayısıyla (örneğin, Versiyon 12'de $N$) eşleştiğini kontrol edin.
2. **Quorum Doğrulaması**:
   - `vAdamSolutions` dizisi üzerinde döngü çalıştırın.
   - Bir çözüm boşsa (`std::vector<unsigned char>()`), bu bir yer tutucu olarak kabul edilir. Doğrulama fonksiyonu `VerifyAdamSolution()` hemen `false` döndürür (veya atlanır) ve bu yer tutucu geçerli çözümler arasında sayılmaz.
   - Bir çözüm boş değilse, eş, kısmi PoW hedef hash zorluğunu doğrular ve madencinin kriptografik imzasını seçilen madencinin açık anahtarına karşı doğrular. Her iki kontrol de geçerse, geçerli çözümlerin sayısı artırılır.
3. **Eşik Dayatması**: Kriptografik olarak doğrulanmış, boş olmayan çözümlerin sayısı gerekli eşikten ($T$) azsa, blok bir konsensüs ihlali hatası ile reddedilir.

---

## 3. Tehdit Modellemesi ve Güvenlik Önlemleri

### 3.1. Kriptografik Sahtecilik ve Bypass Koruması
* **Tehdit**: Bir saldırgan, Proof-of-Work kontrollerini atlatmak veya madenci imzalarını taklit etmek için boş yer tutucular kullanmaya çalışır ve gerekli fiziksel çözüm sayısından daha azına sahip bir blok gönderir.
* **Önlem**:
  - Boş yer tutucular, imza veya zorluk kontrollerini matematiksel olarak karşılayamazlar. Doğrulama kodu bunları açıkça başarısız çözümler olarak değerlendirir.
  - Blok doğrulama kuralları, en az $T = \text{GetAdamThreshold}(nHeight)$ (bootstrap döneminde 5, bootstrap sonrasında Mainnet/Regtest'te 7 veya Testnet'te 3) çözümün *tamamen geçerli, boş olmayan, kriptografik olarak imzalanmış ve hedef zorluğu karşılayan* olmasını şart koşar.
  - Bir saldırgan fiziksel iş gereksinimini bypass edemez; ağın kabul edeceği bir blok oluşturmak için en az $T$ koltuk için gerekli çoklu algoritmalı hash hesaplamalarını gerçekleştirmesi gerekir.

### 3.2. Koordinatör İstismarı ve Madenci Sansürü
* **Tehdit**: Kötü niyetli bir Koordinatör, dürüst madencileri blok katılımının dışında bırakmak amacıyla çözümlerini göz ardı ederek ve bunları boş yer tutucularla değiştirerek kasıtlı olarak sansürler.
* **Önlem**:
  - Bir Koordinatörün uygulayabileceği sansür derecesi, quorum eşiğiyle ($T = \text{GetAdamThreshold}(nHeight)$) kesin bir şekilde sınırlandırılmıştır.
  - Fallback Mode'da ($N \in \{11..14\}$) ve Standard Mode'da ($N = 11$), bootstrap döneminde $T = 5$, bootstrap sonrasında Mainnet/Regtest üzerinde $T = 7$ ve Testnet üzerinde $T = 3$ olduğundan, Koordinatör en fazla $N - T$ madenciyi sansürleyebilir (örneğin $N = 11$ iken Mainnet/Regtest üzerinde en fazla 4).
  - Bir Koordinatör daha fazla madenciyi sansürlemeye çalışırsa, blok tüm eş düğümlerde doğrulamadan geçemez ve reddedilir.
  - Ek olarak, Masternode ağı deterministik hibrit imzalarla desteklenen bir Verifiable Random Function (VRF) kullanarak koordinatörleri ve madencileri her blok yüksekliğinde dinamik olarak döndürdüğü için, kötü niyetli bir koordinatörün sansür uygulamak için yalnızca geçici bir fırsatı vardır. Bir madenciyi süresiz olarak devre dışı bırakamazlar.

### 3.3. Blok Ödülü ve Ödeme Güvenliği (Sansürlemek İçin Finansal Teşvik Yok)
* **Tehdit**: Bir Koordinatör, bir madencinin blok ödülündeki payını çalmak veya kendi adresine ya da kalan doğrulayıcı koltuklarına yönlendirmek için madencinin çözümünü hariç tutar.
* **Önlem**:
  - Blok ödülü dağıtımı, protokol düzeyindeki konsensüs kuralları tarafından deterministik olarak yönetilir. Ödeme adresleri, o blok yüksekliğindeki seçilmiş açık anahtarlar için `SelectAdamNodes()` sorgulanarak hesaplanır.
  - Model D (`IsModelDActive`) kapsamında **Eşit Emeğe Eşit Kazanç** prensibi geçerlidir: 11 kişilik komitedeki her $i \in \{0,\dots,10\}$ koltuğu, `vAdamSolutions[i]` içinde geçerli bir PoW çözümü sunduğu sürece — ister asıl birincil doğrulayıcı `vExpectedMiners[i]` tarafından ister asıl koltuk zaman aşımına uğradığında ($t \ge 60\text{ sn}$) devreye giren aynı kulvardaki meşru yedek doğrulayıcı `vAdamMiners[i]` tarafından çözülmüş olsun — `%25` Katılımcı Doğrulayıcı ödül havuzundan sabit `1/11` payını (`nPartSplitTotal / vMiners.size()`) alır. Çözümsüz/boş kalan koltuklar `%0` alır; sahipsiz kalan tüm katılımcı payları (`nUnclaimedPartSplit = nPartSplitTotal - nPaidPartTotal`; 0 çözüm durumunda `%25`'in tamamı) **Geliştirici Hazinesine (`nExpectedTreasury = nTreasurySplit + nUnclaimedPartSplit`)** aktarılırken Pasif Masternode Kazananı tam `%50` (`nMNSplit`) payını alır.
  - Ayrıca `qblockprop` doğrulamasında LLMQ kuorum üyeleri kendi yerel `mapAdamSolutionsCache[hashPrevBlock]` önbelleklerini kontrol eder ve `vExpectedMiners[i]`'ye ait geçerli bir çözüm yerelde mevcutken o birincil koltuğu boş bırakan veya yedek anahtarla değiştiren hiçbir blok önerisini (`QUORUMSIGSHARE`) imzalamaz. Dürüst bir koltuğun düşürülmesi ne Koordinatörün `%15` ücretini, ne Pasif Masternode Kazananının `%50` payını, ne de diğer aktif koltukların sabit `1/11` ödülünü artırdığından, Koordinatörün dürüst doğrulayıcıları sansürlemek için **sıfır finansal teşviki** vardır.

### 3.4. Yeniden Oynatma ve Kurcalama Saldırıları
* **Tehdit**: Bir saldırgan, önceki bir bloktaki geçerli çözümleri yeniden oynatır veya madenciler çözümlerini imzaladıktan sonra blok şablonundaki işlem verilerini değiştirmeye çalışır.
* **Önlem**:
  - **Tohum Bağlama (Seed Binding)**: Her madenci, hareketli seçim tohumundan türetilen benzersiz bir bulmaca hash'ini imzalar:

    $$\text{PuzzleHash} = \text{CalculateAdamPuzzleHash}\left(\text{algoIndex}, \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i\right)$$

    Hareketli tohum `Seed_H`, önceki bloğun VRF kanıtından türetilir ve mevcut blok yüksekliğine özeldir. $H$ yüksekliği için imzalanmış bir çözüm, tohumlar eşleşmeyeceğinden $H+1$ yüksekliğinde yeniden oynatılamaz ve bu da imza doğrulamasının başarısız olmasına neden olur.
  - **Başlık Bütünlüğü (Header Integrity)**: Nihai blok başlığı hash'i tüm işlemleri (`hashMerkleRoot` aracılığıyla), blok zamanını, önceki blok hash'ini, VRF kanıtını ve seçilmiş madencilerin listesini bağlar.
  - **Çifte İmzalar (Dual Signatures)**: Blok şablonu (yer tutucularla veya yer tutucusuz) nihai hale getirildikten sonra Koordinatör, hibrit BLS12-381 + ECDSA fallback imza mekanizmasını (`SignBLSWithECDSAFallback` / `VerifyBLSWithECDSAFallback`) kullanarak tüm blok başlığı hash'ini (`vAdamCoordinatorSig`) imzalar; burada bir BLS imzası, ilgili BLS açık anahtarının bir ECDSA imzasıyla yetkilendirilir. Yükseklik $\ge 200$ (Cooperative PoS) olduğunda, staker da staking anahtarını (`vchBlockSig`) kullanarak bloğu imzalar. İşlemlerde veya blok meta verilerinde yapılacak herhangi bir değişiklik bu kapsayıcı imzaları geçersiz kılarak aracı düğümler tarafından sonradan yapılacak herhangi bir kurcalamayı önler.

### 3.5. Nothing-at-Stake ve Sybil Direnci
* **Tehdit**: Saldırganlar sıfır maliyetle birden fazla çatallanma (fork) üzerinde blok imzalar veya lider seçimini domine etmek için sanal düğümler oluşturur.
* **Önlem**:
  - Seçilen $T$ düğümü üzerinde fiziksel PoW hesaplaması gereksinimi, rakip çatallanmalar üzerinde blok oluşturmanın hesaplama açısından pahalı kalmasını sağlar. Birden fazla çatallanma üzerinde staking yapmak ücretsiz değildir ve bu durum nothing-at-stake riskini azaltır.
  - **Çift Kulvarlı Kota Seçimi (`ElectDualTrackValidators`, `nAdamDualTrackHeight = UPGRADE_ADAM` yüksekliğinde aktif)** kapsamında, 11 doğrulayıcı koltuğu **1. Kulvar (8 Teminatlı Koltuk: 7 Aktif Masternode [`4.200 KRISTA`] + 1 Coin-Lock [`1.000 KRISTA`], Kulvar-1 içi taşma destekli)** ve **2. Kulvar (3 Açık PoW Koltuğu: `10 KRISTA` `PoW-Lock` / `Zero-Coin PoW`)** olarak ikiye ayrılmıştır; aktif Masternode sayısı eşik değerini aştığında Koordinatör doğrudan aktif Masternode havuzundan seçilir. `ADAM_MASTERNODE_TRACK_QUOTA = 7` hem derleme anında (`static_assert`) hem de `chainparams.cpp` içinde çalışma anında (`assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold)`) kilitlendiğinden, Masternode olmayan 4 koltuğun tamamı çözüm göndermeyi kesse dahi Aktif Masternode'lar bootstrap sonrasındaki en yüksek eşik değerini (`GetAdamThreshold = 7`) tek başına karşılar. Ayrıca Masternode olmayan koltuklar susarken seçilen bir Masternode çevrimdışı kalırsa, **Yedek Doğrulayıcı Koltuk Devri (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60 sn`)** sessiz koltukları bloğun `CCoinsViewCache* pView` görünümüne göre yalnızca aynı kulvardaki (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Açık PoW -> Açık PoW`) sıradaki yedek adaylara (çözüm üreten her koltuğa eşit `1/11` katılımcı ödülü, `qblockprop` içinde birincil çözümler için LLMQ sansür koruması ve boş koltuk paylarının Geliştirici Hazinesine aktarılmasıyla) devreder ve **Kademeli Eşik Gevşetme (`ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS = 120 sn`)** eşiği her 60 saniyelik slotta 1 düşürerek bootstrap sonrasında VEYA aktif Masternode sayısı $\ge 7$ olduğunda asgari `ADAM_MIN_RELAXED_THRESHOLD = 5` (`> 4` MN-dışı koltuk; en az 1 Aktif Masternode zorunlu) tabanına, yalnızca bootstrap döneminde aktif Masternode sayısı $< 7$ iken `3` tabanına kadar gevşeterek kalıcı blok tıkanmalarını tamamen engeller.

---

## 4. Sonuç

Quorum dirençli yer tutucu mekanizmasının getirilmesi, KRISTA ağı için büyük bir güvenlik ve sağlamlık yükseltmesini temsil etmektedir. Aşağıdaki hususları başarıyla gerçekleştirir:
* **Liveness'ı (Canlılığı) Geri Kazandırır**: Tek bir çevrimdışı veya geciken madencinin tüm blok zincirini dondurabileceği tek hata noktasını (single point of failure) ortadan kaldırır.
* **Güvenliği (Safety) Korur**: Seçilen doğrulayıcıların önemli bir çoğunluğunun aktif olarak katılması ve imzalaması gerektiğini garanti ederek, ADAM konsensüs modelinin kriptografik ve termodinamik güvenlik güvencelerini korur.
* **Kötüye Kullanım İçin Sıfır Teşvik**: Fiziksel çözümün dahil edilmesini deterministik ödül ödeme mantığından ayırarak, koordinatör sansürü için her türlü finansal teşviki ortadan kaldırır.
