// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/defaultdialog.h"

#include "guiconstants.h"
#include "qrencode.h"
#include "qt/kristatech/snackbar.h"

#include <QAction>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QIconEngine>
#include <QImage>
#include <QListView>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QInputDialog>
#include <algorithm>

Qt::Modifier SHORT_KEY
#ifdef Q_OS_MAC
    = Qt::CTRL;
#else
    = Qt::ALT;
#endif

// Open dialog at the bottom
bool openDialog(QDialog* widget, QWidget* gui)
{
    widget->setWindowFlags(Qt::CustomizeWindowHint);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);
    QPropertyAnimation* animation = new QPropertyAnimation(widget, "pos");
    animation->setDuration(300);
    animation->setStartValue(QPoint(0, gui->height()));
    animation->setEndValue(QPoint(0, gui->height() - widget->height()));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    widget->activateWindow();
    widget->raise();
    return widget->exec();
}

void closeDialog(QDialog* widget, KRISTATECHGUI* gui)
{
    widget->setWindowFlags(Qt::CustomizeWindowHint);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);
    QPropertyAnimation* animation = new QPropertyAnimation(widget, "pos");
    animation->setDuration(300);
    animation->setStartValue(widget->pos());
    animation->setEndValue(QPoint(0, gui->height() + 100));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void openDialogFullScreen(QWidget* parent, QWidget* dialog)
{
    dialog->setWindowFlags(Qt::CustomizeWindowHint);
    dialog->move(0, 0);
    dialog->show();
    dialog->activateWindow();
    dialog->resize(parent->width(), parent->height());
}

bool openDialogWithOpaqueBackgroundY(QDialog* widget, KRISTATECHGUI* gui, double posX, int posY)
{
    Q_UNUSED(posX);
    Q_UNUSED(posY);
    if (gui) {
        gui->showHide(true);
        if (widget->parentWidget() != gui) {
            widget->setParent(gui);
        }
    }
    widget->setWindowFlags(Qt::CustomizeWindowHint);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);

    if (gui) {
        const int maxW = std::max(320, gui->width() - 32);
        const int maxH = std::max(240, gui->height() - 32);
        if (widget->width() > maxW || widget->height() > maxH) {
            widget->resize(std::min(widget->width(), maxW), std::min(widget->height(), maxH));
        }
    }

    const int xPos = gui ? std::max(16, (gui->width() - widget->width()) / 2) : 0;
    const int yPos = gui ? std::max(16, (gui->height() - widget->height()) / 2) : 0;
    QPropertyAnimation* animation = new QPropertyAnimation(widget, "pos");
    animation->setDuration(240);
    animation->setStartValue(QPoint(xPos, gui ? std::min(gui->height(), yPos + 80) : yPos));
    animation->setEndValue(QPoint(xPos, yPos));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    widget->raise();
    widget->activateWindow();
    bool res = widget->exec();
    if (gui) gui->showHide(false);
    return res;
}

bool openDialogWithOpaqueBackground(QDialog* widget, KRISTATECHGUI* gui, double posX)
{
    return openDialogWithOpaqueBackgroundY(widget, gui, posX, 5);
}

bool showThemedConfirm(QWidget* parent, const QString& title, const QString& message, const QString& okText, const QString& cancelText, bool isDanger)
{
    const QString okLabel = okText.trimmed().isEmpty() ? QObject::tr("OK") : okText;
    const QString cancelLabel = cancelText;
    KRISTATECHGUI* gui = qobject_cast<KRISTATECHGUI*>(parent ? parent->window() : nullptr);
    if (gui && gui->isVisible()) {
        DefaultDialog* dialog = new DefaultDialog(gui);
        dialog->setText(title, message, okLabel, cancelLabel);
        if (isDanger) dialog->setDanger(true);
        bool res = openDialogWithOpaqueBackgroundY(dialog, gui, 3, 5);
        dialog->deleteLater();
        return res;
    }

    DefaultDialog dialog(parent);
    dialog.setText(title, message, okLabel, cancelLabel);
    if (isDanger) dialog.setDanger(true);
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground, true);
    dialog.exec();
    return dialog.isOk;
}

void showThemedNotice(QWidget* parent, const QString& title, const QString& message, bool isDanger, const QString& okText)
{
    showThemedConfirm(parent, title, message, okText, QString(), isDanger);
}

