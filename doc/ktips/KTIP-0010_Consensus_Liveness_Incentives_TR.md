```
KTIP: 0010
Başlık: Zincir Üstü Masternode Heartbeat Pingleri, Yedek Koltuk Devri, Zamana Bağlı Eşik Gevşetme ve Eşit Emek Teşvik Uyumu
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Aktif
Tür: Standart Takip (Konsensüs / Ekonomi)
Oluşturulma Tarihi: 2026-11-09
```

## Özet
Bu öneri; **%100 Zincir Üstü Masternode Aktifliği (`SendOnChainPing` / `ExtractOnChainMasternodePing`)**, **Aynı Kulvar Yedek Doğrulayıcı Koltuk Devri ($t \ge 60\text{ sn}$)**, **Zamana Bağlı Eşik Gevşetme ($t \ge 120\text{ sn}$)**, **Eşit Emeğe Eşit Kazanç (`%60 / 15 = %4,00` Dengeli Model D, `%75 / 11 ≈ %6,818` Model D öncesi) ve %50 Otomatik Protokol Yakımı / %50 Hazine Bölüşümü**, **LLMQ İkame Vetosu** ve **Düğüm Genelinde Ebeveyn Başına Tek Blok Koruması (`ActiveProposalGuard`)** mekanizmalarını tanımlar. Bu kurallar birlikte senkronizasyondan bağımsız deterministik konsensüs doğrulamasını, Model D altında `8` veya daha fazla birincil doğrulayıcının (Model D öncesinde `5+`) aynı anda çevrimdışı kalması durumunda kilitlenme kurtarmayı ve dürüst doğrulayıcıları sansürlemenin veya yedek anahtar ikamesinin sıfır maddi kazanç sağladığı tam teşvik uyumunu garanti eder.

## Motivasyon
`KTIP-0010` öncesinde dört yapısal uç durum bulunmaktaydı:
1. **Senkronizasyon/Reorg Sırasında P2P Durum Ayrışması (`SEC-12`)**: Komite veya kurul seçimlerinde yerel bellek içi `mnodeman` P2P durumuna güvenilmesi, yeni senkronize olan veya ağ bölünmesi yaşayan düğümlerin farklı aktif Masternode kümeleri hesaplamasına yol açabiliyordu.
2. **Takılan Blok Kilitlenmesi (`R31-1`)**: `SelectAdamNodes` yalnızca ebeveyn bloğun VRF tohumuna bağlı olduğundan, seçilen `15` birincil doğrulayıcıdan `8`'inin (Model D öncesinde `11` doğrulayıcıdan `5`'inin) aynı anda çevrimdışı olması durumunda blok `8` çoğunluk eşiğine (öncesinde `7`) hiçbir zaman ulaşamıyor; zincir yüksekliği ilerlemediği için 2.880 blokluk aktiflik penceresi de çevrimdışı düğümleri düşüremiyordu.
3. **Yedek İkame İstismarı (`R32-2` / `R33-4` / `R35-2`)**: Koordinatörün 60 saniye bekleyip birincil koltukları kendi yedek anahtarlarıyla değiştirerek o koltuğun ödülünü alabilmesi (veya düşürülen koltukların payını kalan koltuklara dağıtabilmesi), rasyonel bir Koordinatör için dürüst doğrulayıcıları dışlama teşviki oluşturuyordu.
4. **Aynı Ebeveyn Üzerinde Çift PoW+PoS Önerisi (`R32-1`)**: Hem PoW hem PoS iş parçacıklarını çalıştıran bir düğümün aynı ebeveyn blok üzerinde hem PoW hem de PoS bloğu önermesi, yetim çatallara ve Model D altında LLMQ oylarının bölünmesine yol açıyordu.

## Teknik Özellikler

