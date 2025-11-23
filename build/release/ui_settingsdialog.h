/********************************************************************************
** Form generated from reading UI file 'settingsdialog.ui'
**
** Created by: Qt User Interface Compiler version 6.9.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SETTINGSDIALOG_H
#define UI_SETTINGSDIALOG_H

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsDialog
{
public:
    QVBoxLayout *verticalLayout;
    QTabWidget *tabWidget;
    QWidget *collectionsTab;
    QVBoxLayout *collectionsTabLayout;
    QGroupBox *collectionGroupBox;
    QVBoxLayout *collectionLayout;
    QHBoxLayout *collectionButtonLayout;
    QPushButton *addCollectionButton;
    QPushButton *removeCollectionButton;
    QSpacerItem *collectionSpacer;
    QPushButton *saveCollectionButton;
    QTreeWidget *collectionTreeWidget;
    QGroupBox *configGroupBox;
    QFormLayout *formLayout;
    QLabel *label_parentCollection;
    QComboBox *parentCollectionComboBox;
    QLabel *label_launcher;
    QHBoxLayout *launcherLayout;
    QLineEdit *launcherLineEdit;
    QPushButton *browseLauncherButton;
    QLabel *label_core;
    QHBoxLayout *coreLayout;
    QLineEdit *coreLineEdit;
    QPushButton *browseCoreButton;
    QLabel *label_launchParams;
    QLineEdit *launchParamsLineEdit;
    QLabel *label_romDir;
    QHBoxLayout *contentDirLayout;
    QLineEdit *mediaDirLineEdit;
    QPushButton *browseMediaDirButton;
    QLabel *label_artworkDir;
    QHBoxLayout *artworkDirLayout;
    QLineEdit *artworkDirLineEdit;
    QPushButton *browseArtworkDirButton;
    QLabel *label_fileExtensions;
    QLineEdit *fileExtensionsLineEdit;
    QLabel *label_gridLayout;
    QHBoxLayout *gridLayoutLayout;
    QLabel *label_gridWidth_mini;
    QSpinBox *gridWidthSpinBox;
    QLabel *label_hSpace_mini;
    QSpinBox *horizontalSpacingSpinBox;
    QLabel *label_vSpace_mini;
    QSpinBox *verticalSpacingSpinBox;
    QLabel *label_itemLayout;
    QHBoxLayout *itemLayoutLayout;
    QLabel *label_itemWidth_mini;
    QSpinBox *itemWidthSpinBox;
    QLabel *label_itemHeight_mini;
    QSpinBox *itemHeightSpinBox;
    QLabel *label_fontSize_mini;
    QSpinBox *fontSizeSpinBox;
    QLabel *label_scrollbars;
    QHBoxLayout *scrollbarsLayout;
    QCheckBox *hideHorizontalScrollbarCheckBox;
    QCheckBox *hideVerticalScrollbarCheckBox;
    QLabel *label_titles;
    QHBoxLayout *titlesLayout;
    QCheckBox *hideTitlesCheckBox;
    QCheckBox *showSubcollectionTitlesCheckBox;
    QLabel *label_sidebarMode;
    QComboBox *sidebarModeComboBox;
    QLabel *label_showAllSubcollectionItems;
    QCheckBox *showAllSubcollectionItemsCheckBox;
    QLabel *label_horizontalAlignment;
    QComboBox *horizontalAlignmentComboBox;
    QWidget *generalTab;
    QVBoxLayout *generalTabLayout;
    QGroupBox *generalGroupBox;
    QVBoxLayout *generalContentLayout;
    QFormLayout *generalFormLayout;
    QLabel *rememberSelectionLabel;
    QCheckBox *rememberSelectionCheckBox;
    QLabel *wrapNavigationLabel;
    QCheckBox *wrapNavigationCheckBox;
    QSpacerItem *generalVerticalSpacer;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *SettingsDialog)
    {
        if (SettingsDialog->objectName().isEmpty())
            SettingsDialog->setObjectName("SettingsDialog");
        SettingsDialog->resize(900, 800);
        verticalLayout = new QVBoxLayout(SettingsDialog);
        verticalLayout->setObjectName("verticalLayout");
        tabWidget = new QTabWidget(SettingsDialog);
        tabWidget->setObjectName("tabWidget");
        collectionsTab = new QWidget();
        collectionsTab->setObjectName("collectionsTab");
        collectionsTabLayout = new QVBoxLayout(collectionsTab);
        collectionsTabLayout->setObjectName("collectionsTabLayout");
        collectionGroupBox = new QGroupBox(collectionsTab);
        collectionGroupBox->setObjectName("collectionGroupBox");
        collectionLayout = new QVBoxLayout(collectionGroupBox);
        collectionLayout->setObjectName("collectionLayout");
        collectionButtonLayout = new QHBoxLayout();
        collectionButtonLayout->setObjectName("collectionButtonLayout");
        addCollectionButton = new QPushButton(collectionGroupBox);
        addCollectionButton->setObjectName("addCollectionButton");
        addCollectionButton->setMaximumSize(QSize(30, 30));
        QIcon icon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentNew));
        addCollectionButton->setIcon(icon);

        collectionButtonLayout->addWidget(addCollectionButton);

        removeCollectionButton = new QPushButton(collectionGroupBox);
        removeCollectionButton->setObjectName("removeCollectionButton");
        removeCollectionButton->setMaximumSize(QSize(30, 30));
        QIcon icon1(QIcon::fromTheme(QIcon::ThemeIcon::EditDelete));
        removeCollectionButton->setIcon(icon1);

        collectionButtonLayout->addWidget(removeCollectionButton);

        collectionSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        collectionButtonLayout->addItem(collectionSpacer);

        saveCollectionButton = new QPushButton(collectionGroupBox);
        saveCollectionButton->setObjectName("saveCollectionButton");
        saveCollectionButton->setMaximumSize(QSize(30, 30));
        QIcon icon2(QIcon::fromTheme(QIcon::ThemeIcon::DocumentSave));
        saveCollectionButton->setIcon(icon2);

        collectionButtonLayout->addWidget(saveCollectionButton);


        collectionLayout->addLayout(collectionButtonLayout);

        collectionTreeWidget = new QTreeWidget(collectionGroupBox);
        collectionTreeWidget->setObjectName("collectionTreeWidget");
        collectionTreeWidget->setMinimumSize(QSize(0, 200));
        collectionTreeWidget->setRootIsDecorated(true);
        collectionTreeWidget->setItemsExpandable(true);
        collectionTreeWidget->setAnimated(true);

        collectionLayout->addWidget(collectionTreeWidget);


        collectionsTabLayout->addWidget(collectionGroupBox);

        configGroupBox = new QGroupBox(collectionsTab);
        configGroupBox->setObjectName("configGroupBox");
        formLayout = new QFormLayout(configGroupBox);
        formLayout->setObjectName("formLayout");
        label_parentCollection = new QLabel(configGroupBox);
        label_parentCollection->setObjectName("label_parentCollection");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, label_parentCollection);

        parentCollectionComboBox = new QComboBox(configGroupBox);
        parentCollectionComboBox->setObjectName("parentCollectionComboBox");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(parentCollectionComboBox->sizePolicy().hasHeightForWidth());
        parentCollectionComboBox->setSizePolicy(sizePolicy);

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, parentCollectionComboBox);

        label_launcher = new QLabel(configGroupBox);
        label_launcher->setObjectName("label_launcher");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, label_launcher);

        launcherLayout = new QHBoxLayout();
        launcherLayout->setObjectName("launcherLayout");
        launcherLineEdit = new QLineEdit(configGroupBox);
        launcherLineEdit->setObjectName("launcherLineEdit");

        launcherLayout->addWidget(launcherLineEdit);

        browseLauncherButton = new QPushButton(configGroupBox);
        browseLauncherButton->setObjectName("browseLauncherButton");

        launcherLayout->addWidget(browseLauncherButton);


        formLayout->setLayout(1, QFormLayout::ItemRole::FieldRole, launcherLayout);

        label_core = new QLabel(configGroupBox);
        label_core->setObjectName("label_core");

        formLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, label_core);

        coreLayout = new QHBoxLayout();
        coreLayout->setObjectName("coreLayout");
        coreLineEdit = new QLineEdit(configGroupBox);
        coreLineEdit->setObjectName("coreLineEdit");

        coreLayout->addWidget(coreLineEdit);

        browseCoreButton = new QPushButton(configGroupBox);
        browseCoreButton->setObjectName("browseCoreButton");

        coreLayout->addWidget(browseCoreButton);


        formLayout->setLayout(2, QFormLayout::ItemRole::FieldRole, coreLayout);

        label_launchParams = new QLabel(configGroupBox);
        label_launchParams->setObjectName("label_launchParams");

        formLayout->setWidget(3, QFormLayout::ItemRole::LabelRole, label_launchParams);

        launchParamsLineEdit = new QLineEdit(configGroupBox);
        launchParamsLineEdit->setObjectName("launchParamsLineEdit");

        formLayout->setWidget(3, QFormLayout::ItemRole::FieldRole, launchParamsLineEdit);

        label_romDir = new QLabel(configGroupBox);
        label_romDir->setObjectName("label_romDir");

        formLayout->setWidget(4, QFormLayout::ItemRole::LabelRole, label_romDir);

        contentDirLayout = new QHBoxLayout();
        contentDirLayout->setObjectName("contentDirLayout");
        mediaDirLineEdit = new QLineEdit(configGroupBox);
        mediaDirLineEdit->setObjectName("mediaDirLineEdit");

        contentDirLayout->addWidget(mediaDirLineEdit);

        browseMediaDirButton = new QPushButton(configGroupBox);
        browseMediaDirButton->setObjectName("browseMediaDirButton");

        contentDirLayout->addWidget(browseMediaDirButton);


        formLayout->setLayout(4, QFormLayout::ItemRole::FieldRole, contentDirLayout);

        label_artworkDir = new QLabel(configGroupBox);
        label_artworkDir->setObjectName("label_artworkDir");

        formLayout->setWidget(5, QFormLayout::ItemRole::LabelRole, label_artworkDir);

        artworkDirLayout = new QHBoxLayout();
        artworkDirLayout->setObjectName("artworkDirLayout");
        artworkDirLineEdit = new QLineEdit(configGroupBox);
        artworkDirLineEdit->setObjectName("artworkDirLineEdit");

        artworkDirLayout->addWidget(artworkDirLineEdit);

        browseArtworkDirButton = new QPushButton(configGroupBox);
        browseArtworkDirButton->setObjectName("browseArtworkDirButton");

        artworkDirLayout->addWidget(browseArtworkDirButton);


        formLayout->setLayout(5, QFormLayout::ItemRole::FieldRole, artworkDirLayout);

        label_fileExtensions = new QLabel(configGroupBox);
        label_fileExtensions->setObjectName("label_fileExtensions");

        formLayout->setWidget(6, QFormLayout::ItemRole::LabelRole, label_fileExtensions);

        fileExtensionsLineEdit = new QLineEdit(configGroupBox);
        fileExtensionsLineEdit->setObjectName("fileExtensionsLineEdit");

        formLayout->setWidget(6, QFormLayout::ItemRole::FieldRole, fileExtensionsLineEdit);

        label_gridLayout = new QLabel(configGroupBox);
        label_gridLayout->setObjectName("label_gridLayout");

        formLayout->setWidget(7, QFormLayout::ItemRole::LabelRole, label_gridLayout);

        gridLayoutLayout = new QHBoxLayout();
        gridLayoutLayout->setObjectName("gridLayoutLayout");
        label_gridWidth_mini = new QLabel(configGroupBox);
        label_gridWidth_mini->setObjectName("label_gridWidth_mini");

        gridLayoutLayout->addWidget(label_gridWidth_mini);

        gridWidthSpinBox = new QSpinBox(configGroupBox);
        gridWidthSpinBox->setObjectName("gridWidthSpinBox");
        gridWidthSpinBox->setMinimum(1);
        gridWidthSpinBox->setMaximum(10);
        gridWidthSpinBox->setValue(4);

        gridLayoutLayout->addWidget(gridWidthSpinBox);

        label_hSpace_mini = new QLabel(configGroupBox);
        label_hSpace_mini->setObjectName("label_hSpace_mini");

        gridLayoutLayout->addWidget(label_hSpace_mini);

        horizontalSpacingSpinBox = new QSpinBox(configGroupBox);
        horizontalSpacingSpinBox->setObjectName("horizontalSpacingSpinBox");
        horizontalSpacingSpinBox->setMinimum(-20);
        horizontalSpacingSpinBox->setMaximum(50);
        horizontalSpacingSpinBox->setValue(20);

        gridLayoutLayout->addWidget(horizontalSpacingSpinBox);

        label_vSpace_mini = new QLabel(configGroupBox);
        label_vSpace_mini->setObjectName("label_vSpace_mini");

        gridLayoutLayout->addWidget(label_vSpace_mini);

        verticalSpacingSpinBox = new QSpinBox(configGroupBox);
        verticalSpacingSpinBox->setObjectName("verticalSpacingSpinBox");
        verticalSpacingSpinBox->setMinimum(-20);
        verticalSpacingSpinBox->setMaximum(50);
        verticalSpacingSpinBox->setValue(20);

        gridLayoutLayout->addWidget(verticalSpacingSpinBox);


        formLayout->setLayout(7, QFormLayout::ItemRole::FieldRole, gridLayoutLayout);

        label_itemLayout = new QLabel(configGroupBox);
        label_itemLayout->setObjectName("label_itemLayout");

        formLayout->setWidget(8, QFormLayout::ItemRole::LabelRole, label_itemLayout);

        itemLayoutLayout = new QHBoxLayout();
        itemLayoutLayout->setObjectName("itemLayoutLayout");
        label_itemWidth_mini = new QLabel(configGroupBox);
        label_itemWidth_mini->setObjectName("label_itemWidth_mini");

        itemLayoutLayout->addWidget(label_itemWidth_mini);

        itemWidthSpinBox = new QSpinBox(configGroupBox);
        itemWidthSpinBox->setObjectName("itemWidthSpinBox");
        itemWidthSpinBox->setMinimum(100);
        itemWidthSpinBox->setMaximum(400);
        itemWidthSpinBox->setValue(220);

        itemLayoutLayout->addWidget(itemWidthSpinBox);

        label_itemHeight_mini = new QLabel(configGroupBox);
        label_itemHeight_mini->setObjectName("label_itemHeight_mini");

        itemLayoutLayout->addWidget(label_itemHeight_mini);

        itemHeightSpinBox = new QSpinBox(configGroupBox);
        itemHeightSpinBox->setObjectName("itemHeightSpinBox");
        itemHeightSpinBox->setMinimum(100);
        itemHeightSpinBox->setMaximum(400);
        itemHeightSpinBox->setValue(245);

        itemLayoutLayout->addWidget(itemHeightSpinBox);

        label_fontSize_mini = new QLabel(configGroupBox);
        label_fontSize_mini->setObjectName("label_fontSize_mini");

        itemLayoutLayout->addWidget(label_fontSize_mini);

        fontSizeSpinBox = new QSpinBox(configGroupBox);
        fontSizeSpinBox->setObjectName("fontSizeSpinBox");
        fontSizeSpinBox->setMinimum(8);
        fontSizeSpinBox->setMaximum(72);
        fontSizeSpinBox->setValue(12);

        itemLayoutLayout->addWidget(fontSizeSpinBox);


        formLayout->setLayout(8, QFormLayout::ItemRole::FieldRole, itemLayoutLayout);

        label_scrollbars = new QLabel(configGroupBox);
        label_scrollbars->setObjectName("label_scrollbars");

        formLayout->setWidget(9, QFormLayout::ItemRole::LabelRole, label_scrollbars);

        scrollbarsLayout = new QHBoxLayout();
        scrollbarsLayout->setObjectName("scrollbarsLayout");
        hideHorizontalScrollbarCheckBox = new QCheckBox(configGroupBox);
        hideHorizontalScrollbarCheckBox->setObjectName("hideHorizontalScrollbarCheckBox");

        scrollbarsLayout->addWidget(hideHorizontalScrollbarCheckBox);

        hideVerticalScrollbarCheckBox = new QCheckBox(configGroupBox);
        hideVerticalScrollbarCheckBox->setObjectName("hideVerticalScrollbarCheckBox");

        scrollbarsLayout->addWidget(hideVerticalScrollbarCheckBox);


        formLayout->setLayout(9, QFormLayout::ItemRole::FieldRole, scrollbarsLayout);

        label_titles = new QLabel(configGroupBox);
        label_titles->setObjectName("label_titles");

        formLayout->setWidget(10, QFormLayout::ItemRole::LabelRole, label_titles);

        titlesLayout = new QHBoxLayout();
        titlesLayout->setObjectName("titlesLayout");
        hideTitlesCheckBox = new QCheckBox(configGroupBox);
        hideTitlesCheckBox->setObjectName("hideTitlesCheckBox");

        titlesLayout->addWidget(hideTitlesCheckBox);

        showSubcollectionTitlesCheckBox = new QCheckBox(configGroupBox);
        showSubcollectionTitlesCheckBox->setObjectName("showSubcollectionTitlesCheckBox");

        titlesLayout->addWidget(showSubcollectionTitlesCheckBox);


        formLayout->setLayout(10, QFormLayout::ItemRole::FieldRole, titlesLayout);

        label_sidebarMode = new QLabel(configGroupBox);
        label_sidebarMode->setObjectName("label_sidebarMode");

        formLayout->setWidget(11, QFormLayout::ItemRole::LabelRole, label_sidebarMode);

        sidebarModeComboBox = new QComboBox(configGroupBox);
        sidebarModeComboBox->addItem(QString());
        sidebarModeComboBox->addItem(QString());
        sidebarModeComboBox->setObjectName("sidebarModeComboBox");

        formLayout->setWidget(11, QFormLayout::ItemRole::FieldRole, sidebarModeComboBox);

        label_showAllSubcollectionItems = new QLabel(configGroupBox);
        label_showAllSubcollectionItems->setObjectName("label_showAllSubcollectionItems");

        formLayout->setWidget(12, QFormLayout::ItemRole::LabelRole, label_showAllSubcollectionItems);

        showAllSubcollectionItemsCheckBox = new QCheckBox(configGroupBox);
        showAllSubcollectionItemsCheckBox->setObjectName("showAllSubcollectionItemsCheckBox");
        showAllSubcollectionItemsCheckBox->setChecked(false);

        formLayout->setWidget(12, QFormLayout::ItemRole::FieldRole, showAllSubcollectionItemsCheckBox);

        label_horizontalAlignment = new QLabel(configGroupBox);
        label_horizontalAlignment->setObjectName("label_horizontalAlignment");

        formLayout->setWidget(13, QFormLayout::ItemRole::LabelRole, label_horizontalAlignment);

        horizontalAlignmentComboBox = new QComboBox(configGroupBox);
        horizontalAlignmentComboBox->addItem(QString());
        horizontalAlignmentComboBox->addItem(QString());
        horizontalAlignmentComboBox->addItem(QString());
        horizontalAlignmentComboBox->setObjectName("horizontalAlignmentComboBox");

        formLayout->setWidget(13, QFormLayout::ItemRole::FieldRole, horizontalAlignmentComboBox);


        collectionsTabLayout->addWidget(configGroupBox);

        tabWidget->addTab(collectionsTab, QString());
        generalTab = new QWidget();
        generalTab->setObjectName("generalTab");
        generalTabLayout = new QVBoxLayout(generalTab);
        generalTabLayout->setObjectName("generalTabLayout");
        generalGroupBox = new QGroupBox(generalTab);
        generalGroupBox->setObjectName("generalGroupBox");
        generalContentLayout = new QVBoxLayout(generalGroupBox);
        generalContentLayout->setObjectName("generalContentLayout");
        generalFormLayout = new QFormLayout();
        generalFormLayout->setObjectName("generalFormLayout");
        rememberSelectionLabel = new QLabel(generalGroupBox);
        rememberSelectionLabel->setObjectName("rememberSelectionLabel");

        generalFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, rememberSelectionLabel);

        rememberSelectionCheckBox = new QCheckBox(generalGroupBox);
        rememberSelectionCheckBox->setObjectName("rememberSelectionCheckBox");

        generalFormLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, rememberSelectionCheckBox);

        wrapNavigationLabel = new QLabel(generalGroupBox);
        wrapNavigationLabel->setObjectName("wrapNavigationLabel");

        generalFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, wrapNavigationLabel);

        wrapNavigationCheckBox = new QCheckBox(generalGroupBox);
        wrapNavigationCheckBox->setObjectName("wrapNavigationCheckBox");

        generalFormLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, wrapNavigationCheckBox);


        generalContentLayout->addLayout(generalFormLayout);

        generalVerticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        generalContentLayout->addItem(generalVerticalSpacer);


        generalTabLayout->addWidget(generalGroupBox);

        tabWidget->addTab(generalTab, QString());

        verticalLayout->addWidget(tabWidget);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        buttonBox = new QDialogButtonBox(SettingsDialog);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Orientation::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Cancel|QDialogButtonBox::StandardButton::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(SettingsDialog);

        tabWidget->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(SettingsDialog);
    } // setupUi

    void retranslateUi(QDialog *SettingsDialog)
    {
        SettingsDialog->setWindowTitle(QCoreApplication::translate("SettingsDialog", "Settings", nullptr));
        addCollectionButton->setText(QString());
        removeCollectionButton->setText(QString());
#if QT_CONFIG(tooltip)
        saveCollectionButton->setToolTip(QCoreApplication::translate("SettingsDialog", "Save current collection settings", nullptr));
#endif // QT_CONFIG(tooltip)
        saveCollectionButton->setText(QString());
        QTreeWidgetItem *___qtreewidgetitem = collectionTreeWidget->headerItem();
        ___qtreewidgetitem->setText(0, QCoreApplication::translate("SettingsDialog", "Collections", nullptr));
        configGroupBox->setTitle(QCoreApplication::translate("SettingsDialog", "Collection Configuration", nullptr));
        label_parentCollection->setText(QCoreApplication::translate("SettingsDialog", "Parent Collection:", nullptr));
#if QT_CONFIG(tooltip)
        parentCollectionComboBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Select parent collection to make this a subcollection", nullptr));
#endif // QT_CONFIG(tooltip)
        label_launcher->setText(QCoreApplication::translate("SettingsDialog", "Launcher:", nullptr));
        browseLauncherButton->setText(QCoreApplication::translate("SettingsDialog", "Browse", nullptr));
        label_core->setText(QCoreApplication::translate("SettingsDialog", "Core Path:", nullptr));
#if QT_CONFIG(tooltip)
        coreLineEdit->setToolTip(QCoreApplication::translate("SettingsDialog", "Path to RetroArch core file", nullptr));
#endif // QT_CONFIG(tooltip)
        browseCoreButton->setText(QCoreApplication::translate("SettingsDialog", "Browse", nullptr));
        label_launchParams->setText(QCoreApplication::translate("SettingsDialog", "Launch Parameters:", nullptr));
#if QT_CONFIG(tooltip)
        launchParamsLineEdit->setToolTip(QCoreApplication::translate("SettingsDialog", "Additional command-line parameters for the launcher", nullptr));
#endif // QT_CONFIG(tooltip)
        launchParamsLineEdit->setPlaceholderText(QCoreApplication::translate("SettingsDialog", "Optional parameters (e.g., -fullscreen, -config file.cfg)", nullptr));
        label_romDir->setText(QCoreApplication::translate("SettingsDialog", "Content:", nullptr));
        browseMediaDirButton->setText(QCoreApplication::translate("SettingsDialog", "Browse", nullptr));
        label_artworkDir->setText(QCoreApplication::translate("SettingsDialog", "Primary Artwork:", nullptr));
        browseArtworkDirButton->setText(QCoreApplication::translate("SettingsDialog", "Browse", nullptr));
        label_fileExtensions->setText(QCoreApplication::translate("SettingsDialog", "File Extensions:", nullptr));
#if QT_CONFIG(tooltip)
        fileExtensionsLineEdit->setToolTip(QString());
#endif // QT_CONFIG(tooltip)
        label_gridLayout->setText(QCoreApplication::translate("SettingsDialog", "Grid Layout:", nullptr));
        label_gridWidth_mini->setText(QCoreApplication::translate("SettingsDialog", "Width:", nullptr));
#if QT_CONFIG(tooltip)
        gridWidthSpinBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Number of artwork items per row (1-10)", nullptr));
#endif // QT_CONFIG(tooltip)
        label_hSpace_mini->setText(QCoreApplication::translate("SettingsDialog", "H-Space:", nullptr));
#if QT_CONFIG(tooltip)
        horizontalSpacingSpinBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Horizontal spacing between items in pixels (-20-50)", nullptr));
#endif // QT_CONFIG(tooltip)
        label_vSpace_mini->setText(QCoreApplication::translate("SettingsDialog", "V-Space:", nullptr));
#if QT_CONFIG(tooltip)
        verticalSpacingSpinBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Vertical spacing between items in pixels (-20-50)", nullptr));
#endif // QT_CONFIG(tooltip)
        label_itemLayout->setText(QCoreApplication::translate("SettingsDialog", "Item Layout:", nullptr));
        label_itemWidth_mini->setText(QCoreApplication::translate("SettingsDialog", "Width:", nullptr));
#if QT_CONFIG(tooltip)
        itemWidthSpinBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Width of collection items in pixels", nullptr));
#endif // QT_CONFIG(tooltip)
        label_itemHeight_mini->setText(QCoreApplication::translate("SettingsDialog", "Height:", nullptr));
#if QT_CONFIG(tooltip)
        itemHeightSpinBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Height of collection items in pixels", nullptr));
#endif // QT_CONFIG(tooltip)
        label_fontSize_mini->setText(QCoreApplication::translate("SettingsDialog", "Font:", nullptr));
#if QT_CONFIG(tooltip)
        fontSizeSpinBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Font size for item titles", nullptr));
#endif // QT_CONFIG(tooltip)
        label_scrollbars->setText(QCoreApplication::translate("SettingsDialog", "Scrollbars:", nullptr));
#if QT_CONFIG(tooltip)
        hideHorizontalScrollbarCheckBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Hide the horizontal scrollbar for this collection", nullptr));
#endif // QT_CONFIG(tooltip)
        hideHorizontalScrollbarCheckBox->setText(QCoreApplication::translate("SettingsDialog", "Hide Horizontal", nullptr));
#if QT_CONFIG(tooltip)
        hideVerticalScrollbarCheckBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Hide the vertical scrollbar for this collection", nullptr));
#endif // QT_CONFIG(tooltip)
        hideVerticalScrollbarCheckBox->setText(QCoreApplication::translate("SettingsDialog", "Hide Vertical", nullptr));
        label_titles->setText(QCoreApplication::translate("SettingsDialog", "Titles:", nullptr));
#if QT_CONFIG(tooltip)
        hideTitlesCheckBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Hide titles for this collection", nullptr));
#endif // QT_CONFIG(tooltip)
        hideTitlesCheckBox->setText(QCoreApplication::translate("SettingsDialog", "Hide Titles", nullptr));
#if QT_CONFIG(tooltip)
        showSubcollectionTitlesCheckBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Show titles for subcollections even if Hide Titles is enabled", nullptr));
#endif // QT_CONFIG(tooltip)
        showSubcollectionTitlesCheckBox->setText(QCoreApplication::translate("SettingsDialog", "Show Subcollection Titles", nullptr));
        label_sidebarMode->setText(QCoreApplication::translate("SettingsDialog", "Sidebar Mode:", nullptr));
        sidebarModeComboBox->setItemText(0, QCoreApplication::translate("SettingsDialog", "Overlay", nullptr));
        sidebarModeComboBox->setItemText(1, QCoreApplication::translate("SettingsDialog", "Fixed", nullptr));

#if QT_CONFIG(tooltip)
        sidebarModeComboBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Choose how the sidebar is displayed", nullptr));
#endif // QT_CONFIG(tooltip)
        label_showAllSubcollectionItems->setText(QCoreApplication::translate("SettingsDialog", "Show All Subcollection Items:", nullptr));
#if QT_CONFIG(tooltip)
        showAllSubcollectionItemsCheckBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Mix items from subcollections with parent collection items, sorted alphabetically", nullptr));
#endif // QT_CONFIG(tooltip)
        showAllSubcollectionItemsCheckBox->setText(QString());
        label_horizontalAlignment->setText(QCoreApplication::translate("SettingsDialog", "Horizontal Alignment:", nullptr));
        horizontalAlignmentComboBox->setItemText(0, QCoreApplication::translate("SettingsDialog", "Left", nullptr));
        horizontalAlignmentComboBox->setItemText(1, QCoreApplication::translate("SettingsDialog", "Center", nullptr));
        horizontalAlignmentComboBox->setItemText(2, QCoreApplication::translate("SettingsDialog", "Right", nullptr));

#if QT_CONFIG(tooltip)
        horizontalAlignmentComboBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Horizontal alignment of collection grid (Left, Center, Right)", nullptr));
#endif // QT_CONFIG(tooltip)
        tabWidget->setTabText(tabWidget->indexOf(collectionsTab), QCoreApplication::translate("SettingsDialog", "Collections", nullptr));
        generalGroupBox->setTitle(QCoreApplication::translate("SettingsDialog", "General Settings", nullptr));
        rememberSelectionLabel->setText(QCoreApplication::translate("SettingsDialog", "Remember Selection:", nullptr));
#if QT_CONFIG(tooltip)
        rememberSelectionCheckBox->setToolTip(QCoreApplication::translate("SettingsDialog", "Remember the last selected item when returning to a collection", nullptr));
#endif // QT_CONFIG(tooltip)
        wrapNavigationLabel->setText(QCoreApplication::translate("SettingsDialog", "Wrap Navigation:", nullptr));
#if QT_CONFIG(tooltip)
        wrapNavigationCheckBox->setToolTip(QCoreApplication::translate("SettingsDialog", "When navigating with arrow keys, wrap at edges", nullptr));
#endif // QT_CONFIG(tooltip)
        tabWidget->setTabText(tabWidget->indexOf(generalTab), QCoreApplication::translate("SettingsDialog", "General", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsDialog: public Ui_SettingsDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SETTINGSDIALOG_H
