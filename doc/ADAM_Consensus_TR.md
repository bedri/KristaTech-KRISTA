# ADAM (A Decentralized Approach Model) İşbirlikçi Konsensüsü

## 1. Giriş ve Arka Plan

**ADAM (A Decentralized Approach Model)** konsensüs mekanizması; blok zincirini madencilik merkezileşmesine, bencil madenciliğe (selfish mining), blok grinding işlemlerine ve blok liderlerine yönelik hedefli Denial-of-Service (DoS) saldırılarına karşı güçlendirmek için tasarlanmış işbirlikçi bir hibrit konsensüs modelidir. 

Geleneksel Proof-of-Work (PoW) ve Proof-of-Stake (PoS) protokolleri belirgin güvenlik açıklarına sahiptir:
* PoW modelinde, devasa işlem (hashing) gücüne sahip madenciler blok üretimine hakim olur ve bu durum konsensüsü merkezileştiren havuzların oluşmasına yol açar.
* PoS modelinde, en zengin stake eden cüzdanların blok üretme olasılığı orantısız şekilde daha yüksektir.
* Her iki modelde de, bir sonraki blok üreticisinin kimliği önceden biliniyorsa (önceden seçilmişse), DoS saldırılarının veya rüşvet girişimlerinin hedefi haline gelirler. Eğer blok içerikleri bir sonraki bloğun rastgeleliğini değiştirecek şekilde manipüle edilebiliyorsa, madenciler gelecekteki lider seçimlerini saptırmak için *grinding saldırıları* gerçekleştirebilirler.

ADAM, blok üretim sürecini bir **İşbirlikçi Hibrit Turuna** bölerek bu sorunları çözer:
1. **Seçim Aşaması (Election Phase)**: Her blok slotu için, sözde rastgele bir seed deterministik olarak **$N$ adet Madenci (Miners)** ve **1 adet Koordinatör (Coordinator)** havuzunu seçer.
2. **Çözme Aşaması (Solving Phase)**: Seçilen Madenciler, mevcut bloğun hedefine bağlı olarak hafif, paralel hale getirilmiş PoW bulmacalarını (kısmi çözümler) derler ve çözer.
3. **Birleştirme Aşaması (Aggregation Phase)**: Seçilen Koordinatör kısmi çözümleri toplar, doğrular, bunları bir blok şablonu (block template) halinde paketler, kendi gizli anahtarını (private key) kullanarak nihai blok başlığını (block header) imzalar ve bunu ağa yayınlar.

---

## 2. Konsensüs Mimarisi ve Parametreleri

ADAM konsensüsü, blok yüksekliğine bağlı olarak koşullu bir şekilde etkinleştirilir. Temel parametreler `src/consensus/params.h` dosyasında tanımlanmıştır ve ağ bazında `src/chainparams.cpp` dosyasında yapılandırılmıştır.

Aktif Masternode sayısının düşük olduğu durumlarda ağın sorunsuz bir şekilde bootstrap (ilk kurulum) yapabilmesini sağlamak için ADAM, bir ağ Spork'u tarafından dinamik olarak kontrol edilen iki çalışma modunu destekler.

### A. Fallback Mode (Geri Çekilme Modu - Blok Versiyon 11)
Fallback Mode, ağın bootstrap aşaması için tasarlanmıştır. Aktif Masternode kaydı gerektirmeden çalışır ve tamamen kendi kendine yeten (self-contained) bir yapıdadır.
* **Madenci Sayısı ($N$)**: $11 \le K \le 14$ olmak üzere, $K-1$ boyutundaki anahtar havuzuna (key pool) göre dinamik olarak belirlenir (anahtarlar deterministiktir ve seçilen Koordinatör `vAdamMiners` dizisinin son elemanı olarak eklenir).
* **Konsensüs Eşiği ($T$)**: Her iki modda da `nAdamThreshold` konsensüs parametresinde sabitlenmiştir (bu değer Mainnet/Regtest üzerinde **`7`** iken, yerel testleri/blok koordinasyonunu kolaylaştırmak amacıyla Testnet üzerinde **`3`** olarak ayarlanmıştır).
* **Kendi Kendine Yeten Başlık Düzeni**: `vAdamMiners`, tüm $K$ açık anahtarlarını depolar (ilk $K-1$ seçilen madenciler, son anahtar koordinatördür). `vAdamSolutions` ise $K-1$ kısmi çözümü depolar.

