# Masternode Olmayan Madenci Kayıt Sistemi

**ADAM (A Decentralized Approach Model)** konsensüs yapısı altında işbirlikçi blok üretimini desteklemek için KristaTech Core, bir **Masternode Olmayan Madenci Kayıt Sistemi** uygular. 

Masternode'lar, sayıları yeterince yüksekse doğrulayıcı (validator) ve koordinatör (coordinator) kümelerindeki seçimler için otomatik olarak uygun olsalar da bu kayıt sistemi standart düğümlerin/cüzdanların teminat (collateral) kilitlemesine veya aktif madenci havuzuna katılmak için başlangıçta bir iş kanıtı (proof-of-work) kaydı gerçekleştirmesine olanak tanır.

---

## 1. Kayıt Yöntemleri

Madenciler kendilerini iki kriptografik yoldan birini kullanarak kaydedebilirler:

### A. Coin-Lock Kaydı (`lock`)
**Coin-Lock** yolu, bir düğümün bir teminat tutarını kilitleyerek madenci havuzunda yer edinmesini sağlar.
* **Teminat Tutarı:** **1.000 KRISTA** (Mainnet üzerinde zorunludur).
* **Zaman Kilidi (Timelock) Süresi:** Kayıt yüksekliğinden itibaren en az **2.880 blok** (~30s blok süresiyle yaklaşık 24 saat) boyunca kilitlenmelidir.
* **Script Yapısı:** Mutlak `OP_CHECKLOCKTIMEVERIFY` (CLTV) veya göreceli `OP_CHECKSEQUENCEVERIFY` (CSV) script çıktılarını kullanır. Blok yüksekliği belirtilen hedefe ulaşana kadar fonlar harcanamaz, taşınamaz veya kilidi açılamaz.

### B. PoW-Lock Kaydı (`pow`)
**PoW-Lock** yolu, 1.000 KRISTA teminatı olmayan düğümlerin kaynak tahsisini kanıtlamak için bir CPU bulmacası çözerek katılmasına olanak tanır.
* **Kayıt Ücreti:** **0.0001 KRISTA** tutarında minimum bir ücret.
* **Bulmaca Mekanizması:** Düğüm, mevcut blok ucu (tip) ve açık anahtara (public key) dayalı bir CPU hash bulmacası hedefini çözer.
* **Script Yapısı:** Kayıt işlemi (transaction), çözülen nonce değerini ve ebeveyn blok hash sınamasını (parent block hash challenge) kaydeder. Kayıt dalgalanmasını önlemek için çıktı en az **2.880 blok** boyunca zaman kilitli (timelocked) hale getirilir.

---

## 2. Komut Satırı ve Yapılandırma Seçenekleri

Düğüm başlangıcında, konsensüs doğrulama motorunu desteklemek için deterministik açık/gizli anahtarlar (public/private keys) otomatik olarak türetilir.

### `-adamminerseed`
- 15 deterministik anahtarın türetildiği temel seed dizisini yapılandırır.
- **Varsayılan:** `"adam_miner_seed_"`
- **Kullanım:**
  ```bash
  ./src/kristatechd -adamminerseed=my_secret_miner_seed_string
  ```

---

## 3. RPC Komutları

Madenciler ve operatörler, kayıt sistemiyle aşağıdaki JSON-RPC komutlarını kullanarak etkileşime girerler:

### `registerminer`
Bir madenci kayıt işlemi (transaction) oluşturur, imzalar ve yayınlar (broadcast).

#### Argümanlar:
1. `mode` (string, gerekli): Kayıt modu, `"lock"` veya `"pow"` olmalıdır.
2. `target` (string/numeric, gerekli):
   - `"lock"` için: Teminatın kilidinin açılacağı hedef blok yüksekliği (en az mevcut yükseklik + 2880 olmalıdır).
   - `"pow"` için: Mevcut ucun (tip) blok hash'i (bulmaca sınaması olarak kullanılır).
3. `address_or_pubkey` (string, gerekli):
   - `"lock"` için: Teminata sahip olan KRISTA adresi.
   - `"pow"` için: Madencinin hex kodlu sıkıştırılmış açık anahtarı (public key).

#### Örnek (Coin-Lock):
```bash
# Mevcut yüksekliği alın ve kilit hedefi için 3000 blok ekleyin
./src/kristatech-cli registerminer lock 3800 KTiwjsc71G7gyd3yhk6Ycrg3DFLy1wq7G8J
```

