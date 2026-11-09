```
KTIP: 0010
Başlık: Zincir Üstü Masternode Heartbeat Pingleri, Yedek Koltuk Devri, Zamana Bağlı Eşik Gevşetme ve Eşit Emek Teşvik Uyumu
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Active
Tür: Standard Track (Consensus / Economics)
Oluşturulma Tarihi: 2026-11-09
```

## Özet
Bu öneri; **%100 Zincir Üstü Masternode Aktifliği (`MNPING1`)**, **Aynı Kulvar Yedek Doğrulayıcı Koltuk Devri ($t \ge 60\text{ sn}$)**, **Zamana Bağlı Eşik Gevşetme ($t \ge 120\text{ sn}$)**, **Eşit Emek, Eşit Kazanç (`1/11`) ve Koordinatör İkame Cezası (`1/11`)**, **LLMQ İkame Vetosu** ve **Düğüm Genelinde Ebeveyn Başına Tek Blok Koruması (`ActiveProposalGuard`)** mekanizmalarını tanımlar. Bu kurallar birlikte senkronizasyondan bağımsız deterministik konsensüs doğrulamasını, 5 veya daha fazla birincil doğrulayıcının aynı anda çevrimdışı kalması durumunda kilitlenme kurtarmayı ve dürüst doğrulayıcıları sansürlemenin veya yedek anahtar ikamesinin sıfır maddi kazanç sağladığı tam teşvik uyumunu garanti eder.

## Motivasyon
`KTIP-0010` öncesinde dört yapısal uç durum bulunmaktaydı:
1. **Senkronizasyon/Reorg Sırasında P2P Durum Ayrışması (`SEC-12`)**: Komite veya kurul seçimlerinde yerel bellek içi `mnodeman` P2P durumuna güvenilmesi, yeni senkronize olan veya ağ bölünmesi yaşayan düğümlerin farklı aktif Masternode kümeleri hesaplamasına yol açabiliyordu.
2. **Takılan Blok Kilitlenmesi (`R31-1`)**: `SelectAdamNodes` yalnızca ebeveyn bloğun VRF tohumuna bağlı olduğundan, seçilen 11 birincil doğrulayıcıdan 5'inin aynı anda çevrimdışı olması durumunda blok `7` çoğunluk eşiğine hiçbir zaman ulaşamıyor; zincir yüksekliği ilerlemediği için 2.880 blokluk aktiflik penceresi de çevrimdışı düğümleri düşüremiyordu.
3. **Yedek İkame İstismarı (`R32-2` / `R33-4` / `R35-2`)**: Koordinatörün 60 saniye bekleyip birincil koltukları kendi yedek anahtarlarıyla değiştirerek `1/11` ödülü alabilmesi (veya düşürülen koltukların payını kalan koltuklara dağıtabilmesi), rasyonel bir Koordinatör için dürüst doğrulayıcıları dışlama teşviki oluşturuyordu.
4. **Aynı Ebeveyn Üzerinde Çift PoW+PoS Önerisi (`R32-1`)**: Hem PoW hem PoS iş parçacıklarını çalıştıran bir düğümün aynı ebeveyn blok üzerinde hem PoW hem de PoS bloğu önermesi, yetim çatallara ve Model D altında LLMQ oylarının bölünmesine yol açıyordu.

## Teknik Özellikler

### 1. %100 Zincir Üstü Masternode Aktifliği ve Heartbeat Pingleri (`MNPING1`)
Bir Masternode'un $H$ blok yüksekliğinde konsensüs seçimlerine (`SelectAdamNodes`, `ElectQuorumMembers` ve `FillBlockPayee` / `IsBlockPayeeValid`) katılabilmesi için şu iki koşulun birlikte sağlanması zorunludur:
1. `4.200 KRISTA` teminat UTXO'sunun (`collateralOutpoint`) bloğun `CCoinsViewCache` görünümünde harcanmamış olması (`pView->HaveCoin(outpoint)`) ve onay derinliğini karşılaması (`IsValidMasternodeCollateralAndAuth`).
2. Operatör açık anahtarının (`pubKeyMasternode`) son **2.880 blok** içinde zincir üstü aktiflik kaydına sahip olması (`IsMasternodeActiveAtHeight(pubKey, H - 1, 2880)`):
   - Blok Koordinatörü, LLMQ imzacısı veya ADAM doğrulayıcısı olarak blokta yer alması VEYA
   - Aktif Sıcak Masternode'lar tarafından her ~720 blokta bir (`MASTERNODE_HEARTBEAT_TX_INTERVAL_BLOCKS = 720`) otomatik yayınlanan zincir üstü Heartbeat Ping işlemini (`OP_RETURN <"MNPING1"> <collateralOutpoint> <blockHash> <pubKeyMasternode> <vchSig>`) göndermiş olması.

