/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.9.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "metadatasidebar.h"

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionExit;
    QAction *actionShowSidebar;
    QAction *actionSettings;
    QAction *actionAbout;
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    QStackedWidget *stackedWidget;
    QWidget *itemsPage;
    QVBoxLayout *itemsPageLayout;
    QWidget *itemsTopBar;
    QHBoxLayout *itemsTopBarLayout;
    QLabel *itemsTitleLabel;
    QSpacerItem *itemsTitleSpacer;
    QPushButton *searchModeButton;
    QLineEdit *searchBar;
    QWidget *m_mainContentWidget;
    QHBoxLayout *m_mainHorizontalLayout;
    QScrollArea *itemScrollArea;
    QWidget *itemScrollAreaWidgetContents;
    QVBoxLayout *itemScrollLayout;
    QWidget *gridContainer;
    QGridLayout *itemGrid;
    QLabel *loadingLabel;
    metadataSidebar *metadataSidebarWidget;
    QMenuBar *menubar;
    QMenu *menuFile;
    QMenu *menuView;
    QMenu *menuHelp;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(1920, 1080);
        actionExit = new QAction(MainWindow);
        actionExit->setObjectName("actionExit");
        actionShowSidebar = new QAction(MainWindow);
        actionShowSidebar->setObjectName("actionShowSidebar");
        actionShowSidebar->setCheckable(true);
        actionSettings = new QAction(MainWindow);
        actionSettings->setObjectName("actionSettings");
        actionAbout = new QAction(MainWindow);
        actionAbout->setObjectName("actionAbout");
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setSpacing(0);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        stackedWidget = new QStackedWidget(centralwidget);
        stackedWidget->setObjectName("stackedWidget");
        itemsPage = new QWidget();
        itemsPage->setObjectName("itemsPage");
        itemsPageLayout = new QVBoxLayout(itemsPage);
        itemsPageLayout->setSpacing(0);
        itemsPageLayout->setObjectName("itemsPageLayout");
        itemsPageLayout->setContentsMargins(0, 0, 0, 0);
        itemsTopBar = new QWidget(itemsPage);
        itemsTopBar->setObjectName("itemsTopBar");
        itemsTopBar->setMaximumSize(QSize(16777215, 50));
        itemsTopBarLayout = new QHBoxLayout(itemsTopBar);
        itemsTopBarLayout->setSpacing(10);
        itemsTopBarLayout->setObjectName("itemsTopBarLayout");
        itemsTopBarLayout->setContentsMargins(15, 10, 15, 10);
        itemsTitleLabel = new QLabel(itemsTopBar);
        itemsTitleLabel->setObjectName("itemsTitleLabel");
        QFont font;
        font.setPointSize(14);
        font.setBold(true);
        itemsTitleLabel->setFont(font);

        itemsTopBarLayout->addWidget(itemsTitleLabel);

        itemsTitleSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        itemsTopBarLayout->addItem(itemsTitleSpacer);

        searchModeButton = new QPushButton(itemsTopBar);
        searchModeButton->setObjectName("searchModeButton");
        searchModeButton->setMaximumSize(QSize(30, 30));
        searchModeButton->setFlat(true);
        searchModeButton->setFocusPolicy(Qt::NoFocus);

        itemsTopBarLayout->addWidget(searchModeButton);

        searchBar = new QLineEdit(itemsTopBar);
        searchBar->setObjectName("searchBar");
        searchBar->setMaximumSize(QSize(250, 16777215));

        itemsTopBarLayout->addWidget(searchBar);


        itemsPageLayout->addWidget(itemsTopBar);

        m_mainContentWidget = new QWidget(itemsPage);
        m_mainContentWidget->setObjectName("m_mainContentWidget");
        m_mainHorizontalLayout = new QHBoxLayout(m_mainContentWidget);
        m_mainHorizontalLayout->setSpacing(0);
        m_mainHorizontalLayout->setObjectName("m_mainHorizontalLayout");
        m_mainHorizontalLayout->setContentsMargins(0, 0, 0, 0);
        itemScrollArea = new QScrollArea(m_mainContentWidget);
        itemScrollArea->setObjectName("itemScrollArea");
        itemScrollArea->setWidgetResizable(true);
        itemScrollArea->setAlignment(Qt::AlignLeading|Qt::AlignLeft|Qt::AlignTop);
        itemScrollAreaWidgetContents = new QWidget();
        itemScrollAreaWidgetContents->setObjectName("itemScrollAreaWidgetContents");
        itemScrollAreaWidgetContents->setGeometry(QRect(0, 0, 1200, 730));
        itemScrollLayout = new QVBoxLayout(itemScrollAreaWidgetContents);
        itemScrollLayout->setSpacing(0);
        itemScrollLayout->setObjectName("itemScrollLayout");
        itemScrollLayout->setContentsMargins(0, 0, 0, 0);
        gridContainer = new QWidget(itemScrollAreaWidgetContents);
        gridContainer->setObjectName("gridContainer");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(gridContainer->sizePolicy().hasHeightForWidth());
        gridContainer->setSizePolicy(sizePolicy);
        itemGrid = new QGridLayout(gridContainer);
        itemGrid->setSpacing(0);
        itemGrid->setObjectName("itemGrid");
        itemGrid->setContentsMargins(0, 0, 0, 0);

        itemScrollLayout->addWidget(gridContainer);

        loadingLabel = new QLabel(itemScrollAreaWidgetContents);
        loadingLabel->setObjectName("loadingLabel");
        loadingLabel->setAlignment(Qt::AlignCenter);
        loadingLabel->setVisible(false);

        itemScrollLayout->addWidget(loadingLabel);

        itemScrollArea->setWidget(itemScrollAreaWidgetContents);

        m_mainHorizontalLayout->addWidget(itemScrollArea);

        metadataSidebarWidget = new metadataSidebar(m_mainContentWidget);
        metadataSidebarWidget->setObjectName("metadataSidebarWidget");
        QSizePolicy sizePolicy1(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Expanding);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(metadataSidebarWidget->sizePolicy().hasHeightForWidth());
        metadataSidebarWidget->setSizePolicy(sizePolicy1);
        metadataSidebarWidget->setMinimumSize(QSize(300, 0));
        metadataSidebarWidget->setMaximumSize(QSize(300, 16777215));

        m_mainHorizontalLayout->addWidget(metadataSidebarWidget);


        itemsPageLayout->addWidget(m_mainContentWidget);

        stackedWidget->addWidget(itemsPage);

        verticalLayout->addWidget(stackedWidget);

        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 1200, 22));
        menuFile = new QMenu(menubar);
        menuFile->setObjectName("menuFile");
        menuView = new QMenu(menubar);
        menuView->setObjectName("menuView");
        menuHelp = new QMenu(menubar);
        menuHelp->setObjectName("menuHelp");
        MainWindow->setMenuBar(menubar);

        menubar->addAction(menuFile->menuAction());
        menubar->addAction(menuView->menuAction());
        menubar->addAction(menuHelp->menuAction());
        menuFile->addAction(actionExit);
        menuView->addAction(actionShowSidebar);
        menuView->addSeparator();
        menuView->addAction(actionSettings);
        menuHelp->addAction(actionAbout);

        retranslateUi(MainWindow);

        stackedWidget->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "Kartend", nullptr));
        actionExit->setText(QCoreApplication::translate("MainWindow", "Exit", nullptr));