#### Örnek (PoW-Lock):
```bash
# En iyi blok hash'ini getirin
best_hash=$(./src/kristatech-cli getbestblockhash)
# Kaydedilecek bir açık anahtar (pubkey) alın
pubkey=$(./src/kristatech-cli validateaddress KTiwjsc71G7gyd3yhk6Ycrg3DFLy1wq7G8J | jq -r .pubkey)
# PoW kaydını gönderin
./src/kristatech-cli registerminer pow "$best_hash" "$pubkey"
```

### `getadamminers`
Şu anda aktif ve kayıtlı olan tüm madencilerin listesini döndürür. 

* Yeni bir Regtest zincirinde bu, 15 varsayılan deterministik anahtarı döndürür.
* Mainnet/Testnet üzerinde, minimum süre kurallarını karşılayan harcanmamış Coin-Lock'lar ve PoW-Lock'lar için UTXO veritabanını tarar ve bunları havuz endeksleriyle birlikte listeler.

#### Örnek:
```bash
./src/kristatech-cli getadamminers
```

---

## 4. Konsensüs ve Demokratik Doğrulama

Bir blok oluşturulduğunda düğüm, aktif madenci havuzunu tarar. Seçilen madenci kümesi için:
1. Blok zinciri, her kayıt işlemi (transaction) çıktısının **harcanmamış** (unspent) olarak kaldığını doğrular.
2. Konsensüs motoru, kaydın süresinin dolmadığını (zaman kilidinin (timelock) hala aktif olduğunu ve kalan blokları olduğunu) doğrular.
3. Kayıtlı bir madencinin zaman kilidi (timelock) süresi yakında dolacaksa (kalan blok sayısı **240 bloktan** azsa), operatörü kaydını yenilemesi konusunda uyarmak için günlük dosyasında (log) uyarılar gösterir.

---

## 5. Otomatik Madenci Kaydı (`setgenerate`)

Bir düğüm (node) blok üretimini etkinleştirdiğinde (`setgenerate true` komutu veya konfigürasyondaki `gen=1` aracılığıyla), madencilik motoru arka planda madenci kaydını otomatik olarak yönetir:

1. **Aktif Havuz Kontrolü**:
   Motor, yapılandırılmış madenci açık anahtarının (public key) mevcut yükseklikte aktif havuzda zaten kayıtlı olup olmadığını kontrol eder. Eğer zaten kayıtlıysa, otomatik kayıt işlemi sessizce sonlandırılır.

2. **Spam Önleme**:
   Kayıt işlemlerinin (transaction) ağda spam oluşturmasını önlemek için motor, son yayınlanan kaydın blok yüksekliğini takip eder. Eğer son kayıt **50 bloktan** daha kısa bir süre önce gönderildiyse, otomatik kayıt ertelenir.

3. **Anahtar Yönetimi**:
   Motor, madenci açık anahtarı için ilgili BLS anahtarını otomatik olarak oluşturmaya veya cüzdandan almaya çalışır. Cüzdan kilitliyse, şu uyarıyı kaydeder:
   `AutoRegisterMiner: Wallet is locked. Cannot auto-register. Please unlock your wallet or run registerminer manually.`

4. **Mod Seçimi ve Teminat Kontrolü**:
   Motor, uygun kayıt yolunu seçmek için cüzdanın mevcut bakiyesini ve mevcut blok yüksekliğini değerlendirir:
   - **Coin-Lock (PoL) Modu**: Eğer mevcut blok yüksekliği $\ge$ 2.200 ise ve mevcut bakiye en az **1.000 KRISTA** (teminat) ve işlem ücretleri (0.01 KRISTA tampon) kadarsa, motor gelecekte **2.900 blok** kilit süresine sahip bir Coin-Lock işlemi oluşturur. 2.200. bloğun altında (bootstrap aşaması), staker/miner ödüllerinin likit kalmasını sağlamak amacıyla Mainnet üzerinde Coin-Lock otomatik kaydı devre dışı bırakılmıştır.
   - **PoW-Lock Modu**: Eğer blok yüksekliği < 2.200 ise veya bakiye Coin-Lock için yetersizse, motor PoW-Lock moduna geri döner. Mevcut tip blok hash sınamasına karşı arka planda bir CPU PoW bulmaca araması başlatır. Çözüldüğünde, gelecekte **2.900 blok** kilit süresine sahip bir PoW-Lock işlemi oluşturur.

5. **Yayınlama**:
   Oluşturulan işlem (transaction) cüzdana kaydedilir ve ağa yayınlanır (broadcast).