### 1. %100 Zincir Üstü Masternode Aktifliği ve Heartbeat Pingleri (`SendOnChainPing`)
Bir Masternode'un $H$ blok yüksekliğinde konsensüs seçimlerine (`SelectAdamNodes`, `ElectQuorumMembers` ve `FillBlockPayee` / `IsBlockPayeeValid`) katılabilmesi için şu iki koşulun birlikte sağlanması zorunludur:
1. `4.200 KRISTA` teminat UTXO'sunun (`collateralOutpoint`) bloğun `CCoinsViewCache` görünümünde harcanmamış olması (`pView->HaveCoin(outpoint)`) ve onay derinliğini karşılaması (`IsValidMasternodeCollateralAndAuth`).
2. Operatör açık anahtarının (`pubKeyMasternode`) son **2.880 blok** içinde zincir üstü aktiflik kaydına sahip olması (`IsMasternodeActiveAtHeight(pubKey, H - 1, 2880)`):
   - Blok Koordinatörü, LLMQ imzacısı veya ADAM doğrulayıcısı olarak blokta yer alması (sıfır işlem ücretiyle aktifliği yeniler) VEYA
   - Aktif Sıcak Masternode'lar tarafından son zincir üstü blok aktifliğinden itibaren **$\ge 1.440$ blok (~12 saat)** geçtiğinde 2.880 blokluk (~24 saat) pencere dolmadan `SendOnChainPing` (`src/activemasternode.cpp`) aracılığıyla otomatik yayınlanan kimliği doğrulanmış zincir üstü Heartbeat Ping işlemini (`0,01 KRISTA` kendi kendine harcanabilir `<33/65-bayt pubkey> OP_CHECKSIGVERIFY OP_1` UTXO'su + `OP_RETURN` kanıtı `collateralOutpoint || vchPingSig` [+ isteğe bağlı `COnChainMasternodeDelegation`], `1000x` `OP_RETURN` çarpanından muaf sabit `0,0001 KRISTA` komisyon ile) göndermiş olması.

---

