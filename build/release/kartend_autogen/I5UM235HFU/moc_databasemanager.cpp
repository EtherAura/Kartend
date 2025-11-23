/****************************************************************************
** Meta object code from reading C++ file 'databasemanager.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/managers/databasemanager.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'databasemanager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.9.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN15DatabaseManagerE_t {};
} // unnamed namespace

template <> constexpr inline auto DatabaseManager::qt_create_metaobjectdata<qt_meta_tag_ZN15DatabaseManagerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "DatabaseManager",
        "itemsLoaded",
        "",
        "filePaths",
        "QHash<QString,QString>",
        "fileNames",
        "errorOccurred",
        "message",
        "requestLoadAllCollections",
        "QList<CollectionConfig>",
        "allCollections",
        "requestLoadItems",
        "CollectionContext",
        "context",
        "requestLoadItemsWithSubcollections",
        "requestUpdateCachedCounts",
        "onWorkerItemsLoaded",
        "fileToArtworkDir",
        "QHash<QString,int>",
        "fileToCollectionIndex"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'itemsLoaded'
        QtMocHelpers::SignalData<void(const QStringList &, const QHash<QString,QString> &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 3 }, { 0x80000000 | 4, 5 },
        }}),
        // Signal 'errorOccurred'
        QtMocHelpers::SignalData<void(const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Signal 'requestLoadAllCollections'
        QtMocHelpers::SignalData<void(const QList<CollectionConfig> &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 9, 10 },
        }}),
        // Signal 'requestLoadItems'
        QtMocHelpers::SignalData<void(const CollectionContext &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 12, 13 },
        }}),
        // Signal 'requestLoadItemsWithSubcollections'
        QtMocHelpers::SignalData<void(const CollectionContext &, const QList<CollectionConfig> &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 12, 13 }, { 0x80000000 | 9, 10 },
        }}),
        // Signal 'requestUpdateCachedCounts'
        QtMocHelpers::SignalData<void(const QList<CollectionConfig> &)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 9, 10 },
        }}),
        // Slot 'onWorkerItemsLoaded'
        QtMocHelpers::SlotData<void(const QStringList &, const QHash<QString,QString> &, const QHash<QString,QString> &, const QHash<QString,int> &)>(16, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QStringList, 3 }, { 0x80000000 | 4, 5 }, { 0x80000000 | 4, 17 }, { 0x80000000 | 18, 19 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DatabaseManager, qt_meta_tag_ZN15DatabaseManagerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject DatabaseManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15DatabaseManagerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15DatabaseManagerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15DatabaseManagerE_t>.metaTypes,
    nullptr
} };

void DatabaseManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DatabaseManager *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->itemsLoaded((*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QHash<QString,QString>>>(_a[2]))); break;
        case 1: _t->errorOccurred((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->requestLoadAllCollections((*reinterpret_cast< std::add_pointer_t<QList<CollectionConfig>>>(_a[1]))); break;
        case 3: _t->requestLoadItems((*reinterpret_cast< std::add_pointer_t<CollectionContext>>(_a[1]))); break;
        case 4: _t->requestLoadItemsWithSubcollections((*reinterpret_cast< std::add_pointer_t<CollectionContext>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QList<CollectionConfig>>>(_a[2]))); break;
        case 5: _t->requestUpdateCachedCounts((*reinterpret_cast< std::add_pointer_t<QList<CollectionConfig>>>(_a[1]))); break;
        case 6: _t->onWorkerItemsLoaded((*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QHash<QString,QString>>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QHash<QString,QString>>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QHash<QString,int>>>(_a[4]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 2:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<CollectionConfig> >(); break;
            }
            break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< CollectionContext >(); break;
            }
            break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< CollectionContext >(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<CollectionConfig> >(); break;
            }
            break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<CollectionConfig> >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (DatabaseManager::*)(const QStringList & , const QHash<QString,QString> & )>(_a, &DatabaseManager::itemsLoaded, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseManager::*)(const QString & )>(_a, &DatabaseManager::errorOccurred, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseManager::*)(const QList<CollectionConfig> & )>(_a, &DatabaseManager::requestLoadAllCollections, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseManager::*)(const CollectionContext & )>(_a, &DatabaseManager::requestLoadItems, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseManager::*)(const CollectionContext & , const QList<CollectionConfig> & )>(_a, &DatabaseManager::requestLoadItemsWithSubcollections, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseManager::*)(const QList<CollectionConfig> & )>(_a, &DatabaseManager::requestUpdateCachedCounts, 5))
            return;
    }
}

const QMetaObject *DatabaseManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DatabaseManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15DatabaseManagerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int DatabaseManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void DatabaseManager::itemsLoaded(const QStringList & _t1, const QHash<QString,QString> & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void DatabaseManager::errorOccurred(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void DatabaseManager::requestLoadAllCollections(const QList<CollectionConfig> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void DatabaseManager::requestLoadItems(const CollectionContext & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void DatabaseManager::requestLoadItemsWithSubcollections(const CollectionContext & _t1, const QList<CollectionConfig> & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void DatabaseManager::requestUpdateCachedCounts(const QList<CollectionConfig> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}
QT_WARNING_POP
