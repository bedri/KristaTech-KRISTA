# ADAM Konsensüs Quorum Direnci ve Yer Tutucu Güvenlik Analizi

## 1. Yönetici Özeti

Bu belge, KRISTA ağının **ADAM (A Decentralized Approach Model)** konsensüs çerçevesindeki quorum dirençli blok şablonu oluşturma ve doğrulama mekanizmasının güvenlik durumunu ve kriptografik güvenliğini değerlendirmektedir. 

Önceden, kooperatif madencilik döngüsü, bir bloğu başarıyla oluşturmak ve yaymak için seçilen tüm düğümlerden (Fallback Mode'da 11-14 madenci, Standard Mode'da 11 madenci) %100 yanıt oranı gerektiriyordu. Bu modelde, tek bir düğüm bile çevrimdışı olduğunda, çözümünü geciktirdiğinde veya bir ağ bölünmesi yaşadığında, Koordinatör (Coordinator) blok şablonunu oluşturamıyordu. Bu durum zincir donmalarına yol açıyor ve ağın liveness (canlılık) özelliğini tehlikeye atıyordu.

Bunu çözmek için, **Quorum Dirençli Yanıt Verme ve Yer Tutucu Mekanizması** (Quorum-Resilient Responding and Placeholder Mechanism) tanıtılmıştır:
1. Blok şablonu, minimum bir geçerli çözüm quorum'u karşılandığı sürece nihai hale getirilir ve yayılır:
   - **Versiyon 11 (Fallback Mode)**: Seçilen madencilerden en az **10** geçerli çözüm.
   - **Versiyon 12 (Standard Mode)**: 11 seçilmiş madenciden en az **7** (Mainnet/Regtest üzerinde `nAdamThreshold`) veya **3** (Testnet üzerinde) geçerli çözüm.
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
  - Blok doğrulama kuralları, en az $T$ (Fallback'te 10, Mainnet/Regtest Standard Mode'da 7 veya Testnet Standard Mode'da 3) çözümün *tamamen geçerli, boş olmayan, kriptografik olarak imzalanmış ve hedef zorluğu karşılayan* olmasını şart koşar.
  - Bir saldırgan fiziksel iş gereksinimini bypass edemez; ağın kabul edeceği bir blok oluşturmak için en az $T$ koltuk için gerekli çoklu algoritmalı hash hesaplamalarını gerçekleştirmesi gerekir.

### 3.2. Koordinatör İstismarı ve Madenci Sansürü
* **Tehdit**: Kötü niyetli bir Koordinatör, dürüst madencileri blok katılımının dışında bırakmak amacıyla çözümlerini göz ardı ederek ve bunları boş yer tutucularla değiştirerek kasıtlı olarak sansürler.
* **Önlem**:
  - Bir Koordinatörün uygulayabileceği sansür derecesi, quorum eşiğiyle kesin bir şekilde sınırlandırılmıştır.
  - Fallback Mode'da ($N \in \{11..14\}, T = 10$), Koordinatör en fazla $N - 10$ madenciyi sansürleyebilir (1 ile 4 arasında).
  - Standard Mode'da ($N = 11, T = 7$ Mainnet/Regtest üzerinde; Testnet üzerinde $T = 3$), Koordinatör en fazla $N - T$ madenciyi sansürleyebilir (Mainnet/Regtest üzerinde 4; Testnet üzerinde 8).
  - Bir Koordinatör daha fazla madenciyi sansürlemeye çalışırsa, blok tüm eş düğümlerde doğrulamadan geçemez ve reddedilir.
  - Ek olarak, Masternode ağı deterministik hibrit imzalarla desteklenen bir Verifiable Random Function (VRF) kullanarak koordinatörleri ve madencileri her blok yüksekliğinde dinamik olarak döndürdüğü için, kötü niyetli bir koordinatörün sansür uygulamak için yalnızca geçici bir fırsatı vardır. Bir madenciyi süresiz olarak devre dışı bırakamazlar.

### 3.3. Blok Ödülü ve Ödeme Güvenliği (Sansürlemek İçin Finansal Teşvik Yok)
* **Tehdit**: Bir Koordinatör, bir madencinin blok ödülündeki payını çalmak veya kendi adresine yönlendirmek için madencinin çözümünü hariç tutar.
* **Önlem**:
  - Blok ödülü dağıtımı, protokol düzeyindeki konsensüs kuralları tarafından deterministik olarak yönetilir. Ödeme adresleri, o blok yüksekliğindeki seçilmiş açık anahtarlar için `SelectAdamNodes()` sorgulanarak hesaplanır.
  - Ödül, çözümlerinin nihai blok başlığına başarıyla dahil edilip edilmediğine veya bir yer tutucuyla değiştirilip değiştirilmediğine bakılmaksızın seçilen madencilere dağıtılır.
  - Koordinatör, madenci ödemelerinin alıcı adreslerini değiştiremeyeceğinden (coinbase işlem çıktılarını değiştirme yönündeki herhangi bir girişim, bloğu deterministik konsensüs doğrulamasına göre geçersiz kılacaktır), bir madencinin çözümünü atlamaktan **sıfır finansal fayda** elde eder.

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
  - Lider seçimi, Masternode teminatı (2,100 KRISTA) gerektirir. Sybil saldırıları, dolaşımdaki arzın büyük bir yüzdesinin satın alınmasını gerektirir ve bu da saldırganın ekonomik çıkarlarını ağın istikrarı ile uyumlu hale getirir.

---

## 4. Sonuç

Quorum dirençli yer tutucu mekanizmasının getirilmesi, KRISTA ağı için büyük bir güvenlik ve sağlamlık yükseltmesini temsil etmektedir. Aşağıdaki hususları başarıyla gerçekleştirir:
* **Liveness'ı (Canlılığı) Geri Kazandırır**: Tek bir çevrimdışı veya geciken madencinin tüm blok zincirini dondurabileceği tek hata noktasını (single point of failure) ortadan kaldırır.
* **Güvenliği (Safety) Korur**: Seçilen doğrulayıcıların önemli bir çoğunluğunun aktif olarak katılması ve imzalaması gerektiğini garanti ederek, ADAM konsensüs modelinin kriptografik ve termodinamik güvenlik güvencelerini korur.
* **Kötüye Kullanım İçin Sıfır Teşvik**: Fiziksel çözümün dahil edilmesini deterministik ödül ödeme mantığından ayırarak, koordinatör sansürü için her türlü finansal teşviki ortadan kaldırır.
