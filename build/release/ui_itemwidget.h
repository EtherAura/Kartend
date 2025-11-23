/********************************************************************************
** Form generated from reading UI file 'itemwidget.ui'
**
** Created by: Qt User Interface Compiler version 6.9.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ITEMWIDGET_H
#define UI_ITEMWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ItemWidget
{
public:
    QWidget *triangleIndicator;
    QVBoxLayout *mainLayout;
    QLabel *imageLabel;
    QLabel *nameLabel;

    void setupUi(QWidget *ItemWidget)
    {
        if (ItemWidget->objectName().isEmpty())
            ItemWidget->setObjectName("ItemWidget");
        ItemWidget->resize(220, 245);
        ItemWidget->setAutoFillBackground(false);
        triangleIndicator = new QWidget(ItemWidget);
        triangleIndicator->setObjectName("triangleIndicator");
        triangleIndicator->setGeometry(QRect(188, 12, 12, 12));
        QSizePolicy sizePolicy(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(triangleIndicator->sizePolicy().hasHeightForWidth());
        triangleIndicator->setSizePolicy(sizePolicy);
        triangleIndicator->setMinimumSize(QSize(12, 12));
        triangleIndicator->setMaximumSize(QSize(12, 12));
        triangleIndicator->setVisible(false);
        mainLayout = new QVBoxLayout(ItemWidget);
        mainLayout->setSpacing(3);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(10, 10, 10, 10);
        imageLabel = new QLabel(ItemWidget);
        imageLabel->setObjectName("imageLabel");
        sizePolicy.setHeightForWidth(imageLabel->sizePolicy().hasHeightForWidth());
        imageLabel->setSizePolicy(sizePolicy);
        imageLabel->setMinimumSize(QSize(200, 200));
        imageLabel->setMaximumSize(QSize(200, 200));
        imageLabel->setAlignment(Qt::AlignCenter);
        imageLabel->setScaledContents(false);

        mainLayout->addWidget(imageLabel);

        nameLabel = new QLabel(ItemWidget);
        nameLabel->setObjectName("nameLabel");
        QSizePolicy sizePolicy1(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(nameLabel->sizePolicy().hasHeightForWidth());
        nameLabel->setSizePolicy(sizePolicy1);
        nameLabel->setMinimumSize(QSize(0, 25));
        nameLabel->setMaximumSize(QSize(200, 50));
        nameLabel->setAlignment(Qt::AlignTop|Qt::AlignHCenter);
        nameLabel->setWordWrap(true);
        nameLabel->setTextFormat(Qt::PlainText);
        nameLabel->setIndent(0);

        mainLayout->addWidget(nameLabel);


        retranslateUi(ItemWidget);

        QMetaObject::connectSlotsByName(ItemWidget);
    } // setupUi

    void retranslateUi(QWidget *ItemWidget)
    {
        imageLabel->setStyleSheet(QCoreApplication::translate("ItemWidget", "border: 1px solid palette(mid); background-color: palette(mid);", nullptr));
        nameLabel->setStyleSheet(QString());
        (void)ItemWidget;
    } // retranslateUi

};

namespace Ui {
    class ItemWidget: public Ui_ItemWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ITEMWIDGET_H
