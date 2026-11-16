// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingswalletrepairwidget.h"
#include "qt/kristatech/settings/forms/ui_settingswalletrepairwidget.h"
#include "qt/kristatech/defaultdialog.h"
#include "qt/kristatech/qtutils.h"
#include "guiutil.h"
#include "util.h"

#include <QDir>
#include <QHBoxLayout>
#include <QVBoxLayout>

SettingsWalletRepairWidget::SettingsWalletRepairWidget(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsWalletRepairWidget)
{
    ui->setupUi(this);
    this->setStyleSheet(parent->styleSheet());

    // Containers
    ui->left->setAutoFillBackground(false);
    ui->left->setAttribute(Qt::WA_StyledBackground, true);
    ui->left->setStyleSheet("QWidget#left { background-color: transparent; border: none; }");
    ui->left->setContentsMargins(6, 6, 6, 6);
    ui->scrollStack->setFrameShape(QFrame::NoFrame);
    ui->scrollStack->setAutoFillBackground(false);
    ui->scrollStack->setStyleSheet("QScrollArea#scrollStack { background-color: transparent; border: none; }");
    if (ui->scrollStack->viewport()) {
        ui->scrollStack->viewport()->setAutoFillBackground(false);
        ui->scrollStack->viewport()->setAttribute(Qt::WA_StyledBackground, true);
        ui->scrollStack->viewport()->setStyleSheet("background-color: transparent; border: none;");
    }
    ui->scrollAreaWidgetStack->setAutoFillBackground(false);
    ui->scrollAreaWidgetStack->setAttribute(Qt::WA_StyledBackground, true);

    // Title & Subtitle
    ui->labelTitle->setText(tr("Wallet Repair"));
    ui->labelTitle->setProperty("cssClass", "text-title-screen");
    ui->labelSubtitle1->setText(tr("Maintenance and recovery tools for your local wallet database and blockchain index. Each action asks for confirmation before restarting."));
    ui->labelSubtitle1->setProperty("cssClass", "text-subtitle");

    // Hide legacy ungrouped rows from the .ui layout and replace with 3-tier safety cards
    const QList<QWidget*> legacyWidgets = {
        ui->pushButtonSalvage, ui->labelMessageSalvage,
        ui->pushButtonRescan, ui->labelMessageRescan,
        ui->pushButtonRecover1, ui->labelMessageRecover1,
        ui->pushButtonRecover2, ui->labelMessageRecover2,
        ui->pushButtonUpgrade, ui->labelMessageUpgrade,
        ui->pushButtonRebuild, ui->labelMessageRebuild,
        ui->pushButtonDelete, ui->labelMessageDelete
    };
    for (QWidget* w : legacyWidgets) {
        if (w) w->setVisible(false);
    }
    // Collapse fixed spacers in legacy verticalLayout_1 (keep title layout at index 0)
    for (int i = 1; i < ui->verticalLayout_1->count(); ++i) {
        if (QSpacerItem* sp = ui->verticalLayout_1->itemAt(i)->spacerItem()) {
            sp->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
    }

    auto makeActionRow = [this](QVBoxLayout* parentLayout,
                                const QString& btnText,
                                const QString& btnCssClass,
                                const QString& titleText,
                                const QString& descText,
                                void (SettingsWalletRepairWidget::*slot)()) -> QWidget* {
        QWidget* row = new QWidget(this);
        row->setObjectName("repairActionRow");
        QHBoxLayout* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 3, 0, 3);
        h->setSpacing(16);

        QPushButton* btn = new QPushButton(btnText, row);
        btn->setMinimumSize(220, 38);
        btn->setMaximumSize(240, 38);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        setCssProperty(btn, btnCssClass);
        connect(btn, &QPushButton::clicked, this, slot);

        QVBoxLayout* textCol = new QVBoxLayout();
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(2);
        QLabel* lblTitle = new QLabel(titleText, row);
        lblTitle->setObjectName("repairRowTitle");
        lblTitle->setWordWrap(true);
        QLabel* lblDesc = new QLabel(descText, row);
        lblDesc->setObjectName("repairRowDesc");
        lblDesc->setWordWrap(true);
        textCol->addWidget(lblTitle);
        textCol->addWidget(lblDesc);

        h->addWidget(btn, 0, Qt::AlignVCenter);
        h->addLayout(textCol, 1);
        parentLayout->addWidget(row);
        return row;
    };

    QVBoxLayout* cardsRoot = new QVBoxLayout();
    cardsRoot->setContentsMargins(0, 12, 10, 16);
    cardsRoot->setSpacing(12);

    // -------------------------------------------------------------------------
    // 0) Calm Wallet Backup Summary Card
    // -------------------------------------------------------------------------
    cardSafety = new QFrame(ui->scrollAreaWidgetStack);
    cardSafety->setObjectName("cardSafety");
    QVBoxLayout* safetyLayout = new QVBoxLayout(cardSafety);
    safetyLayout->setContentsMargins(16, 12, 16, 12);
    safetyLayout->setSpacing(8);

    QHBoxLayout* safetyTop = new QHBoxLayout();
    safetyTop->setSpacing(12);
    QVBoxLayout* safetyInfoCol = new QVBoxLayout();
    safetyInfoCol->setSpacing(3);

    QLabel* lblSafetyHeader = new QLabel(tr("Wallet Backup"), cardSafety);
    lblSafetyHeader->setObjectName("cardSectionHeader");

    labelEncryptionBadge = new QLabel(tr("Checking wallet status..."), cardSafety);
    labelEncryptionBadge->setObjectName("encryptionBadge");
    labelEncryptionBadge->setWordWrap(true);

    QString dataDirStr = GUIUtil::boostPathToQString(GetDataDir());
    labelDataDirValue = new QLabel(tr("Data Directory: %1").arg(dataDirStr), cardSafety);
    labelDataDirValue->setObjectName("repairRowDesc");
    labelDataDirValue->setWordWrap(true);
    labelDataDirValue->setTextInteractionFlags(Qt::TextSelectableByMouse);

    safetyInfoCol->addWidget(lblSafetyHeader);
    safetyInfoCol->addWidget(labelEncryptionBadge);
    safetyInfoCol->addWidget(labelDataDirValue);

    btnQuickBackup = new QPushButton(tr("Backup Wallet (.dat)"), cardSafety);
    btnQuickBackup->setMinimumSize(195, 38);
    btnQuickBackup->setMaximumSize(215, 38);
    btnQuickBackup->setCursor(Qt::PointingHandCursor);
    btnQuickBackup->setFocusPolicy(Qt::NoFocus);
    setCssBtnPrimary(btnQuickBackup);
    connect(btnQuickBackup, &QPushButton::clicked, this, &SettingsWalletRepairWidget::backupWalletNow);

    safetyTop->addLayout(safetyInfoCol, 1);
    safetyTop->addWidget(btnQuickBackup, 0, Qt::AlignVCenter);
    safetyLayout->addLayout(safetyTop);
    cardsRoot->addWidget(cardSafety);

    // -------------------------------------------------------------------------
    // 1) Routine Maintenance
    // -------------------------------------------------------------------------
    cardTier1 = new QFrame(ui->scrollAreaWidgetStack);
    cardTier1->setObjectName("cardRepairGroup");
    QVBoxLayout* tier1Layout = new QVBoxLayout(cardTier1);
    tier1Layout->setContentsMargins(16, 14, 16, 14);
    tier1Layout->setSpacing(8);

    QLabel* lblTier1Header = new QLabel(tr("Routine Maintenance"), cardTier1);
    lblTier1Header->setObjectName("cardSectionHeader");
    QLabel* lblTier1Sub = new QLabel(tr("Non-destructive scans and index checks. Private keys, labels, and block files are preserved."), cardTier1);
    lblTier1Sub->setObjectName("repairRowDesc");
    lblTier1Sub->setWordWrap(true);
    tier1Layout->addWidget(lblTier1Header);
    tier1Layout->addWidget(lblTier1Sub);

    makeActionRow(tier1Layout,
                  tr("Rescan Blockchain"),
                  "btn-repair-action",
                  tr("Scan local blocks for missing wallet transactions (-rescan)"),
                  tr("Use if your balance or recent rewards look incomplete while already synced."),
                  &SettingsWalletRepairWidget::walletRescan);

    makeActionRow(tier1Layout,
                  tr("Rebuild Block Index"),
                  "btn-repair-action",
                  tr("Reconstruct chainstate & block index from local blk*.dat files (-reindex)"),
                  tr("Rebuilds local index databases without re-downloading blocks from peers."),
                  &SettingsWalletRepairWidget::walletReindex);

    upgradeWalletRow = makeActionRow(tier1Layout,
                  tr("Upgrade Wallet Format"),
                  "btn-repair-action",
                  tr("Upgrade wallet.dat internal database structure (-upgradewallet)"),
                  tr("Updates legacy wallet database metadata on startup."),
                  &SettingsWalletRepairWidget::walletUpgrade);

    cardsRoot->addWidget(cardTier1);

    // -------------------------------------------------------------------------
    // 2) Transaction Cache Recovery
    // -------------------------------------------------------------------------
    cardTier2 = new QFrame(ui->scrollAreaWidgetStack);
    cardTier2->setObjectName("cardRepairGroup");
    QVBoxLayout* tier2Layout = new QVBoxLayout(cardTier2);
    tier2Layout->setContentsMargins(16, 14, 16, 14);
    tier2Layout->setSpacing(8);

    QLabel* lblTier2Header = new QLabel(tr("Transaction Cache Recovery"), cardTier2);
    lblTier2Header->setObjectName("cardSectionHeader");
    QLabel* lblTier2Sub = new QLabel(tr("Clears unconfirmed or conflicted wallet cache entries and rescans confirmed blocks."), cardTier2);
    lblTier2Sub->setObjectName("repairRowDesc");
    lblTier2Sub->setWordWrap(true);
    tier2Layout->addWidget(lblTier2Header);
    tier2Layout->addWidget(lblTier2Sub);

    makeActionRow(tier2Layout,
                  tr("Clear Stuck Txes (Keep Labels)"),
                  "btn-repair-action",
                  tr("Zap unconfirmed wallet transactions & keep address/tx labels (-zapwallettxes=1)"),
                  tr("Removes stuck 0-confirmation or conflicted entries while keeping all saved labels."),
                  &SettingsWalletRepairWidget::walletZaptxes1);

    makeActionRow(tier2Layout,
                  tr("Reset Tx Cache (Drop Notes)"),
                  "btn-repair-action",
                  tr("Zap all cached wallet tx entries & drop tx metadata (-zapwallettxes=2)"),
                  tr("Rebuilds transaction history strictly from on-chain blocks; custom tx notes are cleared."),
                  &SettingsWalletRepairWidget::walletZaptxes2);

    cardsRoot->addWidget(cardTier2);

    // -------------------------------------------------------------------------
    // 3) Advanced Recovery
    // -------------------------------------------------------------------------
    cardTier3 = new QFrame(ui->scrollAreaWidgetStack);
    cardTier3->setObjectName("cardRepairGroup");
    QVBoxLayout* tier3Layout = new QVBoxLayout(cardTier3);
    tier3Layout->setContentsMargins(16, 14, 16, 14);
    tier3Layout->setSpacing(8);

    QLabel* lblTier3Header = new QLabel(tr("Advanced Recovery"), cardTier3);
    lblTier3Header->setObjectName("cardSectionHeader");
    QLabel* lblTier3Sub = new QLabel(tr("Create a fresh wallet.dat backup above before running database salvage or full chain resynchronization."), cardTier3);
    lblTier3Sub->setObjectName("repairRowDesc");
    lblTier3Sub->setWordWrap(true);
    tier3Layout->addWidget(lblTier3Header);
    tier3Layout->addWidget(lblTier3Sub);

    makeActionRow(tier3Layout,
                  tr("Salvage Corrupt Wallet"),
                  "btn-repair-subtle",
                  tr("Attempt private-key extraction from a damaged wallet.dat (-salvagewallet)"),
                  tr("Moves wallet.dat to a timestamped backup and recovers keys. Use only if wallet.dat fails to open."),
                  &SettingsWalletRepairWidget::walletSalvage);

    makeActionRow(tier3Layout,
                  tr("Delete Chain && Resync"),
                  "btn-repair-subtle",
                  tr("Erase local blocks/chainstate folders and synchronize from Genesis (-resync)"),
                  tr("Removes local blockchain files (never deletes wallet.dat) and downloads the chain from peers."),
                  &SettingsWalletRepairWidget::walletResync);

    cardsRoot->addWidget(cardTier3);
    cardsRoot->addStretch(1);

    ui->verticalLayout_1->insertLayout(1, cardsRoot);

    updateSafetyBanner();
    applyTierCardTheme(isLightTheme());
}

