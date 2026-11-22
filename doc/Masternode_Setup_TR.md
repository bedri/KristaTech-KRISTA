# KristaTech Masternode Kurulum Rehberi (VPS / Cold Node)

Bu rehber, KristaTech (KRISTA) ağı üzerinde güvenli bir şekilde Masternode (Cold Node) kurulumu gerçekleştirmek için gerekli adımları içermektedir. Modern cüzdan sürümünde kullanılan **Multinode** desteği ve dinamik ağ eşlemesi sayesinde VPS sunucusunda harici IP adresi belirtme zorunluluğu (`masternodeaddr`) ortadan kalkmıştır.

---

## 1. Gereksinimler

Masternode çalıştırmak için aşağıdaki bileşenlere ihtiyacınız vardır:
* **Teminat (Collateral):** Tam olarak **4200 KRISTA** coin.
* **Soğuk Cüzdan (Controller Wallet):** KRISTA-QT (Arayüzlü masaüstü cüzdanı). Coinlerinizi güvenli bir şekilde saklar ve masternode'u yönetir.
* **Sıcak Cüzdan (VPS Node / Sunucu):** Masternode'un 7/24 çalışacağı sanal sunucu (VPS).
  * **Önerilen VPS Özellikleri:**
    * İşletim Sistemi: Ubuntu 24.04 veya 26.04 LTS (x64)
    * Donanım: En az 1 vCPU, 2 GB RAM, 20 GB SSD
    * Ağ: 1 adet Sabit Statik IPv4 adresi
    * Varsayılan Bağlantı Portu: `27999` (Ağda masternode için bu port zorunludur)

---

## 2. Adım 1: Soğuk Cüzdan (Controller) Yapılandırması

### Yöntem A: GUI Sihirbazı (`Create Masternode Controller` / `Masternode Kontrolörü Yarat`) — Önerilen

Debug Console açmadan tüm soğuk cüzdan adımlarını doğrudan arayüzden tamamlayabilirsiniz; takma adlar (alias), 15 onay bekleyen taslaklar ve şifreli operatör anahtarları otomatik olarak `wallet.dat` içine (`mnmeta`) kaydedilir (`masternode.conf` dosyası kaldırılmıştır):