### B. Standart Mod (Blok Versiyon 12)
Standart Mod, tam bir Masternode ağının mevcut olmasını gerektiren, eksiksiz işbirlikçi konsensüs durumunu temsil eder.
* **Madenci Sayısı ($N$)**: `nAdamMinersCount` konsensüs parametresi aracılığıyla yapılandırılır (`src/consensus/params.h` dosyasında tanımlanmış ve `src/chainparams.cpp` içinde Mainnet/Testnet/Regtest için `11` olarak başlatılmıştır).
* **Konsensüs Eşiği ($T$)**: `nAdamThreshold` konsensüs parametresi aracılığıyla yapılandırılır (`src/consensus/params.h` dosyasında tanımlanmış ve `src/chainparams.cpp` içinde Mainnet/Regtest için **`7`**, Testnet için **`3`** olarak başlatılmıştır).
* **Seçilen Koordinatör**: Koordinatör, aktif Masternode listesinden dinamik olarak seçilir ve madenci listesinden farklıdır.

### C. Spork Kontrollü Etkinleştirme (`SPORK_21_ADAM_STANDARD_MODE`)
Fallback Mode (Versiyon 11) ile Standart Mod (Versiyon 12) arasındaki geçiş `SPORK_21_ADAM_STANDARD_MODE` (Spork ID `10020`) tarafından kontrol edilir.
* **Varsayılan Değer**: `4070908800ULL` (KAPALI).
* **Davranış**:
  - Spork aktif ise: Bloklar, Standart Mod kuralları altında **Versiyon 12** olarak oluşturulur.
  - Spork aktif değilse: Bloklar, Fallback Mode kuralları altında **Versiyon 11** olarak oluşturulur.

### Ağ Yapılandırmaları
| Ağ | Etkinleştirme Yüksekliği (`Consensus::UPGRADE_ADAM`) | Varsayılan Mod | Hedef Blok Süresi (Target Spacing) |
| :--- | :--- | :--- | :--- |
| **Mainnet** | 200 | Fallback (Versiyon 11) | 30 saniye |
| **Testnet** | 200 | Fallback (Versiyon 11) | 30 saniye |
| **Regtest** | 200 | Fallback (Versiyon 11) | 10 saniye |

### D. Bulmaca Zorluğu Bit Kaydırma (Bit-Shift) Parametreleri
Farklı blok yüksekliklerinde dinamik ve yapılandırılabilir zorluk ölçeklendirmesini korurken, ağ üzerinde bulmaca çözümlerinin "yanlış pozitif seline" (false-positive flood) yol açmasını önlemek için ADAM, `Consensus::Params` içinde üç parametre kullanır:
* **V1 Zorluk Kaydırması (`nAdamDifficultyShiftV1`)**: Kaydırma yüksekliğinin altındaki blok yükseklikleri için gevşetilmiş hedef zorluğun bit kaydırma çarpanı. Varsayılan değer: `10` (veya `12`).
* **V2 Zorluk Kaydırması (`nAdamDifficultyShiftV2`)**: Kaydırma yüksekliğinde veya üzerindeki blok yükseklikleri için gevşetilmiş hedef zorluğun bit kaydırma çarpanı. Varsayılan değer: `6`.
* **Kaydırma Yüksekliği (`nAdamDifficultyShiftHeight`)**: Zorluk geçişinin gerçekleştiği blok yüksekliği eşiği. Varsayılan değer: `705`.

Bulmaca doğrulaması sırasında, `scaledTarget` değeri, konsensüs hedefinin (`Target`) mevcut aktif zorluk kaydırma değeri kadar kaydırılmasıyla elde edilir:
$$\text{scaledTarget} = \text{Target} \ll \text{activeShift}$$