void SettingsWalletRepairWidget::loadWalletModel()
{
    if (walletModel) {
        connect(walletModel, &WalletModel::encryptionStatusChanged, this, &SettingsWalletRepairWidget::updateSafetyBanner);
        if (upgradeWalletRow) {
            upgradeWalletRow->setVisible(!walletModel->isHDEnabled());
        }
    }
    updateSafetyBanner();
}

void SettingsWalletRepairWidget::updateSafetyBanner()
{
    if (!labelEncryptionBadge) return;
    if (!walletModel) {
        labelEncryptionBadge->setText(tr("Wallet model loading..."));
        return;
    }

    int status = walletModel->getEncryptionStatus();
    if (status == WalletModel::Unencrypted) {
        labelEncryptionBadge->setText(tr("Wallet status: Unencrypted — save a wallet.dat copy before running maintenance."));
        labelEncryptionBadge->setProperty("encrypted", false);
    } else if (status == WalletModel::Locked) {
        labelEncryptionBadge->setText(tr("Wallet status: Encrypted & Locked — private keys are passphrase-protected."));
        labelEncryptionBadge->setProperty("encrypted", true);
    } else {
        labelEncryptionBadge->setText(tr("Wallet status: Encrypted & Unlocked — ready for maintenance or backup."));
        labelEncryptionBadge->setProperty("encrypted", true);
    }
    applyTierCardTheme(isLightTheme());
}

