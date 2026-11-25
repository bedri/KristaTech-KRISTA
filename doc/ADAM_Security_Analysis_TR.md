# ADAM Konsensüs Quorum Direnci ve Yer Tutucu Güvenlik Analizi

## 1. Yönetici Özeti

Bu belge, KRISTA ağının **ADAM (A Decentralized Approach Model)** konsensüs çerçevesindeki quorum dirençli blok şablonu oluşturma ve doğrulama mekanizmasının güvenlik durumunu ve kriptografik güvenliğini değerlendirmektedir. 

Önceden, kooperatif madencilik döngüsü, bir bloğu başarıyla oluşturmak ve yaymak için seçilen tüm düğümlerden (Fallback Mode'da 11-14 madenci, Dengeli Model D Standard Mode'da 15 madenci / Model D öncesinde 11 madenci) %100 yanıt oranı gerektiriyordu. Bu modelde, tek bir düğüm bile çevrimdışı olduğunda, çözümünü geciktirdiğinde veya bir ağ bölünmesi yaşadığında, Koordinatör (Coordinator) blok şablonunu oluşturamıyordu. Bu durum zincir donmalarına yol açıyor ve ağın liveness (canlılık) özelliğini tehlikeye atıyordu.

Bunu çözmek için, **Quorum Dirençli Yanıt Verme ve Yer Tutucu Mekanizması** (Quorum-Resilient Responding and Placeholder Mechanism) tanıtılmıştır:
1. Blok şablonu, minimum bir geçerli çözüm quorum'u (`GetAdamThreshold(nHeight)`) karşılandığı sürece nihai hale getirilir ve yayılır:
   - **Bootstrap Aşaması (`nHeight < nAdamThresholdBootstrapLimit`, yani Mainnet'te 50.000, Testnet'te 100.000, Regtest'te 1.000. bloğun altında)**: Seçilen madencilerden (Model D altında `15`, öncesinde `11` koltuk arasından) en az **5** geçerli çözüm.
   - **Standart Aşama (`nHeight >= nAdamThresholdBootstrapLimit`, Versiyon 11 ve Versiyon 12)**: Dengeli Model D kapsamında `15` madenciden en az **8** (`nAdamThresholdModelD = 8` Mainnet/Regtest üzerinde; Model D öncesinde `11`'den **7**) veya Testnet üzerinde **3** geçerli çözüm.
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

   $$
   \text{vAdamSolutions}[i] = \text{std::vector}{<}\text{unsigned char}{>}\text{()}
   $$

5. Bu, `vAdamSolutions` dizisinin, `vAdamMiners` içinde listelenen seçilmiş madencilerle 1:1 konumsal eşlemeyi korumasını sağlar.

### 2.2. Ağ Doğrulama Mantığı
Bir bloğu aldığında, her doğrulayan eş (peer), `CheckBlock()` ve `ContextualCheckBlockHeader()` içindeki konsensüs doğrulama kontrollerini yürütür:
1. **Dizi Sınır Doğrulaması**: `vAdamSolutions` boyutunun beklenen madenci sayısıyla (örneğin Dengeli Model D Standart Modda $N = 15$, Model D öncesinde $N = 11$) eşleştiğini kontrol edin.
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
  - Blok doğrulama kuralları, en az $T = \text{GetAdamThreshold}(nHeight)$ (bootstrap döneminde `5`, bootstrap sonrasında Mainnet/Regtest'te Model D ile `15`'ten `8` [Model D öncesinde `11`'den `7`] veya Testnet'te `3`) çözümün *tamamen geçerli, boş olmayan, kriptografik olarak imzalanmış ve hedef zorluğu karşılayan* olmasını şart koşar.
  - Bir saldırgan fiziksel iş gereksinimini bypass edemez; ağın kabul edeceği bir blok oluşturmak için en az $T$ koltuk için gerekli çoklu algoritmalı hash hesaplamalarını gerçekleştirmesi gerekir.

### 3.2. Koordinatör İstismarı ve Madenci Sansürü
* **Tehdit**: Kötü niyetli bir Koordinatör, dürüst madencileri blok katılımının dışında bırakmak amacıyla çözümlerini göz ardı ederek ve bunları boş yer tutucularla değiştirerek kasıtlı olarak sansürler.
* **Önlem**:
  - Bir Koordinatörün uygulayabileceği sansür derecesi, quorum eşiğiyle ($T = \text{GetAdamThreshold}(nHeight)$) kesin bir şekilde sınırlandırılmıştır.
  - Fallback Mode'da ($N \in \lbrace 11, \dots, 14 \rbrace$, POMBL öncesi Model D aktifken en fazla $18$) ve Standard Mode'da (Model D altında $N = 15$; Model D öncesinde $N = 11$), bootstrap döneminde $T = 5$, bootstrap sonrasında Mainnet/Regtest üzerinde Model D ile $T = 8$ (öncesinde $7$) ve Testnet üzerinde $T = 3$ olduğundan, Koordinatör en fazla $N - T$ madenciyi sansürleyebilir (örneğin Model D altında $N = 15$ ve $T = 8$ iken Mainnet/Regtest üzerinde en fazla `7`; Model D öncesinde $N = 11$ ve $T = 7$ iken en fazla `4`).
  - Bir Koordinatör daha fazla madenciyi sansürlemeye çalışırsa, blok tüm eş düğümlerde doğrulamadan geçemez ve reddedilir.
  - Ek olarak, `evodb` üzerinde deterministik olarak izlenen Masternode ağı (`KTDIP3` / `UPGRADE_DIP3`), her blok yüksekliğinde koordinatörleri ve madencileri deterministik BLS12-381 imzalarıyla (`UPGRADE_DIP3 = 1` Mainnet ve Testnet sonrasında KTDIP3 Masternode'lar için `SignBLSOperatorOnly` / `VerifyAdamOrDip3Signature`, öncesinde ve MN dışı doğrulayıcılar için `SignBLSWithECDSAFallback`) çalışan VRF ile döndürdüğünden, kötü niyetli bir koordinatör yalnızca geçici bir fırsat bulur; hiçbir madenciyi süresiz devre dışı bırakamaz.

### 3.3. Blok Ödülü ve Ödeme Güvenliği (Sansürlemek İçin Finansal Teşvik Yok)
* **Tehdit**: Bir Koordinatör, bir madencinin blok ödülündeki payını çalmak veya kendi adresine ya da kalan doğrulayıcı koltuklarına yönlendirmek için madencinin çözümünü hariç tutar.
* **Önlem**:
  - Blok ödülü dağıtımı, protokol düzeyindeki konsensüs kuralları tarafından deterministik olarak yönetilir. Ödeme adresleri, o blok yüksekliğindeki seçilmiş açık anahtarlar için `SelectAdamNodes()` sorgulanarak hesaplanır.
  - Dengeli Model D (`IsModelDActive` ve Blok 1'den itibaren aktif `UPGRADE_FAIRNESS`) kapsamında **Eşit Emeğe Eşit Kazanç** prensibi 15 kişilik komitenin tamamında (`8 MN + 2 Coin-Lock + 3 Fonlu PoW-Lock + 2 Ücretsiz Giriş`) geçerlidir: her $i \in \lbrace 0, \dots, 14 \rbrace$ koltuğu, `vAdamSolutions[i]` içinde geçerli bir PoW çözümü (zorunlu 33 baytlık sıkıştırılmış pubkey ile) sunduğu sürece — ister asıl birincil doğrulayıcı `vExpectedMiners[i]` tarafından ister asıl koltuk zaman aşımına uğradığında ($t \ge 60\text{ sn}$) devreye giren aynı kulvardaki meşru yedek doğrulayıcı `vAdamMiners[i]` (yedek indeksler Model D'de `m >= 15`, Model D öncesinde `m = 11..16`) tarafından çözülmüş olsun — kulvar ayrımı olmaksızın eşit **`%60 / 15 = %4,00`** (Dönem 0'da `0,6324 KRISTA`, tüm `15/15` koltuk çözüldüğünde sıfır tamsayı yuvarlama kalanı; Model D öncesinde `11` koltukta `%75 / 11 ≈ %6,818` / `1,07795454 KRISTA`) koltuk payını alır. Çözümsüz/boş koltuklar `%0` alırken, Koordinatör yedek doğrulayıcılarla bloğu kurtardığında tam **`%25`** üretici payını (`nProducerSplit` = Dönem 0'da `3,9525 KRISTA`; PoS'ta **`%22` Staker [`3,4782 KRISTA`] + `%3` İmzacı Koordinatör [`0,4743 KRISTA`] + `%100` `nFees`**; Model D öncesinde `%20 + %5`) korur. Boş koltuk veya imzasız LLMQ payları (`nUnclaimedPartSplit`) **%50 Otomatik Protokol Yakımına (`nExpectedBurn = nUnclaimedPartSplit / 2`)** ve **%50 Geliştirici Hazinesine (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)** bölüştürülürken Pasif Masternode Kazananı tam **`%5`** (`nMNSplit` = Dönem 0'da `0,7905 KRISTA`) ve önceki blok `prevBlock.vQuorumSig` imzacıları **`%10`** (`%10 / N`, Dönem 0'da `1,5810 KRISTA`, 1-blok gecikmeli, `-5/+5` PoSe) LLMQ payını alır.
  - Ayrıca `qblockprop` doğrulamasında (yalnızca LLMQ üyesinin mevcut ucu `msg.block.hashPrevBlock` ile eşleştiğinde) $t \ge 60\text{ sn}$ olduğunda LLMQ kuorum üyeleri kendi yerel `mapAdamSolutionsCache[hashPrevBlock]` önbelleklerini kontrol eder ve `vExpectedMiners[i]`'ye ait geçerli bir çözüm yerelde mevcutken o birincil koltuğu yedek anahtarla değiştiren (`vAdamMiners[i] != vExpectedMiners[i]`) hiçbir blok önerisini (`QUORUMSIGSHARE`) imzalamaz. Dürüst bir koltuğun düşürülmesi veya yedekle değiştirilmesi ne Koordinatörün net kazancını (`0` net kazanç), ne Pasif Masternode Kazananının `%5` payını, ne de diğer aktif koltukların sabit ödülünü artırdığından, Koordinatörün dürüst doğrulayıcıları sansürlemek için **sıfır finansal teşviki** vardır.

### 3.4. Yeniden Oynatma ve Kurcalama Saldırıları
* **Tehdit**: Bir saldırgan, önceki bir bloktaki geçerli çözümleri yeniden oynatır veya madenciler çözümlerini imzaladıktan sonra blok şablonundaki işlem verilerini değiştirmeye çalışır.
* **Önlem**:
  - **Tohum Bağlama (Seed Binding)**: Her madenci, hareketli seçim tohumundan türetilen benzersiz bir bulmaca hash'ini imzalar (`Challenge = Seed_H || CompactSize(MinerPubKey_i) || MinerPubKey_i || LE32(Nonce_i)`):
    - **Sürüm 11** (`UPGRADE_ADAM` – `UPGRADE_POMBL - 1`): tek aşamalı `CalculateAdamPuzzleHash(GetAdamPuzzleAlgo(Seed_H, MinerPubKey_i), Challenge)`.
    - **Sürüm 12** (`UPGRADE_POMBL`+): `Challenge` üzerinde ardışık çalışan 3 aşamalı bileşik permütasyon `GetAdam3PermutationAlgos(hashPrevBlock, MinerPubKey_i)`.

    Hareketli tohum `Seed_H`, önceki bloğun VRF kanıtından türetilir ve mevcut blok yüksekliğine özeldir. $H$ yüksekliği için imzalanmış bir çözüm, tohumlar (ve `hashPrevBlock`) eşleşmeyeceğinden $H+1$ yüksekliğinde yeniden oynatılamaz ve bu da imza ve zorluk doğrulamasının başarısız olmasına neden olur.
  - **Başlık Bütünlüğü (Header Integrity)**: Nihai blok başlığı hash'i (`CBlockHeader::GetHash()`, `fFallbackMode = (nVersion == 11) || (vAdamMiners.size() == vAdamSolutions.size() + 1)` koşulunu kullanarak tüm `15` Sürüm 12 koltuğunu 3-permütasyonlu modda özetler) tüm işlemleri (`hashMerkleRoot` aracılığıyla), blok zamanını, önceki blok hash'ini, VRF kanıtını ve seçilmiş madencilerin listesini bağlar.
  - **Çok Katmanlı Blok İmzaları**: Blok şablonu (yer tutucularla veya yer tutucusuz) nihai hale getirildikten sonra Koordinatör tüm blok başlığı hash'ini (`vAdamCoordinatorSig`, **KTDIP3 (`UPGRADE_DIP3 = 1` Mainnet ve Testnet)** kapsamında saf `blsPubKeyOperator`, aksi halde/MN dışı durumda hibrit BLS+ECDSA olacak şekilde `VerifyAdamOrDip3Signature` / `VerifyBLSWithECDSAFallback`) imzalar ve `UPGRADE_MODELD` aktifleştiğinde aktif MN sayısına göre dinamik ölçeklenen (`5–15` üyeli) LLMQ kurulu bloğu katı BFT çoğunluğu (`3-of-5`, `5-of-9`, `6-of-11` veya `8-of-15`) birleştirilmiş BLS12-381 korum sertifikası (`vQuorumSig`) ile ortak imzalar. Yükseklik $\ge 200$ olan **İşbirlikçi PoS bloklarında (`IsProofOfStake()`)** staker ayrıca staking anahtarını (`vchBlockSig`) kullanarak bloğu imzalar; ADAM PoW bloklarında ise `vchBlockSig` boş bırakılır. İşlemlerde veya blok meta verilerinde yapılacak herhangi bir değişiklik bu kapsayıcı imzaları geçersiz kılarak aracı düğümler tarafından sonradan yapılacak herhangi bir kurcalamayı önler.

### 3.5. Nothing-at-Stake ve Sybil Direnci
* **Tehdit**: Saldırganlar sıfır maliyetle birden fazla çatallanma (fork) üzerinde blok imzalar veya lider seçimini domine etmek için sanal düğümler oluşturur.
* **Önlem**:
  - Seçilen $T$ düğümü üzerinde fiziksel PoW hesaplaması gereksinimi, rakip çatallanmalar üzerinde blok oluşturmanın hesaplama açısından pahalı kalmasını sağlar. Birden fazla çatallanma üzerinde staking yapmak ücretsiz değildir ve bu durum nothing-at-stake riskini azaltır.
  - **Çift Kulvarlı Kota Seçimi (`ElectDualTrackValidators`, `nAdamDualTrackHeight = UPGRADE_ADAM`)**, **`UPGRADE_FAIRNESS` (Yükseklik `1`)** ve **`UPGRADE_MODELD` (`5.000` Mainnet / `500` Testnet / `200` Regtest)** kapsamında, **15 doğrulayıcı koltuğu** (Model D öncesinde `11`) **1. Kulvar (`10` Teminatlı Koltuk: `8` Aktif KTDIP3 Masternode [`4.200 KRISTA`, `%32` doğrulayıcı payı] + `2` kesin izole Coin-Lock [`250 KRISTA`, `%8`])** ve **2. Kulvar (`5` Açık PoW Koltuğu [`%20`]: `nAdamOpenEntryHeight` [`2.500` Mainnet, `1` Testnet/Regtest] itibarıyla `3` Fonlu `10 KRISTA` PoW-Lock / Bootstrap [`%12`] + `2` kalıcı `0 KRISTA` Açık Giriş koltuğu [`%8`]**, seçilemeyen Coin-Lock'lar `Kademe 2A` dışına çıkarılarak, `AutoRegisterMiner` otomatik Coin-Lock'u Model D altında `< 2` koltuğa [öncesinde `< 1`] bağlayarak, `nAdamOpenEntryHeight` öncesi `GetAdamMinersCount(h) + 2 = 17` fonlu havuz sınırı [öncesinde `13`], `240` Zero-Coin anahtar kapasitesi [`MAX_ZEROCOIN_BOOTSTRAP_MINERS_FAIRNESS`], Mainnet 15-bit `HashX11KVS` PoW, `10` blokluk challenge penceresi ve Mainnet'te `240` / Model D sonrası Testnet & Regtest'te `50` blokluk kiralama ömrüyle; Model D öncesi `7 + 1 + 3`) olarak ikiye ayrılmıştır; Koordinatör ise **Orantılı Kilitli Sermaye VRF (kulvar içi temsilci tavanı olmaksızın Aktif MN için `4.200`, `Coin-Lock` için `250`, `Fonlu PoW-Lock` için `10`: Model D altında `11 * sumNonMN > 9 * sumMN` iken kesin `>= %55` Aktif MN tabanı zorunlu kılan `ADAM_FAIR_COORD_V5`, Model D öncesinde `3 * sumNonMN > sumMN` iken `>= %75` tabanı uygulayan `ADAM_FAIR_COORD_V4`, fonlu bir doğrulayıcı varken `0-KRISTA` Open-Entry madencileri için `0` ağırlık)** ile seçilir. Boş komite koltukları katı sırayla doldurulur: **Aktif Masternode $\to$ Açık PoW $\to$ Coin-Lock (son çare)**. `ADAM_MODELD_MASTERNODE_TRACK_QUOTA = 8` hem derleme anında (`static_assert`) hem de çalışma anında (`assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThresholdModelD)`) kilitlendiğinden, **`7` MN-dışı koltuk (`2 Coin-Lock + 5 Açık PoW`) bootstrap sonrası `8` eşiğinin kesinlikle altındadır (`7 < 8`) — dolayısıyla MN-dışı koltuklar tek başına Aktif Masternode'ları asla aşamazken, `8` Aktif Masternode koltuğu (`8 >= 8`) tüm `7` MN-dışı koltuk sessiz kalsa dahi canlılığı tek başına garanti eder** (Model D öncesinde `4` MN-dışı koltuğa karşı `7 >= 7`). Ayrıca **Yedek Doğrulayıcı Koltuk Devri (`60 sn`)** ile çözüm üreten her koltuğa kulvar farkı olmaksızın **eşit `%60 / 15 = %4,00`** (`0,6324 KRISTA` Dönem 0'da, `15/15`'te `0` kalan; Model D öncesinde `%75 / 11`) ödül ve Kurtarıcı Koordinatöre tam **`%25`** pay verilirken boş koltuk payları %50 Yakım + %50 Hazineye ayrılır ve **Kademeli Eşik Gevşetme (`120 sn`)** Model D'de bootstrap sonrasında (ve $\ge 8$ MN varken) **`ADAM_MODELD_MIN_RELAXED_THRESHOLD = 8`** (`> 7` MN-dışı koltuk, daima en az `1` Aktif MN zorunlu; yalnızca `< 50.000` ve `< 8` MN bootstrap'ında `3`'e kadar gevşer; Model D öncesinde `5 > 4` MN-dışı koltuk) tabanıyla kalıcı tıkanmaları engeller.

---

## 4. Sonuç

Quorum dirençli yer tutucu mekanizmasının getirilmesi, KRISTA ağı için büyük bir güvenlik ve sağlamlık yükseltmesini temsil etmektedir. Aşağıdaki hususları başarıyla gerçekleştirir:
* **Liveness'ı (Canlılığı) Geri Kazandırır**: Tek bir çevrimdışı veya geciken madencinin tüm blok zincirini dondurabileceği tek hata noktasını (single point of failure) ortadan kaldırır.
* **Güvenliği (Safety) Korur**: Seçilen doğrulayıcıların önemli bir çoğunluğunun aktif olarak katılması ve imzalaması gerektiğini garanti ederek, ADAM konsensüs modelinin kriptografik ve termodinamik güvenlik güvencelerini korur.
* **Kötüye Kullanım İçin Sıfır Teşvik**: Fiziksel çözümün dahil edilmesini deterministik ödül ödeme mantığından ayırarak, koordinatör sansürü için her türlü finansal teşviki ortadan kaldırır.
