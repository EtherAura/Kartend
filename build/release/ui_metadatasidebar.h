/********************************************************************************
** Form generated from reading UI file 'metadatasidebar.ui'
**
** Created by: Qt User Interface Compiler version 6.9.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_METADATASIDEBAR_H
#define UI_METADATASIDEBAR_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_metadataSidebar
{
public:
    QVBoxLayout *mainLayout;
    QScrollArea *scrollArea;
    QWidget *contentWidget;
    QVBoxLayout *contentLayout;
    QLabel *titleLabel;
    QFrame *separator1;
    QLabel *artworkLabel;
    QLabel *artworkDisplay;
    QFrame *separator2;
    QLabel *itemNameLabel;
    QLabel *itemNameValue;
    QFrame *separator3;
    QLabel *fileInfoTitle;
    QLabel *filePathLabel;
    QLabel *filePathValue;
    QLabel *fileSizeLabel;
    QLabel *fileSizeValue;
    QLabel *lastModifiedLabel;
    QLabel *lastModifiedValue;
    QLabel *fileExtensionLabel;
    QLabel *fileExtensionValue;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *metadataSidebar)
    {
        if (metadataSidebar->objectName().isEmpty())
            metadataSidebar->setObjectName("metadataSidebar");
        metadataSidebar->resize(300, 600);
        mainLayout = new QVBoxLayout(metadataSidebar);
        mainLayout->setSpacing(0);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(0, 0, 0, 0);
        scrollArea = new QScrollArea(metadataSidebar);
        scrollArea->setObjectName("scrollArea");
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setFrameShape(QFrame::NoFrame);
        contentWidget = new QWidget();
        contentWidget->setObjectName("contentWidget");
        contentWidget->setGeometry(QRect(0, 0, 300, 560));
        contentLayout = new QVBoxLayout(contentWidget);
        contentLayout->setSpacing(12);
        contentLayout->setObjectName("contentLayout");
        contentLayout->setContentsMargins(10, 10, 10, 10);
        titleLabel = new QLabel(contentWidget);
        titleLabel->setObjectName("titleLabel");
        QFont font;
        font.setPointSize(11);
        font.setBold(true);
        titleLabel->setFont(font);

        contentLayout->addWidget(titleLabel);

        separator1 = new QFrame(contentWidget);
        separator1->setObjectName("separator1");
        separator1->setFrameShadow(QFrame::Sunken);
        separator1->setFrameShape(QFrame::Shape::HLine);

        contentLayout->addWidget(separator1);

        artworkLabel = new QLabel(contentWidget);
        artworkLabel->setObjectName("artworkLabel");
        artworkLabel->setFont(font);

        contentLayout->addWidget(artworkLabel);

        artworkDisplay = new QLabel(contentWidget);
        artworkDisplay->setObjectName("artworkDisplay");
        artworkDisplay->setMinimumSize(QSize(200, 200));
        artworkDisplay->setMaximumSize(QSize(200, 200));
        artworkDisplay->setAlignment(Qt::AlignCenter);

        contentLayout->addWidget(artworkDisplay);

        separator2 = new QFrame(contentWidget);
        separator2->setObjectName("separator2");
        separator2->setFrameShadow(QFrame::Sunken);
        separator2->setFrameShape(QFrame::Shape::HLine);

        contentLayout->addWidget(separator2);

        itemNameLabel = new QLabel(contentWidget);
        itemNameLabel->setObjectName("itemNameLabel");
        QFont font1;
        font1.setBold(true);
        itemNameLabel->setFont(font1);

        contentLayout->addWidget(itemNameLabel);

        itemNameValue = new QLabel(contentWidget);
        itemNameValue->setObjectName("itemNameValue");
        itemNameValue->setTextInteractionFlags(Qt::TextSelectableByMouse);

        contentLayout->addWidget(itemNameValue);

        separator3 = new QFrame(contentWidget);
        separator3->setObjectName("separator3");
        separator3->setFrameShadow(QFrame::Sunken);
        separator3->setFrameShape(QFrame::Shape::HLine);

        contentLayout->addWidget(separator3);

        fileInfoTitle = new QLabel(contentWidget);
        fileInfoTitle->setObjectName("fileInfoTitle");
        fileInfoTitle->setFont(font);

        contentLayout->addWidget(fileInfoTitle);

        filePathLabel = new QLabel(contentWidget);
        filePathLabel->setObjectName("filePathLabel");
        filePathLabel->setFont(font1);

        contentLayout->addWidget(filePathLabel);

        filePathValue = new QLabel(contentWidget);
        filePathValue->setObjectName("filePathValue");
        filePathValue->setWordWrap(true);
        filePathValue->setTextInteractionFlags(Qt::TextSelectableByMouse);

        contentLayout->addWidget(filePathValue);

        fileSizeLabel = new QLabel(contentWidget);
        fileSizeLabel->setObjectName("fileSizeLabel");
        fileSizeLabel->setFont(font1);

        contentLayout->addWidget(fileSizeLabel);

        fileSizeValue = new QLabel(contentWidget);
        fileSizeValue->setObjectName("fileSizeValue");
        fileSizeValue->setTextInteractionFlags(Qt::TextSelectableByMouse);

        contentLayout->addWidget(fileSizeValue);

        lastModifiedLabel = new QLabel(contentWidget);
        lastModifiedLabel->setObjectName("lastModifiedLabel");
        lastModifiedLabel->setFont(font1);

        contentLayout->addWidget(lastModifiedLabel);

        lastModifiedValue = new QLabel(contentWidget);
        lastModifiedValue->setObjectName("lastModifiedValue");
        lastModifiedValue->setTextInteractionFlags(Qt::TextSelectableByMouse);

        contentLayout->addWidget(lastModifiedValue);

        fileExtensionLabel = new QLabel(contentWidget);
        fileExtensionLabel->setObjectName("fileExtensionLabel");
        fileExtensionLabel->setFont(font1);

        contentLayout->addWidget(fileExtensionLabel);

        fileExtensionValue = new QLabel(contentWidget);
        fileExtensionValue->setObjectName("fileExtensionValue");
        fileExtensionValue->setTextInteractionFlags(Qt::TextSelectableByMouse);

        contentLayout->addWidget(fileExtensionValue);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        contentLayout->addItem(verticalSpacer);

        scrollArea->setWidget(contentWidget);

        mainLayout->addWidget(scrollArea);


        retranslateUi(metadataSidebar);

        QMetaObject::connectSlotsByName(metadataSidebar);
    } // setupUi

    void retranslateUi(QWidget *metadataSidebar)
    {
        metadataSidebar->setWindowTitle(QCoreApplication::translate("metadataSidebar", "Form", nullptr));
        titleLabel->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(highlight); padding: 4px 0px;", nullptr));
        titleLabel->setText(QCoreApplication::translate("metadataSidebar", "Item Information", nullptr));
        separator1->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(mid);", nullptr));
        artworkLabel->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(highlight); padding: 4px 0px;", nullptr));
        artworkLabel->setText(QCoreApplication::translate("metadataSidebar", "Artwork", nullptr));
        artworkDisplay->setStyleSheet(QCoreApplication::translate("metadataSidebar", "border: 1px solid palette(mid); background-color: palette(base);", nullptr));
        artworkDisplay->setText(QString());
        separator2->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(mid);", nullptr));
        itemNameLabel->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px;", nullptr));
        itemNameLabel->setText(QCoreApplication::translate("metadataSidebar", "Name:", nullptr));
        itemNameValue->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px 8px 12px;", nullptr));
        itemNameValue->setText(QCoreApplication::translate("metadataSidebar", "No item selected", nullptr));
        separator3->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(mid);", nullptr));
        fileInfoTitle->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(highlight); padding: 4px 0px;", nullptr));
        fileInfoTitle->setText(QCoreApplication::translate("metadataSidebar", "File Information", nullptr));
        filePathLabel->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px;", nullptr));
        filePathLabel->setText(QCoreApplication::translate("metadataSidebar", "Path:", nullptr));
        filePathValue->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px 8px 12px;", nullptr));
        filePathValue->setText(QCoreApplication::translate("metadataSidebar", "-", nullptr));
        fileSizeLabel->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px;", nullptr));
        fileSizeLabel->setText(QCoreApplication::translate("metadataSidebar", "Size:", nullptr));
        fileSizeValue->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px 8px 12px;", nullptr));
        fileSizeValue->setText(QCoreApplication::translate("metadataSidebar", "-", nullptr));
        lastModifiedLabel->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px;", nullptr));
        lastModifiedLabel->setText(QCoreApplication::translate("metadataSidebar", "Modified:", nullptr));
        lastModifiedValue->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px 8px 12px;", nullptr));
        lastModifiedValue->setText(QCoreApplication::translate("metadataSidebar", "-", nullptr));
        fileExtensionLabel->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px;", nullptr));
        fileExtensionLabel->setText(QCoreApplication::translate("metadataSidebar", "Type:", nullptr));
        fileExtensionValue->setStyleSheet(QCoreApplication::translate("metadataSidebar", "color: palette(windowtext); padding: 2px 0px 8px 12px;", nullptr));
        fileExtensionValue->setText(QCoreApplication::translate("metadataSidebar", "-", nullptr));
    } // retranslateUi

};

namespace Ui {
    class metadataSidebar: public Ui_metadataSidebar {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_METADATASIDEBAR_H