void SettingsWalletRepairWidget::backupWalletNow()
{
    if (!walletModel) {
        inform(tr("Wallet is not loaded yet."));
        return;
    }
    QString saveDir = QDir::homePath();
    QString filename = GUIUtil::getSaveFileName(this,
                                                tr("Backup Wallet Before Repair"),
                                                saveDir,
                                                tr("Wallet Data (*.dat)"),
                                                nullptr);
    if (filename.isEmpty()) return;

    if (walletModel->backupWallet(filename)) {
        inform(tr("Wallet backup saved successfully!"));
    } else {
        warn(tr("Backup Failed"), tr("Could not write wallet backup to:\n%1\nPlease choose another folder.").arg(filename));
    }
}

void SettingsWalletRepairWidget::changeTheme(bool isLightTheme, QString& theme)
{
    applyTierCardTheme(isLightTheme);
}

void SettingsWalletRepairWidget::applyTierCardTheme(bool isLight)
{
    const QString cardBg = isLight ? "#FFFFFF" : "#111A2E";
    const QString safetyBg = isLight ? "#F4F7FC" : "#132038";
    const QString safetyBorder = isLight ? "#D5E1F2" : "#25395E";
    const QString cardBorder = isLight ? "#E2E8F0" : "#1E2D4A";
    const QString headerCol = isLight ? "#3E5C94" : "#9BB4E2";
    const QString titleCol = isLight ? "#1E293B" : "#E2E8F0";
    const QString descCol = isLight ? "#64748B" : "#94A3B8";
    const QString statusCol = isLight ? "#475569" : "#CBD5E1";

    const QString btnSheet = isLight ? QStringLiteral(
        "QPushButton[cssClass=\"btn-primary\"] {"
        "  background-color: #5576B2; border: 1px solid #48669E; border-radius: 6px;"
        "  color: #FFFFFF; font-size: 13px; font-weight: 500; padding: 6px 14px;"
        "}"
        "QPushButton[cssClass=\"btn-primary\"]:hover {"
        "  background-color: #48669E; border: 1px solid #3E598C; color: #FFFFFF;"
        "}"
        "QPushButton[cssClass=\"btn-primary\"]:pressed {"
        "  background-color: #3E598C; border: 1px solid #354D7A; color: #FFFFFF;"
        "}"
        "QPushButton[cssClass=\"btn-repair-action\"] {"
        "  background-color: #F2F6FC; border: 1px solid #C5D5EE; border-radius: 6px;"
        "  color: #3B5990; font-size: 13px; font-weight: 500; padding: 6px 12px;"
        "}"
        "QPushButton[cssClass=\"btn-repair-action\"]:hover {"
        "  background-color: #E4ECF9; border: 1px solid #8DA8D8; color: #2D4673;"
        "}"
        "QPushButton[cssClass=\"btn-repair-action\"]:pressed {"
        "  background-color: #D5E2F6; border: 1px solid #7695CD; color: #243A61;"
        "}"
        "QPushButton[cssClass=\"btn-repair-subtle\"] {"
        "  background-color: #F8FAFC; border: 1px solid #CBD5E1; border-radius: 6px;"
        "  color: #475569; font-size: 13px; font-weight: 500; padding: 6px 12px;"
        "}"
        "QPushButton[cssClass=\"btn-repair-subtle\"]:hover {"
        "  background-color: #EEF3FA; border: 1px solid #9EB4D8; color: #334E7B;"
        "}"
        "QPushButton[cssClass=\"btn-repair-subtle\"]:pressed {"
        "  background-color: #E2EBF7; border: 1px solid #7695CD; color: #263C63;"
        "}"
    ) : QStringLiteral(
        "QPushButton[cssClass=\"btn-primary\"] {"
        "  background-color: #5576B2; border: 1px solid #6C8BC4; border-radius: 6px;"
        "  color: #FFFFFF; font-size: 13px; font-weight: 500; padding: 6px 14px;"
        "}"
        "QPushButton[cssClass=\"btn-primary\"]:hover {"
        "  background-color: #6384C0; border: 1px solid #8AA6DA; color: #FFFFFF;"
        "}"
        "QPushButton[cssClass=\"btn-primary\"]:pressed {"
        "  background-color: #466399; border: 1px solid #6C8BC4; color: #FFFFFF;"
        "}"
        "QPushButton[cssClass=\"btn-repair-action\"] {"
        "  background-color: #172540; border: 1px solid #2D4470; border-radius: 6px;"
        "  color: #DCE7F7; font-size: 13px; font-weight: 500; padding: 6px 12px;"
        "}"
        "QPushButton[cssClass=\"btn-repair-action\"]:hover {"
        "  background-color: #213458; border: 1px solid #5576B2; color: #FFFFFF;"
        "}"
        "QPushButton[cssClass=\"btn-repair-action\"]:pressed {"
        "  background-color: #2A416E; border: 1px solid #7695CD; color: #FFFFFF;"
        "}"
        "QPushButton[cssClass=\"btn-repair-subtle\"] {"
        "  background-color: #141E33; border: 1px solid #263654; border-radius: 6px;"
        "  color: #CBD5E1; font-size: 13px; font-weight: 500; padding: 6px 12px;"
        "}"
        "QPushButton[cssClass=\"btn-repair-subtle\"]:hover {"
        "  background-color: #1C2B46; border: 1px solid #4C6CA8; color: #F1F5F9;"
        "}"
        "QPushButton[cssClass=\"btn-repair-subtle\"]:pressed {"
        "  background-color: #243759; border: 1px solid #6C8BC4; color: #FFFFFF;"
        "}"
    );

    const QString sheet = QString(
        "QWidget#scrollAreaWidgetStack { background-color: transparent; border: none; }"
        "QWidget#repairActionRow { background-color: transparent; border: none; }"
        "QFrame#cardSafety { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        "QFrame#cardRepairGroup { background-color: %3; border: 1px solid %4; border-radius: 8px; }"
        "QLabel#cardSectionHeader { color: %5; background-color: transparent; border: none; font-size: 14px; font-weight: 600; }"
        "QLabel#repairRowTitle { color: %6; background-color: transparent; border: none; font-size: 13px; font-weight: 500; }"
        "QLabel#repairRowDesc { color: %7; background-color: transparent; border: none; font-size: 12px; font-weight: normal; }"
        "QLabel#encryptionBadge { color: %8; background-color: transparent; border: none; font-size: 12px; font-weight: normal; }"
    ).arg(safetyBg, safetyBorder, cardBg, cardBorder, headerCol, titleCol, descCol, statusCol) + btnSheet;

    if (ui && ui->scrollAreaWidgetStack) {
        ui->scrollAreaWidgetStack->setStyleSheet(sheet);
    }
}