#if QT_CONFIG(shortcut)
        actionExit->setShortcut(QCoreApplication::translate("MainWindow", "Ctrl+Q", nullptr));
#endif // QT_CONFIG(shortcut)
        actionShowSidebar->setText(QCoreApplication::translate("MainWindow", "Show Sidebar", nullptr));
#if QT_CONFIG(shortcut)
        actionShowSidebar->setShortcut(QCoreApplication::translate("MainWindow", "F9", nullptr));
#endif // QT_CONFIG(shortcut)
        actionSettings->setText(QCoreApplication::translate("MainWindow", "Settings", nullptr));
#if QT_CONFIG(shortcut)
        actionSettings->setShortcut(QCoreApplication::translate("MainWindow", "Ctrl+,", nullptr));
#endif // QT_CONFIG(shortcut)
        actionAbout->setText(QCoreApplication::translate("MainWindow", "About", nullptr));
        itemsTitleLabel->setText(QCoreApplication::translate("MainWindow", "Collection", nullptr));
#if QT_CONFIG(tooltip)
        searchModeButton->setToolTip(QCoreApplication::translate("MainWindow", "Search: Current collection only", nullptr));
#endif // QT_CONFIG(tooltip)
        searchModeButton->setText(QCoreApplication::translate("MainWindow", "\360\237\224\215", nullptr));
        searchBar->setPlaceholderText(QCoreApplication::translate("MainWindow", "Search items...", nullptr));
        loadingLabel->setStyleSheet(QCoreApplication::translate("MainWindow", "QLabel { color: palette(text); font-size: 14px; }", nullptr));
        loadingLabel->setText(QCoreApplication::translate("MainWindow", "Loading...", nullptr));
        menuFile->setTitle(QCoreApplication::translate("MainWindow", "&File", nullptr));
        menuView->setTitle(QCoreApplication::translate("MainWindow", "&View", nullptr));
        menuHelp->setTitle(QCoreApplication::translate("MainWindow", "&Help", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
