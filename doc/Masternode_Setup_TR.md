# KristaTech Masternode Kurulum Rehberi (VPS / Cold Node)

Bu rehber, KristaTech (KRISTA) ağı üzerinde güvenli bir şekilde Masternode (Cold Node) kurulumu gerçekleştirmek için gerekli adımları içermektedir. Modern cüzdan sürümünde kullanılan **Multinode** desteği ve dinamik ağ eşlemesi sayesinde VPS sunucusunda harici IP adresi belirtme zorunluluğu (`masternodeaddr`) ortadan kalkmıştır.

---

## 1. Gereksinimler

Masternode çalıştırmak için aşağıdaki bileşenlere ihtiyacınız vardır:
* **Teminat (Collateral):** Tam olarak **2100 KRISTA** coin.
* **Soğuk Cüzdan (Controller Wallet):** KRISTA-QT (Arayüzlü masaüstü cüzdanı). Coinlerinizi güvenli bir şekilde saklar ve masternode'u yönetir.
* **Sıcak Cüzdan (VPS Node / Sunucu):** Masternode'un 7/24 çalışacağı sanal sunucu (VPS).
  * **Önerilen VPS Özellikleri:**
    * İşletim Sistemi: Ubuntu 20.04 veya 22.04 LTS (x64)
    * Donanım: En az 1 vCPU, 2 GB RAM, 20 GB SSD
    * Ağ: 1 adet Sabit Statik IPv4 adresi
    * Varsayılan Bağlantı Portu: `27999` (Ağda masternode için bu port zorunludur)

---

## 2. Adım 1: Soğuk Cüzdan (Controller) Yapılandırması

Tüm KRISTA coinlerinizi kontrol ettiğiniz ve bilgisayarınızda çalışan masaüstü cüzdanı üzerinde aşağıdaki adımları sırayla uygulayın:

### 2.1. Masternode Adresi Oluşturma ve Teminat Gönderimi
1. Cüzdanınızı açın ve senkronize olmasını bekleyin.
2. **Alım (Receive)** kısmından masternode'unuza bir takma ad vererek (örn. `mn1`) yeni bir adres üretin.
3. Bu adrese **tam olarak 2100 KRISTA** gönderin. 
   > [!IMPORTANT]
   > Gönderim yaparken işlem ücretinin (tx fee) 2100 KRISTA miktarından düşülmediğinden emin olun. İşlem sonrasında cüzdanınızda tek bir işlemde tam olarak `2100 KRISTA` içeren bir tx çıktısı (UTXO) oluşmalıdır.
4. Gönderim işleminin blok zincirinde en az **15 onay (confirmation)** almasını bekleyin.

### 2.2. Masternode Özel Anahtarını (Private Key) Üretme
1. Cüzdan menüsünden **Araçlar (Tools) -> Hata Ayıklama Konsolu (Debug Console)** ekranını açın.
2. Aşağıdaki komutu yazarak masternode için benzersiz bir özel anahtar oluşturun:
   ```bash
   createmasternodekey
   ```
   *Konsolda size uzun bir anahtar üretilecektir (örn: `93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg`). Bu anahtarı güvenli bir yere not edin. Bu sizin **Masternode Private Key**'inizdir.*

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
# Format: [alias] [activemasternodeprivkey]
mn1 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg
```
*Eğer sunucunuzda birden fazla IP adresi tanımlıysa ve birden fazla masternode'u tek bir cüzdan servisiyle yönetmek istiyorsanız, alt alta ekleyebilirsiniz.*

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

1. VPS sunucunuzun tamamen senkronize olduğundan ve soğuk cüzdanınızdaki 2100 KRISTA transfer işleminin en az 15 onay aldığından emin olun.
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