bool SettingsWalletRepairWidget::confirmRepairAction(const QString& title, const QString& htmlBody, const QString& okText, bool isDanger)
{
    QWidget* host = window ? static_cast<QWidget*>(window) : static_cast<QWidget*>(this);
    showHideOp(true);
    DefaultDialog* dialog = new DefaultDialog(host);
    dialog->setDanger(isDanger);
    dialog->setText(title, htmlBody, okText, tr("CANCEL"));
    bool confirmed = openDialogWithOpaqueBackgroundY(dialog, window, 3, 5);
    dialog->deleteLater();
    return confirmed;
}

/** Restart wallet with "-salvagewallet" */
void SettingsWalletRepairWidget::walletSalvage()
{
    QString msg = tr("<b>Emergency Wallet Salvage (-salvagewallet)</b><br/><br/>"
                     "This moves <code>wallet.dat</code> to a timestamped backup and attempts raw private-key recovery.<br/><br/>"
                     "Use this <b>ONLY</b> if your wallet database is corrupted. Make sure you have saved an external backup first.<br/><br/>"
                     "Restart and salvage wallet now?");
    if (!confirmRepairAction(tr("Confirm Wallet Salvage"), msg, tr("SALVAGE"), true)) return;
    buildParameterlist(SALVAGEWALLET);
}

