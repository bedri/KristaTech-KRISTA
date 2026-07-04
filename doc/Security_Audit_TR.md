# ADAM & MPA Konsensüs Güvenlik Denetim Raporu

Bu rapor, KRISTA kod tabanında uygulanan **ADAM (A Decentralized Approach Model)** ve **MPA (Multi-Proof Algorithm)** işbirlikçi konsensüs mekanizmalarının güvenlik duruşunu, mantığını ve olası çalışma zamanı (runtime) açıklarını, yeni entegre edilen çoklu algoritmalı bulmaca madenciliği (multi-algorithm puzzle mining) ve yedek quorum ölçeklendirmesini (fallback quorum scaling) içerecek şekilde değerlendirmektedir.

---

## 1. Tehdit Modellemesi: Yaygın Saldırı Vektörlerine Karşı Güvenlik

Bu bölüm, hibrit ADAM/MPA konsensüs modelinin blokzincir ağlarındaki en yaygın ve kritik saldırı vektörlerini nasıl azalttığını analiz etmektedir.

### 1.1. %51 Hash Gücü (Hashpower) Saldırısı / Kartel Tekelleşmesi
* **Geleneksel Güvenlik Açığı**: Standart PoW'da, ağın hash gücünün %51'ini kontrol eden bir aktör çifte harcama (double-spend) yapabilir, blokları yeniden düzenleyebilir (reorganize blocks) ve işlemleri sansürleyebilir.
* **ADAM/MPA Önlemi**: 
  - Yalnızca hash gücü (hashpower) yeterli değildir. Blok üretimine katılmak için bir düğümün (node) 11 madenciden biri veya koordinatör (coordinator) olarak seçilmesi gerekir.
  - Seçim havuzu, kilitli teminat (2.100 KRISTA) ile güvence altına alınan Masternode ağıdır.
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
  - Bu bulmacaları çözmek, gerçek C++ hashleme döngülerinin çalıştırılmasını gerektirir (13 algoritmadan biri kullanılarak).
  - Madenciler, rakip her bir çatallanma için bulmacaları çözmek adına gerçek fiziksel işlemci gücü (CPU/GPU döngüleri) harcamak zorunda olduklarından, birden fazla çatallanmada stake etmenin maliyeti sıfır değildir. Birden fazla çatallanmada stake etmek hesaplama açısından maliyetlidir ve bu durum nothing-at-stake açığını çözer.

### 1.5. Düğüm Seçiminde Sybil Saldırıları
* **Geleneksel Güvenlik Açığı**: Bir saldırgan, akran (peer) listesini domine etmek ve tüm lider seçimlerini kazanmak için binlerce sanal düğüm (node) oluşturur.
* **ADAM/MPA Önlemi**: 
  - Uygun düğümlerin havuzu, kesin bir şekilde etkinleştirilmiş Masternode listesi ile sınırlıdır.
  - Bir Masternode kurmak teminat (collateral) gerektirir. Bir Sybil saldırısı, büyük miktarda coin satın almayı ve kilitlemeyi gerektirir.
  - Bir saldırgan bir Sybil saldırısı gerçekleştirmek için coinlerin çoğunluğunu satın almaya çalışırsa, piyasa arzı düşer, bu da coin fiyatını yukarı çeker ve saldırının maliyetini önemli ölçüde artırır; aynı zamanda saldırganın sermayesini ağın değerine bağlar.

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
