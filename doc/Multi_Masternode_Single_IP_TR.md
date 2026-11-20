# Tek IP Üzerinde Çoklu Masternode (Multi-Masternode) Mantığı ve Yapılandırması

KristaTech (ve diğer DECENOMY tabanlı core coin projelerinde), tek bir fiziksel sunucu (ve tek bir IPv4 adresi) üzerinde, birden fazla masternode çalıştırmak mümkündür. Bu doküman, bu mimarinin çalışma mantığını ve yapılandırma esaslarını açıklamaktadır.

---

## 1. Çalışma Mantığı (Mimarisi)

Geleneksel Dash/PIVX masternode yapısında, her masternode için ayrı bir IP adresi ve ayrı bir cüzdan sürecinin (process/daemon) çalışması gerekirdi. Bu durum sunucu tarafında yüksek RAM/CPU tüketimine ve her yeni düğüm için ek IP maliyetlerine yol açardı.

KristaTech'in dayandığı **DECENOMY** çekirdeği ise, tek bir çalışan `kristatechd` süreci (daemon) içerisinde **birden fazla masternode'u** aynı anda yönetme yeteneğine (Multi-Masternode) sahiptir.

### Temel Prensipler:
1. **Tek Süreç, Tek Port:** Sunucu üzerinde sadece varsayılan portta (`27999`) çalışan tek bir `kristatechd` süreci bulunur.
2. **Çoklu Kimlik:** Bu tek süreç, `~/.kristatech/activemasternode.conf` dosyasında tanımlanan tüm özel anahtarları yükler. Her anahtar için konsensüs aktifliğini ve zincir üstü heartbeat ping sinyallerini bağımsız olarak yönetir.
3. **Ağ Doğrulaması:** Soğuk cüzdan (Controller), her bir masternode için ayrı ayrı 4200 KRISTA teminat işlemini (collateral) kilitler ve ağa yayınlar. Düğümlerin hepsi aynı IP:27999 adresini paylaşsa da, her masternode'un benzersiz bir özel anahtarı ve TxID'si olduğu için ağ tarafından ayrı birer düğüm olarak kabul edilirler.
4. **Katı Hot/Cold İmza İzolasyonu (KTDIP3 ve v2):** KTDIP3 (`UPGRADE_DIP3`, Blok #1'den itibaren aktif) ve KTDIP3 v2 (`nDip3V2Height = 1` Mainnet, Testnet & Regtest) kapsamında soğuk cüzdan (`masternode.conf` + tek tık GUI **Start** veya `protx register`) yalnızca zincir üstü `ProRegTx` / `ProUpRegTx` / `ProUpRevTx` işlemlerini yayınlar; LLMQ korum paylarını, blok önerilerini veya koordinatör imzalarını asla imzalamaz. `nDip3V2Height` itibarıyla VPS Sıcak Düğümü (`masternode=1` + `activemasternode.conf`) kimliğini saf operatör BLS anahtarı (`blsPubKeyOperator` ve `SignBLSOperatorOnly`, boş `ecdsaSig`) ile doğrular; böylece operatör anahtarları sahip teminatından ve `scriptPayout` / `scriptOperatorPayout` ödül adreslerinden tamamen ayrılır.

---

## 2. Yapılandırma Dosyaları

### A. Soğuk Cüzdan (Controller / Host) - `masternode.conf`
Soğuk cüzdanda, her masternode için ayrı bir satır eklenir. Sunucuların IP adresleri aynı port (`27999`) ile tekrarlanır:

```text
# Format: alias IP:port privkey txid vout
mn1_203 203.0.113.10:27999 <MN1_PRIVKEY_WIF> <MN1_COLLATERAL_TXID> 1
mn2_203 203.0.113.10:27999 <MN2_PRIVKEY_WIF> <MN2_COLLATERAL_TXID> 1
mn3_203 203.0.113.10:27999 <MN3_PRIVKEY_WIF> <MN3_COLLATERAL_TXID> 1
```

*Farklı alias'lar aynı IP ve varsayılan 27999 portunu paylaşmaktadır.*

### B. Sıcak Düğüm (VPS Sunucu) - `activemasternode.conf`
VPS sunucusundaki veri dizininde (`~/.kristatech/activemasternode.conf`) o sunucu üzerinde barındırılacak tüm masternode özel anahtarları listelenir:

```text
# Format: alias activemasternodeprivkey [bls_privkey_hex]
# (bls_privkey_hex belirtilmezse KristaTech Core, activemasternodeprivkey üzerinden BLST_KeyGen ile BLS12-381 anahtarını deterministik olarak türetir.)
mn1_203 <MN1_PRIVKEY_WIF> <MN1_BLS_PRIVKEY_HEX>
mn2_203 <MN2_PRIVKEY_WIF> <MN2_BLS_PRIVKEY_HEX>
mn3_203 <MN3_PRIVKEY_WIF> <MN3_BLS_PRIVKEY_HEX>
```

### C. Sıcak Düğüm (VPS Sunucu) - `kristatech.conf`
VPS sunucusundaki veri dizininde (`~/.kristatech/kristatech.conf`), herhangi bir masternode özel anahtarı yazmadan çoklu-masternode modunu etkinleştirin:

```ini
masternode=1
listen=1
txindex=1
server=1
daemon=1
```

> [!IMPORTANT]
> Sıcak Düğümdeki `kristatech.conf` dosyasına **asla** `masternodeprivkey=...` veya `masternodeaddr=...` satırı yazmayın. Sunucudaki tüm aktif masternode özel anahtarları yalnızca `activemasternode.conf` (`0600` izinli) dosyasında tutulmalı, `kristatech.conf` içerisinde yalnızca `masternode=1` bulunmalıdır.

---

## 3. Avantajları

1. **Düşük Kaynak Tüketimi:** Her masternode için ayrı bir `kristatechd` süreci çalıştırmak yerine tek bir süreç çalıştığı için sunucu belleği (RAM) ve işlemci (CPU) kullanımı minimumda kalır.
2. **IP Tasarrufu:** Ekstra IPv4 veya IPv6 adresi satın alma gereksinimi olmadan, tek bir IPv4 adresi üzerinden sınırsız masternode (bakiye yettiği ölçüde) çalıştırılabilir.
3. **Port Karışıklığı Yoktur:** Ağ kurallarının gerektirdiği varsayılan `27999` portundan sapmaya gerek kalmaz.
4. **Basit Yönetim:** Sunucuda ağ ayarları (bind, route, virtual interfaces vb.) ile uğraşmaya gerek yoktur.