/** Restart wallet with "-rescan" */
void SettingsWalletRepairWidget::walletRescan()
{
    QString msg = tr("<b>Rescan Local Blockchain (-rescan)</b><br/><br/>"
                     "Your wallet will restart and scan local blocks for missing transactions.<br/>"
                     "All keys, labels, and local block files remain untouched.<br/><br/>"
                     "Restart and rescan now?");
    if (!confirmRepairAction(tr("Confirm Blockchain Rescan"), msg, tr("RESCAN"), false)) return;
    buildParameterlist(RESCAN);
}

/** Restart wallet with "-zapwallettxes=1" */
void SettingsWalletRepairWidget::walletZaptxes1()
{
    QString msg = tr("<b>Clear Stuck Transactions — Keep Labels (-zapwallettxes=1)</b><br/><br/>"
                     "Removes unconfirmed or conflicted wallet transactions and rescans confirmed blocks.<br/>"
                     "All address book entries and transaction labels are preserved.<br/><br/>"
                     "Restart and clean stuck transactions now?");
    if (!confirmRepairAction(tr("Confirm Transaction Recovery (Keep Labels)"), msg, tr("RECOVER"), false)) return;
    buildParameterlist(ZAPTXES1);
}

/** Restart wallet with "-zapwallettxes=2" */
void SettingsWalletRepairWidget::walletZaptxes2()
{
    QString msg = tr("<b>Reset Transaction Cache — Drop Notes (-zapwallettxes=2)</b><br/><br/>"
                     "Clears all cached wallet transaction entries and rebuilds history strictly from confirmed blocks.<br/>"
                     "<b>Note:</b> Custom transaction comments/metadata will be removed (private keys and funds remain safe).<br/><br/>"
                     "Restart and reset transaction cache now?");
    if (!confirmRepairAction(tr("Confirm Deep Transaction Reset"), msg, tr("RESET TX CACHE"), false)) return;
    buildParameterlist(ZAPTXES2);
}