* Blok yüksekliği $< \text{nAdamDifficultyShiftHeight}$ ise $\text{activeShift} = \text{nAdamDifficultyShiftV1}$.
* Blok yüksekliği $\ge \text{nAdamDifficultyShiftHeight}$ ise $\text{activeShift} = \text{nAdamDifficultyShiftV2}$.

### E. Başlangıç Zorluk Limiti (powLimit)
1–199 arasındaki blokların çok hızlı kazılmasını (bu durum çatallanmalara ve kuorum kilitlenmelerine yol açıyordu) önlemek için, başlangıç zorluk hedefi `powLimit` şu şekilde ayarlanmıştır:
$$\text{powLimit} = \text{~UINT256\_ZERO} \gg 20$$
Mainnet ve Testnet üzerinde bu değer tam olarak `1/2^20`'dir (genesis bloğunun `0x1e0ffff0` olan `nBits` değerine eşdeğerdir). Bu, blokların genesis'ten itibaren doğal olarak yaklaşık 30 saniye aralıklarla yerleşmesini sağlayarak düğümlerin (nodes) kararlı P2P bağlantıları kurmasına ve birleşik bir zincir ucunu (chain tip) sürdürmesine olanak tanır.

---

## 3. Doğrulanabilir Rastgele Fonksiyon (VRF) ve Rolling Seed Yapısı

Madencilerin blok $H$ hash'ini manipüle etmek için işlemleri veya nonce'ları değiştirmesini ve böylece blok $H+1$ için yapılacak lider seçimini saptırmasını (yani **grinding saldırılarını**) önlemek amacıyla ADAM, bir **Doğrulanabilir Rastgele Fonksiyon (VRF) Rolling Seed** modeli uygular.

### Matematiksel Formülasyon
ADAM ağ yükseltmesinin (`Consensus::UPGRADE_ADAM`) aktif olduğu herhangi bir $H$ blok yüksekliği için:
$$\text{Seed}_H = \text{Hash}\left(\text{Seed}_{H-1} \mathbin{\Vert} \text{VRFProof}_{H-1}\right)$$

Burada:
* $H$ yüksekliğindeki seçim seed'i ($\text{Seed}$), blok $H+1$ için liderleri belirlemek üzere kullanılır.
* $H-1$ yüksekliğindeki VRF kanıtı ($\text{VRFProof}$), $H-1$ bloğunun seçilen **Koordinatörü** tarafından, hibrit bir BLS12-381 + ECDSA fallback imza mekanizması (`SignBLSWithECDSAFallback`) kullanılarak $H-2$ yüksekliğindeki önceki seed üzerinde oluşturulan imzadır.
* Bu şema altında Koordinatör, kendi ECDSA gizli anahtarından (private key) deterministik olarak türetilen bir BLS gizli anahtarını kullanarak bir BLS12-381 imzası oluşturur. Yetkilendirmeyi kanıtlamak ve anahtar manipülasyonunu önlemek için Koordinatör, BLS açık anahtarını (public key) kendi ECDSA gizli anahtarını kullanarak imzalar. Doğrulama (`VerifyBLSWithECDSAFallback`) işlemi, hem BLS anahtarının ECDSA yetkilendirmesinin hem de hedef seed'in BLS imzasının geçerli olmasını sağlar.
* Elde edilen imza deterministik olduğundan, Koordinatör'ün belirli bir seed için tam olarak bir geçerli imzası vardır; bu da rolling seed yapısını, Koordinatör imzalayıp yayınlayana kadar tamamen değiştirilemez ve tahmin edilemez kılar.

