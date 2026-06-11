# KristaTech (KRISTA) Teknik Whitepaper
## Proof-of-BLS (PoBLS) Önerici Seçimi, Korum Dirençli İşbirlikçi Hibrid Mutabakat ve JSON Tabanlı Bildirimsel Akıllı Sözleşmeler (MESCAL) ile Güçlendirilmiş Blokzincir Protokolü

**Özet**  
Bu çalışma, mutabakat merkeziyeti, blok liderlerini hedef alan Hizmet Dışı Bırakma (DoS) saldırıları ve geleneksel sanal makinelerin mimari karmaşıklıklarını çözmek üzere tasarlanan KristaTech (KRISTA) blokzincir protokolünü tanıtmaktadır. KristaTech, onaylayıcı kümesinin seçimini blok teklif etme (proposing) sürecinden ayıran iki katmanlı bir konsensüs mimarisi uygulamaktadır. Seçim katmanı olan **ADAM (A Decentralized Approach Model)**, Doğrulanabilir Rastgele Fonksiyon (VRF) tabanlı döngüsel tohumlar kullanarak blok yüksekliği başına $N$ adet onaylayıcı (madenci) ve 1 adet koordinatörü deterministik olarak seçer. Blok önerme katmanı olan **Proof of BLS (PoBLS)** ise seçilen bu onaylayıcı havuzu içinden geçici (ephemeral) kriptografik BLS anahtarları yardımıyla XOR mesafe hesabı yaparak blok üreticisini dinamik bir piyango mekanizmasıyla belirler. Bu sayede ağ, ön hesaplama (pre-computation) ve lider hedefli DoS saldırılarına karşı tam koruma sağlar. Çevrimdışı onaylayıcılar veya ağ bölünmeleri durumunda liveness (canlılık) durumunun korunabilmesi için, dinamik bir eşik değeriyle doğrulanan boş vektör yer tutucu (`std::vector<unsigned char>()`) mekanizması geliştirilmiştir. Akıllı sözleşmeler, Bitcoin benzeri yığın tabanlı `CScript` bayt koduna doğrudan derlenen bildirimsel ve sade JSON yapısındaki **MESCAL (Minimalistically Envisioned Smart Contract Assembling Language)** dili ile yürütülür. Bu yapı, durum tabanlı zafiyetleri ve gaz ücreti hesaplama karmaşıklıklarını tamamen ortadan kaldırır. Son olarak, ağın sürdürülebilirliği, **%1.9 üç aylık emisyon azalması (decay)** ve **Model D İşbirlikçi Paylaşım** modeli ile yönetilen **210 Milyon KRISTA** üst sınırı (hard cap) ile güvence altına alınmıştır. Bu ekonomik model; blok ödüllerini pasif masternode'lar (%50), aktif LLMQ korum üyeleri (%10), blok üreticisi (%15) ve katılımcı onaylayıcılar (%25) arasında adil bir şekilde dağıtırken, ekosistemin geliştirilmesi amacıyla kurumsal geliştirici ve musluk (faucet) fonlarını da içermektedir.

---

## 1. Giriş ve Arka Plan

Dağıtık mutabakat protokolleri, özünde Bizans Generalleri Problemini hasmane ve açık ağ ortamlarında çözmeyi amaçlar. Geleneksel İş Kanıtı (PoW) ve Pay Kanıtı (PoS) tasarımları ağ koordinasyonunu başarıyla sağlamış olsalar da, beraberlerinde kritik yapısal zayıflıklar getirmektedir:

1. **Konsensüs Merkeziyeti**: PoW ağlarında ölçek ekonomisi, hash gücünün sınırlı sayıda endüstriyel madencilik havuzunda toplanmasına yol açar. PoS ağlarında ise zenginlik birikimi ("zenginin daha da zenginleştiği" dinamikler), yüksek miktarda teminat tutan cüzdanların blok üretimini tekeline almasına neden olur.
2. **Lider Hedeflenebilirliği**: Bir sonraki blok üreticisinin önceden bilindiği veya tahmin edilebildiği protokollerde, saldırganlar bu düğümü hedef alan DDoS saldırıları düzenleyebilir veya işlemleri sansürlemesi için blok üreticisine baskı uygulayabilir.
3. **Blok Grinding (Öğütme) Saldırıları**: Blok başlığındaki veya işlemlerdeki verilerin değiştirilmesiyle sonraki lider seçimlerinde kullanılacak rastgelelik kaynağının manipüle edilebildiği sistemlerde, kötü niyetli aktörler blok öğüterek kendi seçilme olasılıklarını artırabilir.
4. **Akıllı Sözleşme Sadeliği**: Geleneksel sanal makineler, durum yönetimindeki karmaşıklık nedeniyle işlemlerde öngörülemeyen hatalara yol açabilir. Güvenli geçişleri garanti altına almak için daha sade ve öngörülebilir bir akıllı sözleşme dili gereklidir.

