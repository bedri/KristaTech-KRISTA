// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SEND_H
#define SEND_H

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "bitcoinunits.h"
#include "coincontroldialog.h"
#include "qt/kristatech/contactsdropdown.h"
#include "qt/kristatech/pwidget.h"
#include "qt/kristatech/sendcustomfeedialog.h"
#include "qt/kristatech/sendmultirow.h"
#include "qt/kristatech/tooltipmenu.h"
#include "walletmodel.h"

static const int MAX_SEND_POPUP_ENTRIES = 8;

class KRISTATECHGUI;
class ClientModel;
class WalletModel;
class WalletModelTransaction;

class SendWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SendWidget(KRISTATECHGUI* parent);
    ~SendWidget();

    void addEntry();
    void setRecipientAddress(const QString& address, const QString& label = QString());

    void loadClientModel() override;
    void loadWalletModel() override;

Q_SIGNALS:
    /** Signal raised when a URI was entered or dragged to the GUI */
    void receivedURI(const QString& uri);

public Q_SLOTS:
    void onChangeAddressClicked();
    void onChangeCustomFeeClicked();
    void onCoinControlClicked();
    void onOpenUriClicked();
    void onContactsOptionClicked();
    void onPsbtClicked();
    void onValueChanged();
    void refreshAmounts();
    void changeTheme(bool isLightTheme, QString &theme) override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private Q_SLOTS:
    void onSendClicked();
    void onContactsClicked(SendMultiRow* entry);
    void onMenuClicked(SendMultiRow* entry);
    void onMaxClicked(SendMultiRow* entry);
    void onPercentClicked(SendMultiRow* entry, int percent);
    void onAddEntryClicked();
    void clearEntries();
    void clearAll(bool fClearSettings = true);
    void onContactMultiClicked();
    void onDeleteClicked();
    void onResetCustomOptions(bool fRefreshAmounts);
    void onResetSettings();

private:
    QWidget *leftColumn = nullptr;
    QFrame *rightColumn = nullptr;
    QLabel *lblTitle = nullptr;
    QLabel *lblSubtitle = nullptr;
    QLabel *lblHeaderBalance = nullptr;

    QScrollArea *scrollArea = nullptr;
    QWidget *scrollContents = nullptr;
    QVBoxLayout *recipientsLayout = nullptr;

    QPushButton *btnAddRecipient = nullptr;
    QPushButton *btnClearAll = nullptr;
    QPushButton *btnSend = nullptr;

    // Summary Card
    QFrame *summaryCard = nullptr;
    QLabel *lblSummaryTitle = nullptr;
    QLabel *lblRecipientCountKey = nullptr;
    QLabel *lblRecipientCountVal = nullptr;
    QLabel *lblTotalSendKey = nullptr;
    QLabel *lblAmountSend = nullptr;
    QLabel *lblEstFeeKey = nullptr;
    QLabel *lblEstFeeVal = nullptr;
    QFrame *summaryDivider = nullptr;
    QLabel *lblTitleRemaining = nullptr;
    QLabel *lblAmountRemaining = nullptr;
    QLabel *lblBalanceWarning = nullptr;

    QWidget *activeOptionsBox = nullptr;
    QPushButton *btnChipCoinControl = nullptr;
    QPushButton *btnChipChangeAddr = nullptr;
    QPushButton *btnChipCustomFee = nullptr;

    // Tools Card
    QFrame *toolsCard = nullptr;
    QLabel *lblToolsTitle = nullptr;
    QPushButton *btnToolFee = nullptr;
    QPushButton *btnToolCoinControl = nullptr;
    QPushButton *btnToolChangeAddr = nullptr;
    QPushButton *btnToolUri = nullptr;
    QPushButton *btnToolPsbt = nullptr;
    QPushButton *btnToolContacts = nullptr;
    QLabel *lblBadgeFee = nullptr;
    QLabel *lblBadgeCoinControl = nullptr;
    QLabel *lblBadgeChangeAddr = nullptr;
    QLabel *lblBadgeUri = nullptr;
    QLabel *lblBadgePsbt = nullptr;
    QLabel *lblBadgeContacts = nullptr;
    QPushButton *btnResetOptions = nullptr;

    SendCustomFeeDialog* customFeeDialog = nullptr;
    bool isCustomFeeSelected = false;
    CAmount currentCustomFeePerKb = DEFAULT_TRANSACTION_FEE;

    int nDisplayUnit = BitcoinUnits::PIV;
    QList<SendMultiRow*> entries;
    CoinControlDialog *coinControlDialog = nullptr;

    ContactsDropdown *menuContacts = nullptr;
    TooltipMenu *menu = nullptr;
    SendMultiRow* focusedEntry = nullptr;

    void applyTheme(bool isLight);
    void updateActiveOptionsUi();
    CAmount estimateCurrentFee(CAmount& feeRatePerKbOut) const;
    QPushButton* createToolRowButton(QWidget* parent,
                                     const QString& title,
                                     const QString& subtitle,
                                     const QString& defaultBadge,
                                     QLabel*& badgeLabelOut);
    void resizeMenu();
    QString recipientsToString(QList<SendCoinsRecipient> recipients);
    SendMultiRow* createEntry();
    bool send(QList<SendCoinsRecipient> recipients);
    void setFocusOnLastEntry();
    void updateEntryLabels(QList<SendCoinsRecipient> recipients);
    void setCustomFeeSelected(bool isSelected, const CAmount& customFee = DEFAULT_TRANSACTION_FEE);
    void setCoinControlPayAmounts();
};

#endif // SEND_H
