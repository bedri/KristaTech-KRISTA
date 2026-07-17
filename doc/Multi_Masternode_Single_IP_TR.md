# Tek IP Üzerinde Çoklu Masternode (Multi-Masternode) Mantığı ve Yapılandırması

KristaTech (ve diğer DECENOMY tabanlı core coin projelerinde), tek bir fiziksel sunucu (ve tek bir IPv4 adresi) üzerinde, birden fazla masternode çalıştırmak mümkündür. Bu doküman, bu mimarinin çalışma mantığını ve yapılandırma esaslarını açıklamaktadır.

---

## 1. Çalışma Mantığı (Mimarisi)

Geleneksel Dash/PIVX masternode yapısında, her masternode için ayrı bir IP adresi ve ayrı bir cüzdan sürecinin (process/daemon) çalışması gerekirdi. Bu durum sunucu tarafında yüksek RAM/CPU tüketimine ve her yeni düğüm için ek IP maliyetlerine yol açardı.

KristaTech'in dayandığı **DECENOMY** çekirdeği ise, tek bir çalışan `kristatechd` süreci (daemon) içerisinde **birden fazla masternode'u** aynı anda yönetme yeteneğine (Multi-Masternode) sahiptir.

### Temel Prensipler:
1. **Tek Süreç, Tek Port:** Sunucu üzerinde sadece varsayılan portta (`27999`) çalışan tek bir `kristatechd` süreci bulunur.
2. **Çoklu Kimlik:** Bu tek süreç, `/root/.kristatech/activemasternode.conf` dosyasında tanımlanan tüm özel anahtarları yükler. Her anahtar için ağa ayrı ayrı "ping" (keep-alive) sinyali gönderir.
3. **Ağ Doğrulaması:** Soğuk cüzdan (Controller), her bir masternode için ayrı ayrı 4200 KRISTA teminat işlemini (collateral) kilitler ve ağa yayınlar. Düğümlerin hepsi aynı IP:27999 adresini paylaşsa da, her masternode'un benzersiz bir özel anahtarı ve TxID'si olduğu için ağ tarafından ayrı birer düğüm olarak kabul edilirler.

---

## 2. Yapılandırma Dosyaları

### A. Soğuk Cüzdan (Controller / Host) - `masternode.conf`
Soğuk cüzdanda, her masternode için ayrı bir satır eklenir. Sunucuların IP adresleri aynı port (`27999`) ile tekrarlanır:

```text
# Format: alias IP:port privkey txid vout
mn1_168 168.119.227.178:27999 2UNUY2rGp3gDL... 7eea932a0ae27634... 1
mn5_168 168.119.227.178:27999 2TMQxYrc4f2p2... 7d18aed3bcc90211... 1
mn6_168 168.119.227.178:27999 2TGtMdUx4FMZJ... d09403ccb83a4f0f... 1
```

*Farklı alias'lar aynı IP ve varsayılan 27999 portunu paylaşmaktadır.*

### B. Sıcak Düğüm (VPS Sunucu) - `activemasternode.conf`
VPS sunucusundaki veri dizininde (`~/.kristatech/activemasternode.conf`) o sunucu üzerinde barındırılacak tüm masternode özel anahtarları listelenir:

```text
# Format: alias activemasternodeprivkey
mn1_168 2UNUY2rGp3gDL9wCyjcXoaBqfL4qCpe5oJbEpYaTGyovSh1fa8F
mn5_168 2TMQxYrc4f2p2Vva3eEFV3t5YxUweibALxV8fnC2KYSVfuNsjP3
mn6_168 2TGtMdUx4FMZJc9TCHWoaddh5MB34jsLmjwJ1xSipNFEosimayt
```

---

## 3. Avantajları

1. **Düşük Kaynak Tüketimi:** Her masternode için ayrı bir `kristatechd` süreci çalıştırmak yerine tek bir süreç çalıştığı için sunucu belleği (RAM) ve işlemci (CPU) kullanımı minimumda kalır.
2. **IP Tasarrufu:** Ekstra IPv4 veya IPv6 adresi satın alma gereksinimi olmadan, tek bir IPv4 adresi üzerinden sınırsız masternode (bakiye yettiği ölçüde) çalıştırılabilir.
3. **Port Karışıklığı Yoktur:** Ağ kurallarının gerektirdiği varsayılan `27999` portundan sapmaya gerek kalmaz.
4. **Basit Yönetim:** Sunucuda ağ ayarları (bind, route, virtual interfaces vb.) ile uğraşmaya gerek yoktur.
