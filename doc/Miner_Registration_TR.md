# Masternode Olmayan Madenci Kayıt Sistemi

**ADAM (A Decentralized Approach Model)** konsensüs yapısı altında işbirlikçi blok üretimini desteklemek için KristaTech Core, bir **Masternode Olmayan Madenci Kayıt Sistemi** uygular. 

Masternode'lar, sayıları yeterince yüksekse doğrulayıcı (validator) ve koordinatör (coordinator) kümelerindeki seçimler için otomatik olarak uygun olsalar da bu kayıt sistemi standart düğümlerin/cüzdanların teminat (collateral) kilitlemesine veya aktif madenci havuzuna katılmak için başlangıçta bir iş kanıtı (proof-of-work) kaydı gerçekleştirmesine olanak tanır.

---

## 1. Kayıt Yöntemleri

Madenciler kendilerini iki kriptografik yoldan birini kullanarak kaydedebilirler:

### A. Coin-Lock Kaydı (`lock`)
**Coin-Lock** yolu, bir düğümün bir teminat tutarını kilitleyerek madenci havuzunda (`Alt-Kulvar 1B`, 1. Kulvarda 1 öncelikli koltuk, 8 teminatlı koltuğa kadar taşma ve 2. Kulvar `Alt-Katman 2A` VRF taşma desteği) yer edinmesini sağlar.
* **Teminat Tutarı:** **250 KRISTA** (`MINER_REGISTRATION_LOCK_AMOUNT = 250 * COIN`, mempool'da `bad-txns-coinlock-amount-too-small` ile ve `GetAdamMinerPool` içinde zorunludur; konsensüs ve manuel `registerminer lock` tüm blok yüksekliklerinde `250 KRISTA` Coin-Lock kabul ederken, arka plan `AutoRegisterMiner` bootstrap ödüllerini likit tutmak için `>= 2.200` yüksekliğini bekler).
* **Zaman Kilidi (Timelock) Süresi:** Kayıt yüksekliğinden itibaren en az **2.880 blok** (`nRegPeriod`, Mainnet'te ~30s blok süresiyle yaklaşık 24 saat; Model D aktif olduğunda Testnet/Regtest üzerinde `100 blok`, Model D öncesinde `2.880 blok`) boyunca kilitlenmelidir. Cüzdan varsayılan olarak `+20` blokluk onay tamponu ekler (Mainnet'te `2.900 blok`, Model D sonrası Testnet/Regtest'te `120 blok`).
* **Script Yapısı (`MatchCoinLockRegistration`):** Mutlak `OP_CHECKLOCKTIMEVERIFY` (CLTV) script çıktılarını (`<pubkey> OP_DROP <locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <pubkeyHash> OP_EQUALVERIFY OP_CHECKSIG`) kullanır. Blok yüksekliği belirtilen hedefe ulaşana kadar fonlar harcanamaz, taşınamaz veya kilidi açılamaz.

### B. PoW-Lock Kaydı (`pow`)
**PoW-Lock** yolu, 250 KRISTA teminatı olmayan düğümlerin kaynak tahsisini kanıtlamak için bir CPU bulmacası çözerek katılmasına olanak tanır.

* **Fonlu PoW Kaydı (`POW_LOCK_REGISTRATION_AMOUNT`):** Olgunlaşmış UTXO bakiyesi olan bir düğüm `OP_CHECKLOCKTIMEVERIFY` ile **10 KRISTA** kilitler ve son `100` blok içindeki bir ata bloğa bağlı CPU hash bulmacasını (`GetMinerPoWLimit`: Mainnet üzerinde 28-bit, Testnet üzerinde 20-bit, Regtest üzerinde 8-bit sıfır öneki) çözerek zaman kilitli bir çıktı oluşturur. Kilitlenen 10 KRISTA madencinin cüzdanında kalır ve kilit süresi dolduğunda tekrar harcanabilir.
* **Anti-Sybil PoW-Lock Konsensüs Kuralları (`nAdamDualTrackHeight` = Blok 200):**
  - **Girdi UTXO Olgunluğu (`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100`):** Fonlu (`10 KRISTA`) bir `PoW-Lock` kayıt işlemini besleyen her girdi UTXO'su en az **100 onaya (confirmation)** sahip olmalıdır; bu kural tek bir cüzdandaki bakiyenin yüzlerce parçaya bölünerek anlık toplu kayıt yapılmasını engeller.
  - **Blok ve İşlem Hız Sınırları (`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 3`):** Konsensüs (`CheckBlock`) ve mempool (`AcceptToMemoryPool`) tarafından işlem başına en fazla **1 adet `PoW-Lock` çıktısı** ve blok başına en fazla **3 adet `PoW-Lock` işlemi** kabul edilir; blok şablonu oluşturucu (`CreateNewBlock`) ise blok başına en fazla **1 adet `PoW-Lock` işlemi** dahil eder. `10 KRISTA` (`POW_LOCK_REGISTRATION_AMOUNT`) tutarının altındaki fonlu `PoW-Lock` çıktıları hem mempool hem de blok doğrulaması tarafından reddedilir (`bad-txns-powlock-amount-too-small`).
  - **Dual-Track ($8 + 3$) Komite Kotası:** **8 koltuklu Teminatlı Kulvar (`ADAM_COLLATERALIZED_TRACK_QUOTA = 8`)** içinde **7 koltuk (`ADAM_MASTERNODE_TRACK_QUOTA = 7`)** Aktif Masternode'lara (`4.200 KRISTA`), **1 koltuk (`ADAM_COIN_LOCK_TRACK_QUOTA = 1`)** ise `250 KRISTA` Coin-Lock doğrulayıcılarına ayrılmıştır (iki alt-kulvar arasında doğrudan taşma uygulanır); Açık PoW madencileri ise **3 koltuklu Açık PoW Kulvarı (`ADAM_OPEN_POW_TRACK_QUOTA = 3`)** için yarışır.
* **Sıfır-Coin PoW Kaydı (`0 KRISTA Bakiye`, Blok #1'den İtibaren `UPGRADE_HARDENING_V2` ile Sertleştirilmiş):**
  - **Bootstrap Desteği (`GetFundedAdamMinerPoolCount < 13`):** **Fonlu** doğrulayıcı havuzunda (`GetFundedAdamMinerPoolCount`; `4.200 KRISTA` Aktif Masternode, `250 KRISTA` Coin-Lock ve `10 KRISTA` Fonlu PoW-Lock kayıtlarını sayar, `0 KRISTA` Sıfır-Coin PoW ve Blok 1..199 coinbase dolgu anahtarlarını hariç tutar) `nAdamMinersCount + 2` (`13`) adetten az fonlu doğrulayıcı bulunduğunda, bakiyesi **0 KRISTA** olan düğümler/cüzdanlar hiç coin harcamadan veya UTXO'ya ihtiyaç duymadan otomatik olarak kaydolabilirler. `UPGRADE_HARDENING_V2` altında `Blok 1..199` gerçek `X11KVS` blok kazananları (`cachedBootstrapKeys`) fonsuz havuza Sıfır-Coin PoW anahtarlarından **önce** kabul edilerek Sybil kayıtlarının gerçek erken madencileri dışlaması engellenir ve aktif Sıfır-Coin PoW tavanı en güncel **`MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2 = 16`** benzersiz anahtarla sınırlandırılır. Zincirde 13 veya daha fazla fonlu doğrulayıcı aktif olduğunda Sıfır-Coin PoW kapısı konsensüs, mempool, RPC ve cüzdan katmanlarında senkron olarak kapanır (blok ucu ilerlediğinde mempool'da kalan bayat `0x01:0` işlemleri anında tahliye edilir).
  - **Katı Ekonomik Seçim Önceliği (`Fonlu > Bootstrap > Sıfır-Coin`):** `UPGRADE_HARDENING_V2` altında `ElectDualTrackValidators`, Açık PoW (`Kulvar 2`) ve yedek sıralamasını üç kesin alt-katmana ayırır — **Alt-Katman 2A (seçilemeyen `250 KRISTA` Coin-Lock taşması + `10 KRISTA` Fonlu PoW-Lock VRF yarışı)** $\to$ **Alt-Katman 2B (`Blok 1..199` Bootstrap)** $\to$ **Alt-Katman 2C (`0 KRISTA` Sıfır-Coin PoW)** — ve `SelectAdamNodes` `coordinatorOut` seçimini **öncelikle tüm Fonlu Doğrulayıcılardan** (`MN ∪ Coin-Lock ∪ 10 KRISTA PoW-Lock`) bölünmeye duyarsız `4.200 / 250 / 10` teminat ağırlıklı VRF ile yapar; yalnızca ağda sıfır fonlu doğrulayıcı varsa `Bootstrap` ve en son `Sıfır-Coin PoW` katmanına geçer. Böylece `0 KRISTA` anahtarları **yalnızca artan boş koltukları doldurabilir** ve tek bir `4.200 KRISTA` Masternode, `250 KRISTA` Coin-Lock veya `10 KRISTA` PoW-Lock doğrulayıcısının Koordinatörlük ya da Komite koltuğunu asla alamaz.
  - **CPU-Bağımlı `HashX11KVS` Bulmacası & `10` Blokluk Taze Challenge Penceresi:** `UPGRADE_HARDENING_V2` altında `ComputeZeroCoinRegistrationHash`, 80 baytlık deterministik başlık (`"KRZC"[4] || challenge[32] || compressedPubKey[33] || "SYBILV2"[7] || le32(nNonce)[4]`, 33 baytlık sıkıştırılmış pubkey zorunlu) oluşturur ve 7 seviyeli (`127` dallanmalı `X11KV`) **`HashX11KVS`** ile `GetZeroCoinPoWLimit(..., true)` hedefine karşı doğrular (Mainnet üzerinde **14 sıfır bit** [`0003ffff...`], Testnet üzerinde **12 bit** [`000fffff...`], Regtest üzerinde **8 bit** [`00ffffff...`]). Referans verilen `challenge` bloğu son **`10` blok** (`ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2 = 10`, mempool kabulünde son `8` blok) içinde olmalıdır; bu kural çevrimdışı işlem biriktirmeyi (stockpiling) engeller.
  - **Konsensüs ve Cüzdan Güvenliği:** Kayıt işlemi **0 KRISTA** değerinde bir çıktı (`nValue = 0`) ve sentinel girdi outpoint (`0x01:0`, blok başına ve mempool'da aynı anda en fazla 1 adet; zaten `GetAdamMinerPool` içinde aktif olan anahtarlar mempool'dan reddedilir) kullanır. Konsensüs kuralları (`CheckTransaction`), geçerli Sıfır-Coin PoW kayıt scriptleri için açık bir istisna tanır; cüzdan katmanı sentinel `0x01:0` girdilerini coinbase girdisi gibi ele alarak sahte çifte-harcama (`Conflicted`) üretmez, Sıfır-Coin PoW işlemlerini yalnızca mempool kabulünden geçtikten sonra `wallet.dat` veritabanına yazar ve cüzdan başına en fazla **tek bir fonsuz `0 KRISTA` koltuğa** izin verir (ek koltuklar için `10 KRISTA` Fonlu PoW-Lock veya `250 KRISTA` Coin-Lock gerekir).
  - **Kısa Kira Süresi (`240` Blok):** `UPGRADE_HARDENING_V2` altında Sıfır-Coin PoW kayıtlarının aktif havuz ömrü **`240` blok** (Mainnet'te `~2 saat`; Model D sonrası Testnet/Regtest'te **`50` blok**) olarak sınırlandırılmıştır (`ADAM_ZEROCOIN_POW_REG_PERIOD_V2 = 240`, cüzdan kilit yüksekliği `mevcutYükseklik + 260`).

---

## 2. Komut Satırı ve Yapılandırma Seçenekleri (Yalnızca Regtest)

**Yalnızca Regtest (`-regtest`) modunda**, yerel birim ve fonksiyonel test paketlerini desteklemek üzere düğüm başlangıcında deterministik açık/gizli anahtarlar türetilir (Mainnet ve Testnet üzerinde asla aktif değildir).

### `-adamminerseed` *(Yalnızca Regtest)*
- 15 Regtest deterministik anahtarının türetildiği temel seed dizisini yapılandırır.
- **Varsayılan:** `"adam_miner_seed_"`
- **Kullanım:**
  ```bash
  ./src/kristatechd -regtest -adamminerseed=my_secret_miner_seed_string
  ```

---

## 3. RPC Komutları

Madenciler ve operatörler, kayıt sistemiyle aşağıdaki JSON-RPC komutlarını kullanarak etkileşime girerler:

### `registerminer`
Bir madenci kayıt işlemi (transaction) oluşturur, deterministik BLS12-381 anahtarını `wallet.dat` içerisine eşler, imzalar ve ağa yayınlar. Düğüm hâlâ İlk Blok İndirme (`IsInitialBlockDownload`) aşamasındaysa, anahtar zaten aktif havuzda/mempool'daysa veya mempool'da halihazırda bekleyen bir Sıfır-Coin PoW işlemi varsa PoW çözmeden açıklayıcı hata ile erken döner.

#### Argümanlar:
1. `mode` (string, gerekli): Kayıt modu, `"lock"` veya `"pow"` olmalıdır.
2. `target` (string/numeric, isteğe bağlı):
   - `"lock"` için: Teminatın kilidinin açılacağı hedef blok yüksekliği (`< 500.000.000` [`LOCKTIME_THRESHOLD`] blok yüksekliği olmalı, en az `mevcut yükseklik + nRegPeriod` ve en fazla `mevcut yükseklik + 5.760` olmalıdır; boş bırakılırsa veya `0` verilirse otomatik olarak `mevcut yükseklik + nRegPeriod + 20` seçilir).
   - `"pow"` için: Bulmaca sınaması olarak kullanılan mevcut uç (tip) blok hash'i (boş bırakılırsa otomatik olarak `chainActive.Tip()->GetBlockHash()` seçilir).
3. `address_or_pubkey` (string, isteğe bağlı):
   - Yerel cüzdana ait bir KRISTA adresi veya hex kodlu sıkıştırılmış açık anahtar. Boş bırakılırsa cüzdan anahtar havuzundan `"miner-reg"` etiketiyle yeni bir anahtar otomatik olarak ayrılır.

#### Örnek (Tek Argümanlı Otomatik Mod):
```bash
# Otomatik olarak uç blok hash'ini ve yeni bir cüzdan anahtarını seçer (10 KRISTA Fonlu PoW veya 0 KRISTA Bootstrap PoW)
./src/kristatech-cli registerminer pow

# Otomatik olarak yükseklik + nRegPeriod + 20 ve yeni bir cüzdan adresini seçer (250 KRISTA Coin-Lock, manuel RPC her yükseklikte geçerlidir)
./src/kristatech-cli registerminer lock
```

#### Örnek (Açık Coin-Lock):
```bash
# Mevcut yüksekliği alın ve kilit hedefi için 3000 blok ekleyin
./src/kristatech-cli registerminer lock 3800 KTiwjsc71G7gyd3yhk6Ycrg3DFLy1wq7G8J
```

#### Örnek (Açık PoW-Lock):
```bash
# En iyi blok hash'ini getirin
best_hash=$(./src/kristatech-cli getbestblockhash)
# Kaydedilecek bir açık anahtar (pubkey) alın
pubkey=$(./src/kristatech-cli validateaddress KTiwjsc71G7gyd3yhk6Ycrg3DFLy1wq7G8J | jq -r .pubkey)
# PoW kaydını gönderin
./src/kristatech-cli registerminer pow "$best_hash" "$pubkey"
```

### `getadamminers`
Şu anda aktif ve kayıtlı olan tüm madencilerin listesini (`index`, `address`, `pubkey`, `track` [`"masternode"`, `"coin_lock"`, `"pow_lock"`, `"zerocoin_pow"`, `"bootstrap"`] ve `unlock_height`) döndürür; LevelDB `adamregcache_v2` kalıcı önbelleği sayesinde soğuk başlangıçta geçmiş blokları diskten yeniden taramadan milisaniyeler içinde yanıt verir.

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

1. **Aktif Havuz ve Otomatik Fonlu Terfi Kontrolü**:
   Motor, cüzdanın mevcut yükseklikte aktif havuzda zaten **fonlu** bir doğrulayıcı kaydına (`masternode`, `coin_lock` veya `pow_lock`) sahip olup olmadığını kontrol eder; varsa otomatik kayıt sessizce sonlandırılır. Cüzdanda yalnızca **fonsuz** bir kayıt (`zerocoin_pow` veya `bootstrap`) bulunuyorsa, olgunlaşmış bakiye `10,01 KRISTA` altında kaldığı sürece sessizce bekler; cüzdanda olgunlaşmış ($\ge 100$ onaylı) bakiye oluşur oluşmaz düğümü otomatik olarak **Fonlu `10 KRISTA PoW-Lock` (veya $\ge 2.200$ yükseklikte `250 KRISTA Coin-Lock`)** kaydına terfi ettirerek `< 13` bootstrap kapısının operatör müdahalesi gerektirmeden doğal biçimde kapanmasını sağlar.

2. **Spam Önleme**:
   Kayıt işlemlerinin (transaction) ağda spam oluşturmasını önlemek için motor, son yayınlanan kaydın blok yüksekliğini takip eder. Eğer son kayıt **50 bloktan** daha kısa bir süre önce gönderildiyse, otomatik kayıt ertelenir.

3. **Anahtar Yönetimi**:
   Motor, madenci açık anahtarı için ilgili BLS anahtarını otomatik olarak oluşturmaya veya cüzdandan almaya çalışır. Cüzdan kilitliyse, şu uyarıyı kaydeder:
   `AutoRegisterMiner: Wallet is locked. Cannot auto-register. Please unlock your wallet or run registerminer manually.`

4. **Mod Seçimi ve Teminat Kontrolü**:
   Motor, uygun kayıt yolunu seçmek için cüzdanın mevcut bakiyesini ve mevcut blok yüksekliğini değerlendirir:
   - **Coin-Lock (PoL) Modu**: Eğer mevcut blok yüksekliği $\ge$ 2.200 ise ve mevcut bakiye en az **250 KRISTA** (teminat) ve işlem ücretleri (0.01 KRISTA tampon) kadarsa, motor gelecekte **2.900 blok** kilit süresine sahip bir Coin-Lock işlemi oluşturur. 2.200. bloğun altında (bootstrap aşaması), staker/miner ödüllerinin likit kalmasını sağlamak amacıyla Mainnet üzerinde Coin-Lock otomatik kaydı devre dışı bırakılmıştır.
   - **PoW-Lock Modu**: Eğer blok yüksekliği < 2.200 ise veya bakiye Coin-Lock için yetersizse, motor PoW-Lock moduna geri döner. Eğer cüzdanda en az 100 onaylı UTXO'larda **10,01 KRISTA** (`POW_LOCK_REGISTRATION_AMOUNT + 0.01 * COIN`) bakiye varsa, standart PoW bulmacasını (`GetMinerPoWLimit`) çözer ve gelecekte **2.900 blok** kilit süresine sahip **10 KRISTA** (`POW_LOCK_REGISTRATION_AMOUNT`) kilitli bir `PoW-Lock` işlemi oluşturur. Eğer cüzdan bakiyesi `< 10,01 KRISTA` ise, cüzdanda henüz aktif bir fonsuz koltuk yoksa ve **fonlu** doğrulayıcı havuzunda (`GetFundedAdamMinerPoolCount`) `< 13` (`nAdamMinersCount + 2`) fonlu doğrulayıcı bulunuyorsa, harcanacak UTXO'ya ihtiyaç duymadan **`HashX11KVS` Sıfır-Coin PoW bulmacasını** (`GetZeroCoinPoWLimit(..., true)`, Mainnet'te 14-bit) çözer ve **260 blok** (`GetZeroCoinRegPeriod + 20`) kilitli 0-değerli bir kayıt işlemi oluşturur.

5. **Yayınlama**:
   Oluşturulan işlem (transaction) `AcceptToMemoryPool` doğrulamasından geçirildikten sonra cüzdana kaydedilir ve ağa yayınlanır (broadcast).

---

## 6. Qt Cüzdan Dashboard Madenci Merkezi (Miner Hub)

KristaTech-Qt masaüstü cüzdanı, doğrudan Dashboard ekranı üzerinde entegre bir **İşbirlikçi CPU Madenci ve Doğrulayıcı Merkezi (Miner Hub)** sunar (`src/qt/kristatech/dashboardwidget.cpp`):
* **Tek Tıkla İşbirlikçi CPU Madenciliği**: Seçilebilir iş parçacığı (thread) sayısıyla dahili çok iş parçacıklı CPU madenciliğini (`GenerateBitcoins`) tek tıkla başlatır veya durdurur.
* **Göster / Gizle Geçişi & Tam Ekran (`Full Screen`) İzleme Modu**: İşlemler (`Transactions`) başlık satırında canlı madencilik durum göstergeli (`●` / `○`) kalıcı **Show / Hide Miner Hub** düğmesi (`QSettings("fMinerHubHidden")`) ve kart başlığında `✕` hızlı gizleme düğmesi sunar (gizlendiğinde Son İşlemler listesi ekranın %100 genişliğine yayılır); ayrıca kart başlığındaki **`⛶ Full Screen` (`F11` / `Esc`)** düğmesiyle `1-Click Cooperative Miner (ADAM)` paneli tek tıkla tam ekrana büyütülerek 34 karakterlik tam madenci adresleriyle izlenebilir.
* **Arayüzü Dondurmayan Tek Tıkla Kulvar Kaydı**: **Kulvar 2 (`PoW-Lock`: `10 KRISTA` / `0 KRISTA` bootstrap)** ve **Kulvar 1 (`Coin-Lock`: `250 KRISTA`)** için tek tıkla kayıt butonları sunar. PoW bulmaca çözümü arka plan iş parçacığında asenkron olarak (`⏳ Solving PoW...`) yürütülür ve Qt arayüzünü dondurmaz.
* **Canlı Doğrulayıcı ve Komite Rolü Tablosu**: Cüzdandaki tüm madenci kayıtlarını (`Coin-Lock`, `PoW-Lock`, `0-Coin PoW`), Blok `1..199` coinbase dolgu anahtarlarını (`🏁 Bootstrap`) ve aktif/kontrolcü Masternode'ları (`activemasternode.conf` & `masternode.conf`) tek tabloda listeler; canlı çalışma durumunu (`● ACTIVE` yerel CPU iş parçacıkları, `🔌 Ext` harici `cpuminer`/`ccminer` RPC yoklaması, `🌐 VPS` 7/24 uzak Masternode'lar ve `⏸ Stopped` durdurulmuş yerel koltuklar) net biçimde ayırt eder; bir sonraki bloktaki komite rolünü (`Coordinator` / `Seat #i` + V11 tekli veya V12 3-permütasyonlu algoritma hattı), kalan kilit süresini (mempool'dayken `Pending`), ADAM dönemi ödüllü blok/koltuk kazanımlarını (`🏆`), cüzdana yansıyan gerçek KRISTA kazancını, çözülen komite bulmacası sayısını (`🧩`) ve `<RPC_PASSWORD>` maskeli operatör adresiyle (`--coinbase-addr=`) tek tıkla `cpuminer` komutu kopyalama butonunu gösterir.
