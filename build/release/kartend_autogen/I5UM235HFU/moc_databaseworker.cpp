/****************************************************************************
** Meta object code from reading C++ file 'databaseworker.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/managers/databaseworker.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'databaseworker.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14DatabaseWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto DatabaseWorker::qt_create_metaobjectdata<qt_meta_tag_ZN14DatabaseWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "DatabaseWorker",
        "itemsLoaded",
        "",
        "filePaths",
        "QHash<QString,QString>",
        "fileNames",
        "fileToArtworkDir",
        "QHash<QString,int>",
        "fileToCollectionIndex",
        "errorOccurred",
        "message",
        "countsUpdated",
        "initDatabase",
        "loadAllCollections",
        "QList<CollectionConfig>",
        "allCollections",
        "loadItems",
        "CollectionContext",
        "context",
        "loadItemsWithSubcollections",
        "updateCachedCounts"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'itemsLoaded'
        QtMocHelpers::SignalData<void(const QStringList &, const QHash<QString,QString> &, const QHash<QString,QString> &, const QHash<QString,int> &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 3 }, { 0x80000000 | 4, 5 }, { 0x80000000 | 4, 6 }, { 0x80000000 | 7, 8 },
        }}),
        // Signal 'errorOccurred'
        QtMocHelpers::SignalData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Signal 'countsUpdated'
        QtMocHelpers::SignalData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'initDatabase'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'loadAllCollections'
        QtMocHelpers::SlotData<void(const QList<CollectionConfig> &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 14, 15 },
        }}),
        // Slot 'loadItems'
        QtMocHelpers::SlotData<void(const CollectionContext &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 17, 18 },
        }}),
        // Slot 'loadItemsWithSubcollections'
        QtMocHelpers::SlotData<void(const CollectionContext &, const QList<CollectionConfig> &)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 17, 18 }, { 0x80000000 | 14, 15 },
        }}),
        // Slot 'updateCachedCounts'
        QtMocHelpers::SlotData<void(const QList<CollectionConfig> &)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 14, 15 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DatabaseWorker, qt_meta_tag_ZN14DatabaseWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject DatabaseWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14DatabaseWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14DatabaseWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14DatabaseWorkerE_t>.metaTypes,
    nullptr
} };

void DatabaseWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DatabaseWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->itemsLoaded((*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QHash<QString,QString>>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QHash<QString,QString>>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QHash<QString,int>>>(_a[4]))); break;
        case 1: _t->errorOccurred((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->countsUpdated(); break;
        case 3: _t->initDatabase(); break;
        case 4: _t->loadAllCollections((*reinterpret_cast< std::add_pointer_t<QList<CollectionConfig>>>(_a[1]))); break;
        case 5: _t->loadItems((*reinterpret_cast< std::add_pointer_t<CollectionContext>>(_a[1]))); break;
        case 6: _t->loadItemsWithSubcollections((*reinterpret_cast< std::add_pointer_t<CollectionContext>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QList<CollectionConfig>>>(_a[2]))); break;
        case 7: _t->updateCachedCounts((*reinterpret_cast< std::add_pointer_t<QList<CollectionConfig>>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<CollectionConfig> >(); break;
            }
            break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< CollectionContext >(); break;
            }
            break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< CollectionContext >(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<CollectionConfig> >(); break;
            }
            break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<CollectionConfig> >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (DatabaseWorker::*)(const QStringList & , const QHash<QString,QString> & , const QHash<QString,QString> & , const QHash<QString,int> & )>(_a, &DatabaseWorker::itemsLoaded, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseWorker::*)(const QString & )>(_a, &DatabaseWorker::errorOccurred, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseWorker::*)()>(_a, &DatabaseWorker::countsUpdated, 2))
            return;
    }
}

const QMetaObject *DatabaseWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DatabaseWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14DatabaseWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int DatabaseWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void DatabaseWorker::itemsLoaded(const QStringList & _t1, const QHash<QString,QString> & _t2, const QHash<QString,QString> & _t3, const QHash<QString,int> & _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 1
void DatabaseWorker::errorOccurred(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void DatabaseWorker::countsUpdated()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