bool openDialogWithOpaqueBackgroundFullScreen(QDialog* widget, KRISTATECHGUI* gui)
{
    if (gui) {
        gui->showHide(true);
        if (widget->parentWidget() != gui) {
            widget->setParent(gui);
        }
    }
    widget->setWindowFlags(Qt::CustomizeWindowHint);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);

    if (gui) {
        widget->resize(gui->width(), gui->height());
    }

    QPropertyAnimation* animation = new QPropertyAnimation(widget, "pos");
    animation->setDuration(240);
    int xPos = 0;
    animation->setStartValue(QPoint(xPos, gui ? gui->height() : 0));
    animation->setEndValue(QPoint(xPos, 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    widget->raise();
    widget->activateWindow();
    bool res = widget->exec();
    if (gui) gui->showHide(false);
    return res;
}

QPixmap encodeToQr(QString str, QString& errorStr, QColor qrColor)
{
    if (!str.isEmpty()) {
        // limit URI length
        if (str.length() > MAX_URI_LENGTH) {
            errorStr = "Resulting URI too long, try to reduce the text for label / message.";
            return QPixmap();
        } else {
            QRcode* code = QRcode_encodeString(str.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
            if (!code) {
                errorStr = "Error encoding URI into QR Code.";
                return QPixmap();
            }
            QImage myImage = QImage(code->width + 8, code->width + 8, QImage::Format_RGB32);
            myImage.fill(0xffffff);
            unsigned char* p = code->data;
            for (int y = 0; y < code->width; y++) {
                for (int x = 0; x < code->width; x++) {
                    myImage.setPixel(x + 4, y + 4, ((*p & 1) ? qrColor.rgb() : 0xffffff));
                    p++;
                }
            }
            QRcode_free(code);
            return QPixmap::fromImage(myImage);
        }
    }
    return QPixmap();
}

void setFilterAddressBook(QComboBox* filter)
{
    initComboBox(filter);
    filter->addItem(QObject::tr("All"), "");
    filter->addItem(QObject::tr("Receiving"), AddressTableModel::Receive);
    filter->addItem(QObject::tr("Contacts"), AddressTableModel::Send);
}

void setSortTx(QComboBox* filter)
{
    // Sort Transactions
    initComboBox(filter);
    filter->addItem(QObject::tr("Date desc"), SortTx::DATE_DESC);
    filter->addItem(QObject::tr("Date asc"), SortTx::DATE_ASC);
    filter->addItem(QObject::tr("Amount desc"), SortTx::AMOUNT_DESC);
    filter->addItem(QObject::tr("Amount asc"), SortTx::AMOUNT_ASC);
}

void setSortTxTypeFilter(QComboBox* filter)
{
    initComboBox(filter);
    filter->addItem(QObject::tr("All"), TransactionFilterProxy::ALL_TYPES);
    filter->addItem(QObject::tr("Received"), TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) | TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther));
    filter->addItem(QObject::tr("Sent"), TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) | TransactionFilterProxy::TYPE(TransactionRecord::SendToOther));
    filter->addItem(QObject::tr("Mined"), TransactionFilterProxy::TYPE(TransactionRecord::Generated) | TransactionFilterProxy::TYPE(TransactionRecord::AdamReward));
    filter->addItem(QObject::tr("Minted"), TransactionFilterProxy::TYPE(TransactionRecord::StakeMint));
    filter->addItem(QObject::tr("MN reward"), TransactionFilterProxy::TYPE(TransactionRecord::MNReward) | TransactionFilterProxy::TYPE(TransactionRecord::LlmqReward));
    filter->addItem(QObject::tr("To yourself"), TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf));
    filter->addItem(QObject::tr("Mining Registration"), TransactionFilterProxy::TYPE(TransactionRecord::MinerRegistration));
}

void setupSettings(QSettings* settings)
{
    if (!settings->contains("lightTheme")) {
        setTheme(false);
    }
}

QSettings* settings = nullptr;

QSettings* getSettings()
{
    if (!settings) {
        settings = new QSettings();
        // Setup initial values if them are not there
        setupSettings(settings);
    }

    return settings;
}

