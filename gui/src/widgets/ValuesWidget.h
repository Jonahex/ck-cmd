#ifndef VALUESWIDGET_H
#define VALUESWIDGET_H

#include "DockWidget.h"
#include <QItemDelegate>
#include <src/models/HkxItemTableModel.h>
#include <src/hkx/ResourceManager.h>
#include <src/log.h>


QT_BEGIN_NAMESPACE
namespace Ui {
    class ValuesWidget;
}
QT_END_NAMESPACE

class ValuesWidget : public ::ads::CDockWidget
{
    Q_OBJECT

   const ckcmd::HKX::ResourceManager & _manager;
public:
    explicit ValuesWidget(const ckcmd::HKX::ResourceManager& manager, QWidget* parent = 0);
    ~ValuesWidget();

public slots:
    void setVariant(int file_index, hkVariant* v);

private:
    Ui::ValuesWidget* ui;
};

#endif //VALUESWIDGET_H