KristaTech, bu sorunları çözmek amacıyla iki katmanlı işbirlikçi bir konsensüs mimarisi sunar. Blok üretim sürecini deterministik bir seçim katmanı (ADAM) ve öngörülemez bir önerme katmanı (PoBLS) olarak ikiye bölerek hem Sybil saldırılarına karşı direnç hem de DDoS koruması elde eder. Akıllı sözleşme yürütümü, sade ve deterministik çalışan **MESCAL** dili ile sınırlandırılarak blokzincir durum güvenliği korunur. Ekosistem, uzun vadeli sürdürülebilirliği destekleyen emisyon planıyla güvence altına alınmıştır.

---

## 2. Konsensüs Mimarisi

KristaTech'in merkezinde işbirlikçi bir mutabakat süreci yer alır. Blok yaşam döngüsü **Seçim Aşaması**, **Çözüm Aşaması** ve **Birleştirme Aşaması** olmak üzere üç aşamadan oluşur.

```
+-----------------------------------------------------------------------+
|                           ADAM Seçim Katmanı                          |
|   1. VRF Tohumu: Seed_H = Hash(Seed_{H-1} || VRFProof_{H-1})           |
|   2. Düğüm Sıralama: Rank_i = Hash(Seed_H || PubKey_i)                |
|   3. Seçim: En İyi N Düğüm -> Madenci; Sonraki -> Koordinatör          |
+-----------------------------------++----------------------------------+
                                    ||
                                    \/
+-----------------------------------------------------------------------+
|                            PoBLS Önerici Katmanı                      |
|   1. Geçici BLS Anahtarı: (sk_i, pk_i)                                |
|   2. Bilet Üretimi: T_i = Hash(sk_i || pk_i || H)                     |
|   3. Bilet Mesafesi: D_i = |T_i XOR T_target|                         |
|   4. Kazanan: En küçük D_i mesafesine sahip düğüm LLMQ onayı alır      |
+-----------------------------------++----------------------------------+
                                    ||
                                    \/
+-----------------------------------------------------------------------+
|                         Birleştirme ve Doğrulama                      |
|   1. Blok üreticisi, onaylayıcıların kısmi çözümlerini toplar.        |
|   2. Çevrimdışı onaylayıcılar boş vektör yer tutucularla temsil edilir|
|   3. Ağ Doğrulaması: (Geçerli Çözüm Sayısı >= T) ve Koordinatör İmzası|
+-----------------------------------------------------------------------+
```

### 2.1. ADAM (A Decentralized Approach Model)
ADAM, ağın kimlik ve seçim katmanıdır. Tüm düğümlerin serbestçe yarışmasına izin vermek yerine (ki bu durum Sybil zafiyetlerine yol açar), her blok yüksekliği için deterministik olarak $N$ adet Madenci (Validator) ve 1 adet Koordinatör seçer.

#### 2.1.1. VRF Döngüsel Tohumları
Blok öğütme (grinding) saldırılarını engellemek amacıyla, seçim algoritmasında kullanılan rastgelelik Doğrulanabilir Rastgele Fonksiyon (VRF) ile üretilir. $H$ blok yüksekliğindeki seçim tohumu şu şekilde formüle edilir:

$$\text{Seed}_H = \text{Hash}\left(\text{Seed}_{H-1} \mathbin{\Vert} \text{VRFProof}_{H-1}\right)$$

Burada $\text{VRFProof}_{H-1}$, $H-1$ yüksekliğindeki Koordinatörün, $H-2$ yüksekliğindeki tohum ($\text{Seed}_{H-2}$) üzerine RFC 6979 standardına uygun olarak attığı deterministik kriptografik imzadır. RFC 6979 altındaki ECDSA imzaları tamamen deterministik olduğu için, Koordinatör imza değerini manipüle ederek $H+1$ yüksekliğindeki seçimleri kendi lehine değiştiremez. Bu döngüsel tohum blok başlığında saklanır, böylece tarihsel rastgelelik verileri geriye dönük olarak denetlenebilir hale gelir.