### Düğüm Seçimi (SSLE)
Madencilerin ve koordinatörün seçimi `src/adam.cpp` içindeki `SelectAdamNodes()` tarafından gerçekleştirilir:
1. Aktif düğüm havuzu (kayıtlı Masternode listesi ve Coin-Lock veya PoW-Lock aracılığıyla aktif kayıtlı madenciler) derlenir.
2. Seçim havuzu ağa bağlıdır:
   * **Mainnet & Testnet**: Havuz, aktif Masternode'lardan ve aktif kayıtlı madencilerden dinamik olarak oluşturulur. Bununla birlikte, erken bootstrap aşamasında (blok yüksekliği Mainnet üzerinde $< 704$ veya Testnet üzerinde $< 200$ iken), ağ, blok 1 ila 199 arasındaki blok üreticilerini (coinbase çıktıları) otomatik olarak tarar ve açık anahtarlarını madenci havuzuna ekler. Bu, aktif masternodlar veya kayıtlar oluşturulmadan önce zincirin durmasını (stall) önler.
   * **Regtest**: Havuz, otomatik testleri kolaylaştırmak amacıyla harici kayıtları otomatik olarak atlar ve 15 adet deterministik bootstrap açık anahtarı içerir:
     $$\text{Pool}_{\text{bootstrap}} = \{\text{DeterministicPubKey}_0, \dots, \text{DeterministicPubKey}_{14}\}$$
3. Rolling seed'e dayanarak seçim havuzundaki her bir düğüm için benzersiz bir hash sırası (hash rank) hesaplanır:
   $$\text{Rank}_i = \text{Hash}\left(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i\right)$$
4. Havuz, $\text{Rank}_i$ değerlerine göre artan düzende sıralanır.
5. İlk $N$ düğüm **Madenci (Miner)** olarak seçilir.
6. Bir sonraki düğüm **Koordinatör (Coordinator)** olarak seçilir.

---

## 4. Blok Başlığı Uzantıları ve Serileştirme (Serialization)

ADAM ağ yükseltmesi (`Consensus::UPGRADE_ADAM`) aktif olduğunda, bloklar **Versiyon 11** (Fallback Mode) veya **Versiyon 12** (Standart Mod) blok yapıları kullanılarak serileştirilir. `src/primitives/block.h` dosyasındaki `CBlockHeader` sınıfı dört yeni alanla genişletilmiştir:

```cpp
class CBlockHeader {
public:
    // Legacy fields...
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    // ADAM Extended fields (Version >= 11)
    std::vector<CPubKey> vAdamMiners;                  // Public keys of elected miners (and coordinator in v11)
    std::vector<std::vector<unsigned char>> vAdamSolutions; // Serialized partial solutions (nonce + miner signature)
    std::vector<unsigned char> vAdamVRFProof;          // Coordinator's VRF signature on previous seed
    std::vector<unsigned char> vAdamCoordinatorSig;    // Coordinator's signature on final block header hash
};
```

### 4.1 Hashing ve Serileştirme Sıralaması
Nihai blok başlığı hash'ini imzalayan ve `GetHash()` fonksiyonunun dışında tutulan `vAdamCoordinatorSig` alanının aksine, `vAdamVRFProof` blok hash'i hesaplanmadan *önce* üretilir. `GetHash()` sırasında blok başlığı serileştirmesine dahil edilir, bu da VRF kanıtının başlığa kriptografik olarak kilitlenmesini sağlar.

### 4.2 Durumsuz Blok Hashing Zinciri (Stateless)
Blok hash'ini (`CBlockHeader::GetHash()`) hesaplamak için ADAM, serileştirilmiş başlığı (versiyon 12 ise `vAdamCoordinatorSig` ve LLMQ `vQuorumSig` hariç tutularak), seçilen madencilere karşılık gelen $M$ adet ardışık hashing turundan oluşan bir zincir boyunca hash'ler:
* Fallback modunda (`nVersion == 11`): $M = \text{vAdamMiners.size()} - 1$.
* Standart modda (`nVersion == 12`): $M = \text{vAdamMiners.size()}$.

Her bir $i \in \{0, \dots, M-1\}$ turu için:
1. Önceki blok hash'inden ve tur endeksinden benzersiz bir tur hash'i türetilir:
   $$\text{roundHash}_i = \text{Hash}\left(\text{hashPrevBlock} \mathbin{\Vert} i\right)$$
