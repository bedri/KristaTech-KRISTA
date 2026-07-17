# Sentry Node (Sınır Düğümü) Kurulum Kılavuzu

Sentry Node mimarisi, gerçek Masternode'unuzu (ve quorum imzalama anahtarınızı) DDoS/DoS saldırılarından korumak için tasarlanmış gelişmiş bir altyapı çözümüdür. 

Bu mimaride, gerçek masternode'unuz dış dünyaya tamamen kapatılır (gizli IP'de çalışır veya tüm gelen bağlantıları reddeder) ve internete sadece DDoS korumalı public proxy düğümleri (Sentry Node) üzerinden bağlanır.

---

## Mimarinin İşleyişi

```mermaid
graph TD
    Attacker[Saldırgan / Botnet] -- DDoS Saldırısı --x Sentry[Sentry Node - DDoS Korumalı IP]
    Sentry -- Güvenli P2P Geçidi -- Masternode[Masternode - Gizli/Private IP]
    Masternode -- outbound connection -- Sentry
```

* **Sentry Node (Sınır Sunucusu):** DDoS korumalı (OVH, Path.net, Cloudflare Magic Transit vb.) herhangi bir public sunucu. Dış dünya sadece bu düğümün IP adresini görür.
* **Masternode (Ana Sunucu):** Masternode cüzdanınızın çalıştığı sunucu. Dışarıdan gelen hiçbir bağlantıyı kabul etmez (`listen=0`), sadece kendi Sentry düğümünüze dışa doğru (outbound) bağlanır.

---

## Kurulum Adımları

### 1. Sentry Node (Sınır Sunucusu) Yapılandırması
Sentry sunucusunda standart `kristatechd` daemon'ını kurun. Bu düğümün masternode olmasına gerek yoktur (teminat veya özel anahtar içermez).

`kristatech.conf` dosyasını düzenleyin:
```ini
# Sentry Node Yapılandırması
server=1
listen=1
port=27999
maxconnections=125
```

Sentry sunucusunun IP adresini kaydedin (Örn: `198.51.100.50`).

---

### 2. Gerçek Masternode Yapılandırması
Gizli masternode sunucunuzda `kristatech.conf` dosyasını aşağıdaki gibi düzenleyin:

```ini
# Masternode Yapılandırması (Gizli)
server=1
listen=0 # Dışarıdan gelen P2P bağlantı isteklerini reddet

# Sadece kendi Sentry düğümünüze bağlanın
connect=198.51.100.50:27999

# Masternode'un ağda anons edeceği IP (Saldırganlar bu IP'ye paket yollayacaktır ancak düğüm listen=0 olduğu için drop edecektir)
# Buraya gerçek masternode IP'nizi yazın
externalip=203.0.113.10:27999

# Masternode Özel Anahtarı
masternodeprivkey=2UNUY2rGp3gDL9wCyjcXoaBqfL4qCpe5oJbEpYaTGyovSh1fa8F
masternode=1
```

Düğümü başlatın:
```bash
kristatechd -daemon
```

---

## Güvenlik Duvarı (Firewall) Önlemleri

Gerçek masternode sunucusunda, kendi Sentry IP'niz hariç dışarıdan gelen tüm P2P isteklerini UFW kullanarak engelleyin:

```bash
# UFW varsayılan kuralları uygulayın
sudo ufw default deny incoming
sudo ufw default allow outgoing

# Sadece Sentry IP'sinden gelen P2P paketlerine izin verin (198.51.100.50 yerine Sentry IP'nizi yazın)
sudo ufw allow from 198.51.100.50 to any port 27999 proto tcp comment 'Sentry Link'

# SSH portunu sadece kendi IP'nize açın
sudo ufw allow from 1.2.3.4 to any port 22 proto tcp comment 'Safe SSH'

# UFW'yi aktif edin
sudo ufw enable
```

Bu yapılandırma sayesinde:
1. Saldırganlar `externalip` adresini (Masternode IP'nizi) blockchain üzerinden tespit edip saldırsalar dahi, sunucunuz gelen tüm P2P paketlerini firewall veya `listen=0` kuralıyla anında drop eder.
2. Masternode'unuz güvenli bir şekilde blockzinciri verilerini Sentry sunucunuz üzerinden senkronize etmeye ve LLMQ quorum imzalarını oylamaya devam eder.
3. Sentry sunucunuz DDoS saldırısı altında kalsa veya kapansa bile, quorum özel anahtarlarınızın bulunduğu gerçek masternode'unuz asla zarar görmez. Operatör hızlıca yeni bir Sentry IP'si kiralayarak `connect=` parametresini güncelleyebilir ve ağa kesintisiz devam edebilir.
