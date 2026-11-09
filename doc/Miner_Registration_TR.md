# Masternode Olmayan Madenci Kayıt Sistemi

**ADAM (A Decentralized Approach Model)** konsensüs yapısı altında işbirlikçi blok üretimini desteklemek için KristaTech Core, bir **Masternode Olmayan Madenci Kayıt Sistemi** uygular. 

Masternode'lar, sayıları yeterince yüksekse doğrulayıcı (validator) ve koordinatör (coordinator) kümelerindeki seçimler için otomatik olarak uygun olsalar da bu kayıt sistemi standart düğümlerin/cüzdanların teminat (collateral) kilitlemesine veya aktif madenci havuzuna katılmak için başlangıçta bir iş kanıtı (proof-of-work) kaydı gerçekleştirmesine olanak tanır.

---

## 1. Kayıt Yöntemleri

Madenciler kendilerini iki kriptografik yoldan birini kullanarak kaydedebilirler:

### A. Coin-Lock Kaydı (`lock`)
**Coin-Lock** yolu, bir düğümün bir teminat tutarını kilitleyerek madenci havuzunda (`Alt-Kulvar 1B`, 1. Kulvarda 1 ayrılmış koltuk ve 8 teminatlı koltuğa kadar taşma/spillover desteği) yer edinmesini sağlar.
* **Teminat Tutarı:** **250 KRISTA** (`MINER_REGISTRATION_LOCK_AMOUNT = 250 * COIN`, mempool ve `CheckBlock` içinde zorunludur; Mainnet üzerinde Blok 2.200'de Model D aktifleştiğinde açılır).
* **Zaman Kilidi (Timelock) Süresi:** Kayıt yüksekliğinden itibaren en az **2.880 blok** (~30s blok süresiyle yaklaşık 24 saat; Model D aktif olduğunda Testnet/Regtest üzerinde `100 blok`) boyunca kilitlenmelidir.
* **Script Yapısı (`MatchCoinLockRegistration`):** Mutlak `OP_CHECKLOCKTIMEVERIFY` (CLTV) script çıktılarını (`<pubkey> OP_DROP <locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <pubkeyHash> OP_EQUALVERIFY OP_CHECKSIG`) kullanır. Blok yüksekliği belirtilen hedefe ulaşana kadar fonlar harcanamaz, taşınamaz veya kilidi açılamaz.

### B. PoW-Lock Kaydı (`pow`)
**PoW-Lock** yolu, 250 KRISTA teminatı olmayan düğümlerin kaynak tahsisini kanıtlamak için bir CPU bulmacası çözerek katılmasına olanak tanır.

* **Fonlu PoW Kaydı (`POW_LOCK_REGISTRATION_AMOUNT`):** Olgunlaşmış UTXO bakiyesi olan bir düğüm `OP_CHECKLOCKTIMEVERIFY` ile **10 KRISTA** kilitler ve bir CPU hash bulmacasını (`GetMinerPoWLimit`, Mainnet üzerinde 28-bit sıfır öneki) çözerek zaman kilitli bir çıktı oluşturur. Kilitlenen 10 KRISTA madencinin cüzdanında kalır ve kilit süresi dolduğunda tekrar harcanabilir.
* **Anti-Sybil PoW-Lock Konsensüs Kuralları (`nAdamDualTrackHeight` = Blok 200):**
  - **Girdi UTXO Olgunluğu (`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100`):** Fonlu (`10 KRISTA`) bir `PoW-Lock` kayıt işlemini besleyen her girdi UTXO'su en az **100 onaya (confirmation)** sahip olmalıdır; bu kural tek bir cüzdandaki bakiyenin yüzlerce parçaya bölünerek anlık toplu kayıt yapılmasını engeller.
  - **Blok ve İşlem Hız Sınırları (`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 3`):** Konsensüs (`CheckBlock`) ve mempool (`AcceptToMemoryPool`) tarafından işlem başına en fazla **1 adet `PoW-Lock` çıktısı** ve blok başına en fazla **3 adet `PoW-Lock` işlemi** kabul edilir; blok şablonu oluşturucu (`CreateNewBlock`) ise blok başına en fazla **1 adet `PoW-Lock` işlemi** dahil eder. `10 KRISTA` (`POW_LOCK_REGISTRATION_AMOUNT`) tutarının altındaki fonlu `PoW-Lock` çıktıları hem mempool hem de blok doğrulaması tarafından reddedilir (`bad-txns-powlock-amount-too-small`).
  - **Dual-Track ($8 + 3$) Komite Kotası:** **8 koltuklu Teminatlı Kulvar (`ADAM_COLLATERALIZED_TRACK_QUOTA = 8`)** içinde **7 koltuk (`ADAM_MASTERNODE_TRACK_QUOTA = 7`)** Aktif Masternode'lara (`4.200 KRISTA`), **1 koltuk (`ADAM_COIN_LOCK_TRACK_QUOTA = 1`)** ise `250 KRISTA` Coin-Lock doğrulayıcılarına ayrılmıştır (iki alt-kulvar arasında doğrudan taşma uygulanır); Açık PoW madencileri ise **3 koltuklu Açık PoW Kulvarı (`ADAM_OPEN_POW_TRACK_QUOTA = 3`)** için yarışır.
* **Sıfır-Coin PoW Kaydı (`0 KRISTA Bakiye`):**
  - **Bootstrap Desteği:** Aktif doğrulayıcı havuzunda `nAdamMinersCount + 2` (`13`) adetten az doğrulayıcı bulunduğunda, bakiyesi **0 KRISTA** olan düğümler/cüzdanlar hiç coin harcamadan veya UTXO'ya ihtiyaç duymadan otomatik olarak kaydolabilirler.
  - **CPU Bulmaca Hedefi:** Düğüm, `GetZeroCoinPoWLimit` (Mainnet üzerinde 24-bit sıfır öneki) zorluk hedefini çözer.
  - **Konsensüs İstisnası:** Kayıt işlemi **0 KRISTA** değerinde bir çıktı (`nValue = 0`) ve sentinel girdi outpoint (`0x01:0`, blok başına en fazla 1 adet) kullanır. Konsensüs kuralları (`CheckTransaction`), geçerli Sıfır-Coin PoW kayıt scriptleri için açık bir istisna sağlar.
  - **Zaman Kilidi Süresi:** Kayıt yüksekliğinden itibaren **2.900 blok** (~30s blok süresiyle yaklaşık 24 saat / 1 gün) boyunca kilitlenir.

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
   - **Coin-Lock (PoL) Modu**: Eğer mevcut blok yüksekliği $\ge$ 2.200 ise ve mevcut bakiye en az **250 KRISTA** (teminat) ve işlem ücretleri (0.01 KRISTA tampon) kadarsa, motor gelecekte **2.900 blok** kilit süresine sahip bir Coin-Lock işlemi oluşturur. 2.200. bloğun altında (bootstrap aşaması), staker/miner ödüllerinin likit kalmasını sağlamak amacıyla Mainnet üzerinde Coin-Lock otomatik kaydı devre dışı bırakılmıştır.
   - **PoW-Lock Modu**: Eğer blok yüksekliği < 2.200 ise veya bakiye Coin-Lock için yetersizse, motor PoW-Lock moduna geri döner. Eğer cüzdanda en az 100 onaylı UTXO'larda **10,01 KRISTA** (`POW_LOCK_REGISTRATION_AMOUNT + 0.01 * COIN`) bakiye varsa, standart PoW bulmacasını (`GetMinerPoWLimit`) çözer ve gelecekte **2.900 blok** kilit süresine sahip **10 KRISTA** (`POW_LOCK_REGISTRATION_AMOUNT`) kilitli bir `PoW-Lock` işlemi oluşturur. Eğer cüzdan bakiyesi `< 10,01 KRISTA` ise ve aktif havuzda `< 13` (`nAdamMinersCount + 2`) doğrulayıcı bulunuyorsa, harcanacak UTXO'ya ihtiyaç duymadan **Sıfır-Coin PoW bulmacasını** (`GetZeroCoinPoWLimit`) çözer ve **2.900 blok** kilitli 0-değerli bir kayıt işlemi oluşturur.

5. **Yayınlama**:
   Oluşturulan işlem (transaction) cüzdana kaydedilir ve ağa yayınlanır (broadcast).
