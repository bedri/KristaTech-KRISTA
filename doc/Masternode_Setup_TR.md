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

Tüm KRISTA coinlerinizi kontrol ettiğiniz ve bilgisayarınızda çalışan masaüstü cüzdanı üzerinde aşağıdaki adımları sırayla uygulayın:

### 2.1. Masternode Adresi Oluşturma ve Teminat Gönderimi
1. Cüzdanınızı açın ve senkronize olmasını bekleyin.
2. **Alım (Receive)** kısmından masternode'unuza bir takma ad vererek (örn. `mn1`) yeni bir adres üretin.
3. Bu adrese **tam olarak 4200 KRISTA** gönderin.
4. Gönderim işleminin blok zincirinde en az **15 onay (confirmation)** almasını bekleyin.

> [!IMPORTANT]
> 3. adımda gönderim yaparken işlem ücretinin (tx fee) 4200 KRISTA miktarından düşülmediğinden emin olun. İşlem sonrasında cüzdanınızda tek bir işlemde tam olarak `4200 KRISTA` içeren bir tx çıktısı (UTXO) oluşmalıdır.

### 2.2. Masternode Özel Anahtarlarını (ECDSA ve BLS) Üretme
1. Cüzdan menüsünden **Araçlar (Tools) -> Hata Ayıklama Konsolu (Debug Console)** ekranını açın.
2. Aşağıdaki komutu yazarak masternode için benzersiz bir ECDSA özel anahtarı oluşturun:
   ```bash
   createmasternodekey
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

### 2.4. Soğuk Cüzdandaki `masternode.conf` Dosyasını Düzenleme
1. Cüzdanda **Araçlar (Tools) -> Masternode Yapılandırma Dosyasını Aç (Open Masternode Configuration File)** seçeneğine tıklayın.
2. Açılan `masternode.conf` dosyasına en alt satırda şu formatta bilgileri ekleyin:
   ```text
   # Format: [alias] [IP:27999] [masternodeprivkey] [collateral_output_txid] [collateral_output_index]
   mn1 VPS_IP_ADRESINIZ:27999 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg b2c0199e7152063fb55848c9497e28a47895e6cd4fb5d7c34b69c45a7b6b1234 0
   ```
   *Örnekte `VPS_IP_ADRESINIZ` kısmına sunucunuzun dış IP adresini yazın.*
3. Dosyayı kaydedin ve KRISTA-QT cüzdanınızı kapatıp **yeniden başlatın**.

---

## 3. Adım 2: Sıcak Cüzdan (VPS Sunucusu) Yapılandırması

VPS sunucunuzda aşağıdaki adımları izleyerek KRISTA daemon'ını (`kristatechd`) kurup yapılandırın.

### 3.1. Gerekli Kütüphanelerin Kurulumu ve Derleme / İndirme
Sunucunuzda sistem kütüphanelerini güncelleyin:
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install build-essential libtool autotools-dev autoconf pkg-config libssl-dev libevent-dev bsdmainutils python3 libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-program-options-dev libboost-test-dev libboost-thread-dev libminiupnpc-dev libzmq3-dev libdb++-dev -y
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
maxconnections=125
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
> **Kooperatif Madencilik, Katı Hot/Cold İmza İzolasyonu ve Zincir Üstü Ping (Hot Wallet):**
> * **Katı Hot/Cold İmza İzolasyonu**: Soğuk Cüzdan (`masternode.conf`) yalnızca `CMasternodeBroadcast` aktivasyon mesajlarını yayınlayan bir kontrolcü olarak görev yapar; LLMQ korum paylarını, blok önerilerini veya koordinatör mesajlarını asla imzalamaz. Yalnızca aktif Sıcak Düğüm (`masternode=1` + `activemasternode.conf`) ADAM blok şablonlarını, VRF kanıtlarını, bulmaca çözümlerini ve LLMQ korum paylarını imzalar.
> * **Zincir Üstü (On-Chain) Heartbeat Ping (`SendOnChainPing` & `ExtractOnChainMasternodePing`)**: Konsensüs seçimleri (`SelectAdamNodes`, `ElectQuorumMembers` ve `GetModelDPassiveMasternodePayee`) bellek içi P2P `mnodeman` durumundan bağımsız olarak %100 zincir üstü ve deterministik (`SEC-12`) çalıştığından, her aktif Masternode harcanmamış 4.200 KRISTA teminatının yanı sıra son **2.880 blok** (~24 saat) içinde geçerli bir zincir üstü aktiflik kaydına sahip olmalıdır. Bloklara aktif katılım (`RegisterBlockMasternodeActivity`: Koordinatör, bulmaca çözen Komite Doğrulayıcısı veya Pasif Masternode ödül alıcısı olarak yer alma) zincir üstü aktifliği sıfır maliyetle otomatik olarak yeniler. Sıcak Düğüm cüzdanında küçük bir UTXO bakiyesi (ör. 1 KRISTA) bulunduğunda ise düğüm, zincir üstü blok aktifliği olmadan **$\ge 1.440$ blok (~12 saat)** geçtiğinde otomatik olarak MESCAL `<33/65-bayt pubkey> OP_CHECKSIGVERIFY OP_1` betiği ve `CDataStream` `OP_RETURN` kanıtı (`collateralOutpoint` + `vchPingSig` + isteğe bağlı `COnChainMasternodeDelegation`) içeren kimliği doğrulanmış bir zincir üstü Masternode ping işlemi (`0.01 KRISTA` kendine dönen UTXO) yayınlar. Doğrulanmış Masternode ping işlemleri `1000x` `OP_RETURN` ücret çarpanından muaftır ve sabit `0.0001 KRISTA` işlem ücreti öder.

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
   * Cüzdanı kilitli ise kilidini açın (**Settings -> Unlock Wallet**).
   * **Masternodes** sekmesine gelin. `mn1` masternode'unuzu listede "MISSING" veya "PRE_ENABLED" durumunda göreceksiniz.
   * Masternode'u seçin ve **Start Alias** (veya konsoldan `startmasternode alias false mn1`) komutunu çalıştırın.
   * İşlem başarılı ise konsol çıktısı `success` verecektir.

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
    "message": "Masternode successfully started"
  }
]
```