2. İlk bayt $v_i = \text{roundHash}_i[0]$ elde edilir.
3. Tek bir ortak asal (coprime) çarpanı $m_i$ hesaplanır:
   $$m_i = v_i \mid 1$$
   Eğer $m_i < 3$ ise, $m_i = 3$ olarak ayarlanır. Bu, çarpanların $2^{256}$ değerine göre aralarında asal olmasını sağlayarak %100 entropiyi korur.
4. Tur hashing işlemi gerçekleştirilir:
   - **Fallback Modu (Versiyon 11)**:
     - **Tur 0**: $H_0 = \text{CalculateAdamPuzzleHash}(\text{algo}_0, \text{SerializedHeader}) \times m_0 \pmod{2^{256}}$
     - **Tur $i > 0$**: $H_i = \text{CalculateAdamPuzzleHash}(\text{algo}_i, H_{i-1}) \times m_i \pmod{2^{256}}$
   - **Standart Mod (Versiyon 12)**:
     - **Tur 0**:
       * Madenci 0 için 3-permütasyon algoritmaları $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ türetilir.
       * Hesaplama yapılır:
         $$H_0^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, \text{SerializedHeader})$$
         $$H_0^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_0^{(3)} \times 1 \right) \pmod{2^{256}}\right)$$
         $$H_0 = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_0^{(2)} \times 1 \right) \pmod{2^{256}}\right)$$
       * Çarpan uygulanır:
         $$H_{\text{prev}} = (H_0 \times m_0) \pmod{2^{256}}$$
     - **Tur $i > 0$**:
       * Madenci $i$ için 3-permütasyon algoritmaları $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ türetilir.
       * Hesaplama yapılır:
         $$H_i^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, H_{\text{prev}})$$
         $$H_i^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_i^{(3)} \times (i + 1) \right) \pmod{2^{256}}\right)$$
         $$H_i = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_i^{(2)} \times (i + 1) \right) \pmod{2^{256}}\right)$$
       * Çarpan uygulanır:
         $$H_{\text{prev}} = (H_i \times m_i) \pmod{2^{256}}$$

