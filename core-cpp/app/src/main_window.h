// MainWindow — the device inspector. Our first custom QObject-derived class,
// so the `Q_OBJECT` macro makes Qt's MOC generate the signals/slots glue for it.
#pragma once
#include <QMainWindow>

class QTreeWidget;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT                       // <-- tells MOC: generate meta-object code for this class
public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void refresh();                // connect to OpenRGB and rebuild the tree

private:
    QTreeWidget* tree_   = nullptr;
    QLabel*      status_ = nullptr;
};