---

### 2. Aynı Kulvar Yedek Doğrulayıcı Koltuk Devri ($t \ge 60\text{ sn}$)
$\Delta t = \text{block.nTime} - \text{pindexPrev->GetBlockTime()}$ geçen blok süresi olsun.
* $\Delta t < 60\text{ sn}$ (`ADAM_STANDBY_ROTATION_START_SECS = 60`) için `block.vAdamMiners` dizisi 11 birincil doğrulayıcı (`vExpectedMiners`) ile birebir aynı olmalıdır.
* $\Delta t \ge 60\text{ sn}$ için sessiz kalan en fazla $K(\Delta t)$ birincil koltuk `vStandbyMiners` içindeki yedek adaylarla değiştirilebilir (`src/adam.cpp` içindeki `ResolveAdamCommitteeForTime` / `ValidateAdamCommitteeForTime`):
  $$K(\Delta t) = \min\left(11,\; 4 \times \left(\left\lfloor \frac{\Delta t - 60}{30} \right\rfloor + 1\right)\right)$$
* **Katı Aynı Kulvar Değişimi (`pView` ile `GetCandidateTrack`)**:
  - Bir **Masternode** birincil koltuğu (`Kulvar 0`) yalnızca yedek bir **Masternode** (`Kulvar 0`) ile değiştirilebilir.
  - Bir **Coin-Lock** birincil koltuğu (`Kulvar 1`) yalnızca yedek bir **Coin-Lock** doğrulayıcısı (`Kulvar 1`) ile değiştirilebilir.
  - Bir **Açık PoW** birincil koltuğu (`Kulvar 2`) yalnızca yedek bir **Açık PoW** madencisi (`Kulvar 2`) ile değiştirilebilir.
* **Sıralı Rank Zorunluluğu ve Boş Olmayan Çözüm Şartı**: Her kulvar içinde yedek doğrulayıcılar katı artan rank sırasıyla kullanılmalıdır (`k > nextExpectedStandbyRankInTrack[seatTrack]` reddedilir) ve değiştirilen her `i` koltuğu geçerli, boş olmayan bir PoW çözümü (`!vAdamSolutions[i].empty()`) içermek zorundadır.

---

### 3. Zamana Bağlı Eşik Gevşetme ($t \ge 120\text{ sn}$)
Yedek devri başladıktan sonra komite hâlâ temel $T_0 = \text{GetAdamThreshold}(H)$ eşiğine ulaşamazsa, `GetAdamEffectiveThreshold(H, \Delta t, nActiveMasternodes)` (`src/consensus/params.h`) fonksiyonu $\Delta t = 120\text{ sn}$'den itibaren her `60 saniyede` bir eşiği `1` düşürür:

$$T_{\text{eff}}(H, \Delta t) = \max\left(T_{\min},\; T_0 - \left(\left\lfloor \frac{\Delta t - 120}{60} \right\rfloor + 1\right)\right) \quad (\Delta t \ge 120\text{ sn})$$

Burada alt sınır $T_{\min}$:
* `H >= nAdamThresholdBootstrapLimit` VEYA `nActiveMasternodes >= 7` olduğunda **`ADAM_MIN_RELAXED_THRESHOLD = 5`** değeridir. Masternode olmayan koltuklar `1 Coin-Lock + 3 Açık PoW = 4 koltuk` ile sınırlı olduğundan, `5 > 4` alt sınırı **en az 1 Aktif Masternode'un (`4.200 KRISTA`)** her blokta geçerli çözüm sunmasını zorunlu kılar.
* Yalnızca `H < nAdamThresholdBootstrapLimit` VE `nActiveMasternodes < 7` olan erken bootstrap aşamasında **`ADAM_BOOTSTRAP_SPARSE_MIN_THRESHOLD = 3`** değeridir.
* **Senkronizasyon Güvenli Başlık Doğrulaması (`R34-2`)**: `ContextualCheckBlockHeader`, senkronize olan düğümlerin zincir ucuna bağlı MN sayısı nedeniyle dürüst eşleri yanlışlıkla banlamasını (`Misbehaving(100)`) önlemek için `nActiveMasternodes = 0` geçirir; tam blok doğrulaması (`CheckBlock` içindeki `CheckFormsOfWork`) ise bloğun kendi ebeveyn görünümündeki `GetActiveMasternodePoolCount(H - 1, pView)` değerini zorunlu kılar.

---

