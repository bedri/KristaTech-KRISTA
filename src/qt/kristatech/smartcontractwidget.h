// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCONTRACTWIDGET_H
#define SMARTCONTRACTWIDGET_H

#include "qt/kristatech/pwidget.h"
#include "uint256.h"
#include "script/script.h"
#include <QWidget>
#include <map>
#include <univalue.h>

namespace Ui {
class SmartContractWidget;
}

// Forward declarations
class CWalletTx;
class QLabel;
class QShowEvent;
class QTreeWidgetItem;

// Holds info about a scannable contract UTXO
struct ContractUtxo {
    uint256 txid;
    int vout;
    CAmount amount;
    int depth;          // confirmations
    CScript scriptPubKey;
    UniValue decompiled; // result of CMescal::Decompile
};

class SmartContractWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SmartContractWidget(KRISTATECHGUI* parent);
    ~SmartContractWidget();

    void loadWalletModel() override;

protected:
    void showEvent(QShowEvent* event) override;

protected Q_SLOTS:
    void changeTheme(bool isLightTheme, QString& theme) override;

private Q_SLOTS:
    // Build tab
    void generateContract();
    void onSaveClicked();
    void onPublishClicked();
    void onTemplateSelected(int index);

    // Run tab
    void onRefreshContractsClicked();
    void onContractSelected(int index);
    void onRunContractClicked();

    // Custom Builder / Developer Mode slots
    void onAddActionClicked();
    void onEditAction(QTreeWidgetItem* item, int column);
    void onRemoveActionClicked();
    void onMoveActionUp();
    void onMoveActionDown();
    void onOpenVisualDesignerClicked();
    void onDeveloperModeToggled(bool checked);
    void onYAMLTextChanged();

private:
    enum class StatusTone { Neutral, Success, Error, Info, Warning };

    Ui::SmartContractWidget *ui;

    // Build tab state
    UniValue activeDoc;
    std::string activeName;
    StatusTone m_buildStatusTone{StatusTone::Neutral};
    StatusTone m_autoSignTone{StatusTone::Neutral};
    void applyStatusLabelTone(QLabel* label, StatusTone tone, bool isLight);
    void updatePreviews();
    void updatePreviewsForTheme(bool isLight);

    // Custom Builder state
    UniValue customActions;
    void updateCustomTree();
    void buildContractFromCustom();

    // Run tab state
    struct CachedDecompile {
        bool isValidContract{false};
        UniValue decompiled;
    };
    std::map<CScript, CachedDecompile> m_scriptCache;
    QList<ContractUtxo> m_utxos;
    bool m_contractsDirty{true};
    void populateRunDetails(const ContractUtxo& utxo);
    void clearRunDetails();
    void detectAutoSignCapabilities(const ContractUtxo& utxo);
};

#endif // SMARTCONTRACTWIDGET_H