Elde edilen nihai hash $H_{\text{prev}}$ (veya Versiyon 11'de $H_0 \times m_0$), blok hash'i olarak döndürülür.

---

## 5. Konsensüs Doğrulama Kuralları

Bir blok alındığında, ADAM ağ yükseltmesi (`Consensus::UPGRADE_ADAM`) aktifse, `src/main.cpp` içindeki `CheckBlock()` fonksiyonu aşağıdaki doğrulamaları zorunlu kılar:

1. **Versiyon Zorunluluğu ve Sürüm Düşürme (Downgrade) Koruması**:
   - Blok veriyonu en az `11` olmalıdır.
   - Eğer `block.nVersion == 11` and `block.GetBlockTime() >= sporkManager.GetSporkValue(SPORK_21_ADAM_STANDARD_MODE)` ise, kötü niyetli madencilerin sürüm düşürmesini önlemek amacıyla blok bir `bad-version` DoS hatası ile reddedilir.
   - Blok yüksekliği $\ge \text{nPoMBLHeight}$ ise (Regtest üzerinde 300, Testnet üzerinde 400, Mainnet üzerinde 2000), Standart Mod aktif olduğu takdirde blok veriyonu `12` olmalıdır.

2. **Madenciler ve Çözüm Boyutları**:
   - **Versiyon 11 (Fallback Mode)**:
     - `vAdamMiners` boyutu 11 ile 14 arasında olmalıdır.
     - `vAdamSolutions` boyutu tam olarak `vAdamMiners.size() - 1` olmalıdır.
     - Doğrulanacak Koordinatör, `vAdamMiners` dizisinin son elemanıdır (`vAdamMiners.back()`).
   - **Versiyon 12 (Standart Mod)**:
     - `vAdamMiners` boyutu tam olarak `nAdamMinersCount` değerine eşit olmalıdır.
     - `vAdamSolutions` boyutu tam olarak `vAdamMiners.size()` değerine eşit olmalıdır.
     - Koordinatör, `SelectAdamNodes` algoritmasından türetilir.

3. **VRF Kanıtı Doğrulaması**: `vAdamVRFProof`, önceki rolling seed $\text{Seed}_{H-1}$'e göre doğrulanmalıdır ve `VerifyBLSWithECDSAFallback` kullanılarak beklenen Koordinatörün açık anahtarıyla eşleşen geçerli bir imza olmalıdır.

4. **Seçim Yolu Doğrulaması (Election Path Validation)**: Versiyon 12'de, `vAdamMiners` içindeki açık anahtar listesi deterministik `SelectAdamNodes` algoritmasının tam çıktısıyla eşleşmelidir. Versiyon 11, kendi kendine yeten bootstrap modunda çalıştığı için bu kontrolü atlar.

5. **Kısmi Çözümler Doğrulaması (Partial Solutions Validation)**:
   - Geçerli kısmi çözümlerin sayısı gerekli eşiği karşılamalıdır:
      - **Versiyon 11**: En az `nAdamThreshold` konsensüs parametresiyle tanımlanan eşik kadar olmalıdır (Mainnet/Regtest üzerinde 7 çözüm, Testnet üzerinde 3 çözüm).
      - **Versiyon 12**: En az `nAdamThreshold` konsensüs parametresiyle tanımlanan eşik kadar olmalıdır (Mainnet/Regtest üzerinde 7 çözüm, Testnet üzerinde 3 çözüm).
   - Her çözüm bir `nonce` ve bir `signature` (imza) olarak ayrıştırılır.
   - Bulmaca hash'i (puzzle hash), madenciye atanan algoritma(lar) kullanılarak hesaplanır:
     - **Versiyon 11 (Fallback Mode)** içinde:
       $$\text{PuzzleHash} = \text{CalculateAdamPuzzleHash}\left(\text{algoIndex}, \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i\right)$$
       Burada $\text{algoIndex} = \text{Hash}(\text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i) \pmod{18}$ olup, desteklenen **18 algoritmadan** biri kullanılır (bunlar arasında `Hamsi`, `Fugue`, `Shabal`, `Whirlpool` ve `Haval-256` yer alır).
     - **Versiyon 12 (Standart Mod)** içinde: Önceki bloğun hash'ine ve madencinin açık anahtarına dayanarak mevcut 18 algoritma arasından deterministik olarak 3 farklı hashing algoritmasını ($\text{algo1}$, $\text{algo2}$ ve $\text{algo3}$) seçen bir 3-permütasyon seçici şeması `\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{MinerPubKey}_i)` kullanılır. Çözücü bu üç algoritmayı birleştirir:
       $$\text{PuzzleHash} = \text{algo1}\left( (\text{minerIdx} + 1) \times \text{algo2}\left( (\text{minerIdx} + 1) \times \text{algo3}(\text{Challenge}) \right) \right) \pmod{2^{256}}$$
       Burada $\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i$ şeklindedir.
     - `PuzzleHash`, `nBits` ile tanımlanan hedef zorluğu karşılamalıdır (Bölüm 2.D'de açıklandığı gibi `scaledTarget = Target \ll \text{activeShift}` olarak gevşetilir).
   - İmza, `PuzzleHash`'i imzalayan `MinerPubKey_i`'ye göre doğrulanmalıdır.

6. **Koordinatör İmzası Doğrulaması**: `vAdamCoordinatorSig`, `VerifyBLSWithECDSAFallback` kullanılarak nihai blok başlığı hash'ini (imzanın kendisi hariç) imzalayan beklenen Koordinatörün açık anahtarına göre doğrulanmalıdır.

---

## 6. Cüzdan ve RPC API Entegrasyonu

Geliştiricileri ve madencilik havuzu operatörlerini desteklemek amacıyla RPC API, işbirlikçi konsensüs alanlarını dışarıya sunar (expose):

### `getblocktemplate` Yanıtı
`nVersion >= 11` olduğunda, JSON yanıtı şunları içerir:
* `version`: 11
* `adamminers`: Seçilen madencilerin hex-kodlu açık anahtarlarından oluşan dizi.
* `adamvrfproof`: Önceki bloğun seed'i üzerindeki hex-kodlu VRF imzası.
* `adamsolutions`: Şu anda toplanan hex-kodlu kısmi çözümlerden oluşan dizi.
* `adamcoordinatorsig`: Koordinatörün hex-kodlu imzası.

### `generate` RPC Güncellemesi
`generate` RPC'si, ADAM blokları için blok birleştirme (block assembly) işlemini gerçekleştirir. Geleneksel yüksek zorluklu hash grinding işlemi yapmak yerine:
1. Rolling seed'e dayanarak düğümün bir Madenci mi yoksa Koordinatör mü olarak seçildiğini belirler.
2. Madenci olarak seçildiyse, hafif kısmi PoW bulmacasını çözer.
3. Koordinatör olarak seçildiyse çözümleri toplar, VRF kanıtını ve nihai blok hash'ini deterministik olarak imzalar ve tamamlanmış blok şablonunu ağa gönderir.

---

## 7. Proof-of-Stake (PoS) Entegrasyonu ve İşbirlikçi PoS

ADAM, Proof-of-Stake (PoS) mekanizmasının yerine geçmez, aksine onunla entegre olarak PoS ile PoW'un (ADAM işbirlikçi madenciliği) birlikte çalıştığı bir **Hibrit İşbirlikçi PoS (Hybrid Cooperative PoS)** konsensüs mekanizması oluşturur.

Geleneksel PoS modelinde, blok üretimi yalnızca stake ağırlığına (cüzdanda tutulan coin miktarı) göre belirlenir. ADAM'da bu süreç, blok grinding'i, bencil staking'i ve hedefli lider DoS saldırılarını önlemek için işbirlikçi madenci-koordinatör doğrulama döngüsüyle birleştirilir.

### Staking ve İşbirlikçi Yaşam Döngüsü (Blok yüksekliği $\ge$ 200)

Ağ yükseltmesi `Consensus::UPGRADE_POS` etkinleştirildiğinde (Mainnet üzerinde 200. blok yüksekliğinde), blok üretimi saf İşbirlikçi PoW'dan, PoW işbirlikçi madenciliği ile PoS staking'in paralel olarak çalıştığı Hibrit İşbirlikçi PoS modeline geçer:

> [!IMPORTANT]
> **Tek İmzalı PoS Bloklarına İzin Verilmez:** Proof-of-Work (ADAM işbirlikçi madenciliği) asla sonlanmaz veya PoS lehine devre dışı bırakılmaz. KristaTech bünyesindeki her PoS bloğu hibrittir; seçilen ADAM madencileri tarafından çözülen PoW bulmacalarının doğrulanmasını gerektirir ve hem stake edenin anahtarı (`vchBlockSig`) hem de seçilen ADAM koordinatörünün anahtarı (`vAdamCoordinatorSig`) ile çift imzalanmış (double-signed) olması gerekir. Tek imzalı bloklar, konsensüs doğrulama kuralları tarafından kesin bir şekilde reddedilir.

1. **Staking Yetkisi (Kernel Kontrolü)**:
   Cüzdanın staking iş parçacığı (staking thread - `ThreadStakeMinter`), kernel hash kontrolünü (coin ağırlığı ile orantılı olarak) doğrulayarak herhangi bir UTXO'nun bir blokta stake etmeye uygun olup olmadığını periyodik olarak değerlendirir.
   
2. **İşbirlikçi Bulmaca Toplama**:
   Bir staking iş parçacığı blok önerme hakkını kazandığında, bir blok şablonu (block template) oluşturur (blok yüksekliği $\ge 2000$ olduğunda hem PoS hem de `UPGRADE_POMBL` aktif olduğu için Versiyon 12 olarak).
   Stake edenin cüzdanı, `SelectAdamNodes` aracılığıyla mevcut blok yüksekliği için seçilen madencileri alır ve bu seçilen madenciler tarafından çözülen hafif PoW bulmacalarını P2P ağı bellek önbelleğinden (`mapAdamSolutionsCache`) toplar. Seçilen herhangi bir madencinin çözümü yerel önbellekte eksikse, gerekli tüm çözümler alınana kadar blok şablonu ertelenir.

3. **Koordinatör Doğrulaması ve İmza**:
   Mevcut turun seçilen Koordinatörü blok şablonunu doğrular, VRF kanıtını (`vAdamVRFProof`) oluşturmak üzere önceki bloğin seed'ini imzalar ve Koordinatör imzasını (`vAdamCoordinatorSig`) oluşturmak üzere blok başlığını imzalar.

4. **Stake Eden Blok İmzası (Çift Kilitleme)**:
   Tamamlanan blok, iki ayrı imza türü kullanılarak güvence altına alınır:
   - **ADAM İmzası (`vAdamCoordinatorSig`)**: İşbirlikçi PoW madencilik turunun başarıyla tamamlandığını doğrulamak için seçilen Koordinatör tarafından oluşturulur.
   - **PoS Blok İmzası (`vchBlockSig`)**: Stake edenin cüzdanı tarafından `SignBlock` kullanılarak oluşturulur (stake edilen UTXO'nun gizli anahtarı kullanılarak nihai blok hash'i imzalanır).

### Dual Validation on the Network

When a peer receives a Cooperative PoS block, the validation rules in `CheckBlock` require both consensus checks to pass:
1. **Proof-of-Stake Doğrulaması**: Düğüm, `coinstake` işlemini doğrular, kernel hash hedef zorluğunu kontrol eder ve stake edenin blok imzasını (`vchBlockSig`) doğrular.

---

## 8. LLMQ (Long-Living Masternode Quorums) ve DKG (Distributed Key Generation)

Standart Mod altında Versiyon 12 blok şablonlarını doğrulamak ve imzalamak için ağ, **Dağıtık Anahtar Üretimi (DKG - Distributed Key Generation)** oturumlarını yürüten **Uzun Ömürlü Masternode Kuorumlarına (LLMQ - Long-Living Masternode Quorums)** dayanır.

### A. Kuorum Topolojisi ve Parametreleri
* **Kuorum Boyutu**: Her aktif LLMQ tam olarak **5 üyeden** oluşur (`llmq.cpp:197`).
* **DKG Aralıkları**:
  - **Mainnet**: Her **100 blokta** bir yeni bir DKG oturumu gerçekleştirilir (`GetActiveQuorum`).
  - **Testnet & Regtest**: Test sürecini hızlandırmak için DKG oturumları her **10 blokta** bir gerçekleştirilir.
* **Kuorum İmzası Doğrulama Eşiği (`CQuorumSignature::Verify`)**:
  - **Mainnet**: Doğrulama eşiği, kuorum boyutunun **%75**'i olarak ayarlanmıştır (en az 2 imzanın mevcut olması gerekir).
  - **Testnet & Regtest**:
    - Blok yüksekliği **Model D** etkinleştirme yüksekliğinin (`UPGRADE_MODELD`) altındaysa, eşik **0 imzadır** (bootstrap işlemine izin vermek için doğrulama atlanır).
    - Model D aktif olduğunda (Testnet üzerinde yükseklik $\ge 500$, Regtest üzerinde $\ge 200$), eşik tam olarak **2 imza** olacak şekilde zorunlu kılınır.

#### B. Aktif Masternode Filtreleme (Dinamik Havuz)
Ağın sağlam, merkeziyetsiz olmasını ve yerel düğüm sıfırlamalarından etkilenmemesini sağlamak için:
* Kuorumlar, ağ üzerindeki aktif ve etkinleştirilmiş Masternode havuzundan dinamik olarak seçilir.
* 5'ten az aktif masternode kayıtlıysa ağ, kuorum üyelerini dinamik olarak kayıtlı madenci havuzundan (blok 1-199 bootstrap madenciliği veya coin-lock/pow-lock kayıtları) seçmeye geri döner.
* Mainnet, Testnet veya Regtest üzerinde kod içinde sabitlenmiş (hardcoded) herhangi bir anahtar kimliği (key ID) filtrelemesi veya yerel cüzdan kısıtlaması uygulanmaz, bu da tamamen merkeziyetsiz ve güven gerektirmeyen (trustless) bir test ortamı sağlar.