1. **KRISTA-QT** masaüstü cüzdanınızı açın, senkronizasyonun tamamlanmasını bekleyin ve kullanılabilir bakiyenizin en az `4200 KRISTA` (+ küçük işlem ücreti) olduğundan emin olun.
2. **MASTERNODE'LAR** sekmesine geçin ve sağ alttaki **Masternode Kontrolörü Yarat (Create Masternode Controller)** butonuna tıklayın.
3. **1. Adım (Giriş)**: Sihirbaz cüzdanınızda boşta bir `4200 KRISTA` teminat UTXO'su olup olmadığını kontrol eder (yoksa otomatik oluşturmaya hazırlanır). **İLERİ (NEXT)** butonuna basın.
4. **2. Adım (İsim)**: Boşluk içermeyen benzersiz bir takma ad girin (örn. `mn1`) ve **İLERİ (NEXT)** butonuna basın.
5. **3. Adım (Adres & BLS Anahtarı)**:
   - VPS sunucunuzun genel IPv4 adresini (örn. `157.90.130.224`) ve P2P portunu (varsayılan `27999`; Rule 8 ile aynı IP'de birden fazla node desteklenir) girin.
   - **Operator BLS Public Key (optional)** alanını boş bırakırsanız cüzdan şifreli operatör ECDSA+BLS anahtar çiftini (`DeriveBLSFromCKey`) otomatik üretip `wallet.dat` içine kaydeder ve `<alias> <privkey> <bls_privkey_hex>` satırını üretir; ayrı bir Sıcak Düğümde `protx bls_generate` ile ürettiğiniz harici 48 baytlık hex `blsPubKeyOperator` anahtarını yapıştırırsanız Sıcak/Soğuk ayrımı korunarak `operatorPubKey=<hex>` çıktısı verilir.
   - **CREATE** butonuna basın.
6. Cüzdan otomatik olarak `4200 KRISTA` teminat işlemini oluşturup kilitler, taslağı/takma adı/anahtarları `wallet.dat` veritabanına yazar ve ekranda `activemasternode.conf` satırını (`<alias> <privkey> <bls_privkey_hex>` veya harici operatör anahtarı seçildiyse `operatorPubKey=<hex>`) gösterir. **COPY LINE** ile kopyalayıp Adım 2'de VPS sunucunuzdaki `~/.kristatech/activemasternode.conf` dosyasına uygulayın. Yerel operatör satırını daha sonra da cüzdan kilidini açıp **Bilgi -> Dışa Aktar (Export)** üzerinden kopyalayabilirsiniz.
7. Teminat işlemi **15 onaya** ulaştığında **MASTERNODES** sekmesinde ilgili satırdaki **Start** butonuna tıklamanız yeterlidir; cüzdan KTDIP3 `ProRegTx` kaydını otomatik imzalayıp ağa gönderir (mempool'dayken `PRE_ENABLED`, blok onayından sonra `ENABLED` olur). Toplu durumu **HOME** özetindeki sıralanabilir **My Masternodes** kartından da izleyebilirsiniz.

> [!NOTE]
> **Eski `masternode.conf` kurulumundan geçiş:** Güncel KristaTech sürümü başlatıldığında veri dizinindeki eski `masternode.conf` dosyası tek seferde otomatik olarak `wallet.dat` içine (`mnmeta` + anahtar deposu) aktarılır ve `0600` izinli `masternode.conf.migrated` dosyasına yeniden adlandırılır. Açılışta cüzdan şifreli ve kilitliyse meta veriler hemen yüklenir, özel anahtar aktarımı ise cüzdan kilidi açıldığı anda otomatik tamamlanır.

---

### Yöntem B: Konsol Üzerinden Manuel Kurulum (İleri Düzey)

Tüm KRISTA coinlerinizi kontrol ettiğiniz ve bilgisayarınızda çalışan masaüstü cüzdanı üzerinde aşağıdaki adımları sırayla uygulayın:

### 2.1. Masternode Adresi Oluşturma ve Teminat Gönderimi
1. Cüzdanınızı açın ve senkronize olmasını bekleyin.
2. **Alım (Receive)** kısmından masternode'unuza bir takma ad vererek (örn. `mn1`) yeni bir adres üretin.
3. Bu adrese **tam olarak 4200 KRISTA** gönderin.
4. Gönderim işleminin blok zincirinde en az **15 onay (confirmation)** almasını bekleyin.

> [!IMPORTANT]
> 3. adımda gönderim yaparken işlem ücretinin (tx fee) 4200 KRISTA miktarından düşülmediğinden emin olun. İşlem sonrasında cüzdanınızda tek bir işlemde tam olarak `4200 KRISTA` içeren bir tx çıktısı (UTXO) oluşmalıdır.

### 2.2. Masternode Özel Anahtarlarını (ECDSA ve BLS) Üretme
1. Cüzdanda **Ayarlar (Settings) -> RPC Debug Console** ekranını açın (veya TopBar'daki **Debug Console** simgesine tıklayın).
2. Masternode için benzersiz bir ECDSA anahtarı (ve Sıcak/Soğuk KTDIP3 kurulumlarında ayrık operatör BLS anahtarı için `protx bls_generate`) üretmek üzere şu komutu yazın:
   ```bash
   createmasternodekey
   ```
   ```bash
   protx bls_generate
   ```
   *Konsolda size uzun bir anahtar üretilecektir (örn: `93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg`). Bu anahtarı güvenli bir yere not edin. Bu sizin **Masternode Private Key (ECDSA)** anahtarınızdır.*
3. **(İsteğe Bağlı / Eski Yöntem)** BLS12-381 imzalama anahtarları, her Masternode veya doğrulayıcı `secp256k1` ECDSA özel anahtarından `DeriveBLSFromCKey` (`BLST_KeyGen(sk_ecdsa)` / HKDF-SHA256) aracılığıyla deterministik olarak türetilir ve `wallet.dat` içerisine otomatik olarak kaydedilir (`AddBLSKey` veya tek argümanlı `setblsprivkey <adres>` RPC komutu ile). Bu nedenle manuel BLS anahtarı üretilmesi **gerekli değildir**; `createblsprivkey` komutu yalnızca geriye dönük uyumluluk amacıyla isteğe bağlı olarak korunmaktadır:
   ```bash
   createblsprivkey
   ```
   *Manuel olarak çalıştırıldığında konsol 64 karakterli hex formatında bir BLS gizli anahtarı döndürür. Standart kurulumlarda `activemasternode.conf` dosyasındaki isteğe bağlı 3. sütunu boş bırakarak daemon'ın kanonik `DeriveBLSFromCKey` anahtar çiftini deterministik olarak türetmesini ve mıhlamasını sağlayın.*

### 2.3. Teminat Girdi Bilgilerini Alma (UTXO / Transaction Hash)
1. Hata ayıklama konsolunda aşağıdaki komutu çalıştırın:
   ```bash
   getmasternodeoutputs
   ```
2. Çıktı şu şekilde olacaktır:
   ```json
   [
     {
       "txhash": "b2c0199e7152063fb55848c9497e28a47895e6cd4fb5d7c34b69c45a7b6b1234",
       "outputidx": 0
     }
   ]
   ```
   Buradaki `"txhash"` değerini (Transaction ID) ve `"outputidx"` (Output Index) değerini not edin.

### 2.4. Zincir Üstü Kayıt ve `wallet.dat` İçinde Takma Ad Yönetimi (CLI / Debug Console)
Düz metin `masternode.conf` dosyası ve kaldırılan eski RPC'ler (`startmasternode` / `listmasternodeconf` / `createmasternodebroadcast`) yerine KTDIP3 kayıtları ve yerel takma adlar doğrudan `wallet.dat` içinde saklanır:
1. `4200 KRISTA` teminatınız `>= 15` onaya ulaştığında zincir üstü kaydı yayınlayın:
   ```bash
   protx register <txhash> <outputidx> <VPS_IP_ADRESINIZ:27999> <SAHIP_ADRESI> <OPERATOR_BLS_PUBKEY> <OYLAMA_ADRESI> 0 <ODUL_ADRESI>
   ```
2. Yerel takma ad atayın ve `wallet.dat` kayıtlarını doğrulayın:
   ```bash
   protx set_alias <txhash>:<outputidx> mn1
   protx wallet_list
   ```
3. İleride yerel takma adı silmek isterseniz `protx remove_meta mn1 [unlock_collateral]` kullanabilirsiniz (zincir üstünde aktif düğümlerin teminatı güvenli olarak kilitli kalır).

---

## 3. Adım 2: Sıcak Cüzdan (VPS Sunucusu) Yapılandırması

VPS sunucunuzda aşağıdaki adımları izleyerek KRISTA daemon'ını (`kristatechd`) kurup yapılandırın.

### 3.1. Gerekli Kütüphanelerin Kurulumu ve Derleme / İndirme
Sunucunuzda sistem kütüphanelerini güncelleyin (`libgmp-dev` zorunludur; `blst` 0.3.13 ve Berkeley DB 4.8 `depends/` üzerinden derlenebilir veya `./contrib/install_db4.sh` / `--with-incompatible-bdb` kullanılabilir — bkz. [build-unix.md](build-unix.md)):
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install build-essential libtool autotools-dev autoconf pkg-config libssl-dev libgmp-dev libevent-dev bsdmainutils python3 libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-program-options-dev libboost-test-dev libboost-thread-dev libminiupnpc-dev libzmq3-dev -y
```
KRISTA cüzdanını derleyin veya binary dosyalarını sunucunuza aktarın.

### 3.2. Sunucu `kristatech.conf` Ayarları
Sunucunuzda veri dizinini (varsayılan: `~/.kristatech/`) oluşturun ve `kristatech.conf` dosyasını düzenleyin:
```bash
mkdir -p ~/.kristatech
nano ~/.kristatech/kristatech.conf
```
Aşağıdaki yapılandırmayı yapıştırın:
```ini
# VPS / Masternode Ayarları
masternode=1
listen=1
txindex=1
server=1
daemon=1

# RPC Bağlantı Ayarları (Sadece yerel bağlantıya izin verilmesi önerilir)
rpcuser=kristarpc
rpcpassword=SunucuİçinÇokGüçlüBirŞifreBelirleyin
rpcallowip=127.0.0.1

# Harici Ağ Ayarları
maxconnections=256
```
> [!IMPORTANT]
> `kristatech.conf` dosyasına kesinlikle `masternodeaddr` veya `masternodeprivkey` parametresi **eklemeyin**. Bu ayarlar artık kullanılmamakta veya başka bir dosyada saklanmaktadır.

### 3.3. Sunucu `activemasternode.conf` Ayarları (Multinode Desteği)
Cüzdanın yeni sürümündeki **Multinode** yapısı gereği masternode özel anahtar(lar)ı `activemasternode.conf` dosyasında saklanır.
```bash
nano ~/.kristatech/activemasternode.conf
```
Aşağıdaki formatta soğuk cüzdanda 2.2. adımda ürettiğiniz **Masternode Private Key**'i girin:
```text
# Format: [alias] [activemasternodeprivkey] (isteğe bağlı: bls_privkey_hex)
mn1 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg
```
*Eğer **tek bir IPv4 adresi** (veya birden fazla IP adresi) üzerinde birden fazla masternode'u tek bir `kristatechd` servisiyle yönetmek istiyorsanız, her bir masternode'un takma adını ve özel anahtarını alt alta satırlar halinde ekleyebilirsiniz (bkz. [Multi_Masternode_Single_IP_TR.md](Multi_Masternode_Single_IP_TR.md)).*

> [!NOTE]
> `bls_privkey_hex` parametresinin girilmesi isteğe bağlıdır. Boş bırakılması durumunda cüzdan daemon'ı Masternode ECDSA özel anahtarından deterministik BLS12-381 anahtarını (`DeriveBLSFromCKey`) otomatik olarak türetir veya cüzdan veritabanında kayıtlı yerel BLS anahtarını yükler.

> [!TIP]
> **Kooperatif Madencilik, Katı Hot/Cold İmza İzolasyonu ve KTDIP3 Deterministik Durum (Hot Wallet):**
> * **Katı Hot/Cold İmza İzolasyonu**: Birleşik KTDIP3 (`UPGRADE_DIP3`, Mainnet, Testnet ve Regtest'te Blok #1'den itibaren aktif) kapsamında Soğuk Cüzdan (`wallet.dat` `mnmeta`) yalnızca zincir üstü `ProRegTx` / `ProUpServTx` / `ProUpRegTx` / `ProUpRevTx` işlemlerini yayınlayan bir kontrolcü olarak görev yapar; LLMQ korum paylarını, blok önerilerini veya koordinatör mesajlarını asla imzalamaz. Sıcak Düğüm (`masternode=1` + `activemasternode.conf`), kimliğini boş `ecdsaSig` ile saf operatör BLS anahtarıyla (`blsPubKeyOperator` + `SignBLSOperatorOnly`) doğrular; böylece operatör anahtarları sahip teminatından ve `scriptPayout` / `scriptOperatorPayout` ödeme betiklerinden (`AppendDip3OrDirectPayment`) ayrıştırılır.
> * **Deterministik Zincir Üstü Durum ve PoSe (`CDeterministicMNManager`)**: Her aktif Masternode'un durumu `evodb` içinde (`4.200 KRISTA` teminatı `>= 15` onaylı `ProRegTx`, `blsPubKeyOperator` ve `ip:port`) deterministik olarak tutulur; böylece eski P2P `mnb`/`mnp` senkron kaymaları ortadan kalkar. Servis güncellemeleri veya PoSe kurtarmaları `ProUpServTx` (`protx update_service`) ile yapılır.

### 3.4. VPS Daemon'ını Başlatma
Sunucuyu çalıştırın:
```bash
kristatechd
```
> [!NOTE]
> Eğer cüzdan ilk kez `txindex=1` ile başlatılıyorsa veya daha önce indekssiz çalıştıysa, cüzdanın indeksleri yeniden oluşturması için ilk başlangıçta şu şekilde çalıştırılmalıdır:
> `kristatechd -reindex`

Masternode'un ağa senkronize olmasını şu komutla izleyebilirsiniz:
```bash
kristatech-cli getblockchaininfo
kristatech-cli mnsync status
```
*`mnsync status` çıktısındaki `"IsBlockchainSynced": true` ve `"IsMasternodeListSynced": true` olana kadar bekleyin.*

---

## 4. Adım 3: Masternode'u Ağa Kaydetme ve Başlatma

1. VPS sunucunuzun tamamen senkronize olduğundan ve soğuk cüzdanınızdaki 4200 KRISTA transfer işleminin en az 15 onay aldığından emin olun.
2. Soğuk Cüzdanda (Controller):
   * Cüzdan kilitli ise kilidini açın (**TopBar -> Wallet Locked -> Unlock Wallet**).
   * **MASTERNODES** sekmesine gelin. `mn1` masternode'unuzu listede "MISSING" veya "PRE_ENABLED" durumunda göreceksiniz.
   * **KTDIP3 (Önerilen, GUI Tek Tık)**: Masternode satırındaki **Start** (veya **Start All** / **Start Inactive/s**) butonuna tıklayın. `UPGRADE_DIP3` aktifken cüzdan tanımlanan/türetilen `blsPubKeyOperator` anahtarını otomatik kullanır ve zincir üstü `ProRegTx` işlemini imzalayıp yayınlar (mempool'da beklerken `PRE_ENABLED`, blok onayından sonra `ENABLED` olur). RPC Debug Console veya CLI gerekmez.
   * **Manuel CLI / RPC (İsteğe Bağlı)**: İsterseniz harici operatör anahtar çiftini `protx bls_generate` ile üreterek `protx register <colHash> <colIdx> <ip:port> <ownerAddr> <opBlsPub> <voteAddr> <opRewardBps> <payoutAddr>` komutuyla kayıt yapabilir, `protx list detailed` veya `listmasternodes` ile doğrulayabilirsiniz.

---

## 5. Durum Kontrolü ve İzleme

Masternode'un başarıyla aktif edildiğini doğrulamak için **VPS Sunucusunda** şu komutu çalıştırın:
```bash
kristatech-cli getmasternodestatus
```

Başarılı bir kurulumda çıktı şu şekilde olmalıdır:
```json
[
  {
    "alias": "mn1",
    "txhash": "b2c0199e7152063fb55848c9497e28a47895e6cd4fb5d7c34b69c45a7b6b1234",
    "outputidx": 0,
    "netaddr": "VPS_IP_ADRESINIZ:27999",
    "addr": "kristacollateraladdress...",
    "status": 4,
    "message": "Masternode successfully started (KTDIP3)"
  }
]
```

Eğer `"status": 4` ve `"message": "Masternode successfully started (KTDIP3)"` (veya DIP3 öncesi kayıtlarda `(KTDIP3)` eki olmadan) mesajını görüyorsanız masternode'unuz başarıyla çalışıyor ve zincir üstünde aktiftir.

### Sık Karşılaşılan Durum Kodları (Status Codes):
* `0 (ACTIVE_MASTERNODE_INITIAL)`: Düğüm yeni başladı, henüz soğuk cüzdandan anons edilmedi.
* `1 (ACTIVE_MASTERNODE_SYNC_IN_PROCESS)`: Düğüm senkronize ediliyor, bitmesi beklenmeli.
* `3 (ACTIVE_MASTERNODE_NOT_CAPABLE)`: Masternode çalışmaya uygun değil (IP eşleşmedi, cüzdan kilitli vb.). Detay için `message` alanını okuyun.
* `4 (ACTIVE_MASTERNODE_STARTED)`: Masternode sorunsuz çalışıyor.

---

## 6. Staking, Ödül Dağıtımı ve Geliştirici Fonu Notları
* **Geliştirici Fonunun Staking'den Muaf Tutulması**: Konsensüs ağırlığının geliştirici hazinesinde merkezileşmesini önlemek ve maksimum merkeziyetsizliği sağlamak amacıyla, protokol Geliştirici Fonu (Developer Fund) çıktılarını Proof-of-Stake staking hakkından kesin olarak muaf tutar.
* **Masternode Ödülleri ve Blok Aşamaları**: Masternode kurulumları blok yüksekliğinden bağımsız olarak **herhangi bir zamanda** yapılabilir. `UPGRADE_FAIRNESS` (Blok `1`) kapsamında Blok `200`–`4.999` arasında (`IsAdamValidatorSplitActive`) Aktif Masternode'lar seçildikleri bloklarda **%75 Aktif Doğrulayıcı Havuzundan** (11 koltuk arasında eşit `~%6,818`/koltuk) ve Koordinatör seçildiklerinde **%25 Koordinatör** payından (PoS'ta `%20` Staker + `%5` Koordinatör) ödül alırlar. **Blok `5.000` (`UPGRADE_MODELD`)** itibarıyla tam Model D dağılımı devreye girer ve **%15 Üretici** (PoS'ta `%12` Staker + `%3` İmzacı Koordinatör) + **%40 Eşit Aktif Doğrulayıcı** (`%40 / 11 ≈ %3,636`/koltuk) paylarına ek olarak **%35 Pasif Masternode Kuyruğu** ile **%10 İmzacı-Odaklı LLMQ Koruma** (1-blok gecikmeyle önceki blok `vQuorumSig` imzacılarına eşit paylı, `5'te 3`..`15'te 8` ölçekli ve deterministik `-5/+5` PoSe puanlamalı) ödülleri başlar (`AppendDip3OrDirectPayment` ile `scriptPayout` ve isteğe bağlı `scriptOperatorPayout` adresine iletilir).

---

## 7. Güvenlik ve DoS/DDoS Koruması

Masternode IP adresleri P2P ağında anons edildiği için, aktif LLMQ Quorum üyeleri hedefli servis dışı bırakma (DoS/DDoS) saldırısı riski altındadır. Düğümünüzün ve ağın kararlılığı için aşağıdaki güvenlik standartlarının uygulanması **şiddetle tavsiye edilir**.

### A. UFW Güvenlik Duvarı Yapılandırması
Sunucuda sadece P2P bağlantı portunu (`27999`) dış dünyaya açın. RPC (`27979`) ve SSH (`22`) portlarını güvenli hale getirin.

```bash
# UFW'yi temizleyin ve varsayılan kuralları uygulayın
sudo ufw default deny incoming
sudo ufw default allow outgoing

# Sadece P2P portunu herkese açın
sudo ufw allow 27999/tcp comment 'KristaTech P2P'

# SSH portunu sadece kendi belirlediğiniz güvenli IP'ye açın (Örn: 1.2.3.4 yerine kendi IP'nizi yazın)
# UYARI: Kendi IP'nizi eklemeden SSH portunu kapatırsanız sunucuya erişiminizi kaybedersiniz!
sudo ufw allow from 1.2.3.4 to any port 22 proto tcp comment 'Safe SSH'

# RPC portunu sadece localhost veya portal sunucunuza açın
sudo ufw allow from 127.0.0.1 to any port 27979 proto tcp comment 'Local RPC'

# Güvenlik duvarını aktif edin
sudo ufw enable
```

### B. Fail2Ban ile DDoS ve Kaynak Tüketim Koruması
P2P portuna geçersiz veri göndererek düğümü kilitlemeye çalışan saldırganları engellemek için `fail2ban` kurun:

```bash
sudo apt-get install fail2ban -y
```

`/etc/fail2ban/jail.local` dosyasına aşağıdaki yapılandırmayı ekleyin:

```ini
[kristatech-p2p]
enabled = true
port = 27999
filter = nosmtp
logpath = /root/.kristatech/debug.log
maxretry = 5
findtime = 600
bantime = 86400
action = iptables-multiport[name=kristatech, port="27999", protocol=tcp]
```

### C. kristatech.conf Optimizasyonları
Düğümünüzün bellek (RAM) ve işlemci tüketimini sınırlandırmak için aşağıdaki parametreleri `kristatech.conf` dosyanıza ekleyin:

```ini
# Maksimum aktif P2P bağlantısı sayısını sınırlandırın (varsayılan: 256)
maxconnections=64

# Sadece P2P trafiğine izin verin, RPC dışarıya kapalı kalsın
server=1
rpcallowip=127.0.0.1
```

> [!TIP]
> **Sentry Node Mimarisi:** Masternode'unuzu internete doğrudan açmadan, DDoS korumalı sınır sunucuları (Sentry) arkasında çalıştırmak istiyorsanız [Sentry_Node_Setup_TR.md](Sentry_Node_Setup_TR.md) kılavuzunu inceleyin.

