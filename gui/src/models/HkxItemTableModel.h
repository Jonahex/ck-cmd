#pragma once

#include <QAbstractTableModel>
#include <core/HKXWrangler.h>

namespace ckcmd {
    namespace HKX {

        class HkxItemTableModel : public QAbstractTableModel {
            Q_OBJECT

            hkVariant* _variant;
            int _file;
            hkVariant* getObject(const QModelIndex& index) const;

        public:

            HkxItemTableModel(hkVariant* variant, int file, QObject* parent = 0);
            ~HkxItemTableModel() {}

            QVariant headerData(int section, Qt::Orientation orientation,
                int role = Qt::DisplayRole) const override;


            QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
            bool setData(const QModelIndex& index, const QVariant& value,
                int role = Qt::EditRole) override;

            int rowCount(const QModelIndex& parent = QModelIndex()) const override;
            int columnCount(const QModelIndex& parent = QModelIndex()) const override;

        };
    }
}