bool isLightTheme()
{
    return getSettings()->value("lightTheme", true).toBool();
}

void setTheme(bool isLight)
{
    QSettings* settings = getSettings();
    settings->setValue("theme", isLight ? "default" : "default-dark");
    settings->setValue("lightTheme", isLight);
}


// Style

void updateStyle(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}


QColor getRowColor(bool isLightTheme, bool isHovered, bool isSelected)
{
    if (isLightTheme) {
        if (isSelected) {
            return QColor("#f9f9f9");
        } else if (isHovered) {
            return QColor("#f9f9f9");
        } else {
            return QColor("#ffffff");
        }
    } else {
        if (isSelected) {
            return QColor(118, 149, 205, 80); // Logo Slate-Blue (#7695CD) selected highlight
        } else if (isHovered) {
            return QColor(118, 149, 205, 40); // Logo Slate-Blue (#7695CD) hover highlight
        } else {
            return QColor("#121b2d"); // Dark night-blue row background
        }
    }
}

void initComboBox(QComboBox* combo, QString cssClass)
{
    setCssProperty(combo, cssClass);
    combo->setView(new QListView());
    combo->setMaxVisibleItems(10);
}

QString getCustomItem(QWidget* parent, const QString& title, const QString& label, const QStringList& items, int current, bool editable, bool* ok)
{
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setComboBoxItems(items);
    dialog.setComboBoxEditable(editable);
    dialog.setProperty("cssClass", "container-dialog");

    QComboBox* combo = dialog.findChild<QComboBox*>();
    if (combo) {
        initComboBox(combo, "btn-combo-dialog");
        combo->setCurrentIndex(current);
    }

    if (parent) {
        dialog.setStyleSheet(parent->styleSheet());
    }

    bool res = dialog.exec();
    if (ok) {
        *ok = res;
    }
    return res ? dialog.textValue() : QString();
}

void fillAddressSortControls(QComboBox* boxType, QComboBox* boxOrder)
{
    // Sort Type
    initComboBox(boxType, "btn-combo-small");
    boxType->addItem(QObject::tr("by Label"), AddressTableModel::Label);
    boxType->addItem(QObject::tr("by Address"), AddressTableModel::Address);
    boxType->addItem(QObject::tr("by Date"), AddressTableModel::Date);
    boxType->setCurrentIndex(0);
    // Sort Order
    initComboBox(boxOrder, "btn-combo-small");
    boxOrder->addItem("asc", Qt::AscendingOrder);
    boxOrder->addItem("desc", Qt::DescendingOrder);
    boxOrder->setCurrentIndex(0);
}

void initCssEditLine(QLineEdit* edit, bool isDialog)
{
    if (isDialog)
        setCssEditLineDialog(edit, true, false);
    else
        setCssEditLine(edit, true, false);
    setShadow(edit);
    edit->setAttribute(Qt::WA_MacShowFocusRect, 0);
}

void setCssEditLine(QLineEdit* edit, bool isValid, bool forceUpdate)
{
    setCssProperty(edit, isValid ? "edit-primary" : "edit-primary-error", forceUpdate);
}

void setCssEditLineDialog(QLineEdit* edit, bool isValid, bool forceUpdate)
{
    setCssProperty(edit, isValid ? "edit-primary-dialog" : "edit-primary-dialog-error", forceUpdate);
}

void setShadow(QWidget* edit)
{
    QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect();
    shadowEffect->setColor(QColor(0, 0, 0, 22));
    shadowEffect->setXOffset(0);
    shadowEffect->setYOffset(3);
    shadowEffect->setBlurRadius(6);
    edit->setGraphicsEffect(shadowEffect);
}

void setCssBtnPrimary(QPushButton* btn, bool forceUpdate)
{
    setCssProperty(btn, "btn-primary", forceUpdate);
}

void setCssBtnSecondary(QPushButton* btn, bool forceUpdate)
{
    setCssProperty(btn, "btn-secundary", forceUpdate);
}

void setCssTextBodyDialog(std::initializer_list<QWidget*> args)
{
    for (QWidget* w : args) {
        setCssTextBodyDialog(w);
    }
}

void setCssTextBodyDialog(QWidget* widget)
{
    setCssProperty(widget, "text-body1-dialog", false);
}