### 2. Aynı Kulvar Yedek Doğrulayıcı Koltuk Devri ($t \ge 60\text{ sn}$)
$\Delta t = \text{block.nTime} - \text{pindexPrev->GetBlockTime()}$ geçen blok süresi olsun.
* $\Delta t < 60\text{ sn}$ (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60`) için `block.vAdamMiners` dizisi Dengeli Model D altında `15` birincil doğrulayıcı (`vExpectedMiners`; Model D öncesinde `11`) ile birebir aynı olmalıdır.
* $\Delta t \ge 60\text{ sn}$ için (`ADAM_COMMITTEE_FALLBACK_SLOT_SECS = 30` saniyelik dilimlerle ilerleyerek) **kulvar başına** en fazla $n_{\text{slot}}(\Delta t)$ sessiz birincil koltuk (toplamda Model D'de $M = 15$, öncesinde $11$ koltuğa kadar) `vStandbyMiners` içindeki aynı kulvar yedek adaylarla sıralı olarak değiştirilebilir (`src/adam.cpp` içindeki `ResolveAdamCommitteeForTime` / `ValidateAdamCommitteeForTime`):

  $$
  n_{\text{slot}}(\Delta t) = \left\lfloor \frac{\Delta t - 60}{30} \right\rfloor + 1, \quad \text{trackStandbyIdx} < \text{nReplacedInTrack}[t] + n_{\text{slot}}(\Delta t)
  $$

* **Katı Aynı Kulvar Değişimi (`pView` ile `GetCandidateTrack`)**:
  - Bir **Masternode** birincil koltuğu (`Kulvar 0`) yalnızca yedek bir **Masternode** (`Kulvar 0`) ile değiştirilebilir.
  - Bir **Coin-Lock** birincil koltuğu (`Kulvar 1`) yalnızca yedek bir **Coin-Lock** doğrulayıcısı (`Kulvar 1`) ile değiştirilebilir.
  - Bir **Açık PoW** birincil koltuğu (`Kulvar 2`) yalnızca yedek bir **Açık PoW** madencisi (`Kulvar 2`) ile değiştirilebilir.
* **Sıralı Rank Zorunluluğu ve Boş Olmayan Çözüm Şartı**: Her kulvar içinde yedek doğrulayıcılar katı artan rank sırasıyla kullanılmalıdır (`trackStandbyIdx <= prevStandbyIdxInTrack[expectedTrack]` reddedilir) ve değiştirilen her `i` koltuğu geçerli, boş olmayan bir PoW çözümü (`!vAdamSolutions[i].empty()`) içermek zorundadır.

---

### 3. Zamana Bağlı Eşik Gevşetme ($t \ge 120\text{ sn}$)
Yedek devri başladıktan sonra komite hâlâ temel $T_0 = \text{GetAdamThreshold}(H)$ eşiğine ulaşamazsa, `GetAdamEffectiveThreshold(nHeight, nElapsedSecs, nActiveMasternodes)` (`src/consensus/params.h`) fonksiyonu $\Delta t = 120\text{ sn}$'den itibaren her `60 saniyede` bir eşiği `1` düşürür:

$$
T_{\text{eff}}(H, \Delta t) = \max\left(\min(T_0, T_{\min}), T_0 - \left(\left\lfloor \frac{\Delta t - 120}{60} \right\rfloor + 1\right)\right) \quad (\Delta t \ge 120\text{ sn})
$$

Burada alt sınır $T_{\min}$:
* `UPGRADE_MODELD` aktifken VE (`H >= nAdamThresholdBootstrapLimit` VEYA `nActiveMasternodes >= 8`) olduğunda **`ADAM_MODELD_MIN_RELAXED_THRESHOLD = 8`** değeridir. Model D altında Masternode olmayan koltuklar `2 Coin-Lock + 5 Açık PoW = 7 koltuk` ile sınırlı olduğundan, **`8 > 7`** alt sınırı **en az 1 Aktif Masternode'un (`4.200 KRISTA`)** her blokta geçerli çözüm sunmasını zorunlu kılar (Testnet'te `nAdamThresholdModelD = 3` olduğundan $T_0 = 3$ ile sınırlanır).
* Model D öncesinde `H >= nAdamThresholdBootstrapLimit` VEYA `nActiveMasternodes >= 7` iken **`ADAM_MIN_RELAXED_THRESHOLD = 5`** (`> 4` MN-dışı koltuk: `1 Coin-Lock + 3 Açık PoW`).
* Yalnızca `H < nAdamThresholdBootstrapLimit` VE `nActiveMasternodes` Model D'de `< 8` (Model D öncesinde `< 7`) olan erken bootstrap aşamasında **`ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP = 3`** değeridir.
* **Senkronizasyon Güvenli Başlık Doğrulaması (`R34-2`)**: `ContextualCheckBlockHeader`, senkronize olan düğümlerin zincir ucuna bağlı MN sayısı nedeniyle dürüst eşleri yanlışlıkla banlamasını (`Misbehaving(100)`) önlemek için `nActiveMasternodes = 0` geçirir; tam blok doğrulaması (`CheckBlock` içindeki `CheckFormsOfWork`) ise bloğun kendi ebeveyn görünümündeki `GetActiveMasternodePoolCount(H - 1, pView)` değerini zorunlu kılar.

---

### 4. Eşit Emeğe Eşit Kazanç ve %50 Otomatik Protokol Yakımı / %50 Hazine Bölüşümü
`UPGRADE_FAIRNESS` (Yükseklik `1`) ve Dengeli Model D (`src/masternode-payments.cpp` içindeki `FillBlockPayee` ve `IsBlockPayeeValid`) kapsamında **%60**'lık Katılımcı Doğrulayıcı havuzu (`nParticipantSplit`, Model D öncesinde **%75**) ile **%25**'lik Koordinatör/Üretici havuzu (`nProducerSplit`, Model D öncesinde de **%25**) üç kuralla yönetilir:
1. **Çözülen Koltuk Başına Eşit Kazanç** (Model D altında $M = \text{vCommitteeMiners.size()} = 15$, Model D öncesinde $11$):

   $$
   \text{nPartPaymentPerSeat}[i] = \begin{cases}
   \lfloor \text{nPartSplitTotal} / M \rfloor & < M \text{ koltuk çözüldüyse (Model D } \%60/15 = \%4,00\text{, öncesi } {\sim}\%6,818) \\
   \lfloor \text{nPartSplitTotal} / M \rfloor + (\text{kalan satoshi son çözülen koltuğa}) & \text{tüm } M/M \text{ koltuklar çözüldüyse (}15/15\text{ Model D, }11/11\text{ öncesi)}
   \end{cases}
   $$

   Zorunlu 33 baytlık sıkıştırılmış pubkey ile geçerli ve boş olmayan çözüm sunan (`!block.vAdamSolutions[i].empty()`, birincil veya $\Delta t \ge 60\text{ sn}$ aynı kulvar yedek doğrulayıcısı) her $i \in \lbrace 0, \dots, M - 1 \rbrace$ koltuğu (Model D'de `0..14`, öncesinde `0..10`) kulvar ayrımı olmadan eşit `nPartPaymentPerSeat[i]` ödülü alır (Dönem 0'da Model D altında `15/15` çözümlemede `0` satoshi artıkla `0,6324 KRISTA`/koltuk); kurtarma Koordinatörü ise tam **%25** (`PoS'ta Model D'de %22 Staker + %3 Koordinatör`, Model D öncesinde `%20 Staker + %5 Koordinatör`) ödülünü korur.
2. **Çözülmeyen Koltuklar ve Sahipsiz Paylar**:
   Boş bırakılan her yer tutucu koltuk (`block.vAdamSolutions[i].empty()`) **%0** alır ve sahipsiz kalan koltuk payı `nUnclaimedPartSplit` tutarına eklenir.
3. **%50 Otomatik Protokol Yakımı ve %50 Geliştirici Hazinesi Bölüşümü**:
   `nUnclaimedPartSplit > 0` olduğunda, bu tutarın **%50'si (`nProtocolBurnSplit = nUnclaimedPartSplit / 2`)** otomatik olarak zincir üstünde birincil konsensüs yakım adresine (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr`) yakılır (dolaşımdaki `nMoneySupply` arzından saf deflasyon olarak kalıcı biçimde düşülür; blok üreticilerinin komite çözümlerini sansürleme teşvikini önlemek amacıyla `mapAddressBurns` önbelleğinde kişisel PoB ağırlığı oluşturmaz); kalan **%50'si (`nUnclaimedPartSplit - nProtocolBurnSplit`)** ise `nExpectedTreasury` tutarına eklenir:

   $$
   \text{nExpectedBurn} = \lfloor \text{nUnclaimedPartSplit} / 2 \rfloor, \quad \text{nExpectedTreasury} = \text{nTreasurySplit} + (\text{nUnclaimedPartSplit} - \text{nExpectedBurn})
   $$

