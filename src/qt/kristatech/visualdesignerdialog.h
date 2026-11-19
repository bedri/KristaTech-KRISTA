// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VISUALDESIGNERDIALOG_H
#define VISUALDESIGNERDIALOG_H

#include <QDialog>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QList>
#include <QString>
#include <univalue.h>

class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

class DesignerNodeItem : public QGraphicsRectItem
{
public:
    DesignerNodeItem(int stepIdx,
                     const QString& roleName,
                     const QString& blockTitle,
                     const QString& categoryName,
                     const QString& detailText,
                     const UniValue& data,
                     bool lightTheme,
                     QGraphicsItem* parent = nullptr);

    void updateContent(int stepIdx, const QString& detailText, const UniValue& data, bool lightTheme);
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

    int stepIndex;
    QString role;
    QString title;
    QString category;
    QString details;
    UniValue nodeData;
    bool isLightTheme;
};

class BlueprintScene : public QGraphicsScene
{
public:
    explicit BlueprintScene(bool isLight, QObject* parent = nullptr);
    void setLightTheme(bool isLight);
    bool isLightTheme() const { return m_isLight; }

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    bool m_isLight;
};

struct DesignerLibraryItem {
    QWidget* card;
    QString role;
    QString title;
    QString description;
    QString category;
};

class VisualDesignerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VisualDesignerDialog(const UniValue& initialActions, QWidget* parent = nullptr);
    ~VisualDesignerDialog();

    UniValue getActions() const { return currentActions; }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private Q_SLOTS:
    void onSceneSelectionChanged();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onDuplicateClicked();
    void onRemoveNodeClicked();
    void onAutoLayoutClicked();
    void onClearCanvasClicked();
    void onLoadRecipeClicked();
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();
    void onZoomFit();
    void onApplyClicked();

private:
    QWidget* dialogRoot = nullptr;

    // Header controls
    QLabel* lblHeaderTitle = nullptr;
    QLabel* lblHeaderSubtitle = nullptr;
    QLabel* lblBlockCount = nullptr;
    QComboBox* comboRecipes = nullptr;
    QPushButton* btnLoadRecipe = nullptr;
    QPushButton* btnAutoLayout = nullptr;
    QPushButton* btnClearAll = nullptr;

    // Left: Block Library
    QLineEdit* editLibrarySearch = nullptr;
    QScrollArea* libraryScroll = nullptr;
    QWidget* libraryContainer = nullptr;
    QVBoxLayout* libraryLayout = nullptr;
    QList<DesignerLibraryItem> libraryEntries;

    // Center: Blueprint Canvas & Toolbar
    BlueprintScene* scene = nullptr;
    QGraphicsView* view = nullptr;
    QPushButton* btnMoveUp = nullptr;
    QPushButton* btnMoveDown = nullptr;
    QPushButton* btnDuplicate = nullptr;
    QPushButton* btnRemove = nullptr;
    QPushButton* btnZoomOut = nullptr;
    QLabel* lblZoomPct = nullptr;
    QPushButton* btnZoomIn = nullptr;
    QPushButton* btnZoomFit = nullptr;
    double currentZoom = 1.0;

    // Right: Block Inspector
    QLabel* lblInspectorTitle = nullptr;
    QLabel* lblInspectorSub = nullptr;
    QScrollArea* inspectorScroll = nullptr;
    QWidget* inspectorBody = nullptr;
    QVBoxLayout* inspectorLayout = nullptr;

    // Footer
    QLabel* lblValidationStatus = nullptr;
    QPushButton* btnCancel = nullptr;
    QPushButton* btnApply = nullptr;

    // State
    UniValue currentActions;
    QList<DesignerNodeItem*> nodeItems;
    QList<QGraphicsItem*> overlayItems;
    int selectedIndex = -1;
    bool m_rebuildingScene = false;
    bool m_isClosing = false;
    bool m_isLight = true;
    bool m_draggingNode = false;

    void setupLayout();
    void applyTheme(bool isLight);
    void populateBlockLibrary();
    void filterBlockLibrary(const QString& query);
    void addBlockByRole(const QString& role);
    void loadStarterRecipe(int recipeIdx);

    void rebuildScene(int selectIdx = -1);
    void relayoutFlowItems();
    void syncOrderFromScenePositions();
    void updateNodeDisplay(int idx);
    void updateToolbarButtons();

    void rebuildInspector();
    void updateValidationStatus();
    void setZoom(double factor);
};

#endif // VISUALDESIGNERDIALOG_H