void setCssTitleScreen(QLabel* label)
{
    setCssProperty(label, "text-title-screen", false);
}

void setCssSubtitleScreen(QWidget* wid)
{
    setCssProperty(wid, "text-subtitle", false);
}

void setCssProperty(std::initializer_list<QWidget*> args, QString value)
{
    for (QWidget* w : args) {
        setCssProperty(w, value);
    }
}

void setCssProperty(QWidget* wid, QString value, bool forceUpdate)
{
    wid->setProperty("cssClass", value);
    forceUpdateStyle(wid, forceUpdate);
}

void forceUpdateStyle(QWidget* widget, bool forceUpdate)
{
    if (forceUpdate)
        updateStyle(widget);
}

void forceUpdateStyle(std::initializer_list<QWidget*> args)
{
    for (QWidget* w : args) {
        forceUpdateStyle(w, true);
    }
}

namespace {
class ClearButtonIconEngine : public QIconEngine
{
public:
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        if (!p || rect.isEmpty()) return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        const QRectF r(rect);
        const qreal side = std::min(r.width(), r.height());
        const QPointF center = r.center();
        const qreal radius = side * 0.41;

        const bool light = isLightTheme();
        QColor circleFill;
        if (mode == QIcon::Active || mode == QIcon::Selected) {
            circleFill = QColor(light ? "#5576B2" : "#6C8EC9");
        } else if (mode == QIcon::Disabled) {
            circleFill = QColor(light ? "#CBD5E1" : "#334155");
        } else {
            circleFill = QColor(light ? "#94A3B8" : "#475569");
        }

        p->setPen(Qt::NoPen);
        p->setBrush(circleFill);
        p->drawEllipse(center, radius, radius);

        const qreal arm = radius * 0.42;
        QPen crossPen(QColor(QStringLiteral("#FFFFFF")),
                      std::max(1.5, side * 0.105),
                      Qt::SolidLine,
                      Qt::RoundCap,
                      Qt::RoundJoin);
        p->setPen(crossPen);
        p->drawLine(QPointF(center.x() - arm, center.y() - arm), QPointF(center.x() + arm, center.y() + arm));
        p->drawLine(QPointF(center.x() + arm, center.y() - arm), QPointF(center.x() - arm, center.y() + arm));
        p->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return renderScaled(size, mode, state, 1.0);
    }

    void virtual_hook(int id, void* data) override
    {
        if (id == QIconEngine::ScaledPixmapHook && data) {
            auto* arg = static_cast<ScaledPixmapArgument*>(data);
            const qreal dpr = (arg->scale > 0.0) ? arg->scale : 1.0;
            arg->pixmap = renderScaled(arg->size, arg->mode, arg->state, dpr);
            return;
        }
        QIconEngine::virtual_hook(id, data);
    }

    QIconEngine* clone() const override
    {
        return new ClearButtonIconEngine();
    }

private:
    QPixmap renderScaled(const QSize& logicalSize, QIcon::Mode mode, QIcon::State state, qreal dpr)
    {
        const QSize sz = logicalSize.isValid() && !logicalSize.isEmpty() ? logicalSize : QSize(16, 16);
        QImage img(std::max(1, qRound(sz.width() * dpr)),
                   std::max(1, qRound(sz.height() * dpr)),
                   QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(dpr);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        paint(&painter, QRect(QPoint(0, 0), sz), mode, state);
        painter.end();
        return QPixmap::fromImage(img);
    }
};
} // namespace

QIcon makeVectorClearIcon()
{
    return QIcon(new ClearButtonIconEngine());
}

void styleLineEditClearButton(QLineEdit* edit)
{
    if (!edit) return;
    const QIcon clearIcon = makeVectorClearIcon();
    for (QAction* act : edit->findChildren<QAction*>()) {
        if (act && act->objectName().contains(QStringLiteral("clear"), Qt::CaseInsensitive)) {
            act->setIcon(clearIcon);
        }
    }
    for (QToolButton* btn : edit->findChildren<QToolButton*>()) {
        if (btn) {
            btn->setIcon(clearIcon);
            btn->setCursor(Qt::PointingHandCursor);
            if (btn->toolTip().isEmpty()) {
                btn->setToolTip(QObject::tr("Clear input"));
            }
            btn->update();
        }
    }
}