#### 2.1.2. Düğüm Seçimi ve Sıralama
Aktif Masternode listesindeki ($P$) her düğüm $i$ için benzersiz bir puan sıralaması hesaplanır:

$$\text{Rank}_i = \text{Hash}\left(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i\right)$$

Düğüm havuzu $\text{Rank}_i$ değerine göre küçükten büyüğe sıralanır. İlk $N$ düğüm **Madenci (Validator)**, sıralamadaki $(N+1)$. düğüm ise **Koordinatör** olarak atanır. Mainnet ve Testnet üzerinde seçim havuzu, aktif Masternode'lardan ve aktif kayıtlı madencilerden (Coin-Lock veya PoW-Lock ile kayıt olanlar) dinamik olarak oluşturulur. Regtest üzerinde ise, otomatik testleri kolaylaştırmak amacıyla havuz otomatik olarak 15 adet deterministik bootstrap genel anahtarı içerir.

#### 2.1.3. Mod Dinamikleri ve Spork Kontrolü
Ağın sorunsuz bir şekilde başlatılabilmesi (bootstrapping) için ADAM iki farklı modda çalışabilir:
* **Fallback Modu (Sürüm 11)**: Ağın ilk başlangıç (bootstrap) aşamasında çalışır. Aktif Masternode sayısı yeterli eşik değerinin altında olduğundan, onaylayıcı havuzu tamamen aktif kayıtlı madencilerden oluşturulur. Madenci sayısı $N \in [11, 14]$ arasında dinamik olarak değişir ve gereken asgari geçerli çözüm eşiği $T$ sabit olarak **10**'dur.
* **Standart Mod (Sürüm 12)**: Yeterli sayıda aktif Masternode ağa katıldığında tam kooperatif konsensüsü etkinleştirir. Madenci sayısı $N$ sabit olarak `nAdamMinersCount` (11), asgari geçerli çözüm eşiği $T$ ise `nAdamThreshold` (7) olarak uygulanır. Koordinatör, aktif Masternode listesinden dinamik olarak seçilirken madenciler ise kayıtlı madenci havuzundan seçilir.
* **Etkinleştirme**: Bu iki mod arasındaki geçiş `SPORK_21_ADAM_STANDARD_MODE` (Spork ID `10020`) üzerinden kontrol edilir. Spork etkinleştirildiğinde ağ otomatik olarak Sürüm 12 blok yapısını zorunlu kılar.

---

### 2.2. Proof of BLS (PoBLS) Konsensüsü
ADAM katmanı tarafından belirlenen onaylayıcı kümesi içinden blok önericisinin seçimi, **Proof of BLS (PoBLS)** piyangosu ile gerçekleştirilir. Bu yapı önericinin kimliğini gizli tutarak hedefli DoS saldırılarını imkansız kılar.

#### 2.2.1. Geçici Bilet Üretimi
Elected durumundaki her onaylayıcı $i$, $H$ yüksekliği için geçici bir BLS anahtar çifti $(sk_i, pk_i)$ üretir. Bu anahtarlar kullanılarak bir piyango bileti $T_i$ hesaplanır:

$$T_i = \text{Hash}(sk_i \parallel pk_i \parallel H)$$

Onaylayıcı, özel anahtarı ifşa etmeden sahipliğini kanıtlamak için önceki blok özetini ($Hash_{\text{prev}}$) geçici özel anahtarıyla imzalar:

$$\sigma_i = \text{Sign}_{sk_i}(Hash_{\text{prev}})$$

Bu bilgilerden oluşan katılım mesajı $(pk_i, \sigma_i, T_i)$ ağdaki aktif **Long-Living Masternode Quorum (LLMQ)** yapısına iletilir.

#### 2.2.2. XOR Mesafesi ile Kazananın Belirlenmesi
Aktif döngüsel tohumdan ($\text{Seed}_H$) bir hedef özet ($T_{\text{target}}$) türetilir. LLMQ korumu, iletilen her bilet ile hedef arasındaki XOR mesafesini hesaplar:

$$D_i = |T_i \oplus T_{\text{target}}|$$

En küçük XOR mesafesine ($D_i$) sahip olan onaylayıcı, bloğu önerme hakkını kazanır. Geçici BLS anahtarları, grinding saldırılarını önlemek amacıyla düğümün kalıcı kimlik anahtarından türetilmelidir:

$$sk_i = \text{DeriveKey}(sk_{\text{node}}, Hash_{\text{prev}})$$

Bu kural, her onaylayıcının blok başına yalnızca tek bir geçerli bilet üretebilmesini sağlayarak bilet ön hesaplama yarışını engeller.

#### 2.2.3. Konsensüs Sinerjisi
* **Sybil Koruması (ADAM)**: Katılımı yalnızca ADAM tarafından seçilen 11 onaylayıcı ile sınırlar. Bu sayede saldırganların binlerce sanal düğüm açarak kazanma şansını artırmasının önüne geçilir.
* **DDoS Koruması (PoBLS)**: Kazanan düğüm, blok slotunun hemen başında dinamik olarak belirlenir. Blok yayınlanana kadar önericinin kimliği öngörülemez olduğundan, saldırganların önleyici DDoS saldırıları düzenlemesi engellenir.

---

### 2.3. Blok Başlığı Uzantıları ve Serileştirme
ADAM güncellemesi aktif olduğunda, blok başlığı yapısı konsensüs kanıtlarını saklayacak şekilde genişletilir:

| Alan | Tür | Açıklama |
| :--- | :--- | :--- |
| `vAdamMiners` | `std::vector<CPubKey>` | Seçilen onaylayıcıların genel anahtarları. |
| `vAdamSolutions` | `std::vector<std::vector<char>>` | Kısmi çözümler (nonce + imza). |
| `vAdamVRFProof` | `std::vector<unsigned char>` | Koordinatörün önceki tohuma ait VRF imzası. |
| `vAdamCoordinatorSig` | `std::vector<unsigned char>` | Koordinatörün nihai blok özetine ait imzası. |

#### 2.3.1. Durumsuz Hashing Zinciri
Blok özeti hesaplanırken, blok başlığı seçilen onaylayıcıların sırasına göre ardışık bir hashing zincirinden geçirilir. Her onaylayıcı $i$ için (zincir uzunluğu $M = \text{size}(vAdamMiners)$):

1. Önceki blok özeti ve indeks bilgisi kullanılarak raunda özel bir özet üretilir:
   $$\text{roundHash}_i = \text{Hash}\left(\text{hashPrevBlock} \mathbin{\Vert} i\right)$$
2. İlk bayt $v_i = \text{roundHash}_i[0]$ değeri alınarak tek bir aralarında asal çarpan hesaplanır:
   $$m_i = v_i \mid 1 \quad (\text{eğer } m_i < 3 \text{ ise } m_i = 3)$$
3. Hashing raundu yürütülür:
   * **Raund 0**: $H_0 = \text{CalculateAdamPuzzleHash}(\text{algo}_0, \text{SerializedHeader}) \times m_0 \pmod{2^{256}}$
   * **Raund $i$**: $H_i = \text{CalculateAdamPuzzleHash}(\text{algo}_i, H_{i-1}) \times m_i \pmod{2^{256}}$

Algoritmalar ($\text{algo}_i$), **18 farklı kriptografik algoritma** (örneğin *Hamsi, Fugue, Shabal, Whirlpool ve Haval-256*) arasından dinamik olarak seçilir. Sürüm 11'de algoritma indeksi şu şekilde hesaplanır:

$$\text{algoIndex} = \text{Hash}(\text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i) \pmod{18}$$

Sürüm 12'de ise işlem basitleştirilerek şu hale getirilir:

$$\text{algoIndex} = i \pmod{13}$$

Zincirin son çıktısı olan $H_{M-1}$ değeri nihai blok özeti (block hash) olarak kabul edilir.

---

### 2.4. Korum Direnci ve Yer Tutucu Güvenliği
Tüm onaylayıcıların %100 katılımını zorunlu kılan mutabakat yapıları, tek bir düğümün çevrimdışı olması durumunda ağın durmasına neden olan liveness zafiyetine sahiptir. KristaTech, bu sorunu aşmak amacıyla **Korum Dirençli Yanıt Mekanizması** kullanır.

#### 2.4.1. Yer Tutucu (Placeholder) Mekanizması
Geçerli çözüm sayısı asgari eşik değeri olan $T$'yi (Sürüm 11'de 10, Sürüm 12'de 7) karşıladığı sürece blok şablonu başarıyla oluşturulur. Çözümünü zamanında ulaştıramayan onaylayıcıların `vAdamSolutions` dizisindeki yerleri, Koordinatör tarafından boş bayt vektörleri ile doldurulur:

$$\text{vAdamSolutions}[i] = \text{std::vector<unsigned char>()}$$

Bu sayede `vAdamMiners` ve `vAdamSolutions` arasındaki birebir konumsal eşleşme korunur.

#### 2.4.2. Doğrulama Mantığı
Doğrulama yapan düğümler, `CheckBlock()` fonksiyonunda şu adımları izler:
1. `vAdamSolutions` boyutunun `vAdamMiners` boyutu ile eşleştiğini doğrular.
2. `vAdamSolutions` elemanları üzerinde döngü çalıştırır. Boş vektörler yer tutucu olarak işaretlenip doğrulamadan muaf tutulur. Boş olmayan çözümlerin imzaları ve zorluk dereceleri denetlenir:
   $$\text{PuzzleHash} = \text{CalculateAdamPuzzleHash}\left(\text{algoIndex}, \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i\right)$$
3. Başarıyla doğrulanan boş olmayan toplam çözüm sayısının $T$ değerine eşit veya büyük olduğunu onaylar.

#### 2.4.3. Güvenlik İspatları
* **Eşik Atlama Koruması**: Boş yer tutucuların imza veya zorluk doğrulama adımlarını geçmesi matematiksel olarak imkansızdır. Doğrulama motoru bunları geçersiz kanıt olarak değerlendirir. Ağın thermodynamic iş yapma zorunluluğu korunur, çünkü en az $T$ adet onaylayıcının geçerli ve yüksek zorlukta kanıt sunması şarttır.
* **Sansürün Maliyetsiz Olmaması**: Kötü niyetli bir Koordinatör, dürüst onaylayıcıların çözümlerini kasten hariç tutarak yer tutucu yerleştirebilir. Ancak blok ödülü dağıtımı, çözümlerin varlığına bakılmaksızın deterministik onaylayıcı listesine (`SelectAdamNodes`) göre yapılır. Koordinatör ödemelerin yönünü değiştiremeyeceğinden, onaylayıcıları sansürlemekten **maddi kazanç elde edemez**.

---

## 3. Akıllı Sözleşme Dili: MESCAL

**MESCAL (Minimalistically Envisioned Smart Contract Assembling Language)**, akıllı sözleşmelerin tanımlanması ve serileştirilmesi için geliştirilmiş JSON tabanlı bildirimsel (declarative) bir dildir.

### 3.1. Tasarım Felsefesi
Sadelik ve güvenlik odaklı bir yapıda tasarlanan MESCAL, doğrudan yığın tabanlı Bitcoin Script (`CScript`) bayt koduna derlenen bildirimsel bir paradigma benimser.
1. **Güvenlik**: Sadece blokzincir işlem kodlarıyla (opcodes) sınırlı, deterministik bir komut seti sunduğundan yeniden giriş (re-entrancy) açıklarını tamamen ortadan kaldırır.
2. **Gazsız Determinizm**: Döngü (loop) komutları barındırmadığından, sözleşmelerin çalışma süresi doğrusal ve öngörülebilirdir. Bu sayede karmaşık gaz tahmin mekanizmaları gereksiz kalır.
3. **Erişilebilirlik**: Sözleşmeler yapılandırılmış JSON dosyaları halinde tanımlandığından, istemci tarafındaki uygulamalar ve cüzdanlar tarafından kolayca okunabilir ve oluşturulabilir.

---

### 3.2. Bileşen Türleri
Bir MESCAL programı üç temel yapıdan oluşur:

1. **Temel Elemanlar (`basic`)**: Genel anahtarlar, kilit süreleri gibi ham veri ve işlem kodlarını sarar.
2. **Koşul Elemanları (`condition`)**: Koşullu yürütme yollarını denetler. Derlendiğinde `OP_IF ... OP_ELSE ... OP_ENDIF` yapılarına dönüşür.
3. **Bütünleşik Sözleşmeler (`contract`)**: Bir işlem çıktısını (UTXO) kilitleyen ve ardışık eylemlerden oluşan en üst düzey şablon yapısıdır.

