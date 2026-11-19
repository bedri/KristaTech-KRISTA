// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSEXPORTCSV_H
#define SETTINGSEXPORTCSV_H

#include <QWidget>
#include "qt/kristatech/pwidget.h"
#include "transactionfilterproxy.h"
#include <QSortFilterProxyModel>

class QLabel;

namespace Ui {
class SettingsExportCSV;
}

class SettingsExportCSV : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsExportCSV(KRISTATECHGUI* _window, QWidget *parent = nullptr);
    ~SettingsExportCSV();

    void changeTheme(bool isLightTheme, QString& theme) override;

private Q_SLOTS:
    void selectFileOutput(const bool& isTxExport);
    void exportTxes(const QString& filename);
    void exportAddrs(const QString& filename);

private:
    void applyTheme(bool isLight);

    Ui::SettingsExportCSV *ui;
    QWidget* modernRoot{nullptr};
    QLabel* lblTxExportPath{nullptr};
    QLabel* lblAddrExportPath{nullptr};
    TransactionFilterProxy* txFilter{nullptr};
    QSortFilterProxyModel* addressFilter{nullptr};
};

#endif // SETTINGSEXPORTCSV_H