Eğer `"status": 4` ve `"message": "Masternode successfully started"` mesajını görüyorsanız masternode'unuz başarıyla çalışıyor ve ağda ping gönderme işlemlerini gerçekleştiriyordur.

### Sık Karşılaşılan Durum Kodları (Status Codes):
* `0 (ACTIVE_MASTERNODE_INITIAL)`: Düğüm yeni başladı, henüz soğuk cüzdandan anons edilmedi.
* `1 (ACTIVE_MASTERNODE_SYNC_IN_PROCESS)`: Düğüm senkronize ediliyor, bitmesi beklenmeli.
* `3 (ACTIVE_MASTERNODE_NOT_CAPABLE)`: Masternode çalışmaya uygun değil (IP eşleşmedi, cüzdan kilitli vb.). Detay için `message` alanını okuyun.
* `4 (ACTIVE_MASTERNODE_STARTED)`: Masternode sorunsuz çalışıyor.

---

## 6. Staking, Ödül Dağıtımı ve Geliştirici Fonu Notları
* **Geliştirici Fonunun Staking'den Muaf Tutulması**: Konsensüs ağırlığının geliştirici hazinesinde merkezileşmesini önlemek ve maksimum merkeziyetsizliği sağlamak amacıyla, protokol Geliştirici Fonu (Developer Fund) çıktılarını Proof-of-Stake staking hakkından kesin olarak muaf tutar.
* **Masternode Ödülleri ve Blok Limiti (Önemli)**: Masternode kurulumları blok yüksekliğinden bağımsız olarak **herhangi bir zamanda** gerçekleştirilebilir ve düğümler ağda aktif edilebilir. Ancak masternode sahiplerine ödül ödemeleri (Model D ödül dağıtımı), ancak **blok 2200'de Model D konsensüs güncellemesi** aktif hale geldikten sonra başlayacaktır. Blok 2200 öncesinde masternode'lar aktif edilse dahi ödül almayacaklardır.

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
# Maksimum aktif P2P bağlantısı sayısını sınırlandırın (Varsayılan limitsiz veya çok yüksektir)
maxconnections=64

# Sadece P2P trafiğine izin verin, RPC dışarıya kapalı kalsın
server=1
rpcallowip=127.0.0.1
```

> [!TIP]
> **Sentry Node Mimarisi:** Masternode'unuzu internete doğrudan açmadan, DDoS korumalı sınır sunucuları (Sentry) arkasında çalıştırmak istiyorsanız [Sentry_Node_Setup_TR.md](Sentry_Node_Setup_TR.md) kılavuzunu inceleyin.

