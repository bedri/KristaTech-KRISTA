// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SENDMULTIROW_H
#define SENDMULTIROW_H

#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QWidget>

#include "amount.h"
#include "bitcoinunits.h"
#include "qt/kristatech/pwidget.h"
#include "walletmodel.h"

class WalletModel;
class SendCoinsRecipient;

class SendMultiRow : public PWidget
{
    Q_OBJECT

public:
    explicit SendMultiRow(PWidget *parent = nullptr);
    ~SendMultiRow();

    void hideLabels();
    void showLabels();
    void setNumber(int number);
    int getNumber();

    void loadWalletModel() override;
    bool validate();
    SendCoinsRecipient getValue();
    QString getAddress();
    CAmount getAmountValue();

    /** Return whether the entry is still empty and unedited */
    bool isClear();
    CAmount getAmountValue(QString str);

    void setAddress(const QString& address);
    void setLabel(const QString& label);
    void setAmount(const QString& amount);
    void setAddressAndLabelOrDescription(const QString& address, const QString& message);
    void setSubtractFeeFromAmount(bool checked);
    bool isSubtractFeeFromAmount() const;
    void setRemoveVisible(bool visible);
    void setMultiMode(bool isMulti);
    void applyTheme(bool isLight);
    void setFocus();
    void focusAmount();

    QRect getEditLineRect();
    int getEditHeight();
    int getEditWidth();
    int getMenuBtnWidth();
    QPoint getAddressPopupPos(QWidget* targetParent) const;

public Q_SLOTS:
    void clear();
    void updateDisplayUnit();

Q_SIGNALS:
    void removeEntry(SendMultiRow* entry);
    void onContactsClicked(SendMultiRow* entry);
    void onSaveContactClicked(SendMultiRow* entry);
    void onMenuClicked(SendMultiRow* entry);
    void onMaxClicked(SendMultiRow* entry);
    void onPercentClicked(SendMultiRow* entry, int percent);
    void onValueChanged();
    void onUriParsed(SendCoinsRecipient rcp);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEvent *) override;
    void leaveEvent(QEvent *) override;

private Q_SLOTS:
    void amountChanged(const QString&);
    bool addressChanged(const QString&, bool fOnlyValidate = false);
    void deleteClicked();
    void onPasteClicked();

private:
    void updateHeaderTitle();
    void updateAddressBadge(const QString& trimmedStr, bool validAddress);
    void refreshInputStyles();

    QFrame *cardFrame = nullptr;
    QLabel *lblTitle = nullptr;
    QLabel *lblAddressStatus = nullptr;
    QLabel *lblAddressField = nullptr;
    QLabel *lblAmountField = nullptr;
    QLabel *lblDescField = nullptr;
    QLabel *lblSubtractHint = nullptr;

    QLineEdit *lineEditAddress = nullptr;
    QLineEdit *lineEditAmount = nullptr;
    QLineEdit *lineEditDescription = nullptr;

    QPushButton *btnPaste = nullptr;
    QPushButton *btnContactsBtn = nullptr;
    QPushButton *btnPct25 = nullptr;
    QPushButton *btnPct50 = nullptr;
    QPushButton *btnMax = nullptr;
    QPushButton *btnSaveContact = nullptr;
    QPushButton *btnRemoveEntry = nullptr;
    QCheckBox *checkSubtractFee = nullptr;

    QString lastAutoFilledLabel;

    int displayUnit = BitcoinUnits::PIV;
    int number = 1;
    bool multiMode = false;
    bool addrInvalid = false;
    bool amountInvalid = false;
    bool isExpanded = false;

    SendCoinsRecipient recipient;
};

#endif // SENDMULTIROW_H