---

### 5. LLMQ `qblockprop` İkame Vetosu ve `ActiveProposalGuard`
1. **LLMQ `qblockprop` İkame Vetosu (`src/main.cpp`)**:
   - Yalnızca LLMQ üyesinin aktif zincir ucu `msg.block.hashPrevBlock` ile eşleştiğinde (`pProposalView != nullptr`) çalıştırılır.
   - $\Delta t \ge 60\text{ sn}$ sonrasında önerilen blok $i$. koltuğu değiştirmişse (`msg.block.vAdamMiners[i] != vExpectedMiners[i]`) ve LLMQ üyesinin `mapAdamSolutionsCache` önbelleğinde asıl sahip `vExpectedMiners[i]`'ye ait geçerli çözüm zaten mevcutsa, LLMQ üyesi öneriyi veto eder.
   - Boş bırakılan birincil koltuklar (`msg.block.vAdamMiners[i] == vExpectedMiners[i]`) asla veto edilmez; böylece ağda yolda olan çözümler hızlı blokları geciktirmez.
2. **Düğüm Genelinde Ebeveyn Başına Tek Blok Koruması (`src/miner.cpp` içindeki `ActiveProposalGuard`)**:
   - Tüm blok sürümlerinde (`v11` ve `v12`) `(parentHash, coordKey)` başına devam eden (`mapInFlightParentCoord`) ve tamamlanan (`mapProducedParentCoord`) blok önerilerini yıkıcı olmayan FIFO/yaş tahliyesiyle (`PruneParentCoordMap`) takip ederek bir düğümün aynı ebeveyn üzerinde hem PoW hem PoS bloğu üretmesini engeller.

## Geriye Dönük Uyumluluk
* Mainnet, Testnet ve Regtest üzerinde `v1.4.5.9+` (mevcut `v1.5.9.0`) sürümüyle tam olarak aktiftir.

## Referans Uygulama
* Yedek koltuk devri ve kulvar doğrulaması: `src/adam.cpp` içindeki `ResolveAdamCommitteeForTime` ve `ValidateAdamCommitteeForTime`.
* Zamana bağlı eşik gevşetme: `src/consensus/params.h` içindeki `GetAdamEffectiveThreshold`.
* Eşit emek ödemeleri ve %50 otomatik protokol yakımı / %50 hazine bölüşümü: `src/masternode-payments.cpp` içindeki `FillBlockPayee` ve `IsBlockPayeeValid`.
* LLMQ ikame vetosu ve tekil blok öneri koruması: `src/main.cpp` ve `src/miner.cpp`.