/** Restart wallet with "-upgradewallet" */
void SettingsWalletRepairWidget::walletUpgrade()
{
    QString msg = tr("<b>Upgrade Wallet Database Format (-upgradewallet)</b><br/><br/>"
                     "Restarts the wallet and upgrades internal <code>wallet.dat</code> format metadata to the latest supported version.<br/><br/>"
                     "Restart and upgrade format now?");
    if (!confirmRepairAction(tr("Confirm Wallet Format Upgrade"), msg, tr("UPGRADE"), false)) return;
    buildParameterlist(UPGRADEWALLET);
}

/** Restart wallet with "-reindex" */
void SettingsWalletRepairWidget::walletReindex()
{
    QString msg = tr("<b>Rebuild Blockchain Index (-reindex)</b><br/><br/>"
                     "Reconstructs block and chainstate indexes from your existing <code>blk*.dat</code> files on disk.<br/>"
                     "This may take several minutes depending on chain height.<br/><br/>"
                     "Restart and rebuild index now?");
    if (!confirmRepairAction(tr("Confirm Rebuild Index"), msg, tr("REBUILD"), false)) return;
    buildParameterlist(REINDEX);
}

/** Restart wallet with "-resync" */
void SettingsWalletRepairWidget::walletResync()
{
    QString resyncWarning = tr("<b>Delete Local Blockchain &amp; Resync (-resync)</b><br/><br/>"
                               "This will erase your local <code>blocks</code>, <code>chainstate</code>, and <code>sporks</code> folders and synchronize the entire KristaTech blockchain from scratch.<br/><br/>"
                               "Your <code>wallet.dat</code> keys are NOT deleted, and your funds will reappear once synchronization completes.<br/><br/>"
                               "Are you sure you want to delete local chain data and resync?");
    if (!confirmRepairAction(tr("Confirm Full Blockchain Resync"), resyncWarning, tr("DELETE && RESYNC"), true)) {
        return;
    }
    buildParameterlist(RESYNC);
}

/** Build command-line parameter list for restart */
void SettingsWalletRepairWidget::buildParameterlist(QString arg)
{
    // Get command-line arguments and remove the application name
    QStringList args = QApplication::arguments();
    args.removeFirst();

    // Remove existing repair-options
    args.removeAll(SALVAGEWALLET);
    args.removeAll(RESCAN);
    args.removeAll(ZAPTXES1);
    args.removeAll(ZAPTXES2);
    args.removeAll(UPGRADEWALLET);
    args.removeAll(REINDEX);
    args.removeAll(RESYNC);

    // Append repair parameter to command line.
    args.append(arg);

    // Send command-line arguments to KRISTATECHGUI::handleRestart()
    Q_EMIT handleRestart(args);
}

SettingsWalletRepairWidget::~SettingsWalletRepairWidget()
{
    delete ui;
}