### 4. Eşit Emek, Eşit Kazanç (`1/11`) ve Koordinatör İkame Cezası
Model D (`src/masternode-payments.cpp` içindeki `FillBlockPayee` ve `IsBlockPayeeValid`) kapsamında %25'lik Katılımcı Doğrulayıcı havuzu (`nParticipantSplit`) ve %15'lik Koordinatör havuzu (`nProducerSplit`) üç kuralla yönetilir:
1. **Eşit Emek, Eşit Kazanç (Çözülen Koltuk Başına `1/11`)**:
   $$\text{nPartPaymentPerSeat} = \left\lfloor \frac{\text{nParticipantSplit}}{11} \right\rfloor$$
   Geçerli ve boş olmayan çözüm sunan (`!block.vAdamSolutions[i].empty()`, birincil veya $\Delta t \ge 60\text{ sn}$ aynı kulvar yedek doğrulayıcısı) her $i \in \{0, \dots, 10\}$ koltuğu tam `nPartPaymentPerSeat` (`1/11`) ödül alır.
2. **Çözülmeyen Koltuk Paylarının Geliştirici Hazinesine Aktarılması**:
   Boş bırakılan her yer tutucu koltuk (`block.vAdamSolutions[i].empty()`) **%0** alır ve sahipsiz kalan `1/11` payı `nUnclaimedPartSplit` tutarına eklenir.
3. **Koordinatör Yedek İkame Cezası (Değiştirilen Koltuk Başına `1/11`)**:
   Birincil doğrulayıcının yedek doğrulayıcı ile değiştirildiği her $i$ koltuğu (`block.vAdamMiners[i] != vExpectedMiners[i]`) için Koordinatörün %15'lik ödülünden eşit bir `1/11` ceza (`nPartPaymentPerSeat`) düşülür (`nCoordSubstitutionPenalty`) ve `nUnclaimedPartSplit` tutarına eklenir.
   - Böylece bir Koordinatör 60 saniye bekleyip dürüst bir birincil doğrulayıcının yerine kendi yedek anahtarını koyarsa, yedek anahtardan `+1/11` kazanırken Koordinatör ödülünden `-1/11` kaybeder:
     $$\Delta \text{Kazanç}_{\text{Koordinatör}} = +\frac{1}{11} - \frac{1}{11} = 0$$
4. **Geliştirici Hazinesi Yönlendirmesi**:
   Tüm sahipsiz boş koltuk payları ve Koordinatör ikame cezaları deterministik olarak **Geliştirici Hazinesine** yönlendirilir:
   $$\text{nExpectedTreasury} = \text{nTreasurySplit} + \text{nUnclaimedPartSplit}$$

---

### 5. LLMQ `qblockprop` İkame Vetosu ve `ActiveProposalGuard`
1. **LLMQ `qblockprop` İkame Vetosu (`src/main.cpp`)**:
   - Yalnızca LLMQ üyesinin aktif zincir ucu `msg.block.hashPrevBlock` ile eşleştiğinde (`pProposalView != nullptr`) çalıştırılır.
   - $\Delta t \ge 60\text{ sn}$ sonrasında önerilen blok $i$. koltuğu değiştirmişse (`msg.block.vAdamMiners[i] != vExpectedMiners[i]`) ve LLMQ üyesinin `mapAdamSolutionsCache` önbelleğinde asıl sahip `vExpectedMiners[i]`'ye ait geçerli çözüm zaten mevcutsa, LLMQ üyesi öneriyi veto eder.
   - Boş bırakılan birincil koltuklar (`msg.block.vAdamMiners[i] == vExpectedMiners[i]`) asla veto edilmez; böylece ağda yolda olan çözümler hızlı blokları geciktirmez.
2. **Düğüm Genelinde Ebeveyn Başına Tek Blok Koruması (`src/miner.cpp` içindeki `ActiveProposalGuard`)**:
   - Tüm blok sürümlerinde (`v11` ve `v12`) `(parentHash, coordKey)` başına devam eden (`mapInFlightParentCoord`) ve tamamlanan (`mapProducedParentCoord`) blok önerilerini yıkıcı olmayan FIFO/yaş tahliyesiyle (`PruneParentCoordMap`) takip ederek bir düğümün aynı ebeveyn üzerinde hem PoW hem PoS bloğu üretmesini engeller.

## Geriye Dönük Uyumluluk
* Mainnet, Testnet ve Regtest üzerinde `v1.4.5.9` sürümüyle tam olarak aktiftir.

## Referans Uygulama
* Yedek koltuk devri ve kulvar doğrulaması: `src/adam.cpp` içindeki `ResolveAdamCommitteeForTime` ve `ValidateAdamCommitteeForTime`.
* Zamana bağlı eşik gevşetme: `src/consensus/params.h` içindeki `GetAdamEffectiveThreshold`.
* Eşit emek ödemeleri ve ikame cezası: `src/masternode-payments.cpp` içindeki `FillBlockPayee` ve `IsBlockPayeeValid`.
* LLMQ ikame vetosu ve tekil blok öneri koruması: `src/main.cpp` ve `src/miner.cpp`.