```json
{
  "type": "contract",
  "name": "Zaman-Kilitli-Cekim",
  "description": "Fonları belirli bir blok yuksekligine kadar kilitler, ardindan cekime izin verir.",
  "actions": [
    { "type": "basic", "role": "lock-time", "inputs": [{"type": "height", "value": 50000}] },
    { "type": "basic", "role": "equalverify-checksig", "inputs": [{"type": "pubkeyhash", "value": "..."}] }
  ]
}
```

---

### 3.3. Komut Haritalama ve Teknik Detaylar
Derleyici, JSON bileşenlerini şu ikili işlemlere dönüştürür:

* **`hash160`**: SHA256 işlemini RIPEMD160 ile birleştirerek yürütür.  
  * *CScript Karşılığı*: `OP_HASH160 <Hash160(input)> OP_EQUALVERIFY`
* **`check-signature-verification`**: Kriptografik imza doğrulaması yapar.  
  * *CScript Karşılığı*: `OP_CHECKSIGVERIFY`
* **`equalverify-checksig`**: Standart P2PKH doğrulaması yürütür.  
  * *CScript Karşılığı*: `OP_DUP OP_HASH160 <pubkeyhash> OP_EQUALVERIFY OP_CHECKSIG`
* **`multi-signature`**: Çoklu imza değerlendirmesi yapar.  
  * *CScript Karşılığı*: `<m> <pubkeys...> <n> OP_CHECKMULTISIG`
* **`lock-time`**: Zaman veya blok yüksekliği kısıtlamalarını denetler.  
  * *CScript Karşılığı*: `<locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP`

---

### 3.4. Akıllı Sözleşme Şablonları ve Mimarisi

#### 3.4.1. Ölü Adam Anahtarı (Miras)
Belirli bir süre boyunca sahibinin anahtarı kullanılmadığında fonların otomatik olarak varise aktarılmasını sağlar, sahibi ise fonlara her an erişebilir:

* *CScript Karşılığı*:  
  `<heir-pubkey> OP_CHECKSIGVERIFY OP_IF <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_ELSE <owner-pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Heir-Sig": {
      "role": "check-signature-verification",
      "inputs": [{ "name": "Pubkey", "type": "pubkey", "value": "02ee1fb8..." }]
    },
    "Lock-Time": {
      "role": "lock-time",
      "inputs": [{ "name": "Lock-Until", "type": "height", "value": 1780718400 }]
    },
    "Owner-Sig": {
      "role": "check-signature-verification",
      "inputs": [{ "name": "Pubkey", "type": "pubkey", "value": "03ee1fb8..." }]
    }
  },
  "condition": {
    "Inheritance-Condition": {
      "role": "if-condition",
      "expressions": [{ "type": "basic", "name": "Heir-Sig" }],
      "true": [{ "type": "basic", "name": "Lock-Time" }],
      "false": [{ "type": "basic", "name": "Owner-Sig" }]
    }
  },
  "contract": {
    "Inheritance-Switch": {
      "actions": [{ "type": "condition", "name": "Inheritance-Condition" }]
    }
  },
  "active_contract": "Inheritance-Switch"
}
```

#### 3.4.2. Hash ve Zaman Kilitli Takas (HTLC)
Zincirler arası atomik takasları (atomic swaps) mümkün kılar. Alıcı, gizli anahtarı sunarak fonları anında çekebilir. Kilit süresi dolduğunda ise gönderici iade talebinde bulunabilir:

