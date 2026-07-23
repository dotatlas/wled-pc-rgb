// MainWindow — the device inspector + colour control. Custom QObject class, so
// the Q_OBJECT macro makes Qt's MOC generate the signals/slots glue.
#pragma once
#include <QMainWindow>

class QTreeWidget;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void refresh();            // connect to OpenRGB and rebuild the tree
    void setSelectedColor();   // pick a colour and apply it to the selected device

private:
    QTreeWidget* tree_   = nullptr;
    QLabel*      status_ = nullptr;
};
