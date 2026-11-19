// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RECEIVEWIDGET_H
#define RECEIVEWIDGET_H

#include "addresstablemodel.h"
#include "qt/kristatech/addressfilterproxymodel.h"
#include "qt/kristatech/pwidget.h"

#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

class KRISTATECHGUI;
class SendCoinsRecipient;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class ReceiveWidget : public PWidget
{
    Q_OBJECT

public:
    explicit ReceiveWidget(KRISTATECHGUI* parent);
    ~ReceiveWidget();

    void loadWalletModel() override;

public Q_SLOTS:
    void onRequestClicked();
    void onMyAddressesClicked();
    void onNewAddressClicked();

protected:
    void showEvent(QShowEvent* event) override;

private Q_SLOTS:
    void changeTheme(bool isLightTheme, QString &theme) override;
    void onLabelClicked();
    void onCopyClicked();
    void onCopyUriClicked();
    void onSaveQrClicked();
    void onSaveInlineLabelClicked();
    void onClearRequestClicked();
    void refreshView(const QModelIndex& tl, const QModelIndex& br);
    void refreshView(QString refreshAddress = QString());
    void handleAddressClicked(const QModelIndex &index);
    void onSortChanged(int idx);
    void onSortOrderChanged(int idx);

private:
    // Header
    QLabel *lblTitle = nullptr;
    QLabel *lblSubtitle = nullptr;

    // Left Column: QR & Payment Request Cards
    QFrame *qrCard = nullptr;
    QLabel *lblActiveLabel = nullptr;
    QLabel *lblUsageBadge = nullptr;
    QLabel *lblDate = nullptr;
    QLabel *lblQrImg = nullptr;
    QLabel *lblAddress = nullptr;
    QLabel *lblLiveUriHint = nullptr;

    QPushButton *btnCopyAddress = nullptr;
    QPushButton *btnCopyUri = nullptr;
    QPushButton *btnSaveQr = nullptr;
    QPushButton *btnNewAddress = nullptr;

    QFrame *requestCard = nullptr;
    QLabel *lblReqTitle = nullptr;
    QLabel *lblReqLabelKey = nullptr;
    QLabel *lblReqAmountKey = nullptr;
    QLabel *lblReqMessageKey = nullptr;
    QLineEdit *editLabel = nullptr;
    QLineEdit *editAmount = nullptr;
    QLineEdit *editMessage = nullptr;
    QPushButton *btnSaveLabel = nullptr;
    QPushButton *btnClearRequest = nullptr;

    // Right Column: Searchable My Receive Addresses Card
    QFrame *listCard = nullptr;
    QLabel *lblListTitle = nullptr;
    QLabel *lblAddrCount = nullptr;
    QPushButton *btnQuickNew = nullptr;
    QPushButton *btnFilterAll = nullptr;
    QPushButton *btnFilterUsed = nullptr;
    QPushButton *btnFilterUnused = nullptr;
    QLineEdit *lineEditSearch = nullptr;
    QComboBox *comboBoxSort = nullptr;
    QComboBox *comboBoxSortOrder = nullptr;
    QTableWidget *tableAddresses = nullptr;
    QPushButton *btnShowAllRows = nullptr;
    QLabel *lblEmptyList = nullptr;

    AddressTableModel* addressTableModel = nullptr;
    AddressFilterProxyModel *filter = nullptr;

    SendCoinsRecipient *info = nullptr;
    QPixmap qrImage;
    QString currentAddress;
    QSet<QString> usedAddresses;
    QTimer *rebuildTimer = nullptr;

    AddressTableModel::ColumnIndex sortType = AddressTableModel::Date;
    Qt::SortOrder sortOrder = Qt::DescendingOrder;
    int usageFilterMode = 0; // 0 = All, 1 = Used, 2 = Unused
    int maxDisplayedRows = 250;

    bool isShowingDialog = false;
    bool updatingRequestFields = false;
    bool tableDirty = false;
    bool usedSetDirty = true;

    void applyTheme(bool isLight);
    void refreshUsedAddressSet();
    void scheduleTableRebuild(bool markUsedDirty = false);
    void flushPendingRefresh();
    void setUsageFilter(int mode);
    void updateQr(QString address);
    void updateQrFromFields();
    void updateLabel();
    void updateActiveAddressMeta();
    void updateAmountPlaceholder();
    void rebuildAddressTable();
    void editLabelForAddress(const QString& address);
    void showAddressGenerationDialog();
    void sortAddresses();
};

#endif // RECEIVEWIDGET_H