* *CScript Karşılığı*:  
  `<hash> OP_HASH160 OP_IF <recipient-pubkey> OP_CHECKSIGVERIFY OP_ELSE <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP <sender-pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

---

## 4. Ekosistem Tokenomisi

KristaTech, güvenlik teşviklerini korurken uzun vadeli arz kıtlığını destekleyen deflasyonist bir emisyon modeli uygular.

### 4.1. Temel Emisyon Parametreleri
* **Maksimum Arz Limit (Hard Cap)**: **210,000,000 KRISTA**
* **Ön Madencilik (Premine)**: **0 KRISTA** (tamamen adil dağıtım)
* **Hedef Blok Süresi**: 30 saniye (yılda yaklaşık 1,051,200 blok)
* **Teminat Gereksinimi**: 1. bloktan itibaren sabit **2,100 KRISTA**
* **Bootstrap Aşaması (2 - 9,999. bloklar)**: Blok başına **100 KRISTA**. Bu aşamada toplam **999,800 KRISTA** (~arzın %0.48'i) üretilir. Bu sayede ilk dönemlerde korumların kurulabilmesi için gereken 20+ aktif Masternode kurulumuna yetecek likidite sağlanmış olur.
* **Başlangıç Blok Ödülü (10,000. blok itibarıyla)**: **15 KRISTA**

---

### 4.2. Üç Aylık Emisyon Azalma Modeli (Decay)
Ağın Bitcoin'deki gibi sert 4 yıllık yarılanma şoklarından kaçınması amacıyla, blok ödülleri her 90 günde bir (259,200 blokta bir) **%1.9 oranında azaltılır**. $P$ periyodundaki blok ödülü şu şekilde hesaplanır:

$$\text{Reward}(P) = 15.0 \times (0.981)^P$$

Burada:
$$P = \left\lfloor \frac{\text{Height} - 10000}{259200} \right\rfloor$$

Bu azalma modeli doğrultusunda, dolaşımdaki toplam arzın ulaşacağı asimptotik limit şu şekildedir:

$$\text{Circulating Supply}_{\text{max}} = 999,800 + \sum_{P=0}^{\infty} \left( 259,200 \times 15.0 \times (0.981)^P \right) \approx 205,631,379 \text{ KRISTA}$$

Bu değer, 210M hard cap sınırı altında tam olarak **4,368,621 KRISTA (%2.08) Boşluk Rezervi (Gap Reserve)** bırakmaktadır. Rezerv yapısı, blok ödüllerinin 50 yılı aşkın süre boyunca aniden kesintiye uğramadan sıfıra yaklaşmasını sağlar. Böylece ağın işlem ücretlerine dayalı bir güvenlik bütçesine geçişi yumuşatılır.

```
Arz Doygunluk Grafiği:
[0] (Genesis) -> [19.71M] (1. Yıl) -> [33.45M] (2. Yıl) -> [68.85M] (5. Yıl) -> [112.43M] (10. Yıl) -> [205.63M] (Asimptot)
```

---

### 4.3. Model D İşbirlikçi Blok Ödülü Paylaşımı
Blok ödülleri, hem işlem onaylayıcılarını hem de ağ altyapı sağlayıcılarını teşvik edecek şekilde bölünmüştür.

```
+------------------------------------------------------------+
|                  Model D Blok Ödülü (100%)                 |
+------------------------------+-----------------------------+
|    Masternode Havuzu (%60)   |     Onaylayıcı Havuzu (%40) |
+--------------+---------------+--------------+--------------+
|   Pasif MN   |   LLMQ Aktif  |    Önerici / |  Katılımcı   |
|  Sırası (%50)|  Korumu (%10) |  Kazanan (%15| Onaylayı (%25)
+--------------+---------------+--------------+--------------+
```

* **İlk Aşama (2 - 2,199. bloklar)**: Masternode'lar henüz kurulamadığından ödülün %100'ü madenci/staker tarafına ödenir.
* **Korum Birikimi (2,000 - 2,199. bloklar)**: LLMQ korumları aktifleşir ve düğümler Masternode kurmaya teşvik edilir.
* **Model D Aktivasyonu (2,200. blok ve sonrası)**: Aşağıdaki dağıtım oranları zorunlu olarak uygulanır:
  1. **Pasif Masternode Payı (%50)**: Sıradaki Masternode'a ödenir (teminat kilitlenmesini teşvik eder).
  2. **LLMQ Korum Aktif Payı (%10)**: PoBLS biletlerini doğrulayan ve imzalayan aktif korum üyeleri arasında eşit olarak bölüştürülür.
  3. **Blok Üretici Payı (%15)**: Bloğu önermeye hak kazanan kazanan onaylayıcıya (PoW bloğu ise) veya paydaşa (PoS bloğu ise) ödenir. İşlem ücretlerinin (fees) tamamı da bu adrese gider.
  4. **Katılımcı Onaylayıcı Payı (%25)**: Seçilen diğer onaylayıcılar arasında eşit olarak paylaşılır (PoS bloklarında 10 madenciye, PoW bloklarında 11 madenciye bölünür).

#### 4.3.1. Hibrid Bloklarda Ödül Dinamikleri (PoW vs. PoS)
Blok türüne göre dağıtımlar şu şekilde esner:
* **PoW Blokları**: %40'lık onaylayıcı payı ADAM havuzuna ödenir (%15 koordinatöre, %25 ise 11 madenciye bölüştürülür).
* **PoS Blokları**: Stakleyen cüzdan %15'lik üretici payını alır. %25'lik onaylayıcı payı ise blok sürecinde çalışmaya devam eden 10 adet ADAM madencisine dağıtılarak madencilik altyapısının sürekliliği korunur.

---

### 4.4. Geliştirici ve Musluk Hazineleri
Protokolün sürdürülebilir fonlanması için ödüllerden doğrudan hazine kesintileri yapılır:
* **Geliştirici Hazinesi**: Blok 2'den itibaren tüm blok ödüllerinin **%7.0**'si doğrudan Geliştirici Fon Adresine (`KTMbi3v9yXtJ4z3QuWG5urXVn5WwxHBEAfm`) aktarılır.
* **Musluk Fonu**: Blok 2 ile 50,000 arasında emisyonun **%0.7**'si Musluk Adresine (`KTP9wyzSbStzXa8xNuZB4pXytzDZkSFsQKh`) yönlendirilir.

Bu kesintiler ham blok değerinden doğrudan düşülür. Örneğin, bootstrap periyodundaki 100 KRISTA ödüllü bir blokta 7 KRISTA geliştiriciye, 0.7 KRISTA musluğa aktarılır ve kalan 92.3 KRISTA aktif konsensüs kurallarına göre paylaşılır.

---

## 5. Güvenlik ve Kriptografik Analiz

### 5.1. Sybil Saldırıları
Onaylayıcı seçimini veya bilet havuzunu ele geçirmeyi amaçlayan Sybil girişimleri yüksek ekonomik engellerle karşılaşır. Her Masternode kurulumu için **2,100 KRISTA** teminat kilitlenmelidir. ADAM katmanındaki 11 onaylayıcının çoğunluğunu (örneğin 6 tanesini) ele geçirmek için ağdaki Masternode havuzunun çok büyük bir kısmına sahip olmak gerekir, bu da saldırganın kendi sermayesini tehlikeye atması anlamına gelir.

### 5.2. Ön Hesaplama ve Nothing-at-Stake Korumaları
* **Ön Hesaplama**: Geçici BLS anahtarları kalıcı kimlik anahtarından ve önceki blok özetinden deterministik olarak türetildiği için, onaylayıcılar bilet değerlerini önceden hesaplayarak piyangoyu manipüle edemez.
* **Nothing-at-Stake**: PoS ağlarında çift imzalama maliyetsizdir. KristaTech üzerinde rakip çatallarda (forks) oy kullanmak, en az $T$ adet onaylayıcı koltuğunda fiziksel çoklu-algoritmalı PoW bulmacalarını çözmeyi gerektirir. Bu durum saldırgana reel bir donanım/enerji maliyeti yükler.

### 5.3. Çift Blok İmzası
Blok yüksekliği $\ge 200$ (Cooperative PoS) olan bloklarda güvenlik iki farklı kriptografik imza ile sağlanır:
1. **ADAM Koordinatör İmzası (`vAdamCoordinatorSig`)**: Seçilen onaylayıcı kümesinin ve oylama turlarının başarıyla tamamlandığını doğrular.
2. **Staker Blok İmzası (`vchBlockSig`)**: Blokta bulunan işlemlerin stakleyen UTXO özel anahtarıyla kilitlenmesini sağlar.

Doğrulama yapan tüm düğümler her iki imzanın da geçerliliğini şart koştuğu için ağ, geçmişi yeniden yazma veya mutabakatı ele geçirme girişimlerine karşı koruma altındadır.

---

## 6. Sonuç

KristaTech blokzincir protokolü, geleneksel ağların kısıtlamalarını aşan işbirlikçi bir konsensüs tasarımı sunmaktadır. Onaylayıcı seçimi (ADAM) ile blok önerme (PoBLS) süreçlerinin ayrılması sayesinde ağda önerici anonimliği sağlanmış, Sybil saldırıları engellenmiş ve hedefli DDoS tehditleri bertaraf edilmiştir. Korum dirençli yer tutucu mekanizması zincir canlılığını (liveness) korurken, bildirimsel MESCAL dili güvenli ve gaz ücreti karmaşasından uzak bir akıllı sözleşme ortamı sunar. %1.9 azalma oranına sahip 210 Milyonluk emisyon modeli ve Model D İşbirlikçi Paylaşımı ile desteklenen KristaTech; madenciler, paydaşlar ve masternode işletmecileri için dengeli bir teşvik yapısı kurarak sürdürülebilir bir blokzincir platformu sağlamaktadır.
