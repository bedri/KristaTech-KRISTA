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
4. **Katı Hot/Cold İmza İzolasyonu (KTDIP3):** KTDIP3 (`UPGRADE_DIP3`, Mainnet, Testnet & Regtest Blok #1'den itibaren aktif) kapsamında soğuk cüzdan; taslakları, yerel takma adları (alias) ve şifreli operatör anahtarlarını doğrudan `wallet.dat` içindeki `mnmeta` kayıtlarında saklar (eski `masternode.conf` dosyası kaldırılmıştır) ve tek tık GUI **Start** veya `protx register` ile yalnızca zincir üstü `ProRegTx` / `ProUpRegTx` / `ProUpRevTx` işlemlerini yayınlar — LLMQ korum paylarını, blok önerilerini veya koordinatör imzalarını asla imzalamaz. VPS Sıcak Düğümü (`masternode=1` + `activemasternode.conf`) kimliğini saf operatör BLS anahtarı (`blsPubKeyOperator` ve `SignBLSOperatorOnly`, boş `ecdsaSig`) ile doğrular; böylece operatör anahtarları sahip teminatından ve `scriptPayout` / `scriptOperatorPayout` ödül adreslerinden tamamen ayrılır.

---

## 2. Kontrolcü Depolaması (`wallet.dat`) ve Sıcak Düğüm Yapılandırması

### A. Soğuk Cüzdan (Controller / Host) — `wallet.dat` (`mnmeta` + `protx`)
Soğuk kontrolcü meta verileri (takma adlar, `IP:port`, 15 onay bekleyen taslaklar ve şifreli operatör anahtarları) doğrudan şifreli `wallet.dat` veritabanında (`mnmeta`) tutulur. Artık elle `masternode.conf` düzenlemeye gerek yoktur (mevcut eski `masternode.conf` dosyaları ilk açılışta otomatik olarak `wallet.dat`'a aktarılıp düz metin WIF özel anahtarları `***REDACTED***` olarak sansürlenmiş ve `0600` izinli `masternode.conf.migrated` olarak arşivlenir):

```bash
# Sıcak düğümde operatör BLS anahtar çifti & Proof-of-Possession üretin (Blok 1'den itibaren ProRegTx v2 için blsPoP zorunludur):
# protx bls_generate  -> {"secret": "...", "public": "<MN1_BLS_PUB>", "pop": "<MN1_BLS_POP>"}
# Aynı VPS IP'sini paylaşan birden fazla düğümü zincir üstüne kaydedin:
# - GUI Sihirbazı 3. Adım: Operator BLS Public Key alanına <MN1_BLS_PUB>:<MN1_BLS_POP> yapıştırın
# - CLI/RPC: <MN1_BLS_PUB> değerini 5. argüman, <MN1_BLS_POP> değerini 9. argüman olarak geçin (operatör anahtarı wallet.dat içindeyse isteğe bağlıdır)
protx register <MN1_TXID> 1 203.0.113.10:27999 <MN1_OWNER_ADDR> <MN1_BLS_PUB> <MN1_VOTE_ADDR> 0 <MN1_PAYOUT_ADDR> <MN1_BLS_POP>
protx set_alias <MN1_TXID>:1 mn1_203
protx wallet_list
```

*Farklı takma adlar aynı IP adresini ve varsayılan portu paylaşabilir (`Rule 8`).*

